#include <QtTest/QtTest>
#include "../TestSourceAssertions.h"

class SmokeCurrentOrdersPageTests final : public QObject {
    Q_OBJECT
private slots:
    void criticalRegressionGuardsRemainWired();
};

void SmokeCurrentOrdersPageTests::criticalRegressionGuardsRemainWired()
{
    cs::tests::requireAnyContains("src/core/services/PartsQueryService.cpp", {"overdue", "delivery_date"});
    cs::tests::requireAnyContains("src/ui/main/MainWindowRuntime.cpp", {"current", "orders"});
}

QTEST_MAIN(SmokeCurrentOrdersPageTests)
#include "SmokeCurrentOrdersPageTests.moc"
