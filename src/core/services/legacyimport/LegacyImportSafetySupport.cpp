#include "core/services/legacyimport/LegacyImportSafetySupport.h"

#include "runtime/AppRuntime.h"
#include "infrastructure/security/EncryptedStorageLocalSupport.h"
#include "core/io/SafeFileOps.h"
#include "core/io/NativeSnapshotMetadata.h"
#include "core/io/SecureSnapshotRestore.h"
#include "core/services/storage/MigrationRestoreJournal.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>
#include "infrastructure/db/sqlite/ScopedConnectionManager.h"
#include <QRegularExpression>
#include <QSet>
#include <QLockFile>
#include <QJsonArray>
#include <QSqlQuery>
#include <QSqlError>
#include <QScopeGuard>
#include <algorithm>

namespace {

struct ArtifactPair {
    QString livePath;
    QString fileName;
};

QList<ArtifactPair> artifactPairs()
{
    QList<ArtifactPair> pairs{
        {AppRuntime::sealedActiveDbFilePath(), QStringLiteral("CarService_workspace.db.enc")},
        {AppRuntime::sealedArchiveDbFilePath(), QStringLiteral("CarService_archive_legacy.db.enc")},
        {AppRuntime::sealedLegacyDbFilePath(), QStringLiteral("CarServicePointG.db.enc")},
        {AppRuntime::sealedRegistryDbFilePath(), QStringLiteral("CarService_registry.db.enc")},
        {AppRuntime::masterKeyFilePath(), QStringLiteral("master.key.dpapi")},
        {QDir(AppRuntime::securityDir()).filePath(QStringLiteral("migration_settings_snapshot.json")), QStringLiteral("profile_settings.json")}
    };
    // Presence is recorded for every known sidecar, so failed-import artifacts
    // absent at snapshot time are removed on restore rather than replayed.
    const QStringList suffixes{QString(), QStringLiteral("-wal"), QStringLiteral("-shm"),
                               QStringLiteral("-journal"), QStringLiteral(".rollback"), QStringLiteral(".staging")};
    for (const auto& mapping : EncryptedStorage::dbMappings()) {
        const auto& database = mapping.runtimePath;
        for (const auto& suffix : suffixes) {
            pairs.append({database + suffix, QStringLiteral("runtime_") + QFileInfo(database).fileName() + suffix});
        }
    }
    return pairs;
}

bool shardArtifact(const QString& name, ArtifactPair* item, QString* errorText)
{
    static const QRegularExpression safeName(QStringLiteral("^[A-Za-z0-9_-]+(?:\\.[A-Za-z0-9_-]+)*$"));
    if (!safeName.match(name).hasMatch()) {
        if (errorText) *errorText = QStringLiteral("Invalid archive snapshot filename: %1").arg(name);
        return false;
    }
    *item = {QDir(QDir(AppRuntime::dataDir()).filePath(QStringLiteral("archive_shards"))).filePath(name),
             QStringLiteral("shards_") + name};
    return true;
}

bool appendCurrentShards(QList<ArtifactPair>* pairs, QString* errorText)
{
    const QFileInfo root(QDir(AppRuntime::dataDir()).filePath(QStringLiteral("archive_shards")));
    if (root.isSymLink() || (root.exists() && (!root.isDir() || !root.isReadable()))) {
        if (errorText) *errorText = QStringLiteral("Invalid archive snapshot directory: %1").arg(root.filePath());
        return false;
    }
    if (!root.exists()) return true;
    for (const auto& file : QDir(root.filePath()).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name)) {
        ArtifactPair item;
        if (!file.isFile() || file.isSymLink() || !shardArtifact(file.fileName(), &item, errorText)) {
            if (errorText) *errorText = QStringLiteral("Unsupported archive snapshot artifact: %1").arg(file.filePath());
            return false;
        }
        pairs->append(item);
    }
    return true;
}

bool requireClosedRuntimeConnections(const QList<ArtifactPair>& pairs, QString* errorText)
{
    for (const auto& item : pairs) {
        if (SqliteConnectionUtils::hasOtherOpenConnectionsForPath(item.livePath)) {
            if (errorText) *errorText = QStringLiteral("Rollback snapshot requires closed database: %1").arg(item.livePath);
            return false;
        }
    }
    return true;
}

QStringList securityDeleteRoots(const QString& extraRoot = QString())
{
    QStringList roots{AppRuntime::securityDir()};
    if (!extraRoot.trimmed().isEmpty()) roots << extraRoot;
    return roots;
}

