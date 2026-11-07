#include "core/import/parsers/InvoiceParsers.h"

namespace InvoiceImport {

ExcelReadResult GenericXlsParser::readRows(const QString& filePath, QString* errorText)
{
    return ParserBridge::readSpreadsheetInvoiceRowsInternal(filePath, errorText);
}

} // namespace InvoiceImport
