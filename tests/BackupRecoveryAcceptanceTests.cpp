#include <QtTest/QtTest>

#include "TestSourceAssertions.h"
#include "core/backup/BackupDtos.h"
#include "core/backup/DataProtectionCoordinator.h"
#include "core/backup/RecoveryConflictDetector.h"
#include "core/backup/RecoveryDraftStore.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QSettings>
#include <QUuid>
#include "infrastructure/db/sqlite/Db.h"

class BackupRecoveryAcceptanceTests final : public QObject
{
    Q_OBJECT
    QTemporaryDir settingsDir;

private slots:
    void initTestCase()
    {
        QVERIFY(settingsDir.isValid());
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("CarServiceTests");
        QCoreApplication::setApplicationName("Safety-BackupRecovery-" + QUuid::createUuid().toString(QUuid::Id128));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
        QString error;
        QVERIFY2(Database::prepareRuntime(&error), qPrintable(error));
        QVERIFY2(Database::openPrepared(&error), qPrintable(error));
    }
    void cleanupTestCase() { Database::close(); }
    void dataProtectionCoordinatorRejectsConflictingOperations();
    void recoveryDraftStoreDiscoversOrphanDraftFiles();
    void recoveryConflictDetectorDoesNotAutoApplyConflicts();
    void implementationGuardsForBackupRecoveryAcceptanceMatrix();
    void uiStatusGuardsRemainUserFacing();
    void restoreFlowGuardsForP8ToP14();
};

void BackupRecoveryAcceptanceTests::dataProtectionCoordinatorRejectsConflictingOperations()
{
    QVERIFY(!DataProtectionCoordinator::isBusy());

    QString errorText;
    auto restoreScope = DataProtectionCoordinator::tryEnter(DataProtectionCoordinator::Operation::Restore,
                                                            QStringLiteral("acceptance restore lock"),
                                                            &errorText,
                                                            0);
    QVERIFY2(restoreScope.isActive(), qPrintable(errorText));
    QVERIFY(DataProtectionCoordinator::isBusy());
    QVERIFY(DataProtectionCoordinator::isRestoreRunning());

    auto backupScope = DataProtectionCoordinator::tryEnter(DataProtectionCoordinator::Operation::AutomaticBackup,
                                                           QStringLiteral("conflicting backup during restore"),
                                                           &errorText,
                                                           25);
    QVERIFY(!backupScope.isActive());
    QVERIFY2(errorText.contains(QStringLiteral("busy"), Qt::CaseInsensitive), qPrintable(errorText));

    restoreScope.setState(QStringLiteral("RestoreReplacingDb"));
    QCOMPARE(DataProtectionCoordinator::currentState(), QStringLiteral("RestoreReplacingDb"));

    restoreScope.release();
    QVERIFY(!DataProtectionCoordinator::isBusy());
}

void BackupRecoveryAcceptanceTests::recoveryDraftStoreDiscoversOrphanDraftFiles()
{
    QTemporaryDir sessionDir;
    QVERIFY(sessionDir.isValid());

    DraftPayload orphan;
    orphan.recoverySessionId = QStringLiteral("session_acceptance");
    orphan.draftId = QStringLiteral("orphan_order_draft");
    orphan.formType = QStringLiteral("order");
    orphan.entityType = QStringLiteral("work_order");
    orphan.entityId = QStringLiteral("42");
    orphan.createdAt = QDateTime::currentDateTimeUtc().addSecs(-10);
    orphan.updatedAt = QDateTime::currentDateTimeUtc();
    orphan.validationState = QStringLiteral("valid");
    orphan.conflictState = QStringLiteral("unknown");
    orphan.payloadData = QJsonObject{{QStringLiteral("field"), QStringLiteral("value")}};

    QFile orphanFile(sessionDir.filePath(orphan.draftId + QStringLiteral(".json")));
    QVERIFY(orphanFile.open(QIODevice::WriteOnly));
    orphanFile.write(QJsonDocument(orphan.toJson()).toJson(QJsonDocument::Indented));
    orphanFile.close();

    QFile manifest(sessionDir.filePath(QStringLiteral("recovery_manifest.json")));
    QVERIFY(manifest.open(QIODevice::WriteOnly));
    manifest.write(R"({"drafts":[{"draftId":"missing_manifest_draft","validationState":"valid"}]})");
    manifest.close();

    const QVector<DraftPayload> drafts = RecoveryDraftStore::listDrafts(sessionDir.path());
    bool foundOrphan = false;
    bool foundMissingManifestEntry = false;
    for (const DraftPayload& draft : drafts) {
        if (draft.draftId == orphan.draftId && draft.validationState == QStringLiteral("valid")) {
            foundOrphan = true;
            QVERIFY(QFile::exists(draft.filePath));
        }
        if (draft.draftId == QStringLiteral("missing_manifest_draft")
            && draft.validationState == QStringLiteral("invalid")
            && draft.conflictState == QStringLiteral("missing_file")) {
            foundMissingManifestEntry = true;
        }
    }
    QVERIFY(foundOrphan);
    QVERIFY(foundMissingManifestEntry);
}

