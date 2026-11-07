#include "core/backup/RestoreCoordinator.h"

#include "AppVersion.h"
#include "runtime/AppRuntime.h"
#include "core/backup/BackupLogging.h"
#include "infrastructure/audit/BackupRecoveryAuditTrail.h"
#include "core/backup/BackupManager.h"
#include "core/backup/BackupManifestService.h"
#include "core/backup/BackupPaths.h"
#include "core/backup/BackupPolicy.h"
#include "core/backup/BackupSnapshotService.h"
#include "core/backup/CrashRecoveryManager.h"
#include "core/backup/DataProtectionCoordinator.h"
#include "core/services/DatabaseImportClassifier.h"
#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/security/EncryptedStorage.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>
#include <QSqlQuery>
#include <QScopeGuard>

namespace {
QString sha256ForRestoreFile(const QString& path, QString* errorText)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorText) *errorText = file.errorString();
        return QString();
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        if (errorText) *errorText = QStringLiteral("Не вдалося прочитати snapshot для SHA256");
        return QString();
    }
    if (errorText) errorText->clear();
    return QString::fromLatin1(hash.result().toHex());
}

bool appVersionCompatible(const QString& snapshotVersion, const QString& runtimeVersion, QString* errorText)
{
    const QString sv = snapshotVersion.trimmed();
    const QString rv = runtimeVersion.trimmed();
    if (sv.isEmpty() || rv.isEmpty()) {
        if (errorText) errorText->clear();
        return true;
    }
    const QString snapshotMajor = sv.section(QLatin1Char('.'), 0, 0);
    const QString runtimeMajor = rv.section(QLatin1Char('.'), 0, 0);
    if (!snapshotMajor.isEmpty() && !runtimeMajor.isEmpty() && snapshotMajor != runtimeMajor) {
        if (errorText) *errorText = QStringLiteral("AppVersion mismatch: snapshot=%1, runtime=%2").arg(sv, rv);
        return false;
    }
    if (errorText) errorText->clear();
    return true;
}

bool preflightSnapshotForRestore(const SnapshotMetadata& meta, QString* errorText)
{
    QFileInfo snapshotInfo(meta.snapshotPath);
    if (!snapshotInfo.exists() || snapshotInfo.size() <= 0) {
        if (errorText) *errorText = QStringLiteral("Snapshot-файл не знайдено або порожній: %1").arg(QDir::toNativeSeparators(meta.snapshotPath));
        return false;
    }
    if (AppRuntime::sealedActiveDbFilePath().trimmed().isEmpty()) {
        if (errorText) *errorText = QStringLiteral("Active DB path is empty; restore cannot continue");
        return false;
    }
    if (!QFileInfo::exists(AppRuntime::sealedActiveDbFilePath())) {
        if (errorText) {
            *errorText = QStringLiteral("Active DB file is not available for rollback/export: %1")
                             .arg(QDir::toNativeSeparators(AppRuntime::sealedActiveDbFilePath()));
        }
        return false;
    }
    QStorageInfo targetStorage(QFileInfo(AppRuntime::sealedActiveDbFilePath()).absolutePath());
    if (targetStorage.isValid() && targetStorage.bytesAvailable() < qMax<qint64>(snapshotInfo.size() * 2, 64ll * 1024ll * 1024ll)) {
        if (errorText) *errorText = QStringLiteral("Недостатньо місця для restore/rollback");
        return false;
    }
    QString backupRootError;
    if (!BackupPaths::probeBackupRoot(BackupPaths::configuredBackupsRoot(), &backupRootError)) {
        if (errorText) *errorText = QStringLiteral("Backup root недоступний для restore emergency/rollback: %1").arg(backupRootError);
        return false;
    }
    if (!meta.hashSha256.trimmed().isEmpty()) {
        QString hashError;
        const QString actualHash = sha256ForRestoreFile(meta.snapshotPath, &hashError);
        if (actualHash.isEmpty() || actualHash.compare(meta.hashSha256.trimmed(), Qt::CaseInsensitive) != 0) {
            if (errorText) {
                *errorText = QStringLiteral("Snapshot hash mismatch: expected=%1 actual=%2 error=%3")
                                 .arg(meta.hashSha256.trimmed(), actualHash, hashError);
            }
            return false;
        }
    }
    QString validationError;
    if (!BackupSnapshotService::validateSnapshot(meta.snapshotPath, &validationError)) {
        if (errorText) *errorText = QStringLiteral("Snapshot не пройшов перевірку цілісності: %1").arg(validationError);
        return false;
    }
    QString appVersionError;
    if (!appVersionCompatible(meta.appVersion, CarService::AppVersion::versionString(), &appVersionError)) {
        if (errorText) *errorText = appVersionError;
        return false;
    }
    const QString currentSchemaVersion = Database::getSetting(QStringLiteral("app.schema.version"), QStringLiteral("runtime")).toString().trimmed();
    if (!meta.schemaVersion.trimmed().isEmpty()
        && !currentSchemaVersion.isEmpty()
        && meta.schemaVersion.trimmed() != currentSchemaVersion) {
        if (errorText) {
            *errorText = QStringLiteral("Schema mismatch: snapshot=%1, runtime=%2")
                             .arg(meta.schemaVersion.trimmed(), currentSchemaVersion);
        }
        return false;
    }
    if (errorText) errorText->clear();
    return true;
}

