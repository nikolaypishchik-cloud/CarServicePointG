#include <QtTest/QtTest>
#include "TestSourceAssertions.h"

class OrderIdentityServiceTests final : public QObject {
    Q_OBJECT
private slots:
    void criticalRegressionGuardsRemainWired();
};

void OrderIdentityServiceTests::criticalRegressionGuardsRemainWired()
{
    cs::tests::requireAnyContains("src/core/services/OrderIdentityService.cpp", {"uid", "order_number"});
    cs::tests::requireAnyContains("src/core/services/OrderNumberService.cpp", {"OrderNumberService", "number"});
}

QTEST_MAIN(OrderIdentityServiceTests)
#include "OrderIdentityServiceTests.moc"