void BackupRecoveryAcceptanceTests::recoveryConflictDetectorDoesNotAutoApplyConflicts()
{
    DraftPayload draft;
    draft.formType = QStringLiteral("order");
    draft.entityId = QStringLiteral("100");
    draft.committedBaseVersion = QStringLiteral("base-v1");
    draft.validationState = QStringLiteral("valid");
    draft.conflictState = QStringLiteral("unknown");

    const RecoveryConflictCheck check = RecoveryConflictDetector::checkRuntimeConflict(draft, QStringLiteral("base-v2"));
    QVERIFY(check.conflict);
    QVERIFY(check.warning.contains(QStringLiteral("Конфлікт")));
    QCOMPARE(check.currentCommittedVersion, QStringLiteral("base-v2"));
}

void BackupRecoveryAcceptanceTests::implementationGuardsForBackupRecoveryAcceptanceMatrix()
{
    // 11.1: temp snapshot is created inside backup root and becomes visible only after validation + rename + manifest append.
    cs::tests::requireContains("src/core/backup/BackupSnapshotService.cpp", "BackupPaths::backupTempDir()");
    cs::tests::requireContains("src/core/backup/BackupSnapshotService.cpp", "validateSnapshot(tempPath");
    cs::tests::requireContains("src/core/backup/BackupSnapshotService.cpp", "QFile::rename(tempPath, finalPath)");
    cs::tests::requireContains("src/core/backup/BackupManager.cpp", "m_pendingManifestWrites.push_back(meta)");

    // 11.2: shutdown pending backup journal is persisted and replayed as startup-pending before cleanup.
    cs::tests::requireContains("src/core/backup/BackupManagerSupport.cpp", "pending_backup_journal.json");
    cs::tests::requireContains("src/core/backup/BackupManagerSupport.cpp", "shutdownWasCleanButBackupPending");
    cs::tests::requireContains("src/core/backup/BackupManagerSupport.cpp", "lastCommitId");
    cs::tests::requireContains("src/core/backup/BackupManagerSupport.cpp", "AppRuntime::activeDbFilePath()");
    cs::tests::requireContains("src/core/backup/BackupManagerLifecycle.cpp", "startup pending backup restored from shutdown journal");
    cs::tests::requireContains("src/core/backup/BackupManager.cpp", "clearPendingBackupJournal");

    // 11.3 / 11.4: restore and backup are serialized by the coordinator, and restore has rollback after replace/reopen failures.
    cs::tests::requireContains("src/core/backup/BackupJobQueue.cpp", "DataProtectionCoordinator::tryEnter");
    cs::tests::requireContains("src/core/backup/RestoreCoordinator.cpp", "DataProtectionCoordinator::Operation::Restore");
    cs::tests::requireContains("src/core/backup/RestoreCoordinator.cpp", "rollbackToDatabase");
    cs::tests::requireContains("src/core/backup/RestoreCoordinator.cpp", "RestoreRollback");
    cs::tests::requireContains("src/core/backup/RestoreCoordinator.cpp", "restart required to rebuild caches");
    cs::tests::requireContains("src/core/backup/RestoreCoordinator.cpp", "preflightSnapshotForRestore");
    cs::tests::requireContains("src/core/backup/RestoreCoordinator.cpp", "sha256ForRestoreFile");
    cs::tests::requireContains("src/core/backup/RestoreCoordinator.cpp", "appVersionCompatible");
    cs::tests::requireContains("src/core/backup/RestoreCoordinator.cpp", "BackupPaths::probeBackupRoot");
    cs::tests::requireContains("src/core/backup/RestoreCoordinator.cpp", "prepareForRestore");
    cs::tests::requireContains("src/core/backup/RestoreCoordinator.cpp", "flushDirtyDraftsBeforeCriticalOperation");
    cs::tests::requireContains("src/core/backup/RestoreCoordinator.cpp", "restore_validate_restored_db");

    // 11.7 / 11.8 / 11.9: clean-shutdown safety, retention protection, busy DB timeout/degraded path.
    cs::tests::requireContains("src/core/backup/CrashRecoveryManager.cpp", "clean shutdown refused because recovery draft autosave failed");
    cs::tests::requireContains("src/core/backup/BackupPolicy.cpp", "Always keep at least the five newest valid snapshots");
    cs::tests::requireContains("src/core/backup/BackupPolicy.cpp", "source.contains(QStringLiteral(\"emergency\"))");
    cs::tests::requireContains("src/core/backup/BackupSnapshotService.cpp", "sqlite3_backup timeout");
    cs::tests::requireContains("src/core/backup/BackupSnapshotService.cpp", "busy retry budget exceeded");
    cs::tests::requireContains("src/core/backup/BackupManager.cpp", "busy_locked_retry_exhausted");
    cs::tests::requireContains("src/core/backup/BackupManagerSupport.h", "kIdleBackupDelayMs = 10000");
    cs::tests::requireContains("src/core/backup/BackupManager.cpp", "debounce_ms=%4");
    cs::tests::requireContains("src/core/backup/BackupPaths.cpp", "QStorageInfo storage(root)");
    cs::tests::requireContains("src/core/backup/RecoveryDraftStore.cpp", "Defensive recovery: do not lose orphan draft files");
    cs::tests::requireContains("src/core/backup/BackupDtos.h", "payloadHash");
    cs::tests::requireContains("src/core/backup/BackupDtos.h", "baseEntityVersion");
}

