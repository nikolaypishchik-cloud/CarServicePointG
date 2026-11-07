#include "TestSourceAssertions.h"

class RegressionUnikTradeInvoiceImportTests final : public QObject
{
    Q_OBJECT
private slots:
    void xlsxSupplierHeaderIsDetectedAsUnikTrade()
    {
        using namespace cs::tests;
        requireContains("src/core/import/internal/PdfTextExtractionStage.ipp", "Юнік-трейд / Юнікс Трейд Ко XLSX invoices");
        requireContains("src/core/import/internal/PdfTextExtractionStage.ipp", "looksLikeUnikTrade");
        requireContains("src/core/import/internal/PdfTextExtractionStage.ipp", "QString::fromUtf8(\"Юнік-трейд\")");
        requireContains("src/core/import/internal/PdfTextExtractionStage.ipp", "QString::fromUtf8(\"юнікс\")");
        requireContains("src/core/import/internal/PdfTextExtractionStage.ipp", "QStringLiteral(\"uniks\")");
    }

    void storeNormalizerHasCanonicalUnikTradeKey()
    {
        using namespace cs::tests;
        requireContains("src/core/util/StoreNameNormalizer.cpp", "QStringLiteral(\"unik_trade\")");
        requireContains("src/core/util/StoreNameNormalizer.cpp", "QString::fromUtf8(\"Юнік-трейд\")");
        requireContains("src/core/util/StoreNameNormalizer.cpp", "QString::fromUtf8(\"юник\")");
        requireContains("src/core/util/StoreNameNormalizer.cpp", "QStringLiteral(\"uniks\")");
    }

    void genericXlsxHeadersStillCoverUnikTradeExportColumns()
    {
        using namespace cs::tests;
        requireContains("src/core/import/internal/InvoiceImportPersistenceStage.ipp", "\"артикул\"");
        requireContains("src/core/import/internal/InvoiceImportPersistenceStage.ipp", "\"товар\"");
        requireContains("src/core/import/internal/InvoiceImportPersistenceStage.ipp", "\"бренд\"");
        requireContains("src/core/import/internal/InvoiceImportPersistenceStage.ipp", "\"ціназпдв\"");
        requireContains("src/core/import/internal/InvoiceImportPersistenceStage.ipp", "\"сума\"");
    }
};

QTEST_MAIN(RegressionUnikTradeInvoiceImportTests)
#include "RegressionUnikTradeInvoiceImportTests.moc"
