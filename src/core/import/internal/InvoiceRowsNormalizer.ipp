// Extracted from src/core/import/InvoiceImportEngine.cpp for P1/P2 monolith reduction.
// Included exactly once by the original translation unit; generated split boundary is outside C++ raw strings.

    if (price) price->clear();
    if (amount) amount->clear();

    const QString line = rawLine;
    const QString normalized = normalizedImportHeader(line);
    if (line.trimmed().isEmpty() || isPdfInvoiceLikelyStopLine(normalized) || isElitePdfTableHeaderSignal(normalized)) {
        return false;
    }

    static const QRegularExpression rx(
        QStringLiteral(R"(^\s*([A-Z0-9][A-Z0-9./\-]*(?:\s+[A-Z0-9][A-Z0-9./\-]*){0,3})\s{2,}(.+?)\s{2,}(\d{8,12})\s{2,}[XХ]\s{2,}([\p{L}.\-]+)\s{2,}(\d[\d\s]*[\.,]\d{2})\s{2,}(\d[\d\s]*[\.,]\d{2})\s{2,}(\d[\d\s]*(?:[\.,]\d{1,3})?)\s{2,}(\d[\d\s]*[\.,]\d{2})\s*$)"),
        QRegularExpression::CaseInsensitiveOption);

    const QRegularExpressionMatch match = rx.match(line);
    if (!match.hasMatch()) return false;

    const QString parsedOem = cleanPdfInvoiceCode(match.captured(1));
    const QString parsedName = match.captured(2).simplified();
    const QString parsedPriceWithoutVat = match.captured(5).trimmed();
    const QString parsedPrice = match.captured(6).trimmed();
    const QString parsedQty = match.captured(7).trimmed();
    const QString parsedAmountWithoutVat = match.captured(8).trimmed();

    if (!isElitePdfLikelyItemCode(parsedOem) || !isPdfInvoiceLikelyNameLine(parsedName)) return false;

    bool qtyOk = false;
    bool priceOk = false;
    bool amountOk = false;
    bool priceWithoutVatOk = false;
    const double qtyValue = parseLooseNumberFallback(parsedQty, &qtyOk);
    const double priceWithoutVatValue = parseLooseNumberFallback(parsedPriceWithoutVat, &priceWithoutVatOk);
    const double priceValue = parseLooseNumberFallback(parsedPrice, &priceOk);
    const double amountWithoutVatValue = parseLooseNumberFallback(parsedAmountWithoutVat, &amountOk);
    if (!qtyOk || !priceWithoutVatOk || !priceOk || !amountOk ||
        qtyValue <= 0.0 || priceWithoutVatValue <= 0.0 || priceValue <= 0.0 || amountWithoutVatValue <= 0.0) return false;

    const double diff = qAbs((priceWithoutVatValue * qtyValue) - amountWithoutVatValue);
    const double tolerance = qMax(0.10, amountWithoutVatValue * 0.03);
    if (diff > tolerance) return false;

    if (oem) *oem = parsedOem;
    if (name) *name = parsedName;
    if (qty) *qty = parsedQty;
    if (price) *price = parsedPrice;
    if (amount) *amount = formatElitePdfMoneyText(priceValue * qtyValue);
    return true;
}

static bool tryParseEliteInvoiceWideVatLayoutTable(const QStringList& lines, ExcelReadResult& result)
{
    int headerStart = -1;
    for (int i = 0; i < lines.size(); ++i) {
        const QString normalized = normalizedImportHeader(lines.at(i));
        if (normalized.contains(QStringLiteral("активнкодтовару")) &&
            normalized.contains(QStringLiteral("разомбез")) &&
            normalized.contains(QStringLiteral("найменування"))) {
            headerStart = i;
            break;
        }
    }
    if (headerStart < 0) return false;

    int parsedCount = 0;
    for (int i = headerStart + 1; i < lines.size(); ++i) {
        const QString rawLine = lines.at(i);
        const QString normalized = normalizedImportHeader(rawLine);
        if (normalized.isEmpty()) continue;
        if (isPdfInvoiceLikelyStopLine(normalized)) break;
        if (isElitePdfTableHeaderSignal(normalized)) continue;

        QString parsedOem;
        QString parsedName;
        QString parsedQty;
        QString parsedPrice;
        QString parsedAmount;
        if (!tryParseEliteInvoiceWideVatLayoutRow(rawLine,
                                                  &parsedOem,
                                                  &parsedName,
                                                  &parsedQty,
                                                  &parsedPrice,
                                                  &parsedAmount)) {
            continue;
        }

        appendPdfInvoiceRow(result,
                            QString::fromUtf8("Еліт"),
                            parsedOem,
                            parsedName,
                            parsedQty,
                            parsedPrice,
                            parsedAmount);
        ++parsedCount;
    }

    return parsedCount > 0;
}

static bool tryParseEliteInvoiceLayoutTable(const QStringList& layoutLines, ExcelReadResult& result)
{
    const ElitePdfLayoutColumns cols = detectElitePdfLayoutColumns(layoutLines);
    if (!cols.valid()) return false;

    int parsedCount = 0;
    for (int i = cols.bodyStart; i < layoutLines.size(); ++i) {
        const QString normalized = normalizedImportHeader(layoutLines.at(i));
        if (normalized.isEmpty()) continue;
        if (isPdfInvoiceLikelyStopLine(normalized)) break;
        if (isElitePdfTableHeaderSignal(normalized)) continue;

        QString combined = layoutLines.at(i);
        if (i + 1 < layoutLines.size()) {
            const QString nextNormalized = normalizedImportHeader(layoutLines.at(i + 1));
            if (!nextNormalized.isEmpty() && !isPdfInvoiceLikelyStopLine(nextNormalized) && !isElitePdfTableHeaderSignal(nextNormalized)) {
                combined += QLatin1Char(' ');
                combined += layoutLines.at(i + 1).trimmed();
            }
        }

        QString oem = elitePdfLayoutSlice(combined, cols.oem, cols.alt > cols.oem ? cols.alt : cols.name);
        QString alt = elitePdfLayoutSlice(combined, cols.alt, cols.name);
        QString name = elitePdfLayoutSlice(combined, cols.name, cols.unit > cols.name ? cols.unit : cols.price);
        const QString priceText = elitePdfLayoutSlice(combined, cols.price, cols.qty);
        const QString qtyText = elitePdfLayoutSlice(combined, cols.qty, cols.amount > cols.qty ? cols.amount : -1);
        const QString amountText = cols.amount >= 0 ? elitePdfLayoutSlice(combined, cols.amount, -1) : QString();

        oem = cleanPdfInvoiceCode(oem);
        alt = cleanPdfInvoiceCode(alt);
        if (oem.isEmpty()) oem = alt;
        if (oem.isEmpty() || !isPdfInvoiceLikelyCode(oem)) continue;
        if (name.isEmpty()) {
            name = combined.mid(qMin(combined.size(), cols.name)).simplified();
        }
        name = stripLeadingPdfInvoiceUnit(name);
        const QString price = firstPdfInvoiceNumberFromText(priceText);
        const QString qty = firstPdfInvoiceNumberFromText(qtyText);
        QString amount = lastPdfInvoiceNumberFromText(amountText);
        if (amount.isEmpty()) amount = lastPdfInvoiceNumberFromText(combined.mid(qMin(combined.size(), cols.qty)));
        if (amount.isEmpty()) amount = price;

        bool qtyOk = false;
        bool priceOk = false;
        bool amountOk = false;
        const double qtyValue = parseLooseNumberFallback(qty, &qtyOk);
        const double priceValue = parseLooseNumberFallback(price, &priceOk);
        const double amountValue = parseLooseNumberFallback(amount, &amountOk);
        if (!qtyOk || !priceOk || !amountOk || qtyValue <= 0.0 || priceValue <= 0.0 || amountValue <= 0.0) continue;
        if (name.isEmpty() || !isPdfInvoiceLikelyNameLine(name)) continue;

        const QString amountWithVat = elitePdfAmountWithVatText(price, qty, amount);
        appendPdfInvoiceRow(result, QString::fromUtf8("Еліт"), oem, name, qty, price, amountWithVat);
        ++parsedCount;
    }
    return parsedCount > 0;
}

