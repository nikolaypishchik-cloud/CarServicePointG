#include <QtTest/QtTest>
#include "../TestSourceAssertions.h"

class RegressionP0ImportModuleExtractionTests : public QObject {
    Q_OBJECT
private slots:
    void invoiceImportPdfServiceIsInCoreImport();
    void uiExcelSupportDoesNotIncludeLegacyInvoiceInc();
};

void RegressionP0ImportModuleExtractionTests::invoiceImportPdfServiceIsInCoreImport()
{
    const QString header = cs::tests::readSourceFile("src/core/import/InvoiceAttachmentPdfService.h");
    const QString source = cs::tests::readSourceFile("src/core/import/InvoiceAttachmentPdfService.cpp");

    QVERIFY2(header.contains(QStringLiteral("namespace InvoiceImport")),
             "Service must stay in the import layer namespace.");
    QVERIFY2(header.contains(QStringLiteral("ensureInvoicePdfAttachment"))
                 && source.contains(QStringLiteral("renderInvoiceTablePdf")),
             "Service must expose PDF attachment preparation outside WorkPartsDialog .inc files.");
}

void RegressionP0ImportModuleExtractionTests::uiExcelSupportDoesNotIncludeLegacyInvoiceInc()
{
    const QString text = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsDialogExcelSupport.cpp");
    QVERIFY2(!text.contains(QStringLiteral("WorkPartsDialogInvoicePdfSupport.inc")),
             "WorkPartsDialogExcelSupport.cpp must not pull invoice import/PDF logic through a legacy .inc.");
    QVERIFY2(text.contains(QStringLiteral("ImportJob")),
             "UI wrapper should keep only async ImportJob/overlay wiring after p1 extraction.");
}

QTEST_MAIN(RegressionP0ImportModuleExtractionTests)
#include "RegressionP0ImportModuleExtractionTests.moc"
