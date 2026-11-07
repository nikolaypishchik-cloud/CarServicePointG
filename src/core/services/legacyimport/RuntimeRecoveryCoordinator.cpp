#include "core/services/legacyimport/RuntimeRecoveryCoordinator.h"

#include "runtime/AppRuntime.h"
#include "core/services/legacyimport/MigrationPreflightValidator.h"
#include "infrastructure/security/EncryptedStorage.h"

#include "core/services/storage/MigrationRestoreJournal.h"
#include "core/io/SafeFileOps.h"
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QDataStream>
#include <QSettings>
#include <QVariantMap>
#include "core/cloudsync/CloudSyncCredentials.h"

namespace {
QString restoreIntentPath()
{
    return QDir(AppRuntime::securityDir()).filePath(QStringLiteral("migration_restore_pending.json"));
}

bool checkedManifestHash(const QString& path, QString* hash, QString* errorText)
{
    // Only a direct, non-linked snapshot child of the managed backup root is valid.
    const QFileInfo dir(path);
    const QDir root(migrationSnapshotsRootPath());
    const QFileInfo manifest(QDir(path).filePath(QStringLiteral("manifest.json")));
    QFile input(manifest.filePath());
    QCryptographicHash digest(QCryptographicHash::Sha256);
    if (!dir.isDir() || dir.isSymLink() || QFileInfo(root.path()).isSymLink()
        || dir.absolutePath() != root.absolutePath() || !dir.fileName().startsWith(QStringLiteral("snapshot_"))
        || !manifest.isFile() || manifest.isSymLink() || !input.open(QIODevice::ReadOnly)
        || !digest.addData(&input) || input.error() != QFileDevice::NoError) {
        if (errorText) *errorText = QStringLiteral("Restore intent refers to an unavailable or unsafe snapshot");
        return false;
    }
    *hash = QString::fromLatin1(digest.result().toHex());
    return true;
}
}

namespace {
bool readProfileSettings(const QString& path, QVariantMap* values, QString* errorText)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorText) *errorText = QStringLiteral("Cannot read protected profile settings snapshot");
        return false;
    }
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse);
    if (file.error() != QFileDevice::NoError || parse.error != QJsonParseError::NoError
        || document.object().value(QStringLiteral("version")).toInt() != 1) {
        if (errorText) *errorText = QStringLiteral("Invalid protected profile settings snapshot");
        return false;
    }
    const auto plain = CloudSyncCredentials::unprotectToken(document.object().value(QStringLiteral("data_dpapi")).toString(), errorText);
    if (plain.isEmpty()) return false;
    const auto decoded = QByteArray::fromBase64Encoding(plain.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded) {
        if (errorText) *errorText = QStringLiteral("Invalid protected settings payload encoding");
        return false;
    }
    QDataStream stream(decoded.decoded); stream.setVersion(QDataStream::Qt_6_0);
    quint32 version = 0; QString identity; qint32 format = -1; QVariantMap result;
    stream >> version >> identity >> format >> result;
    QSettings settings;
    if (stream.status() != QDataStream::Ok || !stream.atEnd() || version != 1
        || identity != settings.fileName() || format != qint32(settings.format())) {
        if (errorText) *errorText = QStringLiteral("Settings snapshot belongs to another profile or is damaged");
        return false;
    }
    if (values) *values = result;
    return true;
}
}

bool MigrationProfileSettings::capture(const QString& path, QString* errorText)
{
    QSettings identity;
    QSettings settings(identity.fileName(), identity.format()); settings.sync();
    QVariantMap values;
    for (const auto& key : settings.allKeys()) values.insert(key, settings.value(key));
    if (settings.status() != QSettings::NoError) {
        if (errorText) *errorText = QStringLiteral("Cannot read profile settings for migration snapshot");
        return false;
    }
    QByteArray bytes; QDataStream stream(&bytes, QIODevice::WriteOnly); stream.setVersion(QDataStream::Qt_6_0);
    stream << quint32(1) << settings.fileName() << qint32(settings.format()) << values;
    if (stream.status() != QDataStream::Ok) {
        if (errorText) *errorText = QStringLiteral("Cannot serialize typed profile settings");
        return false;
    }
    const auto protectedData = CloudSyncCredentials::protectToken(QString::fromLatin1(bytes.toBase64()), errorText);
    if (protectedData.isEmpty()) return false;
    QSaveFile file(path);
    const auto data = QJsonDocument(QJsonObject{{QStringLiteral("version"), 1}, {QStringLiteral("data_dpapi"), protectedData}}).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        if (errorText) *errorText = QStringLiteral("Cannot commit protected profile settings snapshot");
        return false;
    }
    return true;
}

bool MigrationProfileSettings::validate(const QString& path, QString* errorText)
{
    return readProfileSettings(path, nullptr, errorText);
}

bool MigrationProfileSettings::storageRoots(const QString& path, QMap<QString,QString>* roots, QString* errorText)
{
    QVariantMap values;
    if(!roots || !readProfileSettings(path,&values,errorText))return false;
    roots->clear();
    for(const auto& suffix:QStringList{"pdf_dir","invoices_dir","reports_dir","order_photos_dir"}) {
        const QString key=QStringLiteral("settings.int.directories.")+suffix;
        roots->insert(key,values.value(key).toString().trimmed());
    }
    return true;
}