static bool isPdfInvoiceLikelyStopLine(const QString& normalized)
{
    return normalized.startsWith(QStringLiteral("разомбезпдв")) ||
           normalized.startsWith(QStringLiteral("всьогонасуму")) ||
           normalized.startsWith(QStringLiteral("пдв")) ||
           normalized.contains(QStringLiteral("всьогодосплати")) ||
           normalized.contains(QStringLiteral("всьогодооплати")) ||
           normalized.contains(QStringLiteral("відпустив")) ||
           normalized.contains(QStringLiteral("отпустил")) ||
           normalized.contains(QStringLiteral("отримав")) ||
           normalized.contains(QStringLiteral("получил")) ||
           normalized.contains(QStringLiteral("своїмпідписом")) ||
           normalized.contains(QStringLiteral("своейподписью")) ||
           normalized.contains(QStringLiteral("директор")) ||
           normalized.contains(QStringLiteral("голбухгалтер"));
}

static bool isPdfInvoiceLikelyCode(const QString& value)
{
    const QString t = cleanPdfInvoiceCode(value);
    if (t.size() < 3 || t.size() > 40) return false;
    if (t.contains(QStringLiteral(" ")) && !t.contains(QRegularExpression(QStringLiteral(R"(\d)")))) return false;
    if (!t.contains(QRegularExpression(QStringLiteral(R"([A-Za-zА-Яа-яІіЇїЄє0-9])")))) return false;
    if (t.contains(QStringLiteral("рахунок"), Qt::CaseInsensitive) || t.contains(QStringLiteral("наклад"), Qt::CaseInsensitive)) return false;
    return t.contains(QRegularExpression(QStringLiteral(R"([A-ZА-Я0-9])")));
}

static bool isPdfInvoiceLikelyNameLine(const QString& value)
{
    const QString t = value.simplified();
    if (t.size() < 2) return false;
    if (!t.contains(QRegularExpression(QStringLiteral(R"([\p{Ll}])")))) return false;
    return !normalizedImportHeader(t).contains(QStringLiteral("одвим"));
}

static QString stripLeadingPdfInvoiceUnit(QString text)
{
    text = text.trimmed();
    const QStringList prefixes = {
        QString::fromUtf8("шт"), QStringLiteral("шт."), QString::fromUtf8("штука"),
        QString::fromUtf8("од"), QStringLiteral("од."), QString::fromUtf8("компл"),
        QStringLiteral("компл."), QString::fromUtf8("к-т"), QString::fromUtf8("л"),
        QString::fromUtf8("кг"), QString::fromUtf8("г"), QString::fromUtf8("м"),
        QString::fromUtf8("см"), QString::fromUtf8("уп"), QStringLiteral("уп."),
        QString::fromUtf8("упак"), QString::fromUtf8("пара"), QString::fromUtf8("пар")
    };
    bool changed = true;
    while (changed && !text.isEmpty()) {
        changed = false;
        const QString lowered = text.toLower();
        for (const QString& prefix : prefixes) {
            if (!lowered.startsWith(prefix)) continue;
            if (text.size() == prefix.size()) {
                text.clear();
                changed = true;
                break;
            }
            const QChar next = text.at(prefix.size());
            if (!next.isLetterOrNumber() || next.isLower()) {
                text = text.mid(prefix.size()).trimmed();
                while (!text.isEmpty() && QStringLiteral(" .,:;/-").contains(text.front())) text.remove(0, 1);
                text = text.trimmed();
                changed = true;
                break;
            }
        }
    }
    return text;
}

static bool assignPdfInvoiceNumericTriple(const QStringList& numeric,
                                          QString* priceOut,
                                          QString* qtyOut,
                                          QString* amountOut)
{
    if (priceOut) priceOut->clear();
    if (qtyOut) qtyOut->clear();
    if (amountOut) amountOut->clear();
    if (numeric.size() < 3) return false;

    if (numeric.size() >= 4 && elitePdfLooksLikeVatPricePair(numeric.at(0), numeric.at(1))) {
        bool priceWithoutVatOk = false;
        bool qtyOk = false;
        bool amountWithoutVatOk = false;
        const double priceWithoutVatValue = parseLooseNumberFallback(numeric.at(0), &priceWithoutVatOk);
        const double qtyValue = parseLooseNumberFallback(numeric.at(2), &qtyOk);
        const double amountWithoutVatValue = parseLooseNumberFallback(numeric.at(3), &amountWithoutVatOk);
        if (priceWithoutVatOk && qtyOk && amountWithoutVatOk && qtyValue > 0.0 &&
            qAbs((priceWithoutVatValue * qtyValue) - amountWithoutVatValue) <= qMax(0.10, amountWithoutVatValue * 0.03)) {
            if (priceOut) *priceOut = numeric.at(1).trimmed();
            if (qtyOut) *qtyOut = numeric.at(2).trimmed();
            if (amountOut) *amountOut = elitePdfAmountWithVatText(numeric.at(1), numeric.at(2), numeric.at(3));
            return true;
        }
    }

    struct Candidate { QString price; QString qty; QString amount; double score = -1.0; };
    Candidate best;
    auto consider = [&](const QString& price, const QString& qty, const QString& amount) {
        bool priceOk = false;
        bool qtyOk = false;
        bool amountOk = false;
        const double priceValue = parseLooseNumberFallback(price, &priceOk);
        const double qtyValue = parseLooseNumberFallback(qty, &qtyOk);
        const double amountValue = parseLooseNumberFallback(amount, &amountOk);
        if (!priceOk || !qtyOk || !amountOk) return;
        if (priceValue <= 0.0 || qtyValue <= 0.0 || amountValue <= 0.0) return;
        double score = 0.0;
        if (qtyValue <= 10000.0) score += 2.0;
        if (qtyValue <= 1000.0) score += 1.0;
        if (priceValue <= amountValue + 0.01) score += 1.0;
        const double diff = qAbs((priceValue * qtyValue) - amountValue);
        const double tolerance = qMax(0.05, amountValue * 0.03);
        if (diff <= tolerance) score += 10.0;
        else if (diff <= tolerance * 3.0) score += 4.0;
        else if (amountValue >= priceValue) score += 0.5;
        if (score > best.score) best = {price, qty, amount, score};
    };

    consider(numeric.at(0), numeric.at(1), numeric.at(2));
    consider(numeric.at(0), numeric.at(2), numeric.at(1));
    if (numeric.size() >= 4) {
        consider(numeric.at(1), numeric.at(2), numeric.at(3));
        consider(numeric.at(1), numeric.at(3), numeric.at(2));
    }

    if (best.score < 0.0) {
        best = {numeric.at(0), numeric.at(2), numeric.at(1), -0.5};
    }
    if (priceOut) *priceOut = best.price.trimmed();
    if (qtyOut) *qtyOut = best.qty.trimmed();
    if (amountOut) *amountOut = best.amount.trimmed();
    return true;
}

