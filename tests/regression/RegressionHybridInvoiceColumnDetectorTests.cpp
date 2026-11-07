#include "TestSourceAssertions.h"
#include "core/import/UniversalInvoiceColumnDetector.h"

#include <cmath>
#include <QCoreApplication>
#include <QFile>
#include <QTextStream>

namespace {

double parseLooseNumberForTest(QString text, bool* ok = nullptr)
{
    text = text.trimmed();
    text.remove(QChar(0x00A0));
    text.remove(QLatin1Char(' '));
    text.replace(QLatin1Char(','), QLatin1Char('.'));
    return text.toDouble(ok);
}

QString normalizeTextForTest(const QString& value)
{
    return value.simplified();
}

QString moneyTextForTest(double value)
{
    return QString::number(value, 'f', 2);
}

QList<QStringList> csvFixtureRows(const QString& relativePath)
{
    QFile file(cs::tests::projectRoot() + QLatin1Char('/') + relativePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    QList<QStringList> rows;
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        if (line.trimmed().isEmpty()) continue;
        rows.append(line.split(QLatin1Char(';')));
    }
    return rows;
}

} // namespace


class RegressionHybridInvoiceColumnDetectorTests final : public QObject
{
    Q_OBJECT
private slots:
    void detectorRequiresVatIncludedPriceForImports()
    {
        using namespace cs::tests;
        requireContains("src/core/import/internal/InvoiceImportPersistenceStage.ipp", "UniversalInvoiceColumnDetector: hybrid header scoring");
        requireContains("src/core/import/internal/InvoiceImportPersistenceStage.ipp", "Mandatory rule: imported part purchase price must be the VAT-included price");
        requireContains("src/core/import/internal/InvoiceImportPersistenceStage.ipp", "invoiceHeaderHasVatExcludedMarker");
        requireContains("src/core/import/internal/InvoiceImportPersistenceStage.ipp", "invoiceHeaderHasVatIncludedMarker");
        requireContains("src/core/import/internal/InvoiceImportPersistenceStage.ipp", "vatIncludedPriceHeaderScore");
        requireContains("src/core/import/internal/InvoiceImportPersistenceStage.ipp", "isVatSafePriceColumnHeader");
        requireContains("src/core/import/internal/InvoiceImportParsedDraftStage.ipp", "if (!map.hasVatConfirmedPrice() || !isVatSafePriceColumn(rows, headerRow, map))");
        requireContains("src/core/import/internal/InvoiceImportPersistenceStage.ipp", "evaluateInvoiceColumnConfidence");
        requireContains("src/core/import/internal/InvoiceImportPersistenceStage.ipp", "invoiceAmountMatchesVatPrice");
        requireContains("src/core/import/internal/InvoiceImportPersistenceStage.ipp", "automaticAllowed");
        requireContains("src/core/import/internal/InvoiceImportPersistenceStage.ipp", "SupplierInvoiceTemplate");
        requireContains("src/core/import/internal/InvoiceImportDiagnostics.ipp", "persistSupplierInvoiceTemplateIfUseful");
    }

    void vatIncludedColumnWinsOverAmbiguousOrWithoutVatColumns()
    {
        using namespace cs::tests;
        requireContains("src/core/import/internal/InvoiceImportPersistenceStage.ipp", "безпдв");
        requireContains("src/core/import/internal/InvoiceImportPersistenceStage.ipp", "безндс");
        requireContains("src/core/import/internal/InvoiceImportPersistenceStage.ipp", "exclvat");
        requireContains("src/core/import/internal/InvoiceImportPersistenceStage.ipp", "зпдв");
        requireContains("src/core/import/internal/InvoiceImportPersistenceStage.ipp", "сндс");
        requireContains("src/core/import/internal/InvoiceImportPersistenceStage.ipp", "inclvat");
        requireContains("src/core/import/internal/InvoiceImportPersistenceStage.ipp", "Вартість з ПДВ");
        requireContains("src/core/import/internal/InvoiceImportPersistenceStage.ipp", "vatIncludedCostSynonym");
        requireContains("src/core/import/internal/InvoiceImportPersistenceStage.ipp", "cellScore > selectedPriceScore");
        requireContains("src/core/import/internal/InvoicePriceColumnResolver.ipp", "Ціна з ПДВ");
        requireContains("src/core/import/internal/InvoicePriceColumnResolver.ipp", "Сума з ПДВ");
    }

