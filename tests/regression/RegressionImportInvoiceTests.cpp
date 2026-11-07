#include <QtTest/QtTest>
#include "../TestSourceAssertions.h"

class RegressionImportInvoiceTests final : public QObject {
    Q_OBJECT
private slots:
    void criticalRegressionGuardsRemainWired();
};

void RegressionImportInvoiceTests::criticalRegressionGuardsRemainWired()
{
    cs::tests::requireContains("src/core/services/orders/CurrentOrdersInvoiceImportService.cpp", "PARSED_ROW");
    cs::tests::requireAnyContains("src/core/import/InvoiceAttachmentPdfService.cpp", {"invoice", "PDF"});
}

QTEST_MAIN(RegressionImportInvoiceTests)
#include "RegressionImportInvoiceTests.moc"