static bool tryParseEliteInvoiceFlattenedLine(const QString& line,
                                              QString* oem,
                                              QString* name,
                                              QString* qty,
                                              QString* price,
                                              QString* amount)
{
    if (oem) oem->clear();
    if (name) name->clear();
    if (qty) qty->clear();
    if (price) price->clear();
    if (amount) amount->clear();

    QString simplified = line;
    simplified.replace(QChar(0x00A0), QLatin1Char(' '));
    simplified = simplified.simplified();
    if (simplified.isEmpty()) return false;

    const QStringList tokens = simplified.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (tokens.size() < 5) return false;

    int nextIndex = 0;
    const QString primaryCode = extractLeadingPdfInvoiceCode(tokens, 0, &nextIndex);
    if (primaryCode.isEmpty()) return false;
    int currentIndex = nextIndex;
    int altNextIndex = currentIndex;
    QString secondaryCode = extractLeadingPdfInvoiceCode(tokens, currentIndex, &altNextIndex);
    if (!secondaryCode.isEmpty()) currentIndex = altNextIndex;

    QString tail = tokens.mid(currentIndex).join(QLatin1Char(' ')).trimmed();
    if (tail.isEmpty()) return false;

    const QRegularExpression numericRx(QStringLiteral(R"((\d[\d\s]*[\.,]\d{2}))"));
    QList<QRegularExpressionMatch> matches;
    auto it = numericRx.globalMatch(tail);
    while (it.hasNext()) matches.append(it.next());
    if (matches.size() < 3) return false;

    const bool hasExplicitVatPrice = matches.size() >= 4 &&
        elitePdfLooksLikeVatPricePair(matches.at(0).captured(1).trimmed(), matches.at(1).captured(1).trimmed());
    const QString parsedPrice = hasExplicitVatPrice
        ? matches.at(1).captured(1).trimmed()
        : matches.first().captured(1).trimmed();
    const QString parsedAmount = matches.last().captured(1).trimmed();
    const int middleStart = (hasExplicitVatPrice ? matches.at(1) : matches.first()).capturedEnd(1);
    const int middleEnd = matches.last().capturedStart(1);
    QString middle = tail.mid(middleStart, qMax(0, middleEnd - middleStart)).trimmed();
    if (middle.isEmpty()) return false;

    const QRegularExpression leadingQtyRx(QStringLiteral(R"(^([0-9]+(?:[\.,][0-9]+)?))"));
    auto qtyMatch = leadingQtyRx.match(middle);
    if (!qtyMatch.hasMatch()) return false;
    const QString parsedQty = qtyMatch.captured(1).trimmed();
    QString parsedName = middle.mid(qtyMatch.capturedLength(1)).trimmed();
    parsedName = stripLeadingPdfInvoiceUnit(parsedName);
    if (parsedName.isEmpty()) return false;

    bool qtyOk = false;
    bool priceOk = false;
    bool amountOk = false;
    const double qtyValue = parseLooseNumberFallback(parsedQty, &qtyOk);
    const double priceValue = parseLooseNumberFallback(parsedPrice, &priceOk);
    const double amountValue = parseLooseNumberFallback(parsedAmount, &amountOk);
    if (!qtyOk || !priceOk || !amountOk || qtyValue <= 0.0 || priceValue <= 0.0 || amountValue <= 0.0) return false;

    if (oem) *oem = primaryCode;
    if (name) *name = parsedName.simplified();
    if (qty) *qty = parsedQty;
    if (price) *price = parsedPrice;
    if (amount) *amount = hasExplicitVatPrice ? elitePdfAmountWithVatText(parsedPrice, parsedQty, parsedAmount) : parsedAmount;
    return true;
}


static QString normalizeElitePdfBodyLine(QString line)
{
    line.replace(QChar(0x00A0), QLatin1Char(' '));
    line = line.simplified();
    if (line.isEmpty()) return line;
    line.replace(QRegularExpression(QStringLiteral(R"((\d[\d\s]*[\.,]\d{2})(?=\d))")), QStringLiteral("\\1 "));
    line.replace(QRegularExpression(QStringLiteral(R"((\d[\d\s]*[\.,]\d{2})(?=[A-Za-zА-Яа-яІіЇїЄє]))")), QStringLiteral("\\1 "));
    line.replace(QRegularExpression(QStringLiteral(R"(([A-Za-zА-Яа-яІіЇїЄє])(?=\d[\d\s]*[\.,]\d{2}))")), QStringLiteral("\\1 "));
    return line.simplified();
}

static QString normalizeElitePdfAnchoredLine(QString line)
{
    line = normalizeElitePdfBodyLine(line);
    if (line.isEmpty()) return line;
    const QString unitAlternatives = QStringLiteral("шт|шт\\.|штука|од|од\\.|компл|компл\\.|к-т|л|кг|г|м|см|уп|уп\\.|упак|пара|пар");
    line.replace(QRegularExpression(QStringLiteral(R"((\d[\d\s]*[\.,]\d{2})(?=(?:%1)\b))").arg(unitAlternatives),
                                    QRegularExpression::CaseInsensitiveOption),
                 QStringLiteral("\\1 "));
    line.replace(QRegularExpression(QStringLiteral(R"((\d[\d\s]*[\.,]\d{2}\s*)((?:%1))(?=[A-Za-zА-Яа-яІіЇїЄєҐґ]))").arg(unitAlternatives),
                                    QRegularExpression::CaseInsensitiveOption),
                 QStringLiteral("\\1\\2 "));
    line.replace(QRegularExpression(QStringLiteral(R"(([A-Za-zА-Яа-яІіЇїЄєҐґ])(?=\d[\d\s]*[\.,]\d{2}))")),
                 QStringLiteral("\\1 "));
    return line.simplified();
}