bool digest(const QString& path, QString* value, QString* errorText)
{
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!QFileInfo(path).isFile() || QFileInfo(path).isSymLink()
        || !file.open(QIODevice::ReadOnly) || !hash.addData(&file)
        || file.error() != QFileDevice::NoError) {
        if (errorText) *errorText = QStringLiteral("Не вдалося перевірити файл: %1").arg(path);
        return false;
    }
    *value = QString::fromLatin1(hash.result().toHex());
    return true;
}

bool copyVerified(const QString& from, const QString& to, const QString& expected, QString* errorText)
{
    QFile source(from);
    QSaveFile target(to);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!source.open(QIODevice::ReadOnly) || !target.open(QIODevice::WriteOnly)) {
        if (errorText) *errorText = QStringLiteral("Не вдалося відкрити файли копіювання: %1 -> %2").arg(from, to);
        return false;
    }
    while (!source.atEnd()) {
        const QByteArray block = source.read(1024 * 1024);
        if (source.error() != QFileDevice::NoError || target.write(block) != block.size()) {
            if (errorText) *errorText = QStringLiteral("Помилка копіювання: %1 -> %2").arg(from, to);
            return false;
        }
        hash.addData(block);
    }
    if (QString::fromLatin1(hash.result().toHex()) != expected || !target.commit()) {
        if (errorText) *errorText = QStringLiteral("Перевірка або збереження копії неуспішні: %1").arg(to);
        return false;
    }
    QString writtenHash;
    if(!digest(to,&writtenHash,errorText))return false;
    if(writtenHash!=expected) {
        if(errorText)*errorText=QStringLiteral("Written snapshot copy differs from expected digest: %1").arg(to);
        return false;
    }
    return true;
}

#include "ProfileSnapshotInventory.ipp"

}

QString migrationSnapshotsRootPath()
{
    return QDir(AppRuntime::securityDir()).filePath(QStringLiteral("legacy_import_snapshots"));
}

bool LegacyImportSafetySupport::createSnapshot(LegacyImportSafetySnapshot* snapshot, QString* errorText)
{
    if (snapshot) *snapshot = {};
    QLockFile lock(QDir(AppRuntime::securityDir()).filePath(QStringLiteral("migration_snapshot.lock")));
    QString interrupted;
    if (!lock.tryLock(0) || !MigrationRestoreJournal::pending(&interrupted, errorText) || !interrupted.isEmpty()) {
        if (errorText && errorText->isEmpty()) *errorText = QStringLiteral("Migration snapshot is busy or requires interrupted restore recovery");
        return false;
    }
    auto pairs = artifactPairs();
    if (!appendCurrentShards(&pairs, errorText) || !requireClosedRuntimeConnections(pairs, errorText)) return false;
    const QString root = migrationSnapshotsRootPath();
    const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"))
        + QStringLiteral("_") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString dirPath = QDir(root).filePath(QStringLiteral("snapshot_%1").arg(stamp));
    if (!QDir().mkpath(dirPath)) {
        if (errorText) *errorText = QStringLiteral("Не вдалося створити папку snapshot: %1").arg(dirPath);
        return false;
    }

    if(!NativeSnapshotMetadata::protectBackupDirectory(dirPath,errorText))return false;
    if (!MigrationProfileSettings::capture(QDir(AppRuntime::securityDir()).filePath(QStringLiteral("migration_settings_snapshot.json")), errorText)) return false;
    QJsonObject files;
    for (const ArtifactPair& item : pairs) {
        const bool present = QFileInfo::exists(item.livePath);
        QString hash;QJsonObject metadata;
        if(present && !NativeSnapshotMetadata::capture(item.livePath,&metadata,errorText))return false;
        if (present && (!digest(item.livePath, &hash, errorText)
            || !copyVerified(item.livePath, QDir(dirPath).filePath(item.fileName), hash, errorText))) return false;
        QJsonObject afterMetadata;
        if(present && (!NativeSnapshotMetadata::capture(item.livePath,&afterMetadata,errorText) || metadata!=afterMetadata))return false;
        files.insert(item.fileName, QJsonObject{{QStringLiteral("metadata"), metadata}, {QStringLiteral("present"), present},
                                              {QStringLiteral("sha256"), hash}});
    }
    QJsonArray profileInventory;
    if(!ProfileSnapshotInventory::capture(dirPath,pairs,&profileInventory,errorText))return false;
    QSaveFile manifest(QDir(dirPath).filePath(QStringLiteral("manifest.json")));
    const auto bytes = QJsonDocument(QJsonObject{{QStringLiteral("version"), 6},
                                                {QStringLiteral("files"), files},
                                                {QStringLiteral("profile_inventory"), profileInventory}}).toJson();
    if (!manifest.open(QIODevice::WriteOnly) || manifest.write(bytes) != bytes.size() || !manifest.commit()) {
        if (errorText) *errorText = QStringLiteral("Не вдалося зберегти маніфест резервної копії");
        return false;
    }

    if (snapshot) {
        snapshot->snapshotDirPath = dirPath;
        snapshot->created = true;
    }
    return true;
}

