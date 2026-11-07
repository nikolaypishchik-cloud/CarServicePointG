#include "TestSourceAssertions.h"

class RegressionHybridInvoiceImportManualMappingTests final : public QObject
{
    Q_OBJECT
private slots:
    void previewExposesColumnConfidenceAndManualVatMapping()
    {
        using namespace cs::tests;
        requireContains("src/core/import/InvoiceImportTypes.h", "struct InvoiceColumnMappingPreview");
        requireContains("src/core/import/InvoiceImportTypes.h", "QMap<QString, int> columnConfidence");
        requireContains("src/core/import/InvoiceImportTypes.h", "int supplierScore");
        requireContains("src/core/import/InvoiceImportTypes.h", "manualColumnSelectionRequired");
        requireContains("src/core/import/UniversalInvoiceColumnDetector.h", "class UniversalInvoiceColumnDetector final");
        requireContains("src/core/import/internal/InvoiceImportParsedDraftStage.ipp", "detectInvoiceColumnMappingPreview");
        requireContains("src/core/import/internal/InvoiceImportPersistenceStage.ipp", "buildInvoiceColumnMappingPreview");
        requireContains("src/core/import/internal/InvoiceImportParsedDraftStage.ipp", "priceWithVatConfirmed");
        requireContains("src/core/import/internal/InvoiceImportParsedDraftStage.ipp", "columnConfidence.insert(QStringLiteral(\"priceWithVat\")");
        requireContains("src/core/import/internal/InvoiceImportPersistenceStage.ipp", "evaluateInvoiceSupplierScore");
        requireContains("src/core/import/internal/InvoiceImportParsedDraftStage.ipp", "manualPreviewRequired = !confidence.automaticAllowed");
    }

    void manualConfirmationPersistsSupplierTemplate()
    {
        using namespace cs::tests;
        requireContains("src/core/import/internal/InvoiceImportPersistenceStage.ipp", "parseImportedPartDraftsWithConfirmedVatMapping");
        requireContains("src/core/import/internal/InvoiceImportPersistenceStage.ipp", "persistSupplierInvoiceTemplateFromManualConfirmation");
        requireContains("src/core/import/internal/InvoiceImportPersistenceStage.ipp", "Ручний імпорт заблоковано: підтвердьте колонку 'Ціна з ПДВ'");
        requireContains("src/core/import/internal/InvoiceImportParsedDraftStage.ipp", "persistSupplierInvoiceTemplateIfUseful(rows, map, confidence)");
        requireContains("src/core/import/internal/InvoiceImportPersistenceStage.ipp", "confidenceHistory");
        requireContains("src/core/import/internal/InvoiceImportPersistenceStage.ipp", "vatPriceColumn");
    }

    void pdfOcrPipelineUsesSameVatAwareMappingContract()
    {
        using namespace cs::tests;
        requireContains("src/core/import/InvoiceImportParsing.h", "detectInvoiceColumnMappingPreview");
        requireContains("src/core/import/InvoiceImportParsing.h", "parseImportedPartDraftsWithConfirmedVatMapping");
        requireContains("src/core/import/internal/InvoicePriceColumnResolver.ipp", "parsePdfInvoiceText");
        requireContains("src/core/import/internal/InvoiceImportParsedDraftStage.ipp", "invoiceAmountMatchesVatPrice(qtyValue, priceValue, amountValue)");
        requireContains("src/ui/tabs/orders/WorkPartsImportCoordinator.cpp", "WorkPartsInvoiceColumnMappingDialog");
        requireContains("src/ui/tabs/orders/WorkPartsImportCoordinator.cpp", "confirmWorkPartsInvoiceColumnMapping");
        requireContains("src/ui/tabs/orders/WorkPartsImportCoordinator.cpp", "runConfirmedVatMappingParseAsync");
        requireContains("src/ui/tabs/orders/WorkPartsImportCoordinator.cpp", "import_manual_mapping_confirmed_async");
        requireContains("src/ui/tabs/orders/CurrentOrdersPageLocalSupport.cpp", "CurrentOrdersInvoiceColumnMappingDialog");
        requireContains("src/ui/tabs/orders/CurrentOrdersPageLocalSupport.cpp", "confirmCurrentOrdersInvoiceColumnMapping");
        requireContains("src/ui/tabs/orders/CurrentOrdersPageActions.cpp", "runConfirmedVatMappingParseAsync");
        const auto actions = readSourceFile("src/ui/tabs/orders/CurrentOrdersPageActions.cpp");
        const auto support = readSourceFile("src/ui/tabs/orders/CurrentOrdersPageLocalSupport.cpp");
        const auto reviewWired = [](const QString& a, const QString& s) {
            return a.contains("CurrentOrdersSupport::reviewInvoice(filePath,data,drafts,guard)")
                && s.contains("auto dialog = createInvoiceReview(path, data, drafts, parent)")
                && s.contains("std::make_unique<CurrentOrdersExcelImportDialog>(path, data, drafts, parent)")
                && s.contains("return dialog->exec() == QDialog::Accepted");
        };
        QVERIFY(reviewWired(actions, support));
        QString disconnected = support;
        disconnected.replace("dialog->exec()", "QDialog::Rejected");
        QVERIFY(!reviewWired(actions, disconnected));
        requireContains("src/ui/tabs/orders/WorkPartsDialogExcelSupport.h", "runConfirmedVatMappingParseAsync");
        requireContains("src/ui/tabs/orders/WorkPartsDialogExcelSupport.cpp", "invoice_manual_vat_mapping_parse_worker");
        requireContains("src/ui/tabs/orders/WorkPartsDialogExcelSupport.cpp", "розбір не має виконуватися в UI-потоці");
        requireContains("src/ui/tabs/orders/WorkPartsImportCoordinator.cpp", "PDF (*.pdf)");
        requireContains("scripts/check_invoice_detector_tz_100.py", "avtonova_d_excel_vat.xlsx");
        requireContains("scripts/check_invoice_detector_tz_100.py", "unik_trade_excel_vat.xls");
        requireContains("scripts/check_invoice_detector_tz_100.py", "elit_pdf_text_layer_vat.pdf");
        requireContains("scripts/check_invoice_detector_tz_100.py", "ocr_pdf_common_errors_image_only.pdf");
    }
};

QTEST_MAIN(RegressionHybridInvoiceImportManualMappingTests)
#include "RegressionHybridInvoiceImportManualMappingTests.moc"