static bool lineStartsWithElitePdfCode(const QString& rawLine)
{
    const QString simplified = normalizeElitePdfAnchoredLine(rawLine);
    if (simplified.isEmpty()) return false;
    const QStringList tokens = simplified.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (tokens.isEmpty()) return false;
    int nextIndex = 0;
    const QString code = extractLeadingPdfInvoiceCode(tokens, 0, &nextIndex);
    return !code.isEmpty();
}

static bool tryParseEliteInvoiceAnchoredTable(const QStringList& layoutLines, ExcelReadResult& result)
{
    const int headerStart = findElitePdfTableHeaderStart(layoutLines);
    if (headerStart < 0) return false;

    QStringList bodyLines;
    bool bodyStarted = false;
    for (int i = headerStart; i < layoutLines.size(); ++i) {
        const QString line = layoutLines.at(i).trimmed();
        const QString normalized = normalizedImportHeader(line);
        if (line.isEmpty()) continue;
        if (isElitePdfTableHeaderSignal(normalized)) continue;
        if (isPdfInvoiceLikelyStopLine(normalized) && bodyStarted) break;
        bodyStarted = true;
        bodyLines << normalizeElitePdfAnchoredLine(line);
    }
    if (bodyLines.isEmpty()) return false;

    int parsedCount = 0;
    QStringList block;

    auto flushBlock = [&]() {
        if (block.isEmpty()) return;
        const QString joined = normalizeElitePdfAnchoredLine(block.join(QLatin1Char(' ')));
        QString oem;
        QString name;
        QString qty;
        QString price;
        QString amount;
        if (tryParseEliteInvoiceTrailingCodeLine(joined, &oem, &name, &qty, &price, &amount) ||
            tryParseEliteInvoiceFlattenedLine(joined, &oem, &name, &qty, &price, &amount) ||
            tryParseEliteInvoiceRowBlock(block, &oem, &name, &qty, &price, &amount) ||
            tryParseEliteInvoiceRowBlock(QStringList{joined}, &oem, &name, &qty, &price, &amount)) {
            appendPdfInvoiceRow(result, QString::fromUtf8("Еліт"), oem, name, qty, price, amount);
            ++parsedCount;
        }
        block.clear();
    };

    for (const QString& bodyLine : bodyLines) {
        const bool rowStart = lineStartsWithElitePdfCode(bodyLine);
        if (rowStart && !block.isEmpty()) flushBlock();
        block << bodyLine;
    }
    flushBlock();

    if (parsedCount > 0) return true;

    const QString joined = normalizeElitePdfAnchoredLine(bodyLines.join(QLatin1Char(' ')));
    QString oem;
    QString name;
    QString qty;
    QString price;
    QString amount;
    if (tryParseEliteInvoiceTrailingCodeLine(joined, &oem, &name, &qty, &price, &amount) ||
        tryParseEliteInvoiceFlattenedLine(joined, &oem, &name, &qty, &price, &amount) ||
        tryParseEliteInvoiceHeaderWindow(QStringList{QStringLiteral("Активн. код товару"), joined},
                                         &oem, &name, &qty, &price, &amount)) {
        appendPdfInvoiceRow(result, QString::fromUtf8("Еліт"), oem, name, qty, price, amount);
        return true;
    }

    return false;
}

static bool tryParseEliteInvoiceTrailingCodeLine(const QString& rawLine,
                                                 QString* oem,
                                                 QString* name,
                                                 QString* qty,
                                                 QString* price,
                                                 QString* amount)
{
    if (oem) oem->clear();
    if (name) name->clear();
    if (qty) qty->clear();
    if (price) price->clear();
    if (amount) amount->clear();

    QString line = normalizeElitePdfBodyLine(rawLine);
    if (line.isEmpty()) return false;

    const QString moneyPattern = QStringLiteral(R"(([0-9]{1,3}(?:\s[0-9]{3})*(?:[\.,][0-9]{2})|[0-9]+(?:[\.,][0-9]{2})))");
    const QString qtyPattern = QStringLiteral(R"(([0-9]+(?:[\.,][0-9]+)?))");
    const QString unitPattern = QStringLiteral(R"((шт|штука|шт\.|од|од\.|компл|компл\.|к-т|л|кг|г|м|см|уп|уп\.|упак|пара|пар))");
    const QString codePattern = QStringLiteral(R"(([A-ZА-Я0-9]{1,6}(?:\s+[A-ZА-Я0-9._/\-]{1,16}){1,3}))");

    const QList<QRegularExpression> patterns = {
        QRegularExpression(QStringLiteral("^") + moneyPattern + QStringLiteral("\\s+") + moneyPattern + QStringLiteral("\\s+") + qtyPattern + QStringLiteral("\\s+") + unitPattern + QStringLiteral("\\s+(.+?)\\s+") + codePattern + QStringLiteral("\\s+") + codePattern + QStringLiteral("$"), QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral("^") + moneyPattern + QStringLiteral("\\s+") + qtyPattern + QStringLiteral("\\s+") + unitPattern + QStringLiteral("\\s+(.+?)\\s+") + codePattern + QStringLiteral("\\s+") + codePattern + QStringLiteral("$"), QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral("^") + moneyPattern + QStringLiteral("\\s+") + moneyPattern + QStringLiteral("\\s+") + qtyPattern + QStringLiteral("\\s+(.+?)\\s+") + codePattern + QStringLiteral("\\s+") + codePattern + QStringLiteral("$"), QRegularExpression::CaseInsensitiveOption)
    };

    for (int i = 0; i < patterns.size(); ++i) {
        const auto match = patterns.at(i).match(line);
        if (!match.hasMatch()) continue;

        QString parsedPrice;
        QString parsedAmount;
        QString parsedQty;
        QString parsedName;
        QString parsedOem = cleanPdfInvoiceCode(match.captured(match.lastCapturedIndex()).trimmed());
        if (parsedOem.isEmpty()) {
            parsedOem = cleanPdfInvoiceCode(match.captured(match.lastCapturedIndex() - 1).trimmed());
        }
        if (i == 0) {
            const QString firstMoney = match.captured(1).trimmed();
            const QString secondMoney = match.captured(2).trimmed();
            parsedQty = match.captured(3).trimmed();
            if (elitePdfLooksLikeVatPricePair(firstMoney, secondMoney)) {
                parsedPrice = secondMoney;
                parsedAmount = elitePdfAmountWithVatText(parsedPrice, parsedQty, secondMoney);
            } else {
                parsedPrice = firstMoney;
                parsedAmount = secondMoney;
            }
            parsedName = match.captured(5).simplified();
        } else if (i == 1) {
            parsedPrice = match.captured(1).trimmed();
            parsedAmount = match.captured(1).trimmed();
            parsedQty = match.captured(2).trimmed();
            parsedName = match.captured(4).simplified();
        } else {
            const QString firstMoney = match.captured(1).trimmed();
            const QString secondMoney = match.captured(2).trimmed();
            parsedQty = match.captured(3).trimmed();
            if (elitePdfLooksLikeVatPricePair(firstMoney, secondMoney)) {
                parsedPrice = secondMoney;
                parsedAmount = elitePdfAmountWithVatText(parsedPrice, parsedQty, secondMoney);
            } else {
                parsedPrice = firstMoney;
                parsedAmount = secondMoney;
            }
            parsedName = match.captured(4).simplified();
        }
        parsedName = stripLeadingPdfInvoiceUnit(parsedName);
        if (parsedOem.isEmpty() || parsedName.isEmpty() || parsedQty.isEmpty() || parsedPrice.isEmpty() || parsedAmount.isEmpty()) continue;

        bool qtyOk = false;
        bool priceOk = false;
        bool amountOk = false;
        const double qtyValue = parseLooseNumberFallback(parsedQty, &qtyOk);
        const double priceValue = parseLooseNumberFallback(parsedPrice, &priceOk);
        const double amountValue = parseLooseNumberFallback(parsedAmount, &amountOk);
        if (!qtyOk || !priceOk || !amountOk || qtyValue <= 0.0 || priceValue <= 0.0 || amountValue <= 0.0) continue;
        if (amountValue + 0.01 < priceValue) continue;

        if (oem) *oem = parsedOem;
        if (name) *name = parsedName;
        if (qty) *qty = parsedQty;
        if (price) *price = parsedPrice;
        if (amount) *amount = parsedAmount;
        return true;
    }

    return false;
}

