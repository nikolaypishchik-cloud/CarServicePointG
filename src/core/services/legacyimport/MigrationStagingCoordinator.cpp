#include "core/services/legacyimport/MigrationArchiveWorkspace.h"
#include <QRegularExpression>
#include "infrastructure/db/sqlite/MigrationOrderKeys.h"
#include "infrastructure/db/sqlite/DbSourceFingerprintSupport.h"
#include "infrastructure/db/sqlite/MigrationDestinationBaseline.h"
#include "core/services/legacyimport/MigrationStagingCoordinator.h"

#include "core/services/legacyimport/MigrationRunJournal.h"
#include "infrastructure/security/EncryptedStorage.h"

#include "core/io/NativeSnapshotMetadata.h"
#include "infrastructure/db/sqlite/DbBootstrapEnsureSchema.h"
#include "infrastructure/db/sqlite/DbCanonicalRewritePrimaryImportSupport.h"
#include "infrastructure/db/sqlite/DbCanonicalRewriteAncillaryImportSupport.h"
#include "infrastructure/db/sqlite/DbFacadeSupport.h"
#include "infrastructure/db/sqlite/RuntimeDbPreflightValidator.h"
#include <QCryptographicHash>
#include <QScopeGuard>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include "runtime/AppRuntime.h"

