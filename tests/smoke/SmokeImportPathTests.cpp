#include <QtTest/QtTest>
#include "../TestSourceAssertions.h"

class SmokeImportPathTests final : public QObject {
    Q_OBJECT
private slots:
    void criticalRegressionGuardsRemainWired();
};

void SmokeImportPathTests::criticalRegressionGuardsRemainWired()
{
    cs::tests::requireAnyContains("src/core/services/orders/CurrentOrdersInvoiceImportService.cpp", {"RAW_ROW", "PARSED_ROW", "InvoiceImportResult"});
    cs::tests::requireContains("src/ui/tabs/orders/WorkPartsImportCoordinator.cpp", "commitPendingEdits");
}

QTEST_MAIN(SmokeImportPathTests)
#include "SmokeImportPathTests.moc"