static bool tryParseEliteInvoiceGenericPartsRow(const QString& rawLine,
                                                 QString* oem,
                                                 QString* name,
                                                 QString* qty,
                                                 QString* price,
                                                 QString* amount)
{
    if (oem) oem->clear();
    if (name) name->clear();
    if (qty) qty->clear();
    if (price) price->clear();
    if (amount) amount->clear();

    QString line = rawLine;
    line.replace(QChar(0x00A0), QLatin1Char(' '));
    line = line.simplified();
    if (line.isEmpty()) return false;

    QStringList tokens = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (tokens.size() < 5) return false;

    int startIndex = 0;
    if (tokens.at(startIndex).contains(QRegularExpression(QStringLiteral(R"(^\d+$)")))) ++startIndex;
    if (startIndex + 1 >= tokens.size()) return false;

    const auto looksLikeShelf = [](const QString& token) -> bool {
        return token.contains(QRegularExpression(QStringLiteral(R"(^[A-Za-zА-Яа-я]?[0-9]{1,4}[A-Za-zА-Яа-я]?$)")));
    };

    int nextIndex = startIndex;
    QString parsedOem = extractLeadingPdfInvoiceCode(tokens, startIndex, &nextIndex);
    if (parsedOem.isEmpty() && startIndex + 1 < tokens.size() && looksLikeShelf(tokens.at(startIndex))) {
        ++startIndex;
        nextIndex = startIndex;
        parsedOem = extractLeadingPdfInvoiceCode(tokens, startIndex, &nextIndex);
    }
    if (parsedOem.isEmpty() || nextIndex >= tokens.size()) return false;
    if (!isElitePdfLikelyItemCode(parsedOem)) return false;

    int altNextIndex = nextIndex;
    const QString parsedAlt = extractLeadingPdfInvoiceCode(tokens, nextIndex, &altNextIndex);
    const bool hasAltCode = !parsedAlt.isEmpty() && isElitePdfLikelyItemCode(parsedAlt);
    const int contentStart = hasAltCode ? altNextIndex : nextIndex;
    if (contentStart >= tokens.size()) return false;

    QList<int> numericPositions;
    for (int i = contentStart; i < tokens.size(); ++i) {
        bool ok = false;
        const double value = parseLooseNumberFallback(tokens.at(i), &ok);
        if (ok && value > 0.0) numericPositions.append(i);
    }
    if (numericPositions.size() < 3) return false;

    int firstValuePos = numericPositions.at(numericPositions.size() - 3);
    int secondValuePos = numericPositions.at(numericPositions.size() - 2);
    int amountPos = numericPositions.at(numericPositions.size() - 1);
    int nameEndOverride = -1;
    bool explicitVatTail = false;
    if (numericPositions.size() >= 4) {
        const int withoutVatPos = numericPositions.at(numericPositions.size() - 4);
        const int withVatPos = numericPositions.at(numericPositions.size() - 3);
        const int qtyCandidatePos = numericPositions.at(numericPositions.size() - 2);
        const int amountWithoutVatPos = numericPositions.at(numericPositions.size() - 1);
        bool withoutOk = false;
        bool qtyCandidateOk = false;
        bool amountWithoutOk = false;
        const double withoutValue = parseLooseNumberFallback(tokens.at(withoutVatPos), &withoutOk);
        const double qtyCandidateValue = parseLooseNumberFallback(tokens.at(qtyCandidatePos), &qtyCandidateOk);
        const double amountWithoutValue = parseLooseNumberFallback(tokens.at(amountWithoutVatPos), &amountWithoutOk);
        if (elitePdfLooksLikeVatPricePair(tokens.at(withoutVatPos), tokens.at(withVatPos)) &&
            withoutOk && qtyCandidateOk && amountWithoutOk && qtyCandidateValue > 0.0 &&
            qAbs((withoutValue * qtyCandidateValue) - amountWithoutValue) <= qMax(0.10, amountWithoutValue * 0.03)) {
            firstValuePos = qtyCandidatePos;
            secondValuePos = withVatPos;
            amountPos = amountWithoutVatPos;
            nameEndOverride = withoutVatPos;
            explicitVatTail = true;
        } else {
            bool lastOk = false;
            bool prevOk = false;
            const double trailingValue = parseLooseNumberFallback(tokens.at(numericPositions.last()), &lastOk);
            const double penultimateValue = parseLooseNumberFallback(tokens.at(numericPositions.at(numericPositions.size() - 2)), &prevOk);
            if (lastOk && prevOk && trailingValue + 0.01 < penultimateValue) {
                firstValuePos = numericPositions.at(numericPositions.size() - 4);
                secondValuePos = numericPositions.at(numericPositions.size() - 3);
                amountPos = numericPositions.at(numericPositions.size() - 2);
            }
        }
    }

    bool firstOk = false;
    bool secondOk = false;
    bool amountNumericOk = false;
    const double firstValue = parseLooseNumberFallback(tokens.at(firstValuePos), &firstOk);
    const double secondValue = parseLooseNumberFallback(tokens.at(secondValuePos), &secondOk);
    const double amountNumeric = parseLooseNumberFallback(tokens.at(amountPos), &amountNumericOk);
    if (!firstOk || !secondOk || !amountNumericOk || firstValue <= 0.0 || secondValue <= 0.0 || amountNumeric <= 0.0) return false;

    int qtyPos = firstValuePos;
    int pricePos = secondValuePos;
    if (firstValue > secondValue && amountNumeric + 0.01 >= firstValue) {
        qtyPos = secondValuePos;
        pricePos = firstValuePos;
    }
    if (!(contentStart <= qMin(qtyPos, pricePos) && qMax(qtyPos, pricePos) < amountPos)) return false;

    const int nameEnd = nameEndOverride >= 0 ? nameEndOverride : qMin(qtyPos, pricePos);
    QString parsedName = tokens.mid(contentStart, qMax(0, nameEnd - contentStart)).join(QLatin1Char(' ')).simplified();
    parsedName = stripLeadingPdfInvoiceUnit(parsedName);
    QStringList nameTokens = parsedName.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    while (!nameTokens.isEmpty() && isPdfInvoiceLikelyUnit(nameTokens.constLast())) {
        nameTokens.removeLast();
    }
    parsedName = nameTokens.join(QLatin1Char(' ')).simplified();
    if (parsedName.isEmpty()) return false;

    const QString parsedQty = tokens.at(qtyPos).trimmed();
    const QString parsedPrice = tokens.at(pricePos).trimmed();
    const QString parsedAmount = explicitVatTail
        ? elitePdfAmountWithVatText(parsedPrice, parsedQty, tokens.at(amountPos).trimmed())
        : tokens.at(amountPos).trimmed();

    bool qtyOk = false;
    bool priceOk = false;
    bool amountOk = false;
    const double qtyValue = parseLooseNumberFallback(parsedQty, &qtyOk);
    const double priceValue = parseLooseNumberFallback(parsedPrice, &priceOk);
    const double amountValue = parseLooseNumberFallback(parsedAmount, &amountOk);
    if (!qtyOk || !priceOk || !amountOk || qtyValue <= 0.0 || priceValue <= 0.0 || amountValue <= 0.0) return false;
    if (amountValue + 0.01 < priceValue) return false;

    if (oem) *oem = parsedOem;
    if (name) *name = parsedName;
    if (qty) *qty = parsedQty;
    if (price) *price = parsedPrice;
    if (amount) *amount = parsedAmount;
    return true;
}

