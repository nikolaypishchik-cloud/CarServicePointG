#include <QtTest/QtTest>
#include "TestSourceAssertions.h"

class OrderDetailsRepositoryTests final : public QObject {
    Q_OBJECT
private slots:
    void criticalRegressionGuardsRemainWired();
};

void OrderDetailsRepositoryTests::criticalRegressionGuardsRemainWired()
{
    cs::tests::requireAnyContains("src/core/services/OrderDetailsRepository.cpp", {"OrderDetailsRepository", "exec", "bindValue"});
    cs::tests::requireAnyContains("src/core/services/MutationExecutor.cpp", {"BEGIN", "ROLLBACK", "COMMIT"});
}

QTEST_MAIN(OrderDetailsRepositoryTests)
#include "OrderDetailsRepositoryTests.moc"
