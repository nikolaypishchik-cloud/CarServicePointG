#pragma once

#include "core/import/InvoiceImportTypes.h"

namespace InvoiceImport {

InvoiceImportMeta detectInvoiceImportMeta(const QList<QStringList>& rows,
                                          const QSet<int>& highlightedColumns);


InvoiceColumnMappingPreview detectInvoiceColumnMappingPreview(const QList<QStringList>& rows,
                                                            const QSet<int>& highlightedColumns,
                                                            const ParseLooseNumberFn& parseLooseNumber);

QList<ImportedPartDraft> parseImportedPartDraftsWithConfirmedVatMapping(const QList<QStringList>& rows,
                                                                        const InvoiceColumnMappingPreview& confirmedMapping,
                                                                        const NormalizeTextFn& normalizeSentence,
                                                                        const ParseLooseNumberFn& parseLooseNumber,
                                                                        const MoneyTextFn& moneyText,
                                                                        QString* errorText,
                                                                        const QString& documentDate = QString());

QList<ImportedPartDraft> parseImportedPartDrafts(const QList<QStringList>& rows,
                                                 const QSet<int>& highlightedColumns,
                                                 const NormalizeTextFn& normalizeSentence,
                                                 const ParseLooseNumberFn& parseLooseNumber,
                                                 const MoneyTextFn& moneyText,
                                                 QString* errorText,
                                                 const QString& documentDate = QString());

ExcelReadResult parsePdfInvoiceText(const QString& text,
                                    QString* errorText = nullptr);

} // namespace InvoiceImport
