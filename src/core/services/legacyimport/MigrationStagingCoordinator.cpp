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