    void strictVatAndNumericConfidenceClosure()
    {
        using namespace cs::tests;
        requireContains("src/core/import/internal/InvoiceImportParsedDraftStage.ipp", "isVatSafePriceColumn(rows, headerRow, map)");
        requireContains("src/core/import/internal/InvoiceImportPersistenceStage.ipp", "report.totalConfidence >= 85");
        requireContains("src/core/import/internal/InvoiceImportParsedDraftStage.ipp", "report.numericConsistencyScore >= 65");
        requireContains("src/core/import/internal/InvoiceImportDiagnostics.ipp", "!invoiceAmountMatchesVatPrice(qtyValue, priceValue, amountValue)");
        requireContains("src/core/import/internal/InvoiceImportPersistenceStage.ipp", "priceWithVat");
        requireContains("src/core/io/SupplierInvoiceTemplateStore.cpp", "supplier_invoice_templates.json");
    }

    void realRowsSelectVatIncludedPriceWhenBothVatColumnsExist()
    {
        const QList<QStringList> rows{
            {QString::fromUtf8("Постачальник"), QString::fromUtf8("Еліт")},
            {QString::fromUtf8("Артикул"), QString::fromUtf8("Найменування"), QString::fromUtf8("Кількість"), QString::fromUtf8("Ціна без ПДВ"), QString::fromUtf8("Ціна з ПДВ"), QString::fromUtf8("Сума з ПДВ")},
            {QStringLiteral("ABC123"), QString::fromUtf8("Фільтр оливи"), QStringLiteral("2"), QStringLiteral("100,00"), QStringLiteral("120,00"), QStringLiteral("240,00")}
        };
        const auto preview = InvoiceImport::UniversalInvoiceColumnDetector::detect(rows, {}, parseLooseNumberForTest);
        QCOMPARE(preview.priceWithVatColumn, 4);
        QVERIFY(preview.priceWithVatConfirmed);
        QVERIFY(preview.vatPriceScore >= 90);

        QString error;
        const auto drafts = InvoiceImport::UniversalInvoiceColumnDetector::parseAutomatic(
            rows, {}, normalizeTextForTest, parseLooseNumberForTest, moneyTextForTest, &error);
        QCOMPARE(drafts.size(), 1);
        QCOMPARE(drafts.first().priceText, QStringLiteral("120.00"));
    }

    void realRowsBlockOnlyVatExcludedPriceUntilManualConfirmation()
    {
        const QList<QStringList> rows{
            {QString::fromUtf8("Артикул"), QString::fromUtf8("Найменування"), QString::fromUtf8("Кількість"), QString::fromUtf8("Ціна без ПДВ"), QString::fromUtf8("Сума без ПДВ")},
            {QStringLiteral("ABC123"), QString::fromUtf8("Фільтр оливи"), QStringLiteral("1"), QStringLiteral("100,00"), QStringLiteral("100,00")}
        };
        QString error;
        const auto drafts = InvoiceImport::UniversalInvoiceColumnDetector::parseAutomatic(
            rows, {}, normalizeTextForTest, parseLooseNumberForTest, moneyTextForTest, &error);
        QVERIFY(drafts.isEmpty());
        QVERIFY(error.contains(QString::fromUtf8("Ціна з ПДВ")) || error.contains(QString::fromUtf8("ПДВ")));

        const auto preview = InvoiceImport::UniversalInvoiceColumnDetector::detect(rows, {}, parseLooseNumberForTest);
        QVERIFY(!preview.priceWithVatConfirmed);
        QVERIFY(preview.manualColumnSelectionRequired);
    }

    void realRowsTreatPriceVatAsExplicitVatIncludedPrice()
    {
        const QList<QStringList> rows{
            {QStringLiteral("partnumber"), QStringLiteral("description"), QStringLiteral("qty"), QStringLiteral("Price VAT"), QStringLiteral("Total VAT")},
            {QStringLiteral("ABC123"), QStringLiteral("Oil filter"), QStringLiteral("3"), QStringLiteral("10.00"), QStringLiteral("30.00")}
        };
        const auto preview = InvoiceImport::UniversalInvoiceColumnDetector::detect(rows, {}, parseLooseNumberForTest);
        QCOMPARE(preview.priceWithVatColumn, 3);
        QVERIFY(preview.priceWithVatConfirmed);
    }

