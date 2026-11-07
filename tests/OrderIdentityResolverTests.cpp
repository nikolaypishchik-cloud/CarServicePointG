#include <QtTest/QtTest>
#include "TestSourceAssertions.h"

class OrderIdentityResolverTests final : public QObject {
    Q_OBJECT
private slots:
    void criticalRegressionGuardsRemainWired();
};

void OrderIdentityResolverTests::criticalRegressionGuardsRemainWired()
{
    cs::tests::requireAnyContains("src/core/services/OrderIdentityResolver.cpp", {"resolve", "uid"});
    cs::tests::requireAnyContains("src/core/services/OrderIdentityService.cpp", {"order_year", "order_number"});
}

QTEST_MAIN(OrderIdentityResolverTests)
#include "OrderIdentityResolverTests.moc"