static bool tryParseEliteInvoiceCompactMergedRow(const QString& rawLine,
                                                 QString* oem,
                                                 QString* name,
                                                 QString* qty,
                                                 QString* price,
                                                 QString* amount)
{
    if (oem) oem->clear();
    if (name) name->clear();
    if (qty) qty->clear();
    if (price) price->clear();
    if (amount) amount->clear();

    const QString line = normalizeElitePdfAnchoredLine(rawLine);
    if (line.isEmpty()) return false;

    QStringList tokens = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (tokens.size() < 6) return false;

    int firstNext = 0;
    const QString firstCode = extractLeadingPdfInvoiceCode(tokens, 0, &firstNext);
    if (firstCode.isEmpty() || firstNext >= tokens.size()) return false;

    int secondNext = firstNext;
    const QString secondCode = extractLeadingPdfInvoiceCode(tokens, firstNext, &secondNext);
    if (secondCode.isEmpty() || secondNext >= tokens.size()) return false;

    int pricePos = secondNext;
    bool explicitVatPrice = false;
    if (secondNext + 1 < tokens.size() && elitePdfLooksLikeVatPricePair(tokens.at(secondNext), tokens.at(secondNext + 1))) {
        pricePos = secondNext + 1;
        explicitVatPrice = true;
    }

    bool priceOk = false;
    const double priceValue = parseLooseNumberFallback(tokens.at(pricePos), &priceOk);
    if (!priceOk || priceValue <= 0.0) return false;

    int cursor = pricePos + 1;
    if (cursor >= tokens.size()) return false;

    QString parsedQty = tokens.at(cursor).trimmed();
    QString parsedUnit;
    const QRegularExpression qtyUnitRx(QStringLiteral(R"(^([0-9]+(?:[\.,][0-9]+)?)(шт|шт\.|штука|од|од\.|компл|компл\.|к-т|л|кг|г|м|см|уп|уп\.|упак|пара|пар)$)"),
                                       QRegularExpression::CaseInsensitiveOption);
    auto qtyUnitMatch = qtyUnitRx.match(parsedQty);
    if (qtyUnitMatch.hasMatch()) {
        parsedQty = qtyUnitMatch.captured(1).trimmed();
        parsedUnit = qtyUnitMatch.captured(2).trimmed();
    } else {
        ++cursor;
        if (cursor >= tokens.size()) return false;
        if (isPdfInvoiceLikelyUnit(tokens.at(cursor))) {
            parsedUnit = tokens.at(cursor).trimmed();
        } else {
            --cursor;
        }
    }

    bool qtyOk = false;
    const double qtyValue = parseLooseNumberFallback(parsedQty, &qtyOk);
    if (!qtyOk || qtyValue <= 0.0) return false;

    int amountPos = -1;
    for (int i = tokens.size() - 1; i > cursor; --i) {
        bool amountOk = false;
        const double amountValue = parseLooseNumberFallback(tokens.at(i), &amountOk);
        if (amountOk && amountValue > 0.0) {
            amountPos = i;
            break;
        }
    }
    if (amountPos <= cursor + 1) return false;

    const QString rawParsedAmount = tokens.at(amountPos).trimmed();
    const QString parsedAmount = explicitVatPrice
        ? elitePdfAmountWithVatText(tokens.at(pricePos), parsedQty, rawParsedAmount)
        : rawParsedAmount;
    bool amountOk = false;
    const double amountValue = parseLooseNumberFallback(parsedAmount, &amountOk);
    if (!amountOk || amountValue <= 0.0) return false;
    if (amountValue + 0.01 < priceValue) return false;

    int nameStart = cursor + 1;
    if (!parsedUnit.isEmpty() && nameStart < tokens.size() && isPdfInvoiceLikelyUnit(tokens.at(nameStart))) ++nameStart;
    if (nameStart >= amountPos) return false;

    QString parsedName = tokens.mid(nameStart, amountPos - nameStart).join(QLatin1Char(' ')).simplified();
    parsedName = stripLeadingPdfInvoiceUnit(parsedName);
    if (parsedName.isEmpty()) return false;
    if (parsedName.startsWith(secondCode + QLatin1Char(' '), Qt::CaseInsensitive)) {
        parsedName = parsedName.mid(secondCode.size()).trimmed();
    }
    if (parsedName.compare(secondCode, Qt::CaseInsensitive) == 0) return false;

    if (oem) *oem = firstCode;
    if (name) *name = parsedName;
    if (qty) *qty = parsedQty;
    if (price) *price = tokens.at(pricePos).trimmed();
    if (amount) *amount = parsedAmount;
    return true;
}

