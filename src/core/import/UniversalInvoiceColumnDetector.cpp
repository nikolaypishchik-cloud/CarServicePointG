#include "core/import/UniversalInvoiceColumnDetector.h"

namespace InvoiceImport {

InvoiceImportMeta UniversalInvoiceColumnDetector::detectMeta(const QList<QStringList>& rows,
                                                             const QSet<int>& highlightedColumns)
{
    return detectInvoiceImportMeta(rows, highlightedColumns);
}

InvoiceColumnMappingPreview UniversalInvoiceColumnDetector::detect(const QList<QStringList>& rows,
                                                                   const QSet<int>& highlightedColumns,
                                                                   const ParseLooseNumberFn& parseLooseNumber)
{
    return detectInvoiceColumnMappingPreview(rows, highlightedColumns, parseLooseNumber);
}

QList<ImportedPartDraft> UniversalInvoiceColumnDetector::parseAutomatic(const QList<QStringList>& rows,
                                                                        const QSet<int>& highlightedColumns,
                                                                        const NormalizeTextFn& normalizeSentence,
                                                                        const ParseLooseNumberFn& parseLooseNumber,
                                                                        const MoneyTextFn& moneyText,
                                                                        QString* errorText,
                                                                        const QString& documentDate)
{
    return parseImportedPartDrafts(rows, highlightedColumns, normalizeSentence, parseLooseNumber, moneyText, errorText, documentDate);
}

QList<ImportedPartDraft> UniversalInvoiceColumnDetector::parseWithConfirmedVatMapping(const QList<QStringList>& rows,
                                                                                      const InvoiceColumnMappingPreview& confirmedMapping,
                                                                                      const NormalizeTextFn& normalizeSentence,
                                                                                      const ParseLooseNumberFn& parseLooseNumber,
                                                                                      const MoneyTextFn& moneyText,
                                                                                      QString* errorText,
                                                                                      const QString& documentDate)
{
    return parseImportedPartDraftsWithConfirmedVatMapping(rows, confirmedMapping, normalizeSentence, parseLooseNumber, moneyText, errorText, documentDate);
}


UniversalInvoiceDetectionResult UniversalInvoiceColumnDetector::analyzeAndParse(const QList<QStringList>& rows,
                                                                                const QSet<int>& highlightedColumns,
                                                                                const NormalizeTextFn& normalizeSentence,
                                                                                const ParseLooseNumberFn& parseLooseNumber,
                                                                                const MoneyTextFn& moneyText,
                                                                                const QString& documentDate)
{
    UniversalInvoiceDetectionResult result;
    result.performed = true;
    result.meta = detectMeta(rows, highlightedColumns);
    result.columnMappingPreview = detect(rows, highlightedColumns, parseLooseNumber);

    // Pass27 closure: automatic import must use the exact VAT-aware mapping
    // produced by this detector result. Do not call the previous automatic parser
    // here, because it can re-detect columns independently and weaken the
    // single-source-of-truth contract for “Ціна з ПДВ”.
    const bool mappingIsSafeForAutomaticImport =
        result.columnMappingPreview.automaticAllowed &&
        result.columnMappingPreview.priceWithVatConfirmed &&
        result.columnMappingPreview.priceWithVatColumn >= 0 &&
        result.columnMappingPreview.quantityColumn >= 0 &&
        result.columnMappingPreview.partNameColumn >= 0 &&
        result.columnMappingPreview.totalConfidence >= 85 &&
        !result.columnMappingPreview.manualColumnSelectionRequired;

    result.manualMappingRequired = !mappingIsSafeForAutomaticImport;

    QString errorText;
    if (mappingIsSafeForAutomaticImport) {
        result.parsedPartDrafts = parseWithConfirmedVatMapping(rows,
                                                               result.columnMappingPreview,
                                                               normalizeSentence,
                                                               parseLooseNumber,
                                                               moneyText,
                                                               &errorText,
                                                               documentDate);
    } else if (!result.columnMappingPreview.priceWithVatConfirmed || result.columnMappingPreview.priceWithVatColumn < 0) {
        errorText = QString::fromUtf8("Автоматичний імпорт заблоковано: колонку 'Ціна з ПДВ' не підтверджено надійно.");
    } else if (result.columnMappingPreview.totalConfidence < 85) {
        errorText = QString::fromUtf8("Автоматичний імпорт потребує preview: впевненість нижче 85%.");
    } else {
        errorText = QString::fromUtf8("Автоматичний імпорт потребує ручного підтвердження mapping колонок.");
    }

    result.automaticImportError = errorText;
    result.automaticImportAllowed = mappingIsSafeForAutomaticImport &&
                                    errorText.trimmed().isEmpty() &&
                                    !result.parsedPartDrafts.isEmpty();
    if (!result.automaticImportAllowed) {
        result.manualMappingRequired = true;
    }
    return result;
}

} // namespace InvoiceImport
