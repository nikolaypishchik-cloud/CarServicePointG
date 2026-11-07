// Extracted from src/core/import/InvoiceImportEngine.cpp for P1/P2 monolith reduction.
// Included exactly once by the original translation unit; generated split boundary is outside C++ raw strings.


        if (!pending.isEmpty() && lineLooksLikeRowData(line) &&
            line.simplified().contains(QRegularExpression(QStringLiteral(R"(^\d[\d\s]*[\.,]\d{2}$)")))) {
            int numericCount = 0;
            for (const QString& s : pending) {
                if (s.simplified().contains(QRegularExpression(QStringLiteral(R"(^\d[\d\s]*[\.,]\d{2}$)")))) {
                    ++numericCount;
                }
            }
            if (numericCount >= 3) flushPending();
        }

        pending << line;
    }
    flushPending();
    return parsedCount > 0;
}

static void ensurePdfInvoiceResultHeader(ExcelReadResult& result)
{
    if (!result.rows.isEmpty()) return;
    result.rows.append(QStringList{
        QString::fromUtf8("Дата"),
        QString::fromUtf8("OEM номер"),
        QString::fromUtf8("Найменування"),
        QString::fromUtf8("Бренд"),
        QString::fromUtf8("Кількість"),
        QString::fromUtf8("Ціна з ПДВ"),
        QString::fromUtf8("Сума з ПДВ"),
        QString::fromUtf8("Магазин")
    });
}

static void appendPdfInvoiceRow(ExcelReadResult& result,
                                const QString& store,
                                const QString& oem,
                                const QString& name,
                                const QString& qty,
                                const QString& price,
                                const QString& amount)
{
    ensurePdfInvoiceResultHeader(result);
    QString normalizedName;
    QString extractedBrand;
    splitPdfImportedNameAndBrand(name, oem, &normalizedName, &extractedBrand);
    if (normalizedName.isEmpty()) normalizedName = normalizePdfImportedTextCase(name.simplified());
    result.rows.append(QStringList{
        result.documentDate.isEmpty() ? QString() : DateFormats::parseDate(result.documentDate).toString(QStringLiteral("dd.MM.yy")),
        oem.trimmed(),
        normalizedName,
        extractedBrand,
        qty.trimmed(),
        price.trimmed(),
        amount.trimmed(),
        ::StoreNameNormalizer::canonicalDisplayNameOrDefault(store, QString())
    });
}

static void populatePdfInvoiceMetaFromLines(ExcelReadResult& result, const QStringList& lines)
{
    if (lines.isEmpty()) return;
    QList<QStringList> metaRows;
    metaRows.reserve(lines.size());
    for (const QString& line : lines) metaRows.append(QStringList{line});
    const InvoiceImportMeta meta = detectInvoiceImportMeta(metaRows, {});
    if (result.documentNumber.isEmpty()) result.documentNumber = meta.documentNumber;
    if (result.documentDate.isEmpty()) result.documentDate = meta.documentDate;
}

static bool isAutoNovaPdfCandidateText(const QString& lowered)
{
    return lowered.contains(QString::fromUtf8("автонова")) ||
           lowered.contains(QStringLiteral("autonova"));
}

static bool isAutoNovaPdfTableHeader(const QString& normalized)
{
    return (normalized.contains(QStringLiteral("oemномер")) || normalized.contains(QStringLiteral("oem"))) &&
           normalized.contains(QStringLiteral("найменув")) &&
           normalized.contains(QStringLiteral("ціна")) &&
           normalized.contains(QStringLiteral("сума"));
}

static bool isPdfInvoiceLikelyUnit(const QString& token)
{
    const QString t = token.trimmed().toLower();
    return t == QString::fromUtf8("шт") ||
           t == QString::fromUtf8("штука") ||
           t == QString::fromUtf8("шт.") ||
           t == QString::fromUtf8("од") ||
           t == QString::fromUtf8("од.") ||
           t == QString::fromUtf8("компл") ||
           t == QString::fromUtf8("компл.") ||
           t == QString::fromUtf8("к-т") ||
           t == QString::fromUtf8("л") ||
           t == QString::fromUtf8("кг") ||
           t == QString::fromUtf8("г") ||
           t == QString::fromUtf8("м") ||
           t == QString::fromUtf8("см") ||
           t == QString::fromUtf8("уп") ||
           t == QString::fromUtf8("уп.") ||
           t == QString::fromUtf8("упак") ||
           t == QString::fromUtf8("пара") ||
           t == QString::fromUtf8("пар");
}

static bool isPdfInvoiceLikelyCodeFragmentToken(const QString& token)
{
    const QString t = token.trimmed();
    if (t.isEmpty() || isPdfInvoiceLikelyUnit(t)) return false;
    if (t.contains(QRegularExpression(QStringLiteral(R"([\p{Ll}])")))) return false;
    return t.contains(QRegularExpression(QStringLiteral(R"([A-ZА-Я0-9])")));
}

static bool isElitePdfLikelyItemCode(const QString& value)
{
    const QString t = cleanPdfInvoiceCode(value).trimmed();
    if (t.isEmpty()) return false;
    const QString lower = t.toLower();
    if (lower.contains(QString::fromUtf8("єдрпоу")) || lower.contains(QStringLiteral("тел")) ||
        lower.contains(QString::fromUtf8("рахунок")) || lower.contains(QString::fromUtf8("фактура")) ||
        lower.contains(QString::fromUtf8("пдв")) || lower.contains(QString::fromUtf8("директор")) ||
        lower.contains(QString::fromUtf8("бухгалтер"))) {
        return false;
    }
    const bool hasDigit = t.contains(QRegularExpression(QStringLiteral(R"(\d)")));
    if (!hasDigit) return false;
    const bool hasLatin = t.contains(QRegularExpression(QStringLiteral(R"([A-Za-z])")));
    if (hasLatin) return true;

    QString compact = t;
    compact.remove(QRegularExpression(QStringLiteral(R"([^0-9])")));
    return compact.size() >= 4;
}

static QStringList elitePdfFallbackCandidateLines(const QStringList& lines)
{
    const int headerStart = findElitePdfTableHeaderStart(lines);
    if (headerStart < 0) return lines;

    QStringList out;
    bool bodyStarted = false;
    for (int i = headerStart; i < lines.size(); ++i) {
        const QString raw = lines.at(i).trimmed();
        const QString normalized = normalizedImportHeader(raw);
        if (raw.isEmpty()) continue;
        if (isElitePdfTableHeaderSignal(normalized)) continue;
        if (isPdfInvoiceLikelyStopLine(normalized) && bodyStarted) break;
        bodyStarted = true;
        out << normalizeElitePdfAnchoredLine(raw);
    }
    return out.isEmpty() ? lines : out;
}

