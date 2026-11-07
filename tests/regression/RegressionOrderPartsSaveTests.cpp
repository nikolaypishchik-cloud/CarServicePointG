#include <QtTest/QtTest>
#include "../TestSourceAssertions.h"

class RegressionOrderPartsSaveTests final : public QObject {
    Q_OBJECT
private slots:
    void criticalRegressionGuardsRemainWired();
};

void RegressionOrderPartsSaveTests::criticalRegressionGuardsRemainWired()
{
    cs::tests::requireContains("src/ui/tabs/orders/WorkPartsPartPersistence.cpp", "PartTableStateMapper");
    cs::tests::requireContains("src/ui/tabs/orders/WorkPartsPersistenceCoordinator.cpp", "SAVE_DONE");
}

QTEST_MAIN(RegressionOrderPartsSaveTests)
#include "RegressionOrderPartsSaveTests.moc"
