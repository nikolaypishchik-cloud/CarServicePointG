#include <QtTest/QtTest>
#include "../TestSourceAssertions.h"

class RegressionRecoveryAfterCrashTests final : public QObject {
    Q_OBJECT
private slots:
    void criticalRegressionGuardsRemainWired();
};

void RegressionRecoveryAfterCrashTests::criticalRegressionGuardsRemainWired()
{
    cs::tests::requireAnyContains("src/ui/tabs/orders/WorkPartsPartPersistence.cpp", {"crash_snapshot", "CRASH_GUARD"});
    cs::tests::requireAnyContains("src/core/backup/CrashRecoveryManager.cpp", {"Recovery", "Crash"});
}

QTEST_MAIN(RegressionRecoveryAfterCrashTests)
#include "RegressionRecoveryAfterCrashTests.moc"