bool MigrationProfileSettings::restore(const QString& path, QString* errorText)
{
    QVariantMap values;
    if (!readProfileSettings(path, &values, errorText)) return false;
    QSettings identity;
    QSettings settings(identity.fileName(), identity.format());
    for (const auto& key : settings.allKeys()) if (!values.contains(key)) settings.remove(key);
    for (auto it = values.begin(); it != values.end(); ++it) settings.setValue(it.key(), it.value());
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        if (errorText) *errorText = QStringLiteral("Cannot restore protected profile settings");
        return false;
    }
    return true;
}

bool MigrationRestoreJournal::pending(QString* snapshotDirPath, QString* errorText)
{
    if (snapshotDirPath) snapshotDirPath->clear();
    const QFileInfo marker(restoreIntentPath());
    if (!marker.exists() && !marker.isSymLink()) return true;
    QFile input(marker.filePath());
    if (!marker.isFile() || marker.isSymLink() || !input.open(QIODevice::ReadOnly)) {
        if (errorText) *errorText = QStringLiteral("Cannot read pending migration restore intent");
        return false;
    }
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(input.readAll(), &parse);
    const auto obj = doc.object();
    const auto path = obj.value(QStringLiteral("snapshot_dir")).toString();
    const auto hash = obj.value(QStringLiteral("manifest_sha256")).toString();
    QString actual;
    if (input.error() != QFileDevice::NoError || parse.error != QJsonParseError::NoError
        || obj.value(QStringLiteral("version")).toInt() != 1
        || !QRegularExpression(QStringLiteral("^[0-9a-f]{64}$")).match(hash).hasMatch()
        || !checkedManifestHash(path, &actual, errorText) || actual != hash) {
        if (errorText) *errorText = QStringLiteral("Pending restore intent or snapshot manifest is damaged");
        return false;
    }
    if (snapshotDirPath) *snapshotDirPath = path;
    return true;
}

bool MigrationRestoreJournal::begin(const QString& snapshotDirPath, const QString& manifestHash, QString* errorText)
{
    QString previous, actual;
    if (!pending(&previous, errorText) || !checkedManifestHash(snapshotDirPath, &actual, errorText)) return false;
    if (actual != manifestHash || (!previous.isEmpty() && previous != snapshotDirPath)) {
        if (errorText) *errorText = QStringLiteral("Another or changed migration snapshot requires recovery first");
        return false;
    }
    QSaveFile marker(restoreIntentPath());
    const auto bytes = QJsonDocument(QJsonObject{{QStringLiteral("version"), 1},
        {QStringLiteral("snapshot_dir"), snapshotDirPath}, {QStringLiteral("manifest_sha256"), manifestHash}}).toJson();
    if (!marker.open(QIODevice::WriteOnly) || marker.write(bytes) != bytes.size() || !marker.commit()) {
        if (errorText) *errorText = QStringLiteral("Cannot commit migration restore intent");
        return false;
    }
    return true;
}

bool MigrationRestoreJournal::beginForSnapshot(const QString& snapshotDirPath, QString* errorText)
{
    QString hash;
    return checkedManifestHash(snapshotDirPath,&hash,errorText) && begin(snapshotDirPath,hash,errorText);
}

bool MigrationRestoreJournal::finish(QString* errorText)
{
    const auto path = restoreIntentPath();
    if (!SafeFileOps::safeRemoveFile(path, {AppRuntime::securityDir()}, {}, {}, errorText)) {
        if (errorText) *errorText = QStringLiteral("Cannot clear completed migration restore intent");
        return false;
    }
    return true;
}

bool RuntimeRecoveryCoordinator::recoverPendingSnapshotRestore(QString* errorText)
{
    QString snapshotDir;
    if (!MigrationRestoreJournal::pending(&snapshotDir, errorText)) return false;
    return snapshotDir.isEmpty() || restoreMigrationSnapshotFromDirectory(snapshotDir, errorText);
}

bool RuntimeRecoveryCoordinator::prepareMaterializedRuntime(QString* errorText)
{
    QString localError;
    if (!recoverPendingSnapshotRestore(&localError)) {
        if (errorText) *errorText = localError;
        return false;
    }
    if (!EncryptedStorage::prepareRuntimeDatabases(&localError)) {
        if (errorText) *errorText = localError;
        return false;
    }
    return true;
}

bool RuntimeRecoveryCoordinator::preparePublicationRuntime(const QString& snapshotDirPath,QString* errorText)
{
    // Only the live migration owning this exact verified rollback intent may open
    // its uncommitted generation. Normal startup always restores the snapshot.
    QString pending;
    if(!MigrationRestoreJournal::pending(&pending,errorText))return false;
    if(pending.isEmpty() || snapshotDirPath.isEmpty()
        || QFileInfo(pending).canonicalFilePath()!=QFileInfo(snapshotDirPath).canonicalFilePath()) {
        if(errorText)*errorText=QStringLiteral("Publication does not own the pending rollback snapshot");
        return false;
    }
    return EncryptedStorage::prepareRuntimeDatabases(errorText);
}

bool RuntimeRecoveryCoordinator::prepareValidatedRuntime(QString* errorText)
{
    QString localError;
    if (!prepareMaterializedRuntime(&localError)) {
        if (errorText) *errorText = localError;
        return false;
    }

    // First-run or post-clean bootstrap may legitimately start without a materialized
    // active workspace yet. Let openPrepared/schema_check create the empty runtime
    // instead of failing db_preparation on missing files.
    if (!QFileInfo::exists(AppRuntime::activeDbFilePath())
        && !QFileInfo::exists(AppRuntime::sealedActiveDbFilePath())) {
        return true;
    }

    if (!MigrationPreflightValidator::validateRuntimeTargets(&localError)) {
        if (errorText) *errorText = localError;
        return false;
    }
    return true;
}
