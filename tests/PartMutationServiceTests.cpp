#include <QtTest/QtTest>
#include "TestSourceAssertions.h"

class PartMutationServiceTests final : public QObject {
    Q_OBJECT
private slots:
    void criticalRegressionGuardsRemainWired();
};

void PartMutationServiceTests::criticalRegressionGuardsRemainWired()
{
    cs::tests::requireContains("src/core/repository/PartRepository.cpp", "numRowsAffected");
    cs::tests::requireContains("src/core/repository/PartRepository.cpp", "partExistsForOrderUid");
}

QTEST_MAIN(PartMutationServiceTests)
#include "PartMutationServiceTests.moc"
