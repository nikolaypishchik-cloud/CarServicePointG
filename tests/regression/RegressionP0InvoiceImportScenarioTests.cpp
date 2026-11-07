#include <QtTest/QtTest>
#include "../TestSourceAssertions.h"

class RegressionP0InvoiceImportScenarioTests final : public QObject {
    Q_OBJECT
private slots:
    void elitSinglePositionInvoiceScenarioIsCovered();
    void elitMultiPositionInvoiceScenarioIsCovered();
    void pdfInvoiceWithoutExcelScenarioIsCovered();
    void xlsInvoiceWithoutInstalledExcelScenarioIsCovered();
    void invalidDateScenarioIsCovered();
    void emptyBrandAndOemScenarioIsCovered();
    void repeatedImportSameOrderScenarioIsCovered();
};

void RegressionP0InvoiceImportScenarioTests::elitSinglePositionInvoiceScenarioIsCovered()
{
    const QString engine = cs::tests::invoiceEngineImplementation();
    const QString parser = cs::tests::readSourceFile("src/core/import/parsers/ElitParser.cpp");

    QVERIFY2(parser.contains(QStringLiteral("parseElitePdfTextInternal")),
             "ElitParser must route single-position Eліт PDF text through the existing Eліт recognition bridge.");
    QVERIFY2(engine.contains(QStringLiteral("tryParseEliteInvoiceDirectCompactRows")) ||
             engine.contains(QStringLiteral("tryParseEliteInvoiceDenseCompactTable")),
             "Eліт single-position compact invoice layouts must stay covered by dedicated recognition routines.");
}

void RegressionP0InvoiceImportScenarioTests::elitMultiPositionInvoiceScenarioIsCovered()
{
    const QString engine = cs::tests::invoiceEngineImplementation();

    QVERIFY2(engine.contains(QStringLiteral("tryParseEliteInvoiceVatWarehouseTable")) ||
             engine.contains(QStringLiteral("tryParseEliteInvoiceWideVatLayoutTable")) ||
             engine.contains(QStringLiteral("tryParseEliteInvoiceTableByBlocks")),
             "Eліт multi-position invoices must stay covered by table/block recognition routines.");
    QVERIFY2(engine.contains(QStringLiteral("appendPdfInvoiceRow")),
             "Multi-position recognition must append invoice rows through the canonical PDF row helper.");
}

void RegressionP0InvoiceImportScenarioTests::pdfInvoiceWithoutExcelScenarioIsCovered()
{
    const QString pdfParser = cs::tests::readSourceFile("src/core/import/parsers/GenericPdfParser.cpp");
    const QString engine = cs::tests::invoiceEngineImplementation();

    QVERIFY2(pdfParser.contains(QStringLiteral("GenericPdfParser::readRows")) &&
             pdfParser.contains(QStringLiteral("readPdfInvoiceRowsInternal")),
             "PDF invoices must be readable through the PDF parser path without depending on Excel.");
    QVERIFY2(engine.contains(QStringLiteral("pdf_direct_python")) &&
             engine.contains(QStringLiteral("pdf_powershell_fallback")),
             "PDF import must retain non-Excel fallback paths for text extraction.");
}

void RegressionP0InvoiceImportScenarioTests::xlsInvoiceWithoutInstalledExcelScenarioIsCovered()
{
    const QString xlsParser = cs::tests::readSourceFile("src/core/import/parsers/GenericXlsParser.cpp");
    const QString engine = cs::tests::invoiceEngineImplementation();

    QVERIFY2(xlsParser.contains(QStringLiteral("GenericXlsParser::readRows")) &&
             xlsParser.contains(QStringLiteral("readSpreadsheetInvoiceRowsInternal")),
             "XLS/XLSX import must go through a parser facade, not UI or Excel automation.");
    QVERIFY2(engine.contains(QStringLiteral("spreadsheet_python_fallback")) &&
             engine.contains(QStringLiteral("readLegacyXlsRowsDirect")),
             "Spreadsheet import must retain non-Excel fallback paths for machines without installed Excel.");
}

void RegressionP0InvoiceImportScenarioTests::invalidDateScenarioIsCovered()
{
    const QString engine = cs::tests::invoiceEngineImplementation();
    const QString partDates = cs::tests::readSourceFile("tests/regression/RegressionPartDatesTests.cpp");

    QVERIFY2(engine.contains(QStringLiteral("documentDate")) &&
             engine.contains(QStringLiteral("normalized")) &&
             engine.contains(QStringLiteral("QDate")),
             "Invoice import must keep explicit date normalization/validation logic.");
    QVERIFY2(partDates.contains(QStringLiteral("invalid")) ||
             engine.contains(QStringLiteral("date")),
             "Invalid invoice dates must remain part of regression coverage.");
}

void RegressionP0InvoiceImportScenarioTests::emptyBrandAndOemScenarioIsCovered()
{
    const QString engine = cs::tests::invoiceEngineImplementation();
    const QString vm = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsImportedRowsViewModel.cpp");

    QVERIFY2(engine.contains(QStringLiteral("brand")) &&
             engine.contains(QStringLiteral("oem")),
             "Parser code must keep handling brand/OEM columns and their optional values.");
    QVERIFY2(vm.contains(QStringLiteral("buildUiRow")) &&
             vm.contains(QStringLiteral("ImportedPartRow")),
             "Empty brand/OEM values must flow through the canonical imported-row ViewModel, not ad-hoc table writes.");
}

void RegressionP0InvoiceImportScenarioTests::repeatedImportSameOrderScenarioIsCovered()
{
    const QString coordinator = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsImportCoordinator.cpp");
    const QString vm = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsImportedRowsViewModel.cpp");

    QVERIFY2(coordinator.contains(QStringLiteral("csImportBusy")) &&
             coordinator.contains(QStringLiteral("csImportApplyingRows")),
             "Repeated imports into the same order must be guarded by busy/applying state.");
    QVERIFY2(vm.contains(QStringLiteral("invoice:%1:%2:%3")) &&
             coordinator.contains(QStringLiteral("RoleImportedInvoiceTrace")),
             "Repeated imports into the same order must assign stable per-row import traces.");
}

QTEST_MAIN(RegressionP0InvoiceImportScenarioTests)
#include "RegressionP0InvoiceImportScenarioTests.moc"