namespace {
bool seedWorkspace(const QString& destination, bool* copied, QString* error)
{
    const QString active = AppRuntime::activeDbFilePath();
    *copied = false;
    if (!QFileInfo::exists(active)) {
        const auto sealed = AppRuntime::sealedActiveDbFilePath();
        if (!QFileInfo::exists(sealed)) return true;
        if (!EncryptedStorage::exportPlaintextDatabase(sealed,destination,error)) return false;
    } else {
        const auto name = QUuid::createUuid().toString();
        auto input = QSqlDatabase::addDatabase("QSQLITE",name);
        const auto close = qScopeGuard([&]{input.close(); input={}; QSqlDatabase::removeDatabase(name);});
        input.setDatabaseName(active);
        input.setConnectOptions("QSQLITE_OPEN_READONLY");
        if (!input.open()) return NativeSnapshotMetadata::fail(error,input.lastError().text());
        QSqlQuery copy(input);
        copy.prepare("VACUUM INTO ?");
        copy.addBindValue(destination);
        if (!copy.exec()) return NativeSnapshotMetadata::fail(error,copy.lastError().text());
    }
    *copied = true;
    return RuntimeDbPreflightValidator::validateFile(destination, RuntimeDbPreflightValidator::DbRole::Workspace,error);
}

bool retainPreviousArchive(const QString& temporary, QString* retained, QString* error)
{
    const auto runtime=AppRuntime::archiveDbFilePath();
    const auto sealed=AppRuntime::sealedArchiveDbFilePath();
    if(!QFileInfo::exists(runtime) && !QFileInfo::exists(sealed))return true;
    const auto copy=QDir(temporary).filePath("previous-archive.db");
    if(QFileInfo::exists(runtime)) {
        const auto name=QUuid::createUuid().toString();
        auto db=QSqlDatabase::addDatabase("QSQLITE",name);
        const auto close=qScopeGuard([&]{db.close();db={};QSqlDatabase::removeDatabase(name);});
        db.setDatabaseName(runtime);db.setConnectOptions("QSQLITE_OPEN_READONLY");
        if(!db.open())return NativeSnapshotMetadata::fail(error,db.lastError().text());
        QSqlQuery q(db);q.prepare("VACUUM INTO ?");q.addBindValue(copy);
        if(!q.exec())return NativeSnapshotMetadata::fail(error,q.lastError().text());
    } else if(!EncryptedStorage::exportPlaintextDatabase(sealed,copy,error))return false;
    auto hash=[](const QString& path){QFile file(path);QCryptographicHash digest(QCryptographicHash::Sha256);
        if(!file.open(QIODevice::ReadOnly) || !digest.addData(&file))return QByteArray();return digest.result();};
    const auto digest=hash(copy);
    if(digest.isEmpty())return NativeSnapshotMetadata::fail(error,"Cannot hash previous archive");
    const auto directory=QDir(AppRuntime::securityDir()).filePath("migration_retained_archives");
    if(!QDir().mkpath(directory) || !NativeSnapshotMetadata::protectBackupDirectory(directory,error))return false;
    *retained=QDir(directory).filePath("archive_"+QString::fromLatin1(digest.toHex())+".db");
    if(!QFileInfo::exists(*retained) && !QFile::copy(copy,*retained))return NativeSnapshotMetadata::fail(error,"Cannot retain previous archive shard");
    if(hash(*retained)!=digest)return NativeSnapshotMetadata::fail(error,"Previous archive shard differs from snapshot");
    return true;
}

bool prepareWorkspace(const QString& path, const QString& source, const QString& archive, const QString& identity, QString* error)
{
    bool copied = false;
    if (!seedWorkspace(path,&copied,error)) return false;
    const auto name = QUuid::createUuid().toString();
    auto db = QSqlDatabase::addDatabase("QSQLITE", name);
    const auto close = qScopeGuard([&]{ db.close(); db = {}; QSqlDatabase::removeDatabase(name); });
    db.setDatabaseName(path);
    if (!db.open()) return NativeSnapshotMetadata::fail(error, db.lastError().text());
    if (!ensureWorkspaceSchema(db)) return NativeSnapshotMetadata::fail(error, "Cannot initialize migration workspace");
    QString previousArchive;
    if(!retainPreviousArchive(QFileInfo(path).absolutePath(),&previousArchive,error))return false;
    QSet<QString> previousPaths;
    if(!previousArchive.isEmpty())previousPaths.insert(previousArchive);
    QDir shardDirectory(QDir(AppRuntime::dataDir()).filePath("archive_shards"));
    for(const auto& file:shardDirectory.entryList({"orders_*.db"},QDir::Files,QDir::Name))previousPaths.insert(shardDirectory.filePath(file));
    QSqlQuery routes(db);
    if(!routes.exec("SELECT DISTINCT storage_path FROM order_registry WHERE storage_kind='archive'"))return NativeSnapshotMetadata::fail(error,routes.lastError().text());
    while(routes.next()) {
        const auto oldPath=routes.value(0).toString();
        previousPaths.insert(oldPath==AppRuntime::archiveDbFilePath() ? previousArchive : oldPath);
    }
    if(routes.lastError().isValid())return NativeSnapshotMetadata::fail(error,routes.lastError().text());routes.finish();
    auto sortedPaths=previousPaths.values();sortedPaths.sort();
    QSet<QString> materialized;
    for(const auto& previous:sortedPaths) {
        if(previous.isEmpty() || !QFileInfo::exists(previous))return NativeSnapshotMetadata::fail(error,"Previous archive route is missing");
        if(!MigrationArchiveWorkspace::merge(previous,path,error,&materialized))return false;
    }
    if(!materialized.isEmpty())copied=true; // Do not clear restored archive-only graphs in an otherwise fresh profile.
    const QString sourceSchema = QStringLiteral("legacy_rewrite");
    QSqlQuery attach(db);
    const QString rawSchema=QStringLiteral("audited_source");
    attach.prepare(QStringLiteral("ATTACH DATABASE ? AS %1").arg(sqlQuoteIdent(rawSchema)));
    attach.addBindValue(source);
    if (!attach.exec()) return NativeSnapshotMetadata::fail(error,attach.lastError().text());
    attach.finish();
    MigrationOrderKeys::Map keys;
    if (!MigrationOrderKeys::plan(db,rawSchema,&keys,error) || !MigrationOrderKeys::project(source,archive,keys,error))
        return false;
    CanonicalRewriteSourceSchema originalSchema;
    if (!loadCanonicalRewriteSourceSchema(db,rawSchema,&originalSchema,error)) return false;
    attach.prepare(QStringLiteral("ATTACH DATABASE ? AS %1").arg(sqlQuoteIdent(sourceSchema)));
    attach.addBindValue(archive);
    if (!attach.exec()) return NativeSnapshotMetadata::fail(error, attach.lastError().text());
    attach.finish();
    CanonicalRewriteSourceSchema schema;
    if (!loadCanonicalRewriteSourceSchema(db, sourceSchema, &schema, error)) return false;
    if (!db.transaction()) return NativeSnapshotMetadata::fail(error, db.lastError().text());
    auto rollback = qScopeGuard([&]{ if (db.isOpen()) db.rollback(); });
    if ((!copied && !clearCanonicalRewriteWorkspaceTargets(db,error))
        || !MigrationDestinationBaseline::capture(db,identity,error)
        || !importCanonicalRewritePrimaryTables(db,schema,error)
        || !importCanonicalRewriteAncillaryTables(db,schema,error)
        || !MigrationOrderKeys::store(db,identity,keys,error)
        || !recordCanonicalRewriteCorrespondence(db,identity,rawSchema,originalSchema,error,keys)) return false;
    if (!ensureRouterCacheSchema(db) || !rebuildRouterViews(db))
        return NativeSnapshotMetadata::fail(error, "Cannot build migration workspace views");
    QSqlQuery pin(db);
    if (!pin.exec("CREATE TABLE IF NOT EXISTS migration_prepared_source(singleton INTEGER PRIMARY KEY CHECK(singleton=1),source_identity TEXT NOT NULL,finalized INTEGER NOT NULL CHECK(finalized IN (0,1)))"))
        return NativeSnapshotMetadata::fail(error,pin.lastError().text());
    pin.prepare("INSERT INTO migration_prepared_source VALUES(1,?,0) ON CONFLICT(singleton) DO UPDATE SET source_identity=excluded.source_identity, finalized=0");
    pin.addBindValue(identity);
    if (!pin.exec()) return NativeSnapshotMetadata::fail(error,pin.lastError().text());
    if (!pin.exec("CREATE TABLE IF NOT EXISTS migration_archive_pin(source_identity TEXT PRIMARY KEY, fingerprint TEXT NOT NULL)"))
        return NativeSnapshotMetadata::fail(error,pin.lastError().text());
    const QString signature=legacySourceFingerprint(archive).section('|',-2);
    if (!QRegularExpression("^[0-9]+[|][0-9a-f]{64}$").match(signature).hasMatch())
        return NativeSnapshotMetadata::fail(error,"Cannot fingerprint projected archive");
    pin.prepare("INSERT INTO migration_archive_pin VALUES(?,?) ON CONFLICT(source_identity) DO UPDATE SET fingerprint=excluded.fingerprint");
    pin.addBindValue(identity); pin.addBindValue(signature);
    if (!pin.exec()) return NativeSnapshotMetadata::fail(error,pin.lastError().text());
    if (!db.commit()) return NativeSnapshotMetadata::fail(error, db.lastError().text());
    rollback.dismiss();
    return true;
}
}