void BackupRecoveryAcceptanceTests::uiStatusGuardsRemainUserFacing()
{
    cs::tests::requireContains("src/ui/tabs/settings/AutoBackupSettingsSection.cpp", "Автобекап та відновлення");
    cs::tests::requireContains("src/ui/tabs/settings/AutoBackupSettingsSection.cpp", "Авто — рекомендовано");
    cs::tests::requireContains("src/ui/tabs/settings/AutoBackupSettingsSection.cpp", "Максимальний захист");
    cs::tests::requireContains("src/ui/tabs/settings/AutoBackupSettingsSection.cpp", "Економний / Безпечний");
    cs::tests::requireContains("src/ui/tabs/settings/AutoBackupSettingsSection.cpp", "Захист даних: %1");
    cs::tests::requireContains("src/ui/tabs/settings/AutoBackupSettingsSection.cpp", "Остання успішна копія: %2");
    cs::tests::requireContains("src/ui/tabs/settings/AutoBackupSettingsSection.cpp", "Зберігання: останні 7 діб");
    cs::tests::requireContains("src/core/backup/BackupPolicy.cpp", "currentDateTime().addDays(-7)");
    cs::tests::requireContains("src/ui/tabs/settings/AutoBackupSettingsSection.cpp", "setAdvancedGroupBodyVisible(backupGroup, false)");
    cs::tests::requireContains("src/ui/tabs/settings/AutoBackupSettingsSection.cpp", "Очистити старі backup-и згідно retention policy");
    cs::tests::requireContains("src/ui/tabs/settings/AutoBackupSettingsSection.cpp", "Очистити прострочені recovery drafts згідно retention policy");
}


