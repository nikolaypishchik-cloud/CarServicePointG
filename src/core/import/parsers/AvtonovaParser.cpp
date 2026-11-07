#include "core/import/parsers/InvoiceParsers.h"

namespace InvoiceImport {

ExcelReadResult AvtonovaParser::parsePdfText(const QString& text, QString* errorText)
{
    return ParserBridge::parseAvtonovaPdfTextInternal(text, errorText);
}

} // namespace InvoiceImport
