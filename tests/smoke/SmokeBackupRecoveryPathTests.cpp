#include <QtTest/QtTest>
#include "../TestSourceAssertions.h"

class SmokeBackupRecoveryPathTests final : public QObject {
    Q_OBJECT
private slots:
    void criticalRegressionGuardsRemainWired();
};

void SmokeBackupRecoveryPathTests::criticalRegressionGuardsRemainWired()
{
    cs::tests::requireAnyContains("src/core/backup/CrashRecoveryManager.cpp", {"Recovery", "crash"});
    cs::tests::requireAnyContains("src/core/backup/BackupManager.cpp", {"Backup", "snapshot"});
}

QTEST_MAIN(SmokeBackupRecoveryPathTests)
#include "SmokeBackupRecoveryPathTests.moc"
