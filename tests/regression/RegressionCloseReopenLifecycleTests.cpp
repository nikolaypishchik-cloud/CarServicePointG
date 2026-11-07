#include <QtTest/QtTest>
#include "../TestSourceAssertions.h"

class RegressionCloseReopenLifecycleTests final : public QObject {
    Q_OBJECT
private slots:
    void criticalRegressionGuardsRemainWired();
};

void RegressionCloseReopenLifecycleTests::criticalRegressionGuardsRemainWired()
{
    cs::tests::requireContains("src/ui/tabs/orders/WorkPartsPersistenceCoordinator.cpp", "skipUiRefreshAfterCommit");
    cs::tests::requireContains("src/ui/common/dialogs/OrderDialog.cpp", "commitPendingEdits");
}

QTEST_MAIN(RegressionCloseReopenLifecycleTests)
#include "RegressionCloseReopenLifecycleTests.moc"
