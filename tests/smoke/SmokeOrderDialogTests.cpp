#include <QtTest/QtTest>
#include "../TestSourceAssertions.h"

class SmokeOrderDialogTests final : public QObject {
    Q_OBJECT
private slots:
    void criticalRegressionGuardsRemainWired();
};

void SmokeOrderDialogTests::criticalRegressionGuardsRemainWired()
{
    cs::tests::requireContains("src/ui/common/dialogs/OrderDialog.cpp", "commitPendingEdits");
    cs::tests::requireContains("src/ui/common/dialogs/OrderDialog.cpp", "m_workPartsPage");
}

QTEST_MAIN(SmokeOrderDialogTests)
#include "SmokeOrderDialogTests.moc"
