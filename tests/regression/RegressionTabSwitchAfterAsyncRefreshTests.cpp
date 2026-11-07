#include <QtTest/QtTest>
#include "../TestSourceAssertions.h"

class RegressionTabSwitchAfterAsyncRefreshTests final : public QObject {
    Q_OBJECT
private slots:
    void criticalRegressionGuardsRemainWired();
};

void RegressionTabSwitchAfterAsyncRefreshTests::criticalRegressionGuardsRemainWired()
{
    cs::tests::requireContains("src/ui/tabs/orders/WorkPartsPersistenceCoordinator.cpp", "m_commitPendingInProgress");
    cs::tests::requireAnyContains("src/ui/tabs/orders/WorkPartsImportCoordinator.cpp", {"QPointer", "commitPendingEdits"});
}

QTEST_MAIN(RegressionTabSwitchAfterAsyncRefreshTests)
#include "RegressionTabSwitchAfterAsyncRefreshTests.moc"
