#pragma once

#include "core/import/InvoiceImportParsing.h"
#include "core/import/InvoiceAttachmentPdfService.h"

#include <QString>
#include <QWidget>

#include <functional>

namespace WorkPartsDialogExcelSupport {

using InvoiceImport::ImportedPartDraft;
using InvoiceImport::InvoiceImportMeta;
using InvoiceImport::ExcelReadResult;
using InvoiceImport::NormalizeTextFn;
using InvoiceImport::ParseLooseNumberFn;
using InvoiceImport::MoneyTextFn;
using InvoiceImport::detectInvoiceImportMeta;
using InvoiceImport::parseImportedPartDrafts;
using InvoiceImport::parsePdfInvoiceText;

inline QString cancelledReadMarker() { return QStringLiteral("invoice_read_cancelled"); }

void runExcelReadAsync(QWidget* owner,
                       const QString& progressText,
                       const QString& filePath,
                       std::function<void(const ExcelReadResult&, const QString&)> onDone,
                       bool showOverlay = true);

void runConfirmedVatMappingParseAsync(QWidget* owner,
                                      const QList<QStringList>& rows,
                                      const InvoiceImport::InvoiceColumnMappingPreview& confirmedMapping,
                                      const QString& documentDate,
                                      const NormalizeTextFn& normalizeSentence,
                                      const ParseLooseNumberFn& parseLooseNumber,
                                      const MoneyTextFn& moneyText,
                                      std::function<void(const QList<ImportedPartDraft>&, const QString&)> onDone);

using InvoiceImport::ensureInvoicePdfAttachment;

} // namespace WorkPartsDialogExcelSupport