bool MigrationStagingCoordinator::importSourceIntoIsolatedTarget(const LegacyImportBundle& bundle,
                                                                 const LegacyImportRunContext& runContext,
                                                                 QString* errorText)
{
    if (!MigrationRunJournal::stageStarted(runContext, QStringLiteral("staging_import"), QStringLiteral("Import legacy source into isolated target"), errorText)) return false;
    QTemporaryDir generation;
    if (!generation.isValid() || !NativeSnapshotMetadata::protectBackupDirectory(generation.path(),errorText)) return false;
    const auto source = generation.filePath("source.db");
    const auto workspace = generation.filePath("workspace.db");
    if (!copyAuditedDatabase(bundle.primaryDbPath,source,runContext.sourceFingerprint,errorText)) return false;
    const auto archive=generation.filePath("archive.db");
    if (!prepareWorkspace(workspace,source,archive,runContext.sourceFingerprint,errorText)
        || !RuntimeDbPreflightValidator::validateFile(workspace,RuntimeDbPreflightValidator::DbRole::Workspace,errorText)
        || !EncryptedStorage::publishPreparedWorkspace(source,archive,workspace,errorText)) {
        MigrationRunJournal::stageFailed(runContext, QStringLiteral("staging_import"), errorText ? *errorText : QStringLiteral("publishPreparedWorkspace failed"));
        return false;
    }
    return MigrationRunJournal::stageSucceeded(runContext, QStringLiteral("staging_import"), QStringLiteral("Validated workspace and source archive published"), errorText);

}
