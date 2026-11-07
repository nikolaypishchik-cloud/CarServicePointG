#include "core/import/parsers/InvoiceParsers.h"

namespace InvoiceImport {

ExcelReadResult ElitParser::parsePdfText(const QString& text, QString* errorText)
{
    return ParserBridge::parseElitePdfTextInternal(text, errorText);
}

} // namespace InvoiceImport