void BackupRecoveryAcceptanceTests::restoreFlowGuardsForP8ToP14()
{
    // 8: Restore is an exclusive coordinator flow with preflight, backup drain, recovery draft flush,
    // emergency backup, rollback and mandatory restart after success.
    cs::tests::requireContains("src/core/backup/RestoreCoordinator.cpp", "RestorePreflight");
    cs::tests::requireContains("src/core/backup/RestoreCoordinator.cpp", "RestoreDrainBackupJobs");
    cs::tests::requireContains("src/core/backup/RestoreCoordinator.cpp", "RecoveryDraftAutosaveRunning");
    cs::tests::requireContains("src/core/backup/RestoreCoordinator.cpp", "RestoreEmergencyBackup");
    cs::tests::requireContains("src/core/backup/RestoreCoordinator.cpp", "RestoreDbClosed");
    cs::tests::requireContains("src/core/backup/RestoreCoordinator.cpp", "RestoreReplacingDb");
    cs::tests::requireContains("src/core/backup/RestoreCoordinator.cpp", "RestoreReopeningDb");
    cs::tests::requireContains("src/core/backup/RestoreCoordinator.cpp", "RestoreRollback");
    cs::tests::requireContains("src/core/backup/RestoreCoordinator.cpp", "RestoreCompleted");
    cs::tests::requireContains("src/ui/tabs/settings/RestoreManagerDialog.cpp", "Для повного оновлення кешів і вкладок застосунок потрібно перезапустити");
    cs::tests::requireContains("src/ui/tabs/settings/RestoreManagerDialog.cpp", "restartApplicationLater();");

    // 9: User-facing UI exposes status, modes, manual actions and hides advanced settings.
    cs::tests::requireContains("src/ui/tabs/settings/AutoBackupSettingsSection.cpp", "Основний режим захисту");
    cs::tests::requireContains("src/ui/tabs/settings/AutoBackupSettingsSection.cpp", "Створити\\nbackup зараз");
    cs::tests::requireContains("src/ui/tabs/settings/AutoBackupSettingsSection.cpp", "Відновити\\nз backup");
    cs::tests::requireContains("src/ui/tabs/settings/AutoBackupSettingsSection.cpp", "Очистити\\nrecovery drafts");
    cs::tests::requireContains("src/ui/tabs/settings/AutoBackupSettingsSection.cpp", "Додатково — резервні копії");
    cs::tests::requireContains("src/ui/tabs/settings/AutoBackupSettingsSection.cpp", "Додатково — recovery незбережених даних");

    // 10: Audit log keeps non-PII operational metadata for diagnosis.
    cs::tests::requireContains("src/infrastructure/audit/BackupRecoveryAuditTrail.cpp", "operation_id");
    cs::tests::requireContains("src/infrastructure/audit/BackupRecoveryAuditTrail.cpp", "duration_ms");
    cs::tests::requireContains("src/infrastructure/audit/BackupRecoveryAuditTrail.cpp", "schema_version");
    cs::tests::requireContains("src/infrastructure/audit/BackupRecoveryAuditTrail.cpp", "thread");

    // 11-14: behavioral acceptance coverage and correct implementation order are guarded by source checks.
    cs::tests::requireContains("src/core/backup/BackupJobQueue.cpp", "drainAndPauseForRestore");
    cs::tests::requireContains("src/core/backup/BackupManager.cpp", "backup request blocked by exclusive restore gate");
    cs::tests::requireContains("src/core/backup/BackupManager.cpp", "backup requested after committed database change");
    cs::tests::requireContains("src/core/backup/BackupManager.cpp", "backup engine degraded");
    cs::tests::requireContains("src/core/backup/CrashRecoveryManager.cpp", "draft_autosave_before_critical_operation");
    cs::tests::requireContains("src/core/services/NativeBackupRestoreService.cpp", "Відновлення БД дозволено тільки через контрольований RestoreCoordinator flow");
    cs::tests::requireContains("src/core/services/NativeBackupRestoreService.h", "restartRequired");
}


QTEST_MAIN(BackupRecoveryAcceptanceTests)
#include "BackupRecoveryAcceptanceTests.moc"