static QString extractLeadingPdfInvoiceCode(const QStringList& tokens, int startIndex, int* nextIndex)
{
    QString best;
    int bestNextIndex = startIndex;
    QString candidate;
    const int maxProbeTokens = qMin(tokens.size(), startIndex + 3);
    for (int i = startIndex; i < maxProbeTokens; ++i) {
        const QString token = tokens.at(i).trimmed();
        if (token.isEmpty()) break;
        if (i > startIndex && !isPdfInvoiceLikelyCodeFragmentToken(token)) break;
        candidate = candidate.isEmpty() ? token : (candidate + QLatin1Char(' ') + token);
        const QString cleaned = cleanPdfInvoiceCode(candidate);
        if (isPdfInvoiceLikelyUnit(cleaned)) break;
        if (isPdfInvoiceLikelyCode(cleaned)) {
            best = cleaned;
            bestNextIndex = i + 1;
            continue;
        }
        if (!best.isEmpty()) break;
        if (!isPdfInvoiceLikelyCodeFragmentToken(token)) break;
    }
    if (nextIndex) *nextIndex = bestNextIndex;
    return best;
}

static bool isAutoNovaPdfRowStart(const QString& line)
{
    const QString simplified = line.simplified();
    if (simplified.isEmpty()) return false;
    const QStringList tokens = simplified.split(QChar(' '), Qt::SkipEmptyParts);
    if (tokens.isEmpty()) return false;
    int index = 0;
    if (tokens.first().contains(QRegularExpression(QStringLiteral(R"(^\d+$)")))) index = 1;
    if (index >= tokens.size()) return false;
    int nextIndex = index;
    const QString code = extractLeadingPdfInvoiceCode(tokens, index, &nextIndex);
    return !code.isEmpty() && nextIndex > index;
}

