#pragma once

#include "core/import/InvoiceImportTypes.h"

#include <QString>

namespace InvoiceImport {

QString ensureInvoicePdfAttachment(const QString& sourcePath,
                                  const QString& documentNumber = QString(),
                                  const QString& documentDate = QString(),
                                  const QString& supplierName = QString(),
                                  QString* errorText = nullptr);

QString ensureInvoicePdfAttachmentFromRows(const QString& sourcePath,
                                           const ExcelReadResult& data,
                                           const QString& documentNumber = QString(),
                                           const QString& documentDate = QString(),
                                           const QString& supplierName = QString(),
                                           QString* errorText = nullptr);


} // namespace InvoiceImport