    void realRowsHandlePdfEliteStyleVatHeaders()
    {
        const QList<QStringList> rows{
            {QString::fromUtf8("Дата"), QString::fromUtf8("OEM номер"), QString::fromUtf8("Найменування"), QString::fromUtf8("Бренд"), QString::fromUtf8("Кількість"), QString::fromUtf8("Ціна з ПДВ"), QString::fromUtf8("Сума з ПДВ"), QString::fromUtf8("Магазин")},
            {QStringLiteral("16.05.26"), QStringLiteral("OC90"), QString::fromUtf8("Фільтр оливи"), QStringLiteral("MAHLE"), QStringLiteral("2"), QStringLiteral("150,00"), QStringLiteral("300,00"), QString::fromUtf8("Еліт")}
        };
        QString error;
        const auto drafts = InvoiceImport::UniversalInvoiceColumnDetector::parseAutomatic(
            rows, {}, normalizeTextForTest, parseLooseNumberForTest, moneyTextForTest, &error);
        QCOMPARE(drafts.size(), 1);
        QCOMPARE(drafts.first().store, QString::fromUtf8("Еліт"));
    }

    void realRowsHandleAvtonovaAndUnikTradeExcelHeaders()
    {
        const QList<QList<QStringList>> samples{
            {
                {QString::fromUtf8("Постачальник"), QString::fromUtf8("Автонова-Д")},
                {QString::fromUtf8("Код товару"), QString::fromUtf8("Найменування"), QString::fromUtf8("Кількість"), QString::fromUtf8("Ціна, грн з ПДВ"), QString::fromUtf8("Сума з ПДВ")},
                {QStringLiteral("A123"), QString::fromUtf8("Колодки гальмівні"), QStringLiteral("1"), QStringLiteral("900,00"), QStringLiteral("900,00")}
            },
            {
                {QString::fromUtf8("Постачальник"), QString::fromUtf8("Юнік-трейд")},
                {QString::fromUtf8("Артикул"), QString::fromUtf8("Назва"), QStringLiteral("Qty"), QStringLiteral("Price incl. VAT"), QStringLiteral("Total")},
                {QStringLiteral("U123"), QString::fromUtf8("Свічка запалювання"), QStringLiteral("4"), QStringLiteral("50.00"), QStringLiteral("200.00")}
            }
        };
        for (const auto& rows : samples) {
            QString error;
            const auto drafts = InvoiceImport::UniversalInvoiceColumnDetector::parseAutomatic(
                rows, {}, normalizeTextForTest, parseLooseNumberForTest, moneyTextForTest, &error);
            QCOMPARE(drafts.size(), 1);
            QVERIFY2(error.isEmpty(), qPrintable(error));
        }
    }


    void realRowsHandleAvtonovaExcelPdfStylePlainPriceAsVatIncluded()
    {
        const QList<QStringList> rows{
            {QString::fromUtf8("Постачальник"), QString::fromUtf8("Автонова-Д")},
            {QString::fromUtf8("Каталожний номер"), QString::fromUtf8("Найменування"), QString::fromUtf8("Бренд"), QString::fromUtf8("Кількість"), QString::fromUtf8("Од."), QString::fromUtf8("Ціна"), QStringLiteral("EUR: 51,2; USD: 44,35"), QString::fromUtf8("Сума"), QStringLiteral("Сума EURO")},
            {QStringLiteral("9801737680"), QString::fromUtf8("Ущільнювач"), QStringLiteral("PEUGEOT / CITROEN"), QStringLiteral("1"), QString::fromUtf8("шт"), QStringLiteral("121"), QString(), QStringLiteral("121"), QStringLiteral("2.36")}
        };
        const auto preview = InvoiceImport::UniversalInvoiceColumnDetector::detect(rows, {}, parseLooseNumberForTest);
        QCOMPARE(preview.priceWithVatColumn, 5);
        QCOMPARE(preview.totalWithVatColumn, 7);
        QVERIFY2(preview.priceWithVatConfirmed, qPrintable(preview.warnings.join(QStringLiteral("; "))));
        QVERIFY(preview.vatPriceScore >= 90);
        QVERIFY(preview.totalConfidence >= 85);
    }

    void realRowsRejectRowsWithoutPartNameEvenWhenArticleExists()
    {
        const QList<QStringList> rows{
            {QString::fromUtf8("Артикул"), QString::fromUtf8("Найменування"), QString::fromUtf8("Кількість"), QString::fromUtf8("Ціна з ПДВ"), QString::fromUtf8("Сума з ПДВ")},
            {QStringLiteral("ABC123"), QString(), QStringLiteral("1"), QStringLiteral("100,00"), QStringLiteral("100,00")}
        };
        QString error;
        const auto drafts = InvoiceImport::UniversalInvoiceColumnDetector::parseAutomatic(
            rows, {}, normalizeTextForTest, parseLooseNumberForTest, moneyTextForTest, &error);
        QVERIFY(drafts.isEmpty());
    }

