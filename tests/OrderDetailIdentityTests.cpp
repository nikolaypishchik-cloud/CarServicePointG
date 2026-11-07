#include <QtTest/QtTest>
#include "TestSourceAssertions.h"

class OrderDetailIdentityTests final : public QObject {
    Q_OBJECT
private slots:
    void criticalRegressionGuardsRemainWired();
};

void OrderDetailIdentityTests::criticalRegressionGuardsRemainWired()
{
    cs::tests::requireAnyContains("src/core/services/OrderIdentityResolver.cpp", {"order_uid", "uid"});
    cs::tests::requireContains("src/core/repository/PartRepository.cpp", "partExistsForOrderUid");
}

QTEST_MAIN(OrderDetailIdentityTests)
#include "OrderDetailIdentityTests.moc"