bool reopenRuntimeDatabase(QString* errorText)
{
    return Database::prepareRuntime(errorText)
        && Database::openPrepared(errorText)
        && Database::runStartupSchemaCheck(errorText)
        && Database::runStartupMigrationCheck(errorText)
        && Database::runStartupArchiveMaintenance(errorText)
        && Database::runStartupRegistryRefresh(errorText);
}

bool rollbackToDatabase(const QString& rollbackPath, QString* errorText)
{
    QString rollbackError;
    Database::close();
    if (!EncryptedStorage::restorePlaintextBackupToActive(rollbackPath, &rollbackError)) {
        if (errorText) *errorText = QStringLiteral("Restore rollback failed: %1").arg(rollbackError);
        return false;
    }
    if (!reopenRuntimeDatabase(&rollbackError)) {
        if (errorText) *errorText = QStringLiteral("Rollback DB restored but could not be reopened: %1").arg(rollbackError);
        return false;
    }
    if (errorText) errorText->clear();
    return true;
}
}

RestoreCoordinator::RestoreCoordinator(BackupManager* backupManager, CrashRecoveryManager* crashRecoveryManager, QObject* parent)
    : QObject(parent)
    , m_backupManager(backupManager)
    , m_crashRecoveryManager(crashRecoveryManager)
{
}

QVector<SnapshotMetadata> RestoreCoordinator::listSnapshots() const
{
    return BackupManifestService::listSnapshots();
}