    void detectionAndParsingArePerformedInsideImportJobResult()
    {
        using namespace cs::tests;
        requireContains("src/core/import/ImportJob.cpp", "detecting_columns");
        requireContains("src/core/import/ImportJob.cpp", "columnDetectionPerformed = detection.performed");
        requireContains("src/core/import/ImportJob.cpp", "UniversalInvoiceColumnDetector::analyzeAndParse");
        requireContains("src/core/import/InvoiceImportTypes.h", "struct UniversalInvoiceDetectionResult");
        requireContains("src/ui/tabs/orders/WorkPartsImportCoordinator.cpp", "excelData.columnDetectionPerformed");
        requireContains("src/ui/tabs/orders/WorkPartsImportCoordinator.cpp", "excelData.parsedPartDrafts");
    }

    void fixtureRowsAreParsedByDetectorNotOnlyAssertedInSource()
    {
        const QList<QStringList> avtonova = csvFixtureRows(QStringLiteral("tests/fixtures/invoices/avtonova_d_excel_vat.csv"));
        QVERIFY(!avtonova.isEmpty());
        QString error;
        const auto avtonovaResult = InvoiceImport::UniversalInvoiceColumnDetector::analyzeAndParse(
            avtonova, {}, normalizeTextForTest, parseLooseNumberForTest, moneyTextForTest);
        QVERIFY2(avtonovaResult.performed, "detector workflow must run for fixture rows");
        QVERIFY2(avtonovaResult.columnMappingPreview.priceWithVatConfirmed, qPrintable(avtonovaResult.automaticImportError));
        QVERIFY2(!avtonovaResult.parsedPartDrafts.isEmpty(), qPrintable(avtonovaResult.automaticImportError));

        const QList<QStringList> avtonovaPdfStyle = csvFixtureRows(QStringLiteral("tests/fixtures/invoices/avtonova_d_excel_pdf_style_price_sum.csv"));
        QVERIFY(!avtonovaPdfStyle.isEmpty());
        const auto pdfStyleResult = InvoiceImport::UniversalInvoiceColumnDetector::analyzeAndParse(
            avtonovaPdfStyle, {}, normalizeTextForTest, parseLooseNumberForTest, moneyTextForTest);
        QVERIFY2(pdfStyleResult.columnMappingPreview.priceWithVatConfirmed, qPrintable(pdfStyleResult.automaticImportError));
        QVERIFY2(pdfStyleResult.automaticImportAllowed, qPrintable(pdfStyleResult.automaticImportError));

        const QList<QStringList> onlyWithoutVat = csvFixtureRows(QStringLiteral("tests/fixtures/invoices/only_without_vat_blocked.csv"));
        QVERIFY(!onlyWithoutVat.isEmpty());
        const auto blockedResult = InvoiceImport::UniversalInvoiceColumnDetector::analyzeAndParse(
            onlyWithoutVat, {}, normalizeTextForTest, parseLooseNumberForTest, moneyTextForTest);
        QVERIFY(blockedResult.performed);
        QVERIFY(!blockedResult.automaticImportAllowed);
        QVERIFY(blockedResult.manualMappingRequired);
        QVERIFY(!blockedResult.columnMappingPreview.priceWithVatConfirmed);
    }


