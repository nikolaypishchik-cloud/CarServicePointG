#include <QtTest/QtTest>
#include "TestSourceAssertions.h"

class BackupManagerCriticalFlowTests final : public QObject {
    Q_OBJECT
private slots:
    void criticalRegressionGuardsRemainWired();
};

void BackupManagerCriticalFlowTests::criticalRegressionGuardsRemainWired()
{
    cs::tests::requireAnyContains("src/core/backup/BackupManager.cpp", {"Backup", "snapshot"});
    cs::tests::requireAnyContains("src/core/backup/BackupJobQueue.cpp", {"queue", "enqueue"});
    cs::tests::requireContains("src/core/backup/DataProtectionCoordinator.cpp", "DataProtectionCoordinator::tryEnter");
    cs::tests::requireContains("src/core/backup/BackupSnapshotService.cpp", "BackupPaths::backupTempDir");
    cs::tests::requireContains("src/core/backup/BackupManagerSupport.cpp", "pending_backup_journal.json");
    cs::tests::requireContains("src/core/backup/RecoveryDraftStore.cpp", "File scan wins over manifest metadata");
    cs::tests::requireContains("src/core/backup/RestoreCoordinator.cpp", "RestoreRollback");
    cs::tests::requireContains("src/ui/tabs/settings/AutoBackupSettingsSection.cpp", "Автобекап та відновлення");
}

QTEST_MAIN(BackupManagerCriticalFlowTests)
#include "BackupManagerCriticalFlowTests.moc"