bool RestoreCoordinator::restoreSnapshot(const SnapshotMetadata& meta, NativeBackupRestoreResult* result, QString* errorText)
{
    auto restoreGate = DataProtectionCoordinator::tryEnter(DataProtectionCoordinator::Operation::Restore,
                                                           QStringLiteral("snapshot restore requested"),
                                                           errorText,
                                                           0);
    if (!restoreGate.isActive()) {
        return false;
    }
    const auto finishRestoreGuard = qScopeGuard([this]() {
        if (m_backupManager) m_backupManager->finishRestore();
    });

    BackupRecoveryAuditTrail::appendRecoveryEvent(QStringLiteral("started"),
                                                  QStringLiteral("restore_snapshot"),
                                                  !meta.snapshotId.trimmed().isEmpty() ? meta.snapshotId : meta.commitId,
                                                  QStringLiteral("RestoreCoordinator"),
                                                  meta.entityType,
                                                  meta.entityId,
                                                  QStringLiteral("snapshot restore started"));
    restoreGate.setState(QStringLiteral("RestorePreflight"));
    BackupRecoveryAuditTrail::appendRecoveryEvent(QStringLiteral("started"),
                                                  QStringLiteral("restore_preflight"),
                                                  !meta.snapshotId.trimmed().isEmpty() ? meta.snapshotId : meta.commitId,
                                                  QStringLiteral("RestoreCoordinator"),
                                                  meta.entityType,
                                                  meta.entityId,
                                                  QStringLiteral("restore preflight started: exists/hash/quick_check/schema/appVersion/space/backup-root"));

    QString preflightError;
    if (!preflightSnapshotForRestore(meta, &preflightError)) {
        if (errorText) *errorText = preflightError;
        BackupLogging::warn(QStringLiteral("restore preflight failed %1").arg(preflightError));
        BackupRecoveryAuditTrail::appendRecoveryEvent(QStringLiteral("failed"),
                                                      QStringLiteral("restore_preflight"),
                                                      !meta.snapshotId.trimmed().isEmpty() ? meta.snapshotId : meta.commitId,
                                                      QStringLiteral("RestoreCoordinator"),
                                                      meta.entityType,
                                                      meta.entityId,
                                                      QStringLiteral("restore preflight failed"),
                                                      preflightError);
        return false;
    }
    BackupRecoveryAuditTrail::appendRecoveryEvent(QStringLiteral("completed"),
                                                  QStringLiteral("restore_preflight"),
                                                  !meta.snapshotId.trimmed().isEmpty() ? meta.snapshotId : meta.commitId,
                                                  QStringLiteral("RestoreCoordinator"),
                                                  meta.entityType,
                                                  meta.entityId,
                                                  QStringLiteral("restore preflight completed"));

    if (m_backupManager) {
        restoreGate.setState(QStringLiteral("RestoreDrainBackupJobs"));
        QString pauseError;
        if (!m_backupManager->prepareForRestore(&pauseError, 5000)) {
            if (errorText) *errorText = QStringLiteral("Restore скасовано: не вдалося зупинити backup/cleanup jobs: %1").arg(pauseError);
            BackupRecoveryAuditTrail::appendRecoveryEvent(QStringLiteral("failed"),
                                                          QStringLiteral("restore_backup_pause"),
                                                          !meta.snapshotId.trimmed().isEmpty() ? meta.snapshotId : meta.commitId,
                                                          QStringLiteral("RestoreCoordinator"),
                                                          meta.entityType,
                                                          meta.entityId,
                                                          QStringLiteral("restore cancelled because backup manager could not pause/drain"),
                                                          pauseError);
            return false;
        }
    }

    if (m_crashRecoveryManager) {
        restoreGate.setState(QStringLiteral("RecoveryDraftAutosaveRunning"));
        QString draftError;
        if (!m_crashRecoveryManager->flushDirtyDraftsBeforeCriticalOperation(&draftError)) {
            if (errorText) *errorText = QStringLiteral("Restore скасовано: recovery autosave незбережених даних не вдався: %1").arg(draftError);
            BackupRecoveryAuditTrail::appendRecoveryEvent(QStringLiteral("failed"),
                                                          QStringLiteral("draft_autosave_before_restore"),
                                                          !meta.snapshotId.trimmed().isEmpty() ? meta.snapshotId : meta.commitId,
                                                          QStringLiteral("RestoreCoordinator"),
                                                          meta.entityType,
                                                          meta.entityId,
                                                          QStringLiteral("restore cancelled because recovery draft autosave failed"),
                                                          draftError);
            return false;
        }
    }

    if (BackupPolicy::load().emergencyBeforeRestore && m_backupManager) {
        restoreGate.setState(QStringLiteral("RestoreEmergencyBackup"));
        QString emergencyError;
        if (!m_backupManager->createEmergencyBackupNow(QStringLiteral("Emergency backup before restore"), nullptr, &emergencyError)) {
            if (errorText) {
                *errorText = QStringLiteral("Не вдалося створити emergency backup перед restore: %1").arg(emergencyError);
            }
            BackupLogging::warn(QStringLiteral("restore aborted emergency backup failed %1").arg(emergencyError));
            BackupRecoveryAuditTrail::appendRecoveryEvent(QStringLiteral("cancelled"),
                                                          QStringLiteral("restore_snapshot"),
                                                          !meta.snapshotId.trimmed().isEmpty() ? meta.snapshotId : meta.commitId,
                                                          QStringLiteral("RestoreCoordinator"),
                                                          meta.entityType,
                                                          meta.entityId,
                                                          QStringLiteral("snapshot restore cancelled because emergency backup failed"),
                                                          emergencyError);
            return false;
        }
    }

    DatabaseImportClassification classification;
    QString detectError;
    if (!DatabaseImportClassifier::classify(meta.snapshotPath, &classification, &detectError)) {
        if (errorText) *errorText = detectError;
        BackupRecoveryAuditTrail::appendRecoveryEvent(QStringLiteral("failed"),
                                                      QStringLiteral("restore_snapshot"),
                                                      !meta.snapshotId.trimmed().isEmpty() ? meta.snapshotId : meta.commitId,
                                                      QStringLiteral("RestoreCoordinator"),
                                                      meta.entityType,
                                                      meta.entityId,
                                                      QStringLiteral("snapshot restore classification failed"),
                                                      detectError);
        return false;
    }
    if (!classification.isNativeBackup()) {
        if (errorText) *errorText = QStringLiteral("Обраний snapshot не проходить перевірку як native backup");
        BackupRecoveryAuditTrail::appendRecoveryEvent(QStringLiteral("failed"),
                                                      QStringLiteral("restore_snapshot"),
                                                      !meta.snapshotId.trimmed().isEmpty() ? meta.snapshotId : meta.commitId,
                                                      QStringLiteral("RestoreCoordinator"),
                                                      meta.entityType,
                                                      meta.entityId,
                                                      QStringLiteral("snapshot restore failed because backup is not native"),
                                                      QStringLiteral("not_native_backup"));
        return false;
    }

    const QString rollbackDir = QDir(AppRuntime::securityDir()).filePath(QStringLiteral("restore_coordinator"));
    QDir().mkpath(rollbackDir);
    const QString rollbackPath = QDir(rollbackDir).filePath(
        QStringLiteral("active_before_restore_%1.db").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"))));

    QString backupError;
    restoreGate.setState(QStringLiteral("RestoreDbClosed"));
    BackupRecoveryAuditTrail::appendRecoveryEvent(QStringLiteral("started"),
                                                  QStringLiteral("restore_db_close"),
                                                  !meta.snapshotId.trimmed().isEmpty() ? meta.snapshotId : meta.commitId,
                                                  QStringLiteral("RestoreCoordinator"),
                                                  meta.entityType,
                                                  meta.entityId,
                                                  QStringLiteral("DB closed for controlled restore"));
    Database::close();
    if (!EncryptedStorage::exportPlaintextDatabase(AppRuntime::sealedActiveDbFilePath(), rollbackPath, &backupError)) {
        if (errorText) *errorText = backupError;
        BackupRecoveryAuditTrail::appendRecoveryEvent(QStringLiteral("failed"),
                                                      QStringLiteral("restore_snapshot"),
                                                      !meta.snapshotId.trimmed().isEmpty() ? meta.snapshotId : meta.commitId,
                                                      QStringLiteral("RestoreCoordinator"),
                                                      meta.entityType,
                                                      meta.entityId,
                                                      QStringLiteral("snapshot restore failed while exporting rollback backup"),
                                                      backupError);
        return false;
    }

    QString restoreError;
    restoreGate.setState(QStringLiteral("RestoreReplacingDb"));
    BackupRecoveryAuditTrail::appendRecoveryEvent(QStringLiteral("started"),
                                                  QStringLiteral("restore_db_replace"),
                                                  !meta.snapshotId.trimmed().isEmpty() ? meta.snapshotId : meta.commitId,
                                                  QStringLiteral("RestoreCoordinator"),
                                                  meta.entityType,
                                                  meta.entityId,
                                                  QStringLiteral("active DB replacement started"));
    if (!EncryptedStorage::restorePlaintextBackupToActive(meta.snapshotPath, &restoreError)) {
        restoreGate.setState(QStringLiteral("RestoreRollback"));
        QString rollbackError;
        const bool rollbackOk = rollbackToDatabase(rollbackPath, &rollbackError);
        if (errorText) *errorText = rollbackOk
            ? QStringLiteral("Restore failed, rollback applied: %1").arg(restoreError)
            : QStringLiteral("Restore failed and rollback failed: restore=%1 rollback=%2").arg(restoreError, rollbackError);
        BackupRecoveryAuditTrail::appendRecoveryEvent(QStringLiteral("failed"),
                                                      QStringLiteral("restore_snapshot"),
                                                      !meta.snapshotId.trimmed().isEmpty() ? meta.snapshotId : meta.commitId,
                                                      QStringLiteral("RestoreCoordinator"),
                                                      meta.entityType,
                                                      meta.entityId,
                                                      QStringLiteral("snapshot restore failed while replacing active database; rollback attempted"),
                                                      errorText ? *errorText : restoreError);
        return false;
    }

    QString reopenError;
    restoreGate.setState(QStringLiteral("RestoreReopeningDb"));
    if (!reopenRuntimeDatabase(&reopenError)) {
        restoreGate.setState(QStringLiteral("RestoreRollback"));
        QString rollbackError;
        const bool rollbackOk = rollbackToDatabase(rollbackPath, &rollbackError);
        if (errorText) *errorText = rollbackOk
            ? QStringLiteral("Restore reopen failed, rollback applied: %1").arg(reopenError)
            : QStringLiteral("Restore reopen failed and rollback failed: reopen=%1 rollback=%2").arg(reopenError, rollbackError);
        BackupRecoveryAuditTrail::appendRecoveryEvent(QStringLiteral("failed"),
                                                      QStringLiteral("restore_snapshot"),
                                                      !meta.snapshotId.trimmed().isEmpty() ? meta.snapshotId : meta.commitId,
                                                      QStringLiteral("RestoreCoordinator"),
                                                      meta.entityType,
                                                      meta.entityId,
                                                      QStringLiteral("snapshot restore failed during runtime reopen/startup checks; rollback attempted"),
                                                      errorText ? *errorText : reopenError);
        return false;
    }

    QString postRestoreValidationError;
    if (!BackupSnapshotService::validateSnapshot(AppRuntime::activeDbFilePath(), &postRestoreValidationError)) {
        restoreGate.setState(QStringLiteral("RestoreRollback"));
        QString rollbackError;
        const bool rollbackOk = rollbackToDatabase(rollbackPath, &rollbackError);
        if (errorText) *errorText = rollbackOk
            ? QStringLiteral("Restored DB validation failed, rollback applied: %1").arg(postRestoreValidationError)
            : QStringLiteral("Restored DB validation failed and rollback failed: validation=%1 rollback=%2").arg(postRestoreValidationError, rollbackError);
        BackupRecoveryAuditTrail::appendRecoveryEvent(QStringLiteral("failed"),
                                                      QStringLiteral("restore_validate_restored_db"),
                                                      !meta.snapshotId.trimmed().isEmpty() ? meta.snapshotId : meta.commitId,
                                                      QStringLiteral("RestoreCoordinator"),
                                                      meta.entityType,
                                                      meta.entityId,
                                                      QStringLiteral("restored DB failed validation; rollback attempted"),
                                                      errorText ? *errorText : postRestoreValidationError);
        return false;
    }
    BackupRecoveryAuditTrail::appendRecoveryEvent(QStringLiteral("completed"),
                                                  QStringLiteral("restore_validate_restored_db"),
                                                  !meta.snapshotId.trimmed().isEmpty() ? meta.snapshotId : meta.commitId,
                                                  QStringLiteral("RestoreCoordinator"),
                                                  meta.entityType,
                                                  meta.entityId,
                                                  QStringLiteral("restored DB validated; restart required to rebuild caches"));

    if (result) {
        result->sourcePath = meta.snapshotPath;
        result->targetDbPath = AppRuntime::activeDbFilePath();
        result->rollbackBackupPath = rollbackPath;
        QSqlQuery q(Database::db());
        if (q.exec(QStringLiteral("SELECT COUNT(*) FROM work_orders")) && q.next()) {
            result->restoredOrderCount = q.value(0).toInt();
        }
        result->restartRequired = true;
    }
    restoreGate.setState(QStringLiteral("RestoreCompleted"));
    BackupLogging::info(QStringLiteral("restore completed snapshot=%1 restart_required=true").arg(QDir::toNativeSeparators(meta.snapshotPath)));
    BackupRecoveryAuditTrail::appendRecoveryEvent(QStringLiteral("completed"),
                                                  QStringLiteral("restore_snapshot"),
                                                  !meta.snapshotId.trimmed().isEmpty() ? meta.snapshotId : meta.commitId,
                                                  QStringLiteral("RestoreCoordinator"),
                                                  meta.entityType,
                                                  meta.entityId,
                                                  QStringLiteral("snapshot restore completed; restart required to rebuild caches"));
    return true;
}