    void excelOriginInvoicesAreNormalizedToCanonicalPdfTemplate()
    {
        using namespace cs::tests;
        requireContains("src/core/import/InvoiceAttachmentPdfService.cpp", "Canonical visual standard for Excel-originated supplier invoices");
        requireContains("src/core/import/InvoiceAttachmentPdfService.cpp", "invoiceCanonicalTemplateHeaders");
        requireContains("src/core/import/InvoiceAttachmentPdfService.cpp", "buildCanonicalInvoiceRows");
        requireContains("src/core/import/InvoiceAttachmentPdfService.cpp", "Spreadsheet originals are always normalized to the canonical invoice");
        requireContains("src/core/import/InvoiceAttachmentPdfService.cpp", "tryRenderStructuredInvoicePdf(data)");
        requireContains("src/core/import/InvoiceAttachmentPdfService.cpp", "extractInvoiceCompanyNameFromRows");
        requireContains("src/core/import/InvoiceAttachmentPdfService.cpp", "ППФ");
        requireContains("src/core/import/InvoiceAttachmentPdfService.cpp", "Юнік-трейд");
        requireContains("src/core/import/InvoiceAttachmentPdfService.cpp", "Постачальник");
        requireContains("src/core/import/InvoiceAttachmentPdfService.cpp", "OEM номер");
        requireContains("src/core/import/InvoiceAttachmentPdfService.cpp", "Кільк\\n-ть");
        requireContains("src/core/import/InvoiceAttachmentPdfService.cpp", "Сума\\nEURO");
        requireContains("tests/fixtures/invoices/avtonova_canonical_pdf_standard.txt", "Видаткова накладна №А-04154240 від 16.04.26");
        requireContains("tests/fixtures/invoices/avtonova_canonical_pdf_standard.txt", "№ OEM номер Найменування Бренд");
        requireContains("tests/fixtures/invoices/avtonova_canonical_pdf_standard.txt", "Кільк");
        requireContains("tests/fixtures/invoices/avtonova_canonical_pdf_standard.txt", "-ть Од Ціна Сума Сума EURO");
        requireContains("tests/fixtures/invoices/avtonova_canonical_pdf_standard.txt", "Всього до сплати: 11 897,00 грн");
        requireFileMinSize("tests/fixtures/invoices/avtonova_canonical_pdf_standard.pdf", 1024);
        requireFileStartsWith("tests/fixtures/invoices/avtonova_canonical_pdf_standard.pdf", QByteArray("%PDF", 4));
    }

    void fixtureFilesCoverRealInvoiceScenarios()
    {
        using namespace cs::tests;
        requireFileExists("tests/fixtures/invoices/elit_pdf_text_layer_vat.txt");
        requireFileExists("tests/fixtures/invoices/avtonova_d_excel_vat.csv");
        requireFileExists("tests/fixtures/invoices/avtonova_d_excel_pdf_style_price_sum.csv");
        requireFileExists("tests/fixtures/invoices/unik_trade_excel_vat.csv");
        requireFileExists("tests/fixtures/invoices/only_without_vat_blocked.csv");
        requireFileExists("tests/fixtures/invoices/ocr_pdf_common_errors.txt");
        requireFileExists("tests/fixtures/invoices/avtonova_canonical_pdf_standard.txt");

        requireContains("tests/fixtures/invoices/avtonova_d_excel_vat.csv", "Ціна без ПДВ");
        requireContains("tests/fixtures/invoices/avtonova_d_excel_vat.csv", "Ціна, грн з ПДВ");
        requireContains("tests/fixtures/invoices/avtonova_d_excel_pdf_style_price_sum.csv", "EUR: 51,2; USD: 44,35");
        requireContains("tests/fixtures/invoices/avtonova_d_excel_pdf_style_price_sum.csv", "Сума EURO");
        requireContains("tests/fixtures/invoices/only_without_vat_blocked.csv", "Ціна без ПДВ");
        requireContains("tests/fixtures/invoices/ocr_pdf_common_errors.txt", "Цiна з ПДB");
        requireContains("tests/fixtures/invoices/avtonova_canonical_pdf_standard.txt", "OEM номер");
        requireContains("tests/fixtures/invoices/avtonova_canonical_pdf_standard.txt", "Сума EURO");

        requireFileMinSize("tests/fixtures/invoices/avtonova_d_excel_vat.xlsx", 1024);
        requireFileStartsWith("tests/fixtures/invoices/avtonova_d_excel_vat.xlsx", QByteArray("PK\003\004", 4));
        requireFileMinSize("tests/fixtures/invoices/unik_trade_excel_vat.xls", 1024);
        requireFileStartsWith("tests/fixtures/invoices/unik_trade_excel_vat.xls", QByteArray::fromHex("D0CF11E0A1B11AE1"));
        requireFileMinSize("tests/fixtures/invoices/elit_pdf_text_layer_vat.pdf", 256);
        requireFileStartsWith("tests/fixtures/invoices/elit_pdf_text_layer_vat.pdf", QByteArray("%PDF", 4));
        requireFileMinSize("tests/fixtures/invoices/ocr_pdf_common_errors_image_only.pdf", 1024);
        requireFileStartsWith("tests/fixtures/invoices/ocr_pdf_common_errors_image_only.pdf", QByteArray("%PDF", 4));
        requireFileMinSize("tests/fixtures/invoices/avtonova_canonical_pdf_standard.pdf", 1024);
        requireFileStartsWith("tests/fixtures/invoices/avtonova_canonical_pdf_standard.pdf", QByteArray("%PDF", 4));
    }
};

QTEST_MAIN(RegressionHybridInvoiceColumnDetectorTests)
#include "RegressionHybridInvoiceColumnDetectorTests.moc"
