#pragma once

#include "core/import/InvoiceImportParsing.h"

namespace InvoiceImport {

class UniversalInvoiceColumnDetector final {
public:
    UniversalInvoiceColumnDetector() = delete;

    static InvoiceImportMeta detectMeta(const QList<QStringList>& rows,
                                        const QSet<int>& highlightedColumns = {});

    static InvoiceColumnMappingPreview detect(const QList<QStringList>& rows,
                                              const QSet<int>& highlightedColumns,
                                              const ParseLooseNumberFn& parseLooseNumber);

    static QList<ImportedPartDraft> parseAutomatic(const QList<QStringList>& rows,
                                                   const QSet<int>& highlightedColumns,
                                                   const NormalizeTextFn& normalizeSentence,
                                                   const ParseLooseNumberFn& parseLooseNumber,
                                                   const MoneyTextFn& moneyText,
                                                   QString* errorText,
                                                   const QString& documentDate = QString());

    static QList<ImportedPartDraft> parseWithConfirmedVatMapping(const QList<QStringList>& rows,
                                                                 const InvoiceColumnMappingPreview& confirmedMapping,
                                                                 const NormalizeTextFn& normalizeSentence,
                                                                 const ParseLooseNumberFn& parseLooseNumber,
                                                                 const MoneyTextFn& moneyText,
                                                                 QString* errorText,
                                                                 const QString& documentDate = QString());

    static UniversalInvoiceDetectionResult analyzeAndParse(const QList<QStringList>& rows,
                                                          const QSet<int>& highlightedColumns,
                                                          const NormalizeTextFn& normalizeSentence,
                                                          const ParseLooseNumberFn& parseLooseNumber,
                                                          const MoneyTextFn& moneyText,
                                                          const QString& documentDate = QString());
};

} // namespace InvoiceImport
