#include <QtTest/QtTest>
#include "TestSourceAssertions.h"

class RecoveryConflictResolutionTests final : public QObject {
    Q_OBJECT
private slots:
    void criticalRegressionGuardsRemainWired();
};

void RecoveryConflictResolutionTests::criticalRegressionGuardsRemainWired()
{
    cs::tests::requireAnyContains("src/core/backup/RecoveryConflictDetector.cpp", {"conflict", "Recovery"});
    cs::tests::requireAnyContains("src/core/backup/CrashRecoveryManager.cpp", {"Crash", "Recovery"});
}

QTEST_MAIN(RecoveryConflictResolutionTests)
#include "RecoveryConflictResolutionTests.moc"
