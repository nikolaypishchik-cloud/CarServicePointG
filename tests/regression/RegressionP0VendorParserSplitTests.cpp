#include <QtTest/QtTest>
#include "../TestSourceAssertions.h"

class RegressionP0VendorParserSplitTests final : public QObject {
    Q_OBJECT
private slots:
    void sourceScopeRejectsDisconnectedFragments() {
        const QString fragment = QStringLiteral("InvoiceVendorDetector.ipp");
        const QString directive = QStringLiteral("#include \"core/import/internal/InvoiceVendorDetector.ipp\"");
        QVERIFY(cs::tests::invoiceEngineIncludesFragment(directive, fragment));
        QVERIFY(!cs::tests::invoiceEngineIncludesFragment(QString(), fragment));
        QVERIFY(!cs::tests::invoiceEngineIncludesFragment("// " + directive, fragment));
        QVERIFY(!cs::tests::invoiceEngineIncludesFragment(directive, "NotIncluded.ipp"));
        const QString editorPath = QStringLiteral("ui/tabs/orders/internal/WorkPartsEditorCommitGuard.ipp");
        const QString editorDirective = QStringLiteral("#include \"") + editorPath + QLatin1Char('"');
        QVERIFY(cs::tests::sourceIncludesDirective(editorDirective, editorPath));
        QVERIFY(!cs::tests::sourceIncludesDirective(QString(), editorPath));
        QVERIFY(!cs::tests::sourceIncludesDirective("// " + editorDirective, editorPath));
        QVERIFY(!cs::tests::sourceIncludesDirective(editorDirective, editorPath + ".unused"));
    }
    void parserModulesAreSplitByVendorAndFormat();
    void importEngineDispatchesThroughParserContracts();
};

void RegressionP0VendorParserSplitTests::parserModulesAreSplitByVendorAndFormat()
{
    const QString contract = cs::tests::readSourceFile("src/core/import/parsers/InvoiceParsers.h");
    QVERIFY2(contract.contains(QStringLiteral("class ElitParser")),
             "P0 import p6 requires a dedicated ElitParser contract.");
    QVERIFY2(contract.contains(QStringLiteral("class AvtonovaParser")),
             "P0 import p6 requires a dedicated AvtonovaParser contract.");
    QVERIFY2(contract.contains(QStringLiteral("class GenericXlsParser")),
             "P0 import p6 requires a dedicated GenericXlsParser contract.");
    QVERIFY2(contract.contains(QStringLiteral("class GenericPdfParser")),
             "P0 import p6 requires a dedicated GenericPdfParser contract.");

    QVERIFY2(!cs::tests::readSourceFile("src/core/import/parsers/ElitParser.cpp").isEmpty(),
             "ElitParser implementation file must exist.");
    QVERIFY2(!cs::tests::readSourceFile("src/core/import/parsers/AvtonovaParser.cpp").isEmpty(),
             "AvtonovaParser implementation file must exist.");
    QVERIFY2(!cs::tests::readSourceFile("src/core/import/parsers/GenericXlsParser.cpp").isEmpty(),
             "GenericXlsParser implementation file must exist.");
    QVERIFY2(!cs::tests::readSourceFile("src/core/import/parsers/GenericPdfParser.cpp").isEmpty(),
             "GenericPdfParser implementation file must exist.");
}

void RegressionP0VendorParserSplitTests::importEngineDispatchesThroughParserContracts()
{
    const QString engine = cs::tests::invoiceEngineImplementation();
    const QString genericPdf = cs::tests::readSourceFile("src/core/import/parsers/GenericPdfParser.cpp");
    const QString genericXls = cs::tests::readSourceFile("src/core/import/parsers/GenericXlsParser.cpp");

    QVERIFY2(engine.contains(QStringLiteral("GenericPdfParser::parseText")),
             "parsePdfInvoiceText must delegate to GenericPdfParser after the p6 split.");
    QVERIFY2(engine.contains(QStringLiteral("GenericPdfParser::readRows")),
             "readInvoiceRows must route PDF files through GenericPdfParser after the p6 split.");
    QVERIFY2(engine.contains(QStringLiteral("GenericXlsParser::readRows")),
             "readInvoiceRows must route spreadsheet files through GenericXlsParser after the p6 split.");
    QVERIFY2(genericPdf.contains(QStringLiteral("ElitParser::parsePdfText")) &&
             genericPdf.contains(QStringLiteral("AvtonovaParser::parsePdfText")),
             "GenericPdfParser must explicitly dispatch to the vendor-specific PDF parsers.");
    QVERIFY2(genericXls.contains(QStringLiteral("readSpreadsheetInvoiceRowsInternal")),
             "GenericXlsParser must isolate generic spreadsheet import from UI/import engine callers.");
}

QTEST_MAIN(RegressionP0VendorParserSplitTests)
#include "RegressionP0VendorParserSplitTests.moc"
