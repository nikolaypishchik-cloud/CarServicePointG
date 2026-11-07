#include <QtTest/QtTest>
#include "TestSourceAssertions.h"

class CurrentOrdersQueryServiceTests final : public QObject {
    Q_OBJECT
private slots:
    void criticalRegressionGuardsRemainWired();
};

void CurrentOrdersQueryServiceTests::criticalRegressionGuardsRemainWired()
{
    cs::tests::requireContains("src/core/services/orders/CurrentOrdersInvoiceImportService.cpp", "affectedOrderNumbers");
    cs::tests::requireAnyContains("src/core/services/orders/CurrentOrdersInvoiceImportService.cpp", {"INVOICE_IMPORT_END", "INVOICE_IMPORT"});
}

QTEST_MAIN(CurrentOrdersQueryServiceTests)
#include "CurrentOrdersQueryServiceTests.moc"