static bool tryParseAutoNovaPdfRowBlock(const QString& block,
                                        QString* oemOut,
                                        QString* nameOut,
                                        QString* qtyOut,
                                        QString* priceOut,
                                        QString* amountOut)
{
    QString simplified = block;
    simplified.replace(QChar(0x00A0), QLatin1Char(' '));
    simplified = simplified.simplified();
    if (simplified.isEmpty()) return false;

    QStringList tokens = simplified.split(QChar(' '), Qt::SkipEmptyParts);
    if (tokens.isEmpty()) return false;

    int startIndex = 0;
    if (tokens.first().contains(QRegularExpression(QStringLiteral(R"(^\d+$)")))) startIndex = 1;
    if (startIndex >= tokens.size()) return false;

    int nextIndex = startIndex;
    const QString oem = extractLeadingPdfInvoiceCode(tokens, startIndex, &nextIndex);
    if (oem.isEmpty() || nextIndex >= tokens.size()) return false;

    const QString tail = tokens.mid(nextIndex).join(QLatin1Char(' '));
    const QString moneyPattern = QStringLiteral(R"(([0-9]{1,3}(?:\s[0-9]{3})*(?:[\.,][0-9]{2})|[0-9]+(?:[\.,][0-9]{2})))");
    const QString qtyPattern = QStringLiteral(R"(([0-9]+(?:[\.,][0-9]+)?))");
    const QString unitPattern = QStringLiteral(R"((шт|штука|шт\.|од|од\.|компл|компл\.|к-т|л|кг|г|м|см|уп|уп\.|упак|пара|пар))");

    const QString rowPatternWithUnit =
        QStringLiteral("^(.+?)\\s+") + qtyPattern +
        QStringLiteral("\\s+") + unitPattern +
        QStringLiteral("\\s+") + moneyPattern +
        QStringLiteral("\\s+") + moneyPattern +
        QStringLiteral("(?:\\s+") + moneyPattern + QStringLiteral(")?$");
    const QString rowPatternNoUnit =
        QStringLiteral("^(.+?)\\s+") + qtyPattern +
        QStringLiteral("\\s+") + moneyPattern +
        QStringLiteral("\\s+") + moneyPattern +
        QStringLiteral("(?:\\s+") + moneyPattern + QStringLiteral(")?$");

    const QList<QRegularExpression> patterns = {
        QRegularExpression(rowPatternWithUnit, QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(rowPatternNoUnit, QRegularExpression::CaseInsensitiveOption)
    };

    for (int i = 0; i < patterns.size(); ++i) {
        const auto match = patterns.at(i).match(tail);
        if (!match.hasMatch()) continue;

        QString name = match.captured(1).simplified();
        QString qty = match.captured(2).trimmed();
        QString price;
        QString amount;
        if (i == 0) {
            price = match.captured(4).trimmed();
            amount = match.captured(5).trimmed();
        } else {
            price = match.captured(3).trimmed();
            amount = match.captured(4).trimmed();
        }
        if (name.isEmpty() || qty.isEmpty() || price.isEmpty() || amount.isEmpty()) continue;

        if (oemOut) *oemOut = oem;
        if (nameOut) *nameOut = name;
        if (qtyOut) *qtyOut = qty;
        if (priceOut) *priceOut = price;
        if (amountOut) *amountOut = amount;
        return true;
    }

    // Автонова PDF часто повторює Excel-таблицю: після "Ціна" може бути
    // проміжна колонка курсів EUR/USD, а потрібна сума стоїть далі.
    // Тому тут не беремо просто наступне число після ціни, а шукаємо суму,
    // яка сходиться з кількість × ціна.
    auto cleanNumberToken = [](QString token) {
        token.replace(QChar(0x00A0), QLatin1Char(' '));
        token = token.trimmed();
        token.remove(QRegularExpression(QStringLiteral(R"(^[^0-9\-]+)")));
        token.remove(QRegularExpression(QStringLiteral(R"([^0-9\.,]+$)")));
        return token.trimmed();
    };
    auto numberValue = [&](const QString& token, double* valueOut = nullptr) {
        const QString cleaned = cleanNumberToken(token);
        if (!cleaned.contains(QRegularExpression(QStringLiteral(R"(^-?[0-9]+(?:[\.,][0-9]+)?$)")))) return false;
        bool ok = false;
        const double value = parseLooseNumberFallback(cleaned, &ok);
        if (!ok || !std::isfinite(value)) return false;
        if (valueOut) *valueOut = value;
        return true;
    };
    auto amountMatches = [](double qtyValue, double priceValue, double amountValue) {
        if (!std::isfinite(qtyValue) || !std::isfinite(priceValue) || !std::isfinite(amountValue)) return false;
        if (qtyValue <= 0.0 || priceValue < 0.0 || amountValue < 0.0) return false;
        const double expected = qtyValue * priceValue;
        const double tolerance = qMax(0.05, qMax(std::fabs(expected), std::fabs(amountValue)) * 0.035);
        return std::fabs(expected - amountValue) <= tolerance;
    };

    int unitIndex = -1;
    int qtyIndex = -1;
    for (int i = 1; i + 2 < tokens.size(); ++i) {
        if (!isPdfInvoiceLikelyUnit(tokens.at(i))) continue;
        double qtyValue = 0.0;
        if (!numberValue(tokens.at(i - 1), &qtyValue) || qtyValue <= 0.0) continue;
        int numericAfter = 0;
        for (int j = i + 1; j < tokens.size(); ++j) {
            if (numberValue(tokens.at(j))) ++numericAfter;
        }
        if (numericAfter >= 2) {
            unitIndex = i;
            qtyIndex = i - 1;
            break;
        }
    }

    if (unitIndex > nextIndex && qtyIndex > nextIndex) {
        const QString name = tokens.mid(nextIndex, qtyIndex - nextIndex).join(QLatin1Char(' ')).simplified();
        const QString qty = cleanNumberToken(tokens.at(qtyIndex));
        double qtyValue = 0.0;
        if (!name.isEmpty() && numberValue(qty, &qtyValue)) {
            QList<int> numericPositions;
            QList<double> numericValues;
            for (int i = unitIndex + 1; i < tokens.size(); ++i) {
                double value = 0.0;
                if (numberValue(tokens.at(i), &value)) {
                    numericPositions << i;
                    numericValues << value;
                }
            }

            if (numericPositions.size() >= 2) {
                const int priceTokenIndex = numericPositions.first();
                const double priceValue = numericValues.first();
                int amountTokenIndex = -1;
                for (int k = 1; k < numericPositions.size(); ++k) {
                    if (amountMatches(qtyValue, priceValue, numericValues.at(k))) {
                        amountTokenIndex = numericPositions.at(k);
                        break;
                    }
                }
                if (amountTokenIndex < 0) amountTokenIndex = numericPositions.at(1);

                const QString price = cleanNumberToken(tokens.at(priceTokenIndex));
                const QString amount = cleanNumberToken(tokens.at(amountTokenIndex));
                if (!price.isEmpty() && !amount.isEmpty()) {
                    if (oemOut) *oemOut = oem;
                    if (nameOut) *nameOut = name;
                    if (qtyOut) *qtyOut = qty;
                    if (priceOut) *priceOut = price;
                    if (amountOut) *amountOut = amount;
                    return true;
                }
            }
        }
    }

    return false;
}

ExcelReadResult ParserBridge::parseAvtonovaPdfTextInternal(const QString& text, QString* errorText)
{
    ExcelReadResult result;
    const QStringList layoutLines = splitPdfInvoiceLayoutLines(text);
    if (layoutLines.isEmpty()) {
        if (errorText) *errorText = QString::fromUtf8("PDF накладної не містить тексту для розпізнавання.");
        return result;
    }

    const QString lowered = layoutLines.join(QLatin1Char('\n')).toLower();
    bool headerSeen = false;
    bool genericRowSignalSeen = false;
    for (const QString& line : layoutLines) {
        const QString normalized = normalizedImportHeader(line);
        if (isAutoNovaPdfTableHeader(normalized)) {
            headerSeen = true;
        }
        if (isAutoNovaPdfRowStart(line)) {
            genericRowSignalSeen = true;
        }
        if (headerSeen && genericRowSignalSeen) break;
    }
    if (!isAutoNovaPdfCandidateText(lowered) && !headerSeen && !genericRowSignalSeen) {
        if (errorText) *errorText = QString::fromUtf8("PDF не схожий на накладну Автонова-D або універсальну PDF-таблицю запчастин.");
        return result;
    }

    const QStringList plainLines = splitPdfInvoiceLines(text);
    populatePdfInvoiceMetaFromLines(result, plainLines);
    ensurePdfInvoiceResultHeader(result);
    QList<QStringList> metaRows;
    metaRows.reserve(plainLines.size());
    for (const QString& line : plainLines) metaRows.append(QStringList{line});
    const QString detectedStore = detectInvoiceSourceStore(metaRows);
    const QString parserStore = !detectedStore.trimmed().isEmpty()
        ? detectedStore
        : (isAutoNovaPdfCandidateText(lowered) ? QString::fromUtf8("Автонова-D") : QString());

    bool inTable = !headerSeen;
    int parsedCount = 0;
    QStringList currentBlock;

    auto flushBlock = [&]() {
        if (currentBlock.isEmpty()) return;
        const QString block = currentBlock.join(QLatin1Char(' '));
        QString oem;
        QString name;
        QString qty;
        QString price;
        QString amount;
        if (tryParseAutoNovaPdfRowBlock(block, &oem, &name, &qty, &price, &amount)) {
            appendPdfInvoiceRow(result, parserStore, oem, name, qty, price, amount);
            ++parsedCount;
        }
        currentBlock.clear();
    };

    for (const QString& rawLine : layoutLines) {
        const QString line = rawLine.trimmed();
        const QString normalized = normalizedImportHeader(line);
        if (!inTable) {
            if (isAutoNovaPdfTableHeader(normalized)) inTable = true;
            continue;
        }

        if (isPdfInvoiceLikelyStopLine(normalized)) {
            flushBlock();
            break;
        }
        if (normalized.isEmpty() || isAutoNovaPdfTableHeader(normalized)) continue;

        const bool rowStart = isAutoNovaPdfRowStart(line);
        if (rowStart && !currentBlock.isEmpty()) {
            flushBlock();
        }
        if (rowStart || !currentBlock.isEmpty()) {
            currentBlock << line;
        }
    }
    flushBlock();

    if (parsedCount <= 0) {
        result.rows.clear();
        if (errorText) *errorText = QString::fromUtf8("Не вдалося розпізнати рядки товарів у PDF накладної Автонова-D/універсальної PDF-таблиці.");
    }
    return result;
}

ExcelReadResult ParserBridge::parseElitePdfTextInternal(const QString& text, QString* errorText)
{
    ExcelReadResult result;
    const QStringList lines = splitPdfInvoiceLines(text);
    const QStringList layoutLines = splitPdfInvoiceLayoutLines(text);
    if (lines.isEmpty()) {
        if (errorText) *errorText = QString::fromUtf8("PDF накладної не містить тексту для розпізнавання.");
        return result;
    }

    const QString joined = lines.join(QLatin1Char('\n'));
    const QString lowered = joined.toLower();
    if (!lowered.contains(QString::fromUtf8("еліт")) && !lowered.contains(QStringLiteral("elit"))) {
        if (errorText) *errorText = QString::fromUtf8("PDF не схожий на накладну Еліт.");
        return result;
    }

    const QList<QRegularExpression> numDatePatterns = {
        QRegularExpression(QString::fromUtf8(R"((?:видаткова\s+накладна|рахунок\s*-\s*фактура)\s*№\s*([^\s]+)\s*від\s*([^\n\r]+?)(?:\s+р\.?|$))"), QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QString::fromUtf8(R"(рахунок\s*-\s*фактура\s*№\s*([^\s]+)\s*від\s*([^\n\r]+?)(?:\s+р\.?|$))"), QRegularExpression::CaseInsensitiveOption)
    };
    for (const QRegularExpression& numDateRx : numDatePatterns) {
        const auto nd = numDateRx.match(joined);
        if (!nd.hasMatch()) continue;
        result.documentNumber = nd.captured(1).trimmed();
        result.documentDate = normalizePdfInvoiceDateToIso(nd.captured(2));
        break;
    }
    populatePdfInvoiceMetaFromLines(result, lines);
    ensurePdfInvoiceResultHeader(result);

    if (tryParseEliteInvoiceStandardTextTable(lines, result)) {
        appendInvoiceImportLog(QStringLiteral("elite standard-text parser accepted | rows=%1").arg(result.rows.size()));
        return result;
    }

    if (tryParseEliteInvoiceWideVatLayoutTable(layoutLines, result)) {
        appendInvoiceImportLog(QStringLiteral("elite wide-vat-layout parser accepted | rows=%1").arg(result.rows.size()));
        return result;
    }

    if (tryParseEliteInvoiceDirectCompactRows(lines, result)) {
        appendInvoiceImportLog(QStringLiteral("elite direct-compact parser accepted | rows=%1").arg(result.rows.size()));
        return result;
    }

    int detectedTableHeader = -1;
    const QStringList tableLines = extractElitePdfTableLines(layoutLines, &detectedTableHeader);
    appendInvoiceImportLog(QStringLiteral("elite table detect | header=%1 | tableLines=%2 | rawLines=%3")
                               .arg(detectedTableHeader)
                               .arg(tableLines.size())
                               .arg(layoutLines.size()));

    const QStringList denseParsingLines = tableLines.isEmpty() ? layoutLines : tableLines;
    if (tryParseEliteInvoiceDenseCompactTable(denseParsingLines, result)) {
        appendInvoiceImportLog(QStringLiteral("elite dense-compact parser accepted | rows=%1").arg(result.rows.size()));
        return result;
    }

    if (tryParseEliteInvoiceVatWarehouseTable(layoutLines, result)) {
        appendInvoiceImportLog(QStringLiteral("elite vat-warehouse parser accepted | rows=%1").arg(result.rows.size()));
        return result;
    }

    if (tryParseEliteInvoiceLayoutTable(layoutLines, result)) {
        appendInvoiceImportLog(QStringLiteral("elite layout-column parser accepted | rows=%1").arg(result.rows.size()));
        return result;
    }

    if (tryParseEliteInvoiceAnchoredTable(layoutLines, result)) {
        appendInvoiceImportLog(QStringLiteral("elite anchored-table parser accepted | rows=%1").arg(result.rows.size()));
        return result;
    }

    const QStringList parsingLines = tableLines.isEmpty() ? lines : tableLines;

    bool inTable = !tableLines.isEmpty();
    int parsedCount = 0;
    const QRegularExpression itemRx(QStringLiteral(R"(^([A-ZА-Я0-9][A-ZА-Я0-9._/\- ]{1,})\s+([A-ZА-Я0-9][A-ZА-Я0-9._/\- ]{1,})\s+(.+?)\s+(шт|компл|компл\.|к-т|л|кг|м)\s+([0-9\s]+[\.,][0-9]{2})\s+([0-9\s]+[\.,][0-9]{2})\s+([0-9\s]+[\.,][0-9]{2})$)"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression itemRxNoUnit(QStringLiteral(R"(^([A-ZА-Я0-9][A-ZА-Я0-9._/\- ]{1,})\s+([A-ZА-Я0-9][A-ZА-Я0-9._/\- ]{1,})\s+(.+?)\s+([0-9\s]+[\.,][0-9]{2})\s+([0-9\s]+[\.,][0-9]{2})\s+([0-9\s]+[\.,][0-9]{2})$)"), QRegularExpression::CaseInsensitiveOption);

    for (const QString& line : parsingLines) {
        const QString normalized = normalizedImportHeader(line);
        if (!inTable) {
            if (normalized.contains(QStringLiteral("активнкодтовару")) &&
                normalized.contains(QStringLiteral("найменування")) &&
                (normalized.contains(QStringLiteral("цінабезпдв")) || normalized.contains(QStringLiteral("разомбезпдв")))) {
                inTable = true;
            }
            continue;
        }
        if (isPdfInvoiceLikelyStopLine(normalized)) {
            break;
        }
        if (isElitePdfTableHeaderSignal(normalized)) {
            continue;
        }

        QString oem, alt, name, qty, price, amount;
        if (tryParseEliteInvoiceCompactMergedRow(line, &oem, &name, &qty, &price, &amount)) {
            appendPdfInvoiceRow(result, QString::fromUtf8("Еліт"), oem, name, qty, price, amount);
            ++parsedCount;
            continue;
        }

        QRegularExpressionMatch m = itemRx.match(line);
        if (m.hasMatch()) {
            oem = cleanPdfInvoiceCode(m.captured(1));
            alt = cleanPdfInvoiceCode(m.captured(2));
            name = m.captured(3).simplified();
            price = m.captured(5).trimmed();
            qty = m.captured(6).trimmed();
            amount = m.captured(7).trimmed();
        } else {
            m = itemRxNoUnit.match(line);
            if (!m.hasMatch()) continue;
            oem = cleanPdfInvoiceCode(m.captured(1));
            alt = cleanPdfInvoiceCode(m.captured(2));
            name = m.captured(3).simplified();
            price = m.captured(4).trimmed();
            qty = m.captured(5).trimmed();
            amount = m.captured(6).trimmed();
        }

        if (oem.isEmpty()) oem = alt;
        if (oem.isEmpty() || name.isEmpty()) continue;
        appendPdfInvoiceRow(result, QString::fromUtf8("Еліт"), oem, name, qty, price, amount);
        ++parsedCount;
    }

    if (parsedCount <= 0) {
        result.rows.clear();
        ensurePdfInvoiceResultHeader(result);
        bool parsed = tryParseEliteInvoiceTableByBlocks(parsingLines, result);
        if (!parsed) {
            QString oem;
            QString name;
            QString qty;
            QString price;
            QString amount;
            if (tryParseEliteInvoiceHeaderWindow(parsingLines, &oem, &name, &qty, &price, &amount)) {
                appendPdfInvoiceRow(result, QString::fromUtf8("Еліт"), oem, name, qty, price, amount);
                parsed = true;
            }
            const QStringList fallbackLines = elitePdfFallbackCandidateLines(parsingLines);
            if (!parsed) {
                for (const QString& rawLine : fallbackLines) {
                    const QString line = normalizeElitePdfAnchoredLine(rawLine);
                    if (tryParseEliteInvoiceCompactMergedRow(line, &oem, &name, &qty, &price, &amount) ||
                        tryParseEliteInvoiceTrailingCodeLine(line, &oem, &name, &qty, &price, &amount) ||
                        tryParseEliteInvoiceFlattenedLine(line, &oem, &name, &qty, &price, &amount) ||
                        tryParseEliteInvoiceGenericPartsRow(line, &oem, &name, &qty, &price, &amount)) {
                        appendPdfInvoiceRow(result, QString::fromUtf8("Еліт"), oem, name, qty, price, amount);
                        parsed = true;
                        break;
                    }
                }
            }
            if (!parsed) {
                for (const QString& rawLine : fallbackLines) {
                    const QString line = normalizeElitePdfAnchoredLine(rawLine);
                    if (tryParseEliteInvoiceCompactMergedRow(line, &oem, &name, &qty, &price, &amount) ||
                        tryParseEliteInvoiceGenericPartsRow(line, &oem, &name, &qty, &price, &amount)) {
                        appendPdfInvoiceRow(result, QString::fromUtf8("Еліт"), oem, name, qty, price, amount);
                        parsed = true;
                    }
                }
            }
        }
        if (!parsed) {
            if (errorText) *errorText = QString::fromUtf8("Не вдалося знайти таблицю або розпізнати рядки товарів у PDF накладної Еліт.");
            result.rows.clear();
            return result;
        }
    }

    return result;
}

ExcelReadResult parsePdfInvoiceText(const QString& text, QString* errorText)
{
    return GenericPdfParser::parseText(text, errorText);
}

QString ParserBridge::normalizePdfInvoiceDate(const QString& text)
{
    return normalizePdfInvoiceDateToIso(text);
}

ExcelReadResult ParserBridge::readPdfInvoiceRowsInternal(const QString& filePath, QString* errorText)
{
    ExcelReadResult result;
    QString nativePdfError;
    const QString nativeText = extractPdfTextViaQtPdf(filePath, &nativePdfError);
    appendInvoiceImportLog(QStringLiteral("readPdfInvoiceRowsViaPowerShell | nativeClass=%1 | nativeChars=%2 | nativeError=%3")
                               .arg(classifyPdfTextQuality(nativeText))
                               .arg(nativeText.size())
                               .arg(nativePdfError));
    if (!nativeText.trimmed().isEmpty()) {
        ExcelReadResult nativeResult = parsePdfInvoiceText(nativeText, errorText);
        if (!nativeResult.rows.isEmpty() || nativeResult.unsupportedDocument) return nativeResult;
        appendInvoiceImportLog(QStringLiteral("native QtPdf text parsed with 0 rows | err=%1").arg(errorText ? *errorText : QString()));
    }
#if defined(Q_OS_WIN)
    QString localRuntimeEngine;
    QString localRuntimeError;
    const QString localRuntimeText = extractPdfTextViaLocalRuntimeTool(filePath, QCoreApplication::applicationDirPath(), &localRuntimeEngine, &localRuntimeError);
    appendInvoiceImportLog(QStringLiteral("local runtime text summary | engine=%1 | class=%2 | chars=%3 | err=%4")
                               .arg(localRuntimeEngine, classifyPdfTextQuality(localRuntimeText))
                               .arg(localRuntimeText.size())
                               .arg(localRuntimeError));
    if (!localRuntimeText.trimmed().isEmpty()) {
        ExcelReadResult localRuntimeResult = parsePdfInvoiceText(localRuntimeText, errorText);
        if (!localRuntimeResult.rows.isEmpty() || localRuntimeResult.unsupportedDocument) return localRuntimeResult;
        appendInvoiceImportLog(QStringLiteral("local runtime text parsed with 0 rows | engine=%1 | err=%2")
                                   .arg(localRuntimeEngine, errorText ? *errorText : QString()));
    }

    QString directPythonEngine;
    QString directPythonError;
    const QString directPythonText = extractPdfTextViaDirectPython(filePath, QCoreApplication::applicationDirPath(), &directPythonEngine, &directPythonError);
    appendInvoiceImportLog(QStringLiteral("direct python summary | engine=%1 | class=%2 | chars=%3 | err=%4")
                               .arg(directPythonEngine, classifyPdfTextQuality(directPythonText))
                               .arg(directPythonText.size())
                               .arg(directPythonError));
    if (!directPythonText.trimmed().isEmpty()) {
        ExcelReadResult directPythonResult = parsePdfInvoiceText(directPythonText, errorText);
        if (!directPythonResult.rows.isEmpty() || directPythonResult.unsupportedDocument) return directPythonResult;
        appendInvoiceImportLog(QStringLiteral("direct python text parsed with 0 rows | engine=%1 | err=%2")
                                   .arg(directPythonEngine, errorText ? *errorText : QString()));
    }

#if defined(CS_HAS_QT_PDF) && CS_HAS_QT_PDF
    QTemporaryDir localOcrTempDir;
    QString localRenderedImageError;
    const QString localRenderedImageDir = localOcrTempDir.isValid()
        ? renderPdfPagesViaQtPdfForOcr(filePath, localOcrTempDir.filePath(QStringLiteral("pdf_ocr_pages")), &localRenderedImageError)
        : QString();
    appendInvoiceImportLog(QStringLiteral("local qt render ocr summary | dir=%1 | err=%2")
                               .arg(localRenderedImageDir, localRenderedImageError));
    QString localOcrEngine;
    QString localOcrError;
    const QString localOcrText = extractPdfTextViaLocalTesseractOcr(localRenderedImageDir, QCoreApplication::applicationDirPath(), &localOcrEngine, &localOcrError);
    appendInvoiceImportLog(QStringLiteral("local tesseract ocr summary | engine=%1 | class=%2 | chars=%3 | err=%4")
                               .arg(localOcrEngine, classifyPdfTextQuality(localOcrText))
                               .arg(localOcrText.size())
                               .arg(localOcrError));
    if (!localOcrText.trimmed().isEmpty()) {
        ExcelReadResult localOcrResult = parsePdfInvoiceText(localOcrText, errorText);
        if (!localOcrResult.rows.isEmpty() || localOcrResult.unsupportedDocument) return localOcrResult;
        appendInvoiceImportLog(QStringLiteral("local tesseract OCR text parsed with 0 rows | engine=%1 | err=%2")
                                   .arg(localOcrEngine, errorText ? *errorText : QString()));
    }
#endif

    QTemporaryDir localToolRenderTempDir;
    QString localToolRenderEngine;
    QString localToolRenderError;
    const QString localToolImageDir = localToolRenderTempDir.isValid()
        ? renderPdfPagesViaLocalOcrTools(filePath, localToolRenderTempDir.filePath(QStringLiteral("pdf_tool_ocr_pages")), QCoreApplication::applicationDirPath(), &localToolRenderEngine, &localToolRenderError)
        : QString();
    appendInvoiceImportLog(QStringLiteral("local tool render ocr summary | engine=%1 | dir=%2 | err=%3")
                               .arg(localToolRenderEngine, localToolImageDir, localToolRenderError));
    QString localToolOcrEngine;
    QString localToolOcrError;
    const QString localToolOcrText = extractPdfTextViaLocalTesseractOcr(localToolImageDir, QCoreApplication::applicationDirPath(), &localToolOcrEngine, &localToolOcrError);
    appendInvoiceImportLog(QStringLiteral("local tool+tesseract OCR summary | render=%1 | ocr=%2 | class=%3 | chars=%4 | err=%5")
                               .arg(localToolRenderEngine, localToolOcrEngine, classifyPdfTextQuality(localToolOcrText))
                               .arg(localToolOcrText.size())
                               .arg(localToolOcrError));
    if (!localToolOcrText.trimmed().isEmpty()) {
        ExcelReadResult localToolOcrResult = parsePdfInvoiceText(localToolOcrText, errorText);
        if (!localToolOcrResult.rows.isEmpty() || localToolOcrResult.unsupportedDocument) return localToolOcrResult;
        appendInvoiceImportLog(QStringLiteral("local tool+tesseract OCR text parsed with 0 rows | render=%1 | ocr=%2 | err=%3")
                                   .arg(localToolRenderEngine, localToolOcrEngine, errorText ? *errorText : QString()));
    }

    if (rejectBlockingImportProcessOnUiThread(QStringLiteral("pdf_powershell_fallback"), errorText)) {
        return result;
    }

    const QString powerShell = QStandardPaths::findExecutable(QStringLiteral("powershell.exe"));
    if (powerShell.trimmed().isEmpty()) {
        if (errorText) *errorText = QString::fromUtf8("Не знайдено powershell.exe для читання PDF-файлу.");
        return result;
    }

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        if (errorText) *errorText = QString::fromUtf8("Не вдалося створити тимчасову папку для імпорту PDF.");
        return result;
    }

    const QString scriptPath = tempDir.filePath(QStringLiteral("import_pdf_invoice.ps1"));
    QSaveFile scriptFile(scriptPath);
    if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorText) *errorText = QString::fromUtf8("Не вдалося створити тимчасовий PowerShell-скрипт для PDF.");
        return result;
    }
    QByteArray scriptBytes = pdfTextExtractPowerShellScript().toUtf8();
    scriptBytes.prepend(QByteArray::fromHex("EFBBBF"));
    if (scriptFile.write(scriptBytes) != scriptBytes.size() || !scriptFile.commit()) {
        if (errorText) *errorText = QString::fromUtf8("Не вдалося записати PowerShell-скрипт для імпорту PDF.");
        return result;
    }

    QString renderedImageDir;
    QString renderedImageError;
#if defined(CS_HAS_QT_PDF) && CS_HAS_QT_PDF
    renderedImageDir = renderPdfPagesViaQtPdfForOcr(filePath, tempDir.filePath(QStringLiteral("pdf_ocr_pages")), &renderedImageError);
#endif
    appendInvoiceImportLog(QStringLiteral("qt pdf render ocr summary | dir=%1 | err=%2")
                               .arg(renderedImageDir, renderedImageError));

    QProcess process;
    process.setProgram(powerShell);
    process.setArguments({
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), scriptPath,
        QStringLiteral("-Path"), filePath,
        QStringLiteral("-AppDir"), QCoreApplication::applicationDirPath(),
        QStringLiteral("-RenderedImageDir"), renderedImageDir
    });
    const IncrementalImportProcessResult processResult =
        runImportProcessIncrementally(process, 10000, 180000, QStringLiteral("pdf_powershell_fallback"));
    if (!processResult.started) {
        if (errorText) *errorText = processResult.errorString.trimmed().isEmpty()
            ? QString::fromUtf8("Резервний імпорт PDF не зміг запустити PowerShell.")
            : processResult.errorString;
        return result;
    }
    if (processResult.timedOut) {
        if (errorText) *errorText = QString::fromUtf8("Резервний імпорт PDF не завершився вчасно (180 с).");
        return result;
    }

    const QByteArray stdOut = processResult.stdOut;
    const QByteArray stdErr = processResult.stdErr;
    appendInvoiceImportLog(QStringLiteral("powershell finished | exitStatus=%1 | exitCode=%2 | stdout=%3 | stderr=%4")
                               .arg(int(processResult.exitStatus))
                               .arg(processResult.exitCode)
                               .arg(importProcessLogPreview(stdOut))
                               .arg(importProcessLogPreview(stdErr)));
    if (processResult.exitStatus != QProcess::NormalExit || processResult.exitCode != 0) {
        if (errorText) {
            QString details = QString::fromUtf8(stdErr).trimmed();
            const int atIndex = details.indexOf(QStringLiteral("At "));
            if (atIndex > 0) details = details.left(atIndex).trimmed();
            details.replace(QRegularExpression(QStringLiteral(R"([
]+)")), QStringLiteral(" "));
            *errorText = details.isEmpty()
                ? QString::fromUtf8("Резервний рушій повернув помилку під час читання PDF-файлу.")
                : details;
        }
        return result;
    }

    QJsonParseError jsonError;
    const QJsonDocument doc = QJsonDocument::fromJson(stdOut, &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorText) *errorText = QString::fromUtf8("Не вдалося розібрати відповідь PowerShell при читанні PDF.");
        return result;
    }

    const QJsonObject payload = doc.object();
    const QString text = payload.value(QStringLiteral("text")).toString();
    const QString engine = payload.value(QStringLiteral("engine")).toString().trimmed();
    appendInvoiceImportLog(QStringLiteral("powershell payload | engine=%1 | class=%2 | chars=%3")
                               .arg(engine, classifyPdfTextQuality(text))
                               .arg(text.size()));
    if (!text.trimmed().isEmpty()) {
        ExcelReadResult parsed = parsePdfInvoiceText(text, errorText);
        if (!parsed.rows.isEmpty()) {
            if (errorText) errorText->clear();
            return parsed;
        }
        if (errorText && !engine.isEmpty() && !errorText->trimmed().isEmpty()) {
            *errorText = QString::fromUtf8("PDF прочитано рушієм %1, але таблицю товарів не вдалося розпізнати. %2")
                             .arg(engine, errorText->trimmed());
        }
        return parsed;
    }

    if (errorText) {
        *errorText = engine.isEmpty()
            ? QString::fromUtf8("Резервний імпорт PDF не повернув текст.")
            : QString::fromUtf8("Резервний рушій %1 не повернув текст PDF.").arg(engine);
    }
