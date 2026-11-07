#include "core/import/parsers/InvoiceParsers.h"
#include "core/import/InvoiceImportParsing.h"

#include <QStringList>
#include <QRegularExpression>

namespace InvoiceImport {

namespace {
static bool hasEliteMarker(const QString& lowered)
{
    return lowered.contains(QString::fromUtf8("еліт")) || lowered.contains(QStringLiteral("elit"));
}

static bool hasAvtonovaMarker(const QString& lowered)
{
    return lowered.contains(QString::fromUtf8("автонова")) || lowered.contains(QStringLiteral("autonova"));
}

// Preserve source columns when a supplier layout is unknown. Never silently
// treat an unlabelled amount as a VAT-inclusive unit price.
static ExcelReadResult indexedPdfTable(const QString& text)
{
    ExcelReadResult result;
    QList<QStringList> metadata;
    QStringList headers, current;
    QList<int> starts;
    int nameColumn=-1, qtyColumn=-1, priceColumn=-1;
    const auto lines=text.split(QRegularExpression("[\\r\\n\\f]+"), Qt::SkipEmptyParts);
    const auto finishRow=[&] {
        if (!current.isEmpty() && !current[0].isEmpty()
            && !current.value(nameColumn).isEmpty() && !current.value(priceColumn).isEmpty()) result.rows.append(current);
    };
    for (const QString& raw : lines) {
        const QString line=raw.trimmed();
        if (headers.isEmpty()) {
            if (!line.startsWith(QString::fromUtf8("№"))
                || !line.contains(QString::fromUtf8("Каталожний номер"), Qt::CaseInsensitive)
                || !line.contains(QString::fromUtf8("Найменування"), Qt::CaseInsensitive)) {
                metadata.append({line}); continue;
            }
            headers=line.split(QRegularExpression(" {2,}|\\t+"), Qt::SkipEmptyParts);
            int offset=0;
            for (int i=0;i<headers.size();++i) {
                const int start=raw.indexOf(headers[i],offset);starts.append(start);offset=start+headers[i].size();
                if (headers[i].contains(QString::fromUtf8("Найменування"),Qt::CaseInsensitive)) nameColumn=i;
                if (headers[i].contains(QString::fromUtf8("Кількість"),Qt::CaseInsensitive)) qtyColumn=i;
                if (headers[i].compare(QString::fromUtf8("Ціна"),Qt::CaseInsensitive)==0) priceColumn=i;
            }
            if (nameColumn<0 || qtyColumn<0 || priceColumn<0) return {};
            // Header labels are not cell boundaries. Find separators that do not
            // cut a word or number on any of the wrapped physical table lines.
            const auto headerStarts=starts;
            for (int i=1;i<starts.size();++i) {
                const int minimum=headerStarts[i-1]+headers[i-1].size();
                for (int boundary=headerStarts[i];boundary>=minimum;--boundary) {
                    bool safe=true;
                    for (int r=lines.indexOf(raw)+1;r<lines.size();++r) {
                        const auto& physical=lines[r];
                        if (physical.contains(QString::fromUtf8("Всього"),Qt::CaseInsensitive)
                            || physical.trimmed().startsWith(QString::fromUtf8("Відпустив"))) break;
                        if (boundary>0 && boundary<physical.size()
                            && !physical[boundary-1].isSpace() && !physical[boundary].isSpace()) {safe=false;break;}
                    }
                    if (safe) {starts[i]=boundary;break;}
                }
            }
            starts[0]=0;
            result.rows.append(headers);current=QStringList(headers.size(),QString());continue;
        }
        if (line.startsWith(QString::fromUtf8("Всього"),Qt::CaseInsensitive)
            || line.startsWith(QString::fromUtf8("Відпустив"),Qt::CaseInsensitive)
            || line.contains(QString::fromUtf8("Всього до сплати"),Qt::CaseInsensitive)) break;
        QStringList cells;
        for (int i=0;i<starts.size();++i)
            cells.append(raw.mid(starts[i],i+1<starts.size()?starts[i+1]-starts[i]:-1).trimmed());
        const bool numbered=QRegularExpression("^\\d+$").match(cells[0]).hasMatch();
        const bool complete=!current[0].isEmpty() && !current[qtyColumn].isEmpty() && !current[priceColumn].isEmpty();
        // Descriptions and brands may begin on the line preceding the printed row number.
        if ((numbered && !current[0].isEmpty()) || (complete && !cells[nameColumn].isEmpty())) {
            finishRow();current=QStringList(headers.size(),QString());
        }
        if (!numbered && !cells[0].isEmpty()) continue;
        if (complete && !numbered && cells[nameColumn].isEmpty()
            && cells[qtyColumn].isEmpty() && !cells.last().isEmpty()
            && cells.join(QString()).trimmed()==cells.last()) continue;
        for (int i=0;i<cells.size();++i) {
            if (cells[i].isEmpty()) continue;
            if (!current[i].isEmpty()) current[i]+=' ';
            current[i]+=cells[i];
        }
        if (!current[qtyColumn].isEmpty()) {
            const auto qty=QRegularExpression(QString::fromUtf8("^(\\d+(?:[.,]\\d+)?)\\s*(?:шт\\.?|од\\.?)?$"),
                QRegularExpression::CaseInsensitiveOption).match(current[qtyColumn]);
            if (qty.hasMatch()) current[qtyColumn]=qty.captured(1);
        }
    }
    finishRow();
    if (result.rows.size()<2) return {};
    const auto meta=detectInvoiceImportMeta(metadata,{});
    result.documentNumber=meta.documentNumber;result.documentDate=meta.documentDate;
    result.manualMappingRequired=true;
    result.columnMappingPreview.warnings.append(QString::fromUtf8("Перевірте колонки закупівельної ціни перед імпортом."));
    result.rows.append(metadata);
    return result;
}

static ExcelReadResult editablePdfTable(const QString& text)
{
    ExcelReadResult result;
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\\r\\n\\f]+")), Qt::SkipEmptyParts);
    QList<QStringList> metadataRows;
    bool inTable = false;
    bool amountOnly = false;
    int dataRows = 0;
    const QRegularExpression separators(QStringLiteral("(?: {2,}|\\t+)"));
    const QRegularExpression simpleRow(QStringLiteral(R"(^(.+)\s+(\d+(?:[.,]\d+)?)\s+(\d+[.,]\d{2})\s*$)"));
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        const QString lower = line.toLower();
        const bool header = (lower.contains(QString::fromUtf8("найменування")) || lower.contains(QString::fromUtf8("назва")))
            && lower.contains(QString::fromUtf8("кількість"))
            && (lower.contains(QString::fromUtf8("сума")) || lower.contains(QString::fromUtf8("ціна")));
        if (!inTable) {
            if (!header) {
                metadataRows.append({line});
                continue;
            }
            inTable = true;
            QStringList cells = line.split(separators, Qt::SkipEmptyParts);
            amountOnly = !lower.contains(QString::fromUtf8("ціна")) && (cells.size() == 3 || cells.size() == 1);
            if (amountOnly) {
                cells = {QString::fromUtf8("Найменування"), QString::fromUtf8("Кількість"),
                         QString::fromUtf8("Сума, грн"), QString::fromUtf8("Ціна (сума / кількість)")};
            }
            result.rows.append(cells);
            continue;
        }
        if (header) continue;
        if (lower.startsWith(QString::fromUtf8("всього")) || lower.startsWith(QString::fromUtf8("разом"))
            || lower.startsWith(QString::fromUtf8("відпустив")) || lower.startsWith(QString::fromUtf8("примітка"))) break;
        QStringList cells = line.split(separators, Qt::SkipEmptyParts);
        if (amountOnly) {
            if (cells.size() != 3) {
                const auto match = simpleRow.match(line);
                if (match.hasMatch()) cells = {match.captured(1).trimmed(), match.captured(2), match.captured(3)};
            }
            if (cells.size() != 3) {
                // Keep wrapped descriptions attached to their source row.
                if (dataRows > 0 && cells.size() == 1) result.rows.last()[0] += QLatin1Char(' ') + line;
                continue;
            }
            QString qtyText = cells[1];
            QString totalText = cells[2];
            qtyText.replace(QLatin1Char(','), QLatin1Char('.'));
            totalText.remove(QLatin1Char(' '));
            totalText.replace(QLatin1Char(','), QLatin1Char('.'));
            bool qtyOk = false, totalOk = false;
            const double qty = qtyText.toDouble(&qtyOk);
            const double total = totalText.toDouble(&totalOk);
            cells.append(qtyOk && totalOk && qty > 0 ? QString::number(total / qty, 'f', 6) : QString());
        }
        if (cells.size() < 3) continue;
        result.rows.append(cells);
        ++dataRows;
    }
    if (dataRows == 0) {
        // A changed/missing header must not make readable columns inaccessible.
        result.rows.clear();
        for (const auto& raw : lines) {
            const auto cells = raw.trimmed().split(separators, Qt::SkipEmptyParts);
            int numbers = 0;
            for (QString cell : cells) {
                cell.remove(QLatin1Char(' '));
                cell.replace(QLatin1Char(','), QLatin1Char('.'));
                bool ok = false;
                cell.toDouble(&ok);
                if (ok) ++numbers;
            }
            if (cells.size() >= 3 && numbers >= 2) ++dataRows;
            result.rows.append(cells);
        }
        if (dataRows == 0) return {};
        amountOnly = false;
    }
    const auto meta = detectInvoiceImportMeta(metadataRows, {});
    result.documentNumber = meta.documentNumber;
    result.documentDate = meta.documentDate;
    if (result.documentDate.isEmpty()) {
        for (const auto& row : metadataRows) {
            if (!row.first().contains(QString::fromUtf8("накладна"), Qt::CaseInsensitive)) continue;
            result.documentDate = ParserBridge::normalizePdfInvoiceDate(row.first());
            if (!result.documentDate.isEmpty()) break;
        }
    }
    result.manualMappingRequired = true;
    result.columnMappingPreview.warnings.append(QString::fromUtf8("Новий формат PDF: перевірте призначення колонок перед імпортом."));
    if (amountOnly) {
        auto& preview = result.columnMappingPreview;
        preview.headerRow = 0;
        preview.partNameColumn = 0;
        preview.quantityColumn = 1;
        preview.totalWithVatColumn = 2;
        preview.priceWithVatColumn = 3;
        preview.warnings.append(QString::fromUtf8("Ціну за одиницю обчислено: сума / кількість. Підтвердіть, що сума є кінцевою закупівельною сумою з ПДВ."));
    }
    // Metadata also carries the supplier name for the existing detector.
    result.rows.append(metadataRows);
    return result;
}
}

