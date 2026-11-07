#include <QtTest/QtTest>
#include "TestSourceAssertions.h"

class OilServiceClassifierTests final : public QObject {
    Q_OBJECT
private slots:
    void criticalRegressionGuardsRemainWired();
};

void OilServiceClassifierTests::criticalRegressionGuardsRemainWired()
{
    cs::tests::requireAnyContains("src/core/util/OilServiceClassifier.cpp", {"5W", "oil", "масло"});
    cs::tests::requireContains("src/core/util/OilServiceClassifier.h", "OilServiceClassifier");
}

QTEST_MAIN(OilServiceClassifierTests)
#include "OilServiceClassifierTests.moc"