static bool tryParseEliteInvoiceHeaderWindow(const QStringList& lines,
                                             QString* oem,
                                             QString* name,
                                             QString* qty,
                                             QString* price,
                                             QString* amount)
{
    QStringList bodyLines;
    bool inTable = false;
    for (const QString& raw : lines) {
        const QString line = normalizeElitePdfBodyLine(raw);
        const QString normalized = normalizedImportHeader(line);
        if (!inTable) {
            if (normalized.contains(QStringLiteral("активнкодтовару")) &&
                normalized.contains(QStringLiteral("альтернкодтовару")) &&
                normalized.contains(QStringLiteral("найменування"))) {
                inTable = true;
            }
            continue;
        }
        if (isPdfInvoiceLikelyStopLine(normalized)) break;
        if (!line.trimmed().isEmpty()) bodyLines << line;
    }
    if (bodyLines.isEmpty()) return false;

    QString joined = normalizeElitePdfBodyLine(bodyLines.join(QLatin1Char(' ')));
    if (joined.isEmpty()) return false;
    if (tryParseEliteInvoiceTrailingCodeLine(joined, oem, name, qty, price, amount)) return true;
    if (tryParseEliteInvoiceFlattenedLine(joined, oem, name, qty, price, amount)) return true;
    return false;
}

static bool tryParseEliteInvoiceRowBlock(const QStringList& parts,
                                        QString* oem,
                                        QString* name,
                                        QString* qty,
                                        QString* price,
                                        QString* amount)
{
    if (oem) oem->clear();
    if (name) name->clear();
    if (qty) qty->clear();
    if (price) price->clear();
    if (amount) amount->clear();
    if (parts.isEmpty()) return false;

    const QRegularExpression numericRx(QStringLiteral(R"((?<![A-Za-zА-Яа-яІіЇїЄє0-9])(\d[\d\s]*[\.,]\d{2})(?![A-Za-zА-Яа-яІіЇїЄє0-9]))"));
    const QSet<QString> unitTokens = {
        QString::fromUtf8("шт"), QStringLiteral("шт."), QString::fromUtf8("компл"), QStringLiteral("компл."),
        QString::fromUtf8("к-т"), QString::fromUtf8("л"), QString::fromUtf8("кг"), QString::fromUtf8("м")
    };

    QStringList numeric;
    QStringList codeCandidates;
    QStringList nameParts;

    auto collectFromToken = [&](QString token) {
        token = token.trimmed();
        if (token.isEmpty()) return;
        const QString normalized = normalizedImportHeader(token);
        if (normalized.isEmpty()) return;
        if (unitTokens.contains(token.toLower())) return;
        if (normalized.contains(QStringLiteral("активнкодтовару")) ||
            normalized.contains(QStringLiteral("альтернкодтовару")) ||
            normalized.contains(QStringLiteral("найменування")) ||
            normalized.contains(QStringLiteral("разомбезпдв"))) {
            return;
        }
        if (token.contains(numericRx)) {
            QRegularExpressionMatchIterator it = numericRx.globalMatch(token);
            while (it.hasNext()) {
                const auto m = it.next();
                const QString v = m.captured(1).trimmed();
                if (!v.isEmpty()) numeric << v;
            }
            token.remove(numericRx);
            token = token.simplified();
            if (token.isEmpty()) return;
        }
        if (unitTokens.contains(token.toLower())) return;

        const QStringList words = token.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (words.size() >= 4) {
            int lastLowerIndex = -1;
            for (int i = 0; i < words.size(); ++i) {
                if (words.at(i).contains(QRegularExpression(QStringLiteral(R"([\p{Ll}])")))) {
                    lastLowerIndex = i;
                }
            }
            const int tailSize = words.size() - lastLowerIndex - 1;
            if (lastLowerIndex >= 0 && tailSize >= 3) {
                QStringList tail = words.mid(lastLowerIndex + 1);
                QStringList nameTail;
                if (tail.size() >= 4) {
                    nameTail << tail.takeFirst();
                }
                const QString possibleCode = cleanPdfInvoiceCode(tail.join(QLatin1Char(' ')));
                if (isPdfInvoiceLikelyCode(possibleCode)) {
                    codeCandidates << possibleCode;
                    QStringList left = words.mid(0, lastLowerIndex + 1);
                    left.append(nameTail);
                    const QString possibleName = left.join(QLatin1Char(' ')).simplified();
                    if (isPdfInvoiceLikelyNameLine(possibleName)) nameParts << possibleName;
                    return;
                }
            }
        }

        if (isPdfInvoiceLikelyCode(token)) {
            codeCandidates << cleanPdfInvoiceCode(token);
            return;
        }
        if (isPdfInvoiceLikelyNameLine(token)) nameParts << token;
    };

    for (const QString& part : parts) {
        const QString simplified = part.simplified();
        if (simplified.isEmpty()) continue;
        const QStringList splitTokens = simplified.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (splitTokens.size() > 1 && simplified.contains(numericRx)) {
            collectFromToken(simplified);
            continue;
        }
        collectFromToken(simplified);
    }

    if (numeric.size() < 3 || codeCandidates.isEmpty() || nameParts.isEmpty()) return false;

    QString parsedPrice;
    QString parsedQty;
    QString parsedAmount;
    if (!assignPdfInvoiceNumericTriple(numeric, &parsedPrice, &parsedQty, &parsedAmount)) return false;
    const QString parsedOem = codeCandidates.constLast().trimmed();
    QString parsedName = nameParts.join(QLatin1Char(' ')).simplified();
    parsedName = stripLeadingPdfInvoiceUnit(parsedName);

    bool qtyOk = false;
    const double qtyValue = parseLooseNumberFallback(parsedQty, &qtyOk);
    if (!qtyOk || qtyValue <= 0.0 || parsedOem.isEmpty() || parsedName.isEmpty()) return false;

    if (oem) *oem = parsedOem;
    if (name) *name = parsedName;
    if (qty) *qty = parsedQty;
    if (price) *price = parsedPrice;
    if (amount) *amount = parsedAmount;
    return true;
}

