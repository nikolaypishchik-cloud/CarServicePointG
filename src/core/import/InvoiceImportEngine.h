#pragma once

#include "core/import/InvoiceImportParsing.h"

#include <atomic>
#include <memory>

namespace InvoiceImport {
bool validateInvoicePdf(const QString& path, QString* errorText = nullptr);
// Worker-only, text extraction without importing rows or changing the database.
QString invoicePdfText(const QString& path, QString* errorText = nullptr);

// CS_INTERNAL_SYNC_FALLBACK_INTERNAL_ONLY: synchronous external-process fallback
// entry point is exposed only through the core/import engine. UI code must use
// ImportJob instead of including this header or launching Python/PowerShell/
// QProcess directly. This marker is enforced by the P2 legacy cleanup gate.
ExcelReadResult readInvoiceRows(const QString& filePath,
                                QString* errorText = nullptr);

ExcelReadResult readInvoiceRows(const QString& filePath,
                                QString* errorText,
                                const std::shared_ptr<std::atomic_bool>& cancelToken);

} // namespace InvoiceImport
