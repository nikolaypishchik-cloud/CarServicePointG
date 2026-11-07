#pragma once

#include "core/import/InvoiceImportTypes.h"

namespace InvoiceImport {

// P0 Import p6: vendor-specific parsers are addressed through explicit parser
// contracts. The heavy recognition rules stay in the import engine bridge for
// this step, so the parser split is architectural and does not change business
// parsing behavior.
class ElitParser final {
public:
    static ExcelReadResult parsePdfText(const QString& text, QString* errorText = nullptr);
};

class AvtonovaParser final {
public:
    static ExcelReadResult parsePdfText(const QString& text, QString* errorText = nullptr);
};

class GenericPdfParser final {
public:
    static ExcelReadResult parseText(const QString& text, QString* errorText = nullptr);
    static ExcelReadResult readRows(const QString& filePath, QString* errorText = nullptr);
};

class GenericXlsParser final {
public:
    static ExcelReadResult readRows(const QString& filePath, QString* errorText = nullptr);
};

namespace ParserBridge {
QString normalizePdfInvoiceDate(const QString& text);
ExcelReadResult parseElitePdfTextInternal(const QString& text, QString* errorText = nullptr);
ExcelReadResult parseAvtonovaPdfTextInternal(const QString& text, QString* errorText = nullptr);
ExcelReadResult readPdfInvoiceRowsInternal(const QString& filePath, QString* errorText = nullptr);
ExcelReadResult readSpreadsheetInvoiceRowsInternal(const QString& filePath, QString* errorText = nullptr);
}

} // namespace InvoiceImport