static bool isElitePdfTableHeaderAnchor(const QString& normalized)
{
    return normalized.contains(QStringLiteral("активнкодтовару"));
}

static bool isElitePdfTableHeaderSignal(const QString& normalized)
{
    return isElitePdfTableHeaderAnchor(normalized) ||
           normalized.contains(QStringLiteral("альтернкодтовару")) ||
           normalized.contains(QStringLiteral("найменування")) ||
           normalized.contains(QStringLiteral("одвим")) ||
           normalized.contains(QStringLiteral("цінабезпдв")) ||
           normalized.contains(QStringLiteral("кілть")) ||
           normalized.contains(QStringLiteral("разомбезпдв"));
}

static int elitePdfHeaderMarkerScore(const QString& normalizedWindow,
                                     bool* hasAnchor,
                                     bool* hasName,
                                     bool* hasQty,
                                     bool* hasPrice)
{
    const bool anchor = normalizedWindow.contains(QStringLiteral("активнкодтовару"));
    const bool alt = normalizedWindow.contains(QStringLiteral("альтернкодтовару"));
    const bool name = normalizedWindow.contains(QStringLiteral("найменування"));
    const bool unit = normalizedWindow.contains(QStringLiteral("одвим")) ||
                      (normalizedWindow.contains(QStringLiteral("од")) && normalizedWindow.contains(QStringLiteral("вим")));
    const bool price = normalizedWindow.contains(QStringLiteral("цінабезпдв")) ||
                       (normalizedWindow.contains(QStringLiteral("ціна")) && normalizedWindow.contains(QStringLiteral("пдв")));
    const bool qty = normalizedWindow.contains(QStringLiteral("кілть")) ||
                     normalizedWindow.contains(QStringLiteral("кількість")) ||
                     normalizedWindow.contains(QStringLiteral("ксть"));
    const bool amount = normalizedWindow.contains(QStringLiteral("разомбезпдв")) ||
                        normalizedWindow.contains(QStringLiteral("разом"));

    if (hasAnchor) *hasAnchor = anchor;
    if (hasName) *hasName = name;
    if (hasQty) *hasQty = qty;
    if (hasPrice) *hasPrice = price;

    int score = 0;
    if (anchor) ++score;
    if (alt) ++score;
    if (name) ++score;
    if (unit) ++score;
    if (price) ++score;
    if (qty) ++score;
    if (amount) ++score;
    return score;
}

static int findElitePdfTableHeaderStart(const QStringList& lines)
{
    for (int i = 0; i < lines.size(); ++i) {
        QString windowText;
        const int windowEnd = qMin(lines.size(), i + 6);
        for (int j = i; j < windowEnd; ++j) {
            const QString normalized = normalizedImportHeader(lines.at(j));
            if (normalized.isEmpty()) continue;
            windowText += normalized;
        }
        if (windowText.isEmpty()) continue;

        bool hasAnchor = false;
        bool hasName = false;
        bool hasQty = false;
        bool hasPrice = false;
        const int score = elitePdfHeaderMarkerScore(windowText, &hasAnchor, &hasName, &hasQty, &hasPrice);
        if (hasAnchor && hasName && hasQty && hasPrice && score >= 5) {
            for (int j = i; j < windowEnd; ++j) {
                const QString normalized = normalizedImportHeader(lines.at(j));
                if (isElitePdfTableHeaderSignal(normalized) || normalized.contains(QStringLiteral("ціна")) || normalized.contains(QStringLiteral("пдв"))) {
                    return j;
                }
            }
            return i;
        }
    }
    return -1;
}

static QStringList extractElitePdfTableLines(const QStringList& lines, int* headerStartOut = nullptr)
{
    QStringList tableLines;
    const int headerStart = findElitePdfTableHeaderStart(lines);
    if (headerStartOut) *headerStartOut = headerStart;
    if (headerStart < 0) return tableLines;

    bool bodyStarted = false;
    for (int i = headerStart; i < lines.size(); ++i) {
        const QString line = lines.at(i).trimmed();
        const QString normalized = normalizedImportHeader(line);
        if (line.isEmpty()) continue;
        if (isPdfInvoiceLikelyStopLine(normalized) && bodyStarted) break;
        if (isElitePdfTableHeaderSignal(normalized)) {
            tableLines << line;
            continue;
        }
        bodyStarted = true;
        tableLines << line;
    }
    return tableLines;
}

static bool tryParseEliteInvoiceTableByBlocks(const QStringList& lines, ExcelReadResult& result)
{
    bool inTable = false;
    int headerSignals = 0;
    int parsedCount = 0;
    QStringList pending;

    auto flushPending = [&]() {
        if (pending.isEmpty()) return;
        QString oem;
        QString name;
        QString qty;
        QString price;
        QString amount;
        if (tryParseEliteInvoiceRowBlock(pending, &oem, &name, &qty, &price, &amount)) {
            appendPdfInvoiceRow(result, QString::fromUtf8("Еліт"), oem, name, qty, price, amount);
            ++parsedCount;
        }
        pending.clear();
    };

    auto lineLooksLikeRowData = [](const QString& line) {
        const QString simplified = line.simplified();
        if (simplified.isEmpty()) return false;
        return simplified.contains(QRegularExpression(QStringLiteral(R"(\d[\d\s]*[\.,]\d{2})"))) ||
               isPdfInvoiceLikelyCode(simplified) ||
               isPdfInvoiceLikelyNameLine(simplified);
    };

    for (const QString& line : lines) {
        const QString normalized = normalizedImportHeader(line);
        if (!inTable) {
            if (normalized.contains(QStringLiteral("активнкодтовару")) ||
                normalized.contains(QStringLiteral("альтернкодтовару")) ||
                normalized.contains(QStringLiteral("найменування")) ||
                normalized.contains(QStringLiteral("цінабезпдв")) ||
                normalized.contains(QStringLiteral("кілть"))) {
                ++headerSignals;
            }
            if (headerSignals >= 3) {
                inTable = true;
            }
            continue;
        }
        if (isPdfInvoiceLikelyStopLine(normalized)) {
            flushPending();
            break;
        }
        if (normalized.isEmpty()) continue;
        if (normalized.contains(QStringLiteral("альтернкодтовару")) ||
            normalized.contains(QStringLiteral("активнкодтовару")) ||
            normalized.contains(QStringLiteral("найменування")) ||
            normalized.contains(QStringLiteral("одвим")) ||
            normalized.contains(QStringLiteral("кілть")) ||
            normalized.contains(QStringLiteral("цінабезпдв")) ||
            normalized.contains(QStringLiteral("разомбезпдв"))) {
            continue;
        }