bool LegacyImportSafetySupport::restoreSnapshot(const LegacyImportSafetySnapshot& snapshot, QString* errorText)
{
    if (!snapshot.created || snapshot.snapshotDirPath.trimmed().isEmpty()) {
        if (errorText) *errorText = QStringLiteral("Snapshot is unavailable for restore");
        return false;
    }
    return restoreMigrationSnapshotFromDirectory(snapshot.snapshotDirPath, errorText);
}

bool restoreMigrationSnapshotFromDirectory(const QString& snapshotDirPath, QString* errorText)
{
    QLockFile lock(QDir(AppRuntime::securityDir()).filePath(QStringLiteral("migration_snapshot.lock")));
    if (!lock.tryLock(0)) {
        if (errorText) *errorText = QStringLiteral("Migration snapshot operation is busy");
        return false;
    }
    auto currentPairs = artifactPairs();
    if (!appendCurrentShards(&currentPairs, errorText) || !requireClosedRuntimeConnections(currentPairs, errorText)) return false;
    const QDir dir(snapshotDirPath);
    if (!dir.exists()) {
        if (errorText) *errorText = QStringLiteral("Папка snapshot не існує: %1").arg(snapshotDirPath);
        return false;
    }

    QFile manifest(dir.filePath(QStringLiteral("manifest.json")));
    if (!manifest.open(QIODevice::ReadOnly)) {
        if (errorText) *errorText = QStringLiteral("Немає перевіреного маніфесту резервної копії");
        return false;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(manifest.readAll(), &parseError);
    auto files = document.object().value(QStringLiteral("files")).toObject();
    const int snapshotVersion=document.object().value(QStringLiteral("version")).toInt();
    if (manifest.error() != QFileDevice::NoError || parseError.error != QJsonParseError::NoError
        || (snapshotVersion != 4 && snapshotVersion != 5 && snapshotVersion != 6)) {
        if (errorText) *errorText = QStringLiteral("Пошкоджений маніфест резервної копії");
        return false;
    }
    auto pairs = artifactPairs();
    QSet<QString> names;
    for (const auto& item : pairs) names.insert(item.fileName.toLower());
    for (auto it = files.begin(); it != files.end(); ++it) {
        if (names.contains(it.key().toLower())) continue;
        ArtifactPair item;
        if (!it.key().startsWith(QStringLiteral("shards_"))
            || !shardArtifact(it.key().mid(7), &item, errorText)) {
            if (errorText) *errorText = QStringLiteral("Unknown snapshot artifact: %1").arg(it.key());
            return false;
        }
        names.insert(item.fileName.toLower()); pairs.append(item);
    }
    if (files.size() != pairs.size()) {
        if (errorText) *errorText = QStringLiteral("Incomplete snapshot inventory");
        return false;
    }
    for (const auto& item : currentPairs) {
        if (names.contains(item.fileName.toLower())) continue;
        names.insert(item.fileName.toLower()); pairs.append(item);
        files.insert(item.fileName, QJsonObject{{QStringLiteral("present"), false}, {QStringLiteral("sha256"), QString()}});
    }
    if (!requireClosedRuntimeConnections(pairs, errorText)) return false;
    // Validate the entire snapshot before changing any live artifact. Missing
    // backup files must never be confused with files absent before migration.
    for (const ArtifactPair& item : pairs) {
        const auto entry = files.value(item.fileName).toObject();
        const auto present = entry.value(QStringLiteral("present"));
        const auto expected = entry.value(QStringLiteral("sha256"));
        QString actual;
        const auto path = dir.filePath(item.fileName);
        if (!present.isBool() || !expected.isString()
            || (present.toBool() && (!digest(path, &actual, errorText) || actual != expected.toString()))
            || (!present.toBool() && (!expected.toString().isEmpty() || QFileInfo::exists(path)))) {
            if (errorText) *errorText = QStringLiteral("Резервна копія неповна або пошкоджена: %1").arg(item.fileName);
            return false;
        }
    }
    if(snapshotVersion>=6)for(const auto& item:pairs) {
        const auto entry=files.value(item.fileName).toObject();
        if(entry.value("present").toBool() && !NativeSnapshotMetadata::validate(entry.value("metadata").toObject(),false,errorText))return false;
    }
    if (!MigrationProfileSettings::validate(dir.filePath(QStringLiteral("profile_settings.json")), errorText)) return false;
    // Reject invalid destinations for the entire set before replacing the first
    // file. This is preflight only, not a cross-file crash transaction.
    for (const ArtifactPair& item : pairs) {
        const QFileInfo target(item.livePath);
        const QFileInfo parent(target.absolutePath());
        const bool missingShardDirectory = item.fileName.startsWith(QStringLiteral("shards_"))
            && !parent.exists() && QFileInfo(parent.absolutePath()).isDir();
        if (target.isSymLink() || parent.isSymLink() || (target.exists() && !target.isFile())
            || (!parent.isDir() && !missingShardDirectory)) {
            if (errorText) *errorText = QStringLiteral("Недопустимий шлях відновлення: %1").arg(item.livePath);
            return false;
        }
    }
    QJsonArray currentProfile;
    const auto profileInventory=document.object().value(QStringLiteral("profile_inventory")).toArray();
    if(snapshotVersion>=5) {
        if(!document.object().value(QStringLiteral("profile_inventory")).isArray()) {
            if(errorText)*errorText=QStringLiteral("Missing or invalid profile inventory in snapshot");
            return false;
        }
        if(!ProfileSnapshotInventory::preflight(snapshotDirPath,profileInventory,pairs,&currentProfile,errorText,snapshotVersion>=6))return false;
    }
    QString manifestHash;
    if (!digest(dir.filePath(QStringLiteral("manifest.json")), &manifestHash, errorText)
        || !MigrationRestoreJournal::begin(snapshotDirPath, manifestHash, errorText)) return false;
    // Create missing archive parents before replacing any generation files.
    for (const auto& item : pairs) {
        if (!QDir().mkpath(QFileInfo(item.livePath).absolutePath())) {
            if (errorText) *errorText = QStringLiteral("Cannot create restore directory: %1").arg(item.livePath);
            return false;
        }
    }
    for (const ArtifactPair& item : pairs) {
        const auto entry = files.value(item.fileName).toObject();
        if (entry.value(QStringLiteral("present")).toBool()) {
            const QString hash=entry.value(QStringLiteral("sha256")).toString();
            const bool restored=snapshotVersion>=6
                ?SecureSnapshotRestore::restoreFile(dir.filePath(item.fileName),item.livePath,hash,entry.value("metadata").toObject(),copyVerified,errorText)
                :copyVerified(dir.filePath(item.fileName),item.livePath,hash,errorText);
            if(!restored)return false;
        } else if (!SafeFileOps::safeRemoveFile(item.livePath,
                       securityDeleteRoots(QFileInfo(item.livePath).absolutePath()), {}, {}, errorText)) return false;
    }
    if(snapshotVersion>=5 && !ProfileSnapshotInventory::restore(snapshotDirPath,profileInventory,currentProfile,errorText,snapshotVersion>=6))return false;
    if (!MigrationProfileSettings::restore(QDir(AppRuntime::securityDir()).filePath(QStringLiteral("migration_settings_snapshot.json")), errorText)) return false;
    if(snapshotVersion>=6) {
        for(const auto& item:pairs) {
            const auto entry=files.value(item.fileName).toObject();
            if(entry.value("present").toBool() && !NativeSnapshotMetadata::apply(item.livePath,entry.value("metadata").toObject(),errorText))return false;
        }
        if(!ProfileSnapshotInventory::restoreMetadata(profileInventory,errorText))return false;
    }
    return MigrationRestoreJournal::finish(errorText);
}

void LegacyImportSafetySupport::quarantineCurrentArtifacts(const LegacyImportSafetySnapshot& snapshot, const QString& reason)
{
    const QString base = snapshot.snapshotDirPath.trimmed().isEmpty() ? AppRuntime::securityDir() : snapshot.snapshotDirPath;
    const QString quarantineDir = QDir(base).filePath(QStringLiteral("quarantine_%1").arg(reason.trimmed().isEmpty() ? QStringLiteral("failed_import") : reason.trimmed()));
    QDir().mkpath(quarantineDir);

    auto pairs = artifactPairs();
    if (!appendCurrentShards(&pairs, nullptr)) return;
    for (const ArtifactPair& item : pairs) {
        if (!QFileInfo::exists(item.livePath)) continue;
        const QString dst = QDir(quarantineDir).filePath(item.fileName);
        SafeFileOps::safeRemoveFile(dst, securityDeleteRoots(quarantineDir));
        QFile::copy(item.livePath, dst);
    }
}