#else
    Q_UNUSED(filePath);
    if (errorText) *errorText = QString::fromUtf8("Імпорт PDF підтримується у Windows-збірці застосунку.");
#endif
    return result;
}

static QString excelImportPowerShellScript()
{
    QString script;
    script.reserve(20000);
    script += QString::fromUtf8(R"EXCELPS1(
param([Parameter(Mandatory=$true)][string]$Path)
Set-StrictMode -Version 2
$ErrorActionPreference = 'Stop'
try {
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [Console]::InputEncoding = $utf8NoBom
    [Console]::OutputEncoding = $utf8NoBom
    $OutputEncoding = $utf8NoBom
} catch { }

function Get-SafeCount($Value) {
    if ($null -eq $Value) { return 0 }
    if ($Value -is [string]) { return 1 }
    if ($Value -is [System.Array]) { return [int]$Value.Length }
    if ($Value -is [System.Collections.ICollection]) { return [int]$Value.Count }
    return @($Value).Count
}

function Get-CellColumnIndex([string]$Ref) {
    if ([string]::IsNullOrWhiteSpace($Ref)) { return 0 }
    $letters = ($Ref -replace '[^A-Za-z]', '').ToUpperInvariant()
    $index = 0
    foreach ($ch in $letters.ToCharArray()) {
        $index = ($index * 26) + ([int][char]$ch - [int][char]'A' + 1)
    }
    return [Math]::Max(0, $index - 1)
}

function Read-ZipEntryText($Zip, [string]$EntryName) {
    if ($null -eq $Zip) { return $null }
    $entry = $Zip.GetEntry($EntryName)
    if (-not $entry) { return $null }
    $reader = New-Object System.IO.StreamReader($entry.Open())
    try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
}

function Test-IsBuiltinDateFormatId([int]$NumFmtId) {
    return ($NumFmtId -ge 14 -and $NumFmtId -le 22) -or
           ($NumFmtId -ge 45 -and $NumFmtId -le 47)
}

function Test-IsDateFormatCode([string]$FormatCode) {
    if ([string]::IsNullOrWhiteSpace($FormatCode)) { return $false }
    $normalized = $FormatCode.ToLowerInvariant()
    return ($normalized.Contains('dd') -and $normalized.Contains('mm') -and $normalized.Contains('yy')) -or
           ($normalized.Contains('yyyy') -and $normalized.Contains('mm'))
}

function Get-XmlAttributeValue($Node, [string]$AttributeName) {
    if (-not $Node -or -not $Node.Attributes) { return $null }
    $attr = $Node.Attributes.GetNamedItem($AttributeName)
    if ($attr) { return [string]$attr.Value }
    return $null
}

function Get-XmlChildNodeValue($Node, [string]$ChildName) {
    if (-not $Node) { return $null }
    foreach ($child in @($Node.ChildNodes)) {
        if (-not $child) { continue }
        if ([string]$child.LocalName -eq $ChildName -or [string]$child.Name -eq $ChildName) {
            return [string]$child.InnerText
        }
    }
    return $null
}

function Get-XmlDescendantNodeValues($Node, [string]$ChildName) {
    $result = New-Object System.Collections.ArrayList
    if (-not $Node) { return @($result) }
    foreach ($child in @($Node.SelectNodes('.//*[local-name()="' + $ChildName + '"]'))) {
        if (-not $child) { continue }
        [void]$result.Add([string]$child.InnerText)
    }
    return @($result)
}

function Get-XmlChildElements($Node, [string]$ChildName) {
    if (-not $Node) { return @() }
    $items = New-Object System.Collections.ArrayList
    foreach ($child in @($Node.ChildNodes)) {
        if (-not $child) { continue }
        if ([string]$child.LocalName -eq $ChildName -or [string]$child.Name -eq $ChildName) {
            [void]$items.Add($child)
        }
    }
    return @($items)
}
)EXCELPS1");
    script += QString::fromUtf8(R"EXCELPS2(
function Get-DateStyleIndexesFromStylesXml([string]$StylesXmlText) {
    $dateStyleIndexes = New-Object System.Collections.Generic.HashSet[int]
    if ([string]::IsNullOrWhiteSpace($StylesXmlText)) { return $dateStyleIndexes }

    try {
        [xml]$stylesXml = $StylesXmlText
        $customDateFmtIds = New-Object System.Collections.Generic.HashSet[int]
        $styleRoot = $stylesXml.SelectSingleNode('/*[local-name()="styleSheet"]')
        if (-not $styleRoot) { return $dateStyleIndexes }

        foreach ($numFmtsNode in @(Get-XmlChildElements $styleRoot 'numFmts')) {
            foreach ($numFmt in @(Get-XmlChildElements $numFmtsNode 'numFmt')) {
                $numFmtIdText = Get-XmlAttributeValue $numFmt 'numFmtId'
                $formatCode = Get-XmlAttributeValue $numFmt 'formatCode'
                $numFmtId = -1
                if (-not [string]::IsNullOrWhiteSpace($numFmtIdText) -and [int]::TryParse($numFmtIdText, [ref]$numFmtId)) {
                    if (Test-IsDateFormatCode $formatCode) {
                        [void]$customDateFmtIds.Add($numFmtId)
                    }
                }
            }
        }

        $xfIndex = 0
        foreach ($cellXfsNode in @(Get-XmlChildElements $styleRoot 'cellXfs')) {
            foreach ($xf in @(Get-XmlChildElements $cellXfsNode 'xf')) {
                $numFmtId = -1
                $numFmtIdText = Get-XmlAttributeValue $xf 'numFmtId'
                if (-not [string]::IsNullOrWhiteSpace($numFmtIdText)) {
                    [void][int]::TryParse($numFmtIdText, [ref]$numFmtId)
                }
                if ($numFmtId -ge 0 -and ((Test-IsBuiltinDateFormatId $numFmtId) -or $customDateFmtIds.Contains($numFmtId))) {
                    [void]$dateStyleIndexes.Add($xfIndex)
                }
                ++$xfIndex
            }
        }
    } catch {
        return $dateStyleIndexes
    }
    return $dateStyleIndexes
}

function Convert-ExcelSerialToDateText([string]$RawValue) {
    $number = 0.0
    if ([double]::TryParse($RawValue, [System.Globalization.NumberStyles]::Any, [System.Globalization.CultureInfo]::InvariantCulture, [ref]$number)) {
        try {
            return ([datetime]::FromOADate($number)).ToString('dd.MM.yyyy', [System.Globalization.CultureInfo]::InvariantCulture)
        } catch { }
    }
    return $RawValue
}

function Read-XlsxRows([string]$FilePath) {
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = $null
    try {
        $zip = [System.IO.Compression.ZipFile]::OpenRead($FilePath)
        $sharedStrings = @()
        $sharedXmlText = Read-ZipEntryText $zip 'xl/sharedStrings.xml'
        if ($sharedXmlText) {
            [xml]$sharedXml = $sharedXmlText
            $sharedRoot = $sharedXml.SelectSingleNode('/*[local-name()="sst"]')
            foreach ($si in @(Get-XmlChildElements $sharedRoot 'si')) {
                $txt = ''
                $directText = Get-XmlChildNodeValue $si 't'
                if (-not [string]::IsNullOrWhiteSpace($directText)) {
                    $txt = [string]$directText
                } else {
                    foreach ($runText in @(Get-XmlDescendantNodeValues $si 't')) { $txt += [string]$runText }
                }
                $sharedStrings += $txt
            }
        }

        $stylesXmlText = Read-ZipEntryText $zip 'xl/styles.xml'
        $dateStyleIndexes = Get-DateStyleIndexesFromStylesXml $stylesXmlText
        $highlightedColumns = New-Object System.Collections.Generic.HashSet[int]

        $sheetEntry = $zip.Entries |
            Where-Object { $_.FullName -match '^xl/worksheets/[^/]+\.xml$' } |
            Sort-Object FullName |
            Select-Object -First 1
        if (-not $sheetEntry) { throw 'У файлі Excel не знайдено жодного аркуша.' }

        $sheetReader = New-Object System.IO.StreamReader($sheetEntry.Open())
)EXCELPS2");
    script += QString::fromUtf8(R"EXCELPS3(
        try { [xml]$sheetXml = $sheetReader.ReadToEnd() } finally { $sheetReader.Dispose() }

        $worksheetNode = $sheetXml.SelectSingleNode('/*[local-name()="worksheet"]')
        if (-not $worksheetNode) { throw 'У файлі Excel не знайдено вузол worksheet.' }
        $sheetDataNode = $worksheetNode.SelectSingleNode('./*[local-name()="sheetData"]')
        if (-not $sheetDataNode) {
            return [ordered]@{ rows = @(); highlightedColumns = @() }
        }

        $resultRows = New-Object System.Collections.ArrayList
        foreach ($rowNode in @(Get-XmlChildElements $sheetDataNode 'row')) {
            $maxIndex = -1
            $map = @{}
            foreach ($cell in @(Get-XmlChildElements $rowNode 'c')) {
                $colIndex = Get-CellColumnIndex([string](Get-XmlAttributeValue $cell 'r'))
                if ($colIndex -gt $maxIndex) { $maxIndex = $colIndex }
                $value = ''
                $type = [string](Get-XmlAttributeValue $cell 't')

                $styleIndex = -1
                $styleText = [string](Get-XmlAttributeValue $cell 's')
                if (-not [string]::IsNullOrWhiteSpace($styleText)) {
                    [void][int]::TryParse($styleText, [ref]$styleIndex)
                }

                if ($type -eq 's') {
                    $sharedIndex = 0
                    $sharedValue = Get-XmlChildNodeValue $cell 'v'
                    [void][int]::TryParse([string]$sharedValue, [ref]$sharedIndex)
                    if ($sharedIndex -ge 0 -and $sharedIndex -lt (Get-SafeCount $sharedStrings)) {
                        $value = [string]$sharedStrings[$sharedIndex]
                    }
                } elseif ($type -eq 'inlineStr') {
                    $inlineText = Get-XmlChildNodeValue $cell 't'
                    if (-not [string]::IsNullOrWhiteSpace($inlineText)) {
                        $value = [string]$inlineText
                    } else {
                        foreach ($runText in @(Get-XmlDescendantNodeValues $cell 't')) { $value += [string]$runText }
                    }
                } else {
                    $valueNode = Get-XmlChildNodeValue $cell 'v'
                    if ($null -eq $valueNode) { $valueNode = '' }
                    $value = [string]$valueNode
                    if ($styleIndex -ge 0 -and $dateStyleIndexes.Contains($styleIndex)) {
                        $value = Convert-ExcelSerialToDateText $value
                    }
                }

                $map[$colIndex] = $value
            }

            if ($maxIndex -lt 0) { continue }
            $line = New-Object object[] ($maxIndex + 1)
            for ($i = 0; $i -le $maxIndex; ++$i) {
                if ($map.ContainsKey($i)) { $line[$i] = [string]$map[$i] } else { $line[$i] = '' }
            }
            [void]$resultRows.Add($line)
        }

        return [ordered]@{
            rows = $resultRows
            highlightedColumns = @($highlightedColumns | Sort-Object)
        }
    } finally {
        if ($zip) { $zip.Dispose() }
    }
}

function Test-IsExcelComRetryable($ErrorRecord) {
    if ($null -eq $ErrorRecord) { return $false }
    $message = ''
    try { $message = [string]$ErrorRecord.Exception.Message } catch { }
    if ([string]::IsNullOrWhiteSpace($message)) {
        try { $message = [string]$ErrorRecord } catch { }
    }
    return $message -match 'RPC_E_CALL_REJECTED|Call was rejected by callee'
}

function Invoke-ExcelComRetry([scriptblock]$Action, [int]$Attempts = 8, [int]$DelayMs = 250) {
    $lastError = $null
    for ($attempt = 1; $attempt -le $Attempts; ++$attempt) {
        try {
            return & $Action
        } catch {
            $lastError = $_
            if (-not (Test-IsExcelComRetryable $_) -or $attempt -ge $Attempts) {
                throw
            }
            Start-Sleep -Milliseconds ($DelayMs * $attempt)
        }
    }
    if ($lastError) { throw $lastError }
    throw 'Не вдалося виконати Excel COM-операцію.'
}
)EXCELPS3");
    script += QString::fromUtf8(R"EXCELPS4(
function Test-IsComObject($Value) {
    if ($null -eq $Value) { return $false }
    try {
        return [System.Runtime.InteropServices.Marshal]::IsComObject($Value)
    } catch {
        return $false
    }
}

function Release-ComObjectSafe($Value) {
    if (-not (Test-IsComObject $Value)) { return }
    try {
        [void][System.Runtime.InteropServices.Marshal]::FinalReleaseComObject($Value)
    } catch {
    }
}

function Read-FilePrefixBytes([string]$FilePath, [int]$Count = 4096) {
    $fs = [System.IO.File]::Open($FilePath, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
    try {
        $buffer = New-Object byte[] $Count
        $read = $fs.Read($buffer, 0, $Count)
        if ($read -le 0) { return New-Object byte[] 0 }
        if ($read -eq $Count) { return $buffer }
        $trimmed = New-Object byte[] $read
        [System.Array]::Copy($buffer, $trimmed, $read)
        return $trimmed
    } finally {
        $fs.Dispose()
    }
}

function Test-BytePrefix([byte[]]$Bytes, [byte[]]$Prefix) {
    if ($null -eq $Bytes -or $null -eq $Prefix) { return $false }
    if ($Bytes.Length -lt $Prefix.Length) { return $false }
    for ($i = 0; $i -lt $Prefix.Length; ++$i) {
        if ($Bytes[$i] -ne $Prefix[$i]) { return $false }
    }
    return $true
}

function Get-DetectedExcelMode([string]$FilePath, [string]$Extension) {
    switch ($Extension) {
        '.xlsx' { return 'xlsx' }
        '.xlsm' { return 'xlsx' }
        '.xltx' { return 'xlsx' }
        '.xltm' { return 'xlsx' }
        '.csv'  { return 'csv' }
        '.txt'  { return 'csv' }
    }

    $prefix = Read-FilePrefixBytes $FilePath 4096
    if (Test-BytePrefix $prefix ([byte[]](0x50,0x4B,0x03,0x04))) { return 'xlsx' }
    if (Test-BytePrefix $prefix ([byte[]](0xD0,0xCF,0x11,0xE0))) { return 'xls' }

    try {
        $sample = [System.Text.Encoding]::UTF8.GetString($prefix)
    } catch {
        $sample = ''
    }

    if (-not [string]::IsNullOrWhiteSpace($sample)) {
        $normalized = $sample.ToLowerInvariant()
        if (($normalized.Contains(';') -or $normalized.Contains("`t") -or $normalized.Contains(',')) -and
            ($normalized.Contains("`r") -or $normalized.Contains("`n") -or $normalized.StartsWith('sep='))) {
            return 'csv'
        }
    }

    if ($Extension -eq '.xls') { return 'xls' }
    return 'csv'
}

function Read-LegacyExcelRows([string]$FilePath) {
    if ([string]::IsNullOrWhiteSpace($FilePath) -or -not (Test-Path $FilePath)) {
        throw 'Не знайдено .xls файл для імпорту.'
    }

    throw 'Binary .xls імпортується прямим парсером застосунку без Microsoft Excel. PowerShell COM-шлях вимкнено.'
}
)EXCELPS4");
