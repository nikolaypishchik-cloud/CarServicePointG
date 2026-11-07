#pragma once
#include "core/import/InvoiceImportTypes.h"
class QWidget;

bool editInvoiceColumnMapping(QWidget* parent, const QList<QStringList>& rows,
    const InvoiceImport::InvoiceColumnMappingPreview& preview, const QString& sourceFile,
    InvoiceImport::InvoiceColumnMappingPreview* confirmed);