ExcelReadResult GenericPdfParser::parseText(const QString& text, QString* errorText)
{
    ExcelReadResult result;
    QStringList diagnostics;
    const QString normalized = text.simplified().toLower();
    const bool transportOnly = normalized.contains(QString::fromUtf8("дата виконання заявки"))
        && normalized.contains(QString::fromUtf8("вид оплати перевізнику"))
        && !normalized.contains(QString::fromUtf8("кількість"))
        && !normalized.contains(QString::fromUtf8("ціна"));
    const bool serviceOrder = normalized.contains("carservicepointg")
        && normalized.contains(QString::fromUtf8("наряд-замовлення"));
    if (transportOnly || serviceOrder) {
        result.unsupportedDocument = true;
        if (errorText) *errorText = transportOnly
            ? QString::fromUtf8("Це транспортна заявка, а не товарна накладна: документ не містить запчастин, кількостей і закупівельних цін. Виберіть видаткову накладну постачальника з таблицею товарів.")
            : QString::fromUtf8("Це наряд-замовлення СТО, а не накладна постачальника. Ціни клієнта не можна імпортувати як закупівельні. Виберіть оригінальну накладну постачальника.");
        return result;
    }

    // Supplier heuristics can mistake a shelf label for an OEM on wrapped rows.
    // Prefer the physical columns when this indexed catalogue layout is present.
    const auto indexed=indexedPdfTable(text);
    if (!indexed.rows.isEmpty()) {
        if (errorText) errorText->clear();
        return indexed;
    }

    auto tryParser = [&](const auto& parser) -> bool {
        QString localError;
        const ExcelReadResult parsed = parser(text, &localError);
        if (!parsed.rows.isEmpty()) {
            result = parsed;
            return true;
        }
        if (!localError.trimmed().isEmpty() && !diagnostics.contains(localError.trimmed())) {
            diagnostics << localError.trimmed();
        }
        return false;
    };

    const QString lowered = text.toLower();
    const bool looksLikeElite = hasEliteMarker(lowered);
    const bool looksLikeAvtonova = hasAvtonovaMarker(lowered);

    if (looksLikeElite && !looksLikeAvtonova) {
        if (tryParser(ElitParser::parsePdfText)) {
            if (errorText) errorText->clear();
            return result;
        }
    } else if (looksLikeAvtonova && !looksLikeElite) {
        if (tryParser(AvtonovaParser::parsePdfText)) {
            if (errorText) errorText->clear();
            return result;
        }
    } else {
        if (tryParser(AvtonovaParser::parsePdfText)) {
            if (errorText) errorText->clear();
            return result;
        }
        if (tryParser(ElitParser::parsePdfText)) {
            if (errorText) errorText->clear();
            return result;
        }
    }

    if (!looksLikeElite && tryParser(ElitParser::parsePdfText)) {
        if (errorText) errorText->clear();
        return result;
    }
    if (!looksLikeAvtonova && tryParser(AvtonovaParser::parsePdfText)) {
        if (errorText) errorText->clear();
        return result;
    }

    result = editablePdfTable(text);
    if (!result.rows.isEmpty()) {
        if (errorText) errorText->clear();
        return result;
    }
    if (errorText) {
        *errorText = diagnostics.isEmpty()
            ? QString::fromUtf8("Не вдалося розпізнати PDF накладної. Потрібен текстовий PDF з таблицею позицій.")
            : diagnostics.join(QStringLiteral(" | "));
    }
    return result;
}

ExcelReadResult GenericPdfParser::readRows(const QString& filePath, QString* errorText)
{
    return ParserBridge::readPdfInvoiceRowsInternal(filePath, errorText);
}

} // namespace InvoiceImport
