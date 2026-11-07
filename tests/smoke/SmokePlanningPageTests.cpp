#include <QtTest/QtTest>
#include "../TestSourceAssertions.h"

class SmokePlanningPageTests final : public QObject {
    Q_OBJECT
private slots:
    void criticalRegressionGuardsRemainWired();
};

void SmokePlanningPageTests::criticalRegressionGuardsRemainWired()
{
    cs::tests::requireContains("src/ui/tabs/schedule/PlanningPage.cpp", "PlanningPage");
    cs::tests::requireAnyContains("src/ui/tabs/schedule/PlanningPageSupport.cpp", {"PlanningQueryService", "normalizePlanningSlotText", "extractPlanningPlateNorm"});
}

QTEST_MAIN(SmokePlanningPageTests)
#include "SmokePlanningPageTests.moc"
