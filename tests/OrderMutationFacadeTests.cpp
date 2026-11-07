#include <QtTest/QtTest>
#include "TestSourceAssertions.h"

class OrderMutationFacadeTests final : public QObject {
    Q_OBJECT
private slots:
    void criticalRegressionGuardsRemainWired();
};

void OrderMutationFacadeTests::criticalRegressionGuardsRemainWired()
{
    cs::tests::requireContains("src/core/services/MutationExecutor.cpp", "transactionOwner");
    cs::tests::requireAnyContains("src/core/services/MutationExecutor.cpp", {"rollback", "commit"});
}

QTEST_MAIN(OrderMutationFacadeTests)
#include "OrderMutationFacadeTests.moc"
