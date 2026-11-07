// Extracted from src/core/import/InvoiceImportEngine.cpp for P1/P2 monolith reduction.
// Included exactly once by the original translation unit; generated split boundary is outside C++ raw strings.

namespace InvoiceImport {

static void appendInvoiceImportLog(const QString& message);

static bool importCancellationRequested(const std::shared_ptr<std::atomic_bool>& cancelToken)
{
    return (cancelToken && cancelToken->load()) ||
           (QThread::currentThread() && QThread::currentThread()->isInterruptionRequested());
}

static ExcelReadResult makeCancelledImportResult(QString* errorText, const QString& stage)
{
    if (errorText) {
        *errorText = QString::fromUtf8("Імпорт накладної скасовано на етапі: %1.").arg(stage);
    }
    appendInvoiceImportLog(QStringLiteral("cancellation requested | stage=%1 | reason=P1_async_p5_regular_interruption_check").arg(stage));
    return ExcelReadResult{};
}

static QString invoiceImportLogCell(QString value, int maxLen = 140)
{
    value = value.simplified();
    if (value.size() > maxLen) value = value.left(maxLen) + QStringLiteral("...");
    return value;
}

static void logInvoiceImportParserStage(const QString& stage, int rowIndex, const QStringList& cells)
{
    QStringList safe;
    safe.reserve(cells.size());
    for (const QString& cell : cells) safe << invoiceImportLogCell(cell);
    if (RuntimeLoggingControl::deepDebugEnabled()) qInfo().noquote() << QStringLiteral("INVOICE_IMPORT_%1 parserRow=%2 cells=%3")
                         .arg(stage)
                         .arg(rowIndex)
                         .arg(safe.join(QStringLiteral(" | ")));
}

static double parseLooseNumberFallback(QString text, bool* ok = nullptr)
{
    text = text.trimmed();
    text.remove(' ');
    text.replace(',', '.');
    return text.toDouble(ok);
}

struct ExcelImportColumnMap {
    int date = -1;
    int oem = -1;
    int name = -1;
    int brand = -1;
    int qty = -1;
    int price = -1;
    int amount = -1;
    int store = -1;

    int headerScore = 0;
    int priceHeaderScore = 0;
    int amountHeaderScore = 0;
    bool priceVatExplicit = false;
    bool amountVatExplicit = false;
    bool priceVatExcludedCandidate = false;
    bool priceFromSupplierTemplate = false;
    bool amountFromSupplierTemplate = false;
    QString detectionSupplierKey;

    int mappedCount() const {
        int total = 0;
        for (int idx : {date, oem, name, brand, qty, price, amount, store}) {
            if (idx >= 0) ++total;
        }
        return total;
    }

    bool hasCoreColumns() const {
        return name >= 0;
    }

    bool hasVatConfirmedPrice() const {
        return price >= 0 && priceVatExplicit && !priceVatExcludedCandidate;
    }
};

static QString normalizedImportHeader(QString text)
{
    text = text.trimmed().toLower();
    text.remove(QRegularExpression(QStringLiteral(R"([^\p{L}\p{N}]+)")));
    return text;
}

static QString cleanInvoiceMetaNumber(QString raw);
static QString extractInvoiceMetaNumberFromText(const QString& text);
static bool looksLikeInvoiceMetaNumberCandidate(QString raw);

static QString detectInvoiceStoreFromDocumentNumber(QString documentNumber)
{
    documentNumber = cleanInvoiceMetaNumber(documentNumber).trimmed().toUpper();
    if (documentNumber.isEmpty()) return QString();

    if (documentNumber.startsWith(QStringLiteral("А-")) ||
        documentNumber.startsWith(QStringLiteral("A-"))) {
        return QString::fromUtf8("Автонова-D");
    }

    if (documentNumber.startsWith(QStringLiteral("SI"))) {
        return QString::fromUtf8("Автотехнікс");
    }
    return QString();
}

static QString detectInvoiceSourceStore(const QList<QStringList>& rows)
{
    const int scanRows = qMin(rows.size(), 20);
    for (int rowIndex = 0; rowIndex < scanRows; ++rowIndex) {
        const QStringList& row = rows.at(rowIndex);
        for (const QString& cell : row) {
            const QString directCandidate = cleanInvoiceMetaNumber(cell);
            if (looksLikeInvoiceMetaNumberCandidate(directCandidate)) {
                const QString fromDirectNumber = detectInvoiceStoreFromDocumentNumber(directCandidate);
                if (!fromDirectNumber.isEmpty()) return fromDirectNumber;
            }

            const QString fromEmbeddedNumber = detectInvoiceStoreFromDocumentNumber(extractInvoiceMetaNumberFromText(cell));
            if (!fromEmbeddedNumber.isEmpty()) return fromEmbeddedNumber;
        }

        const QString joined = row.join(QLatin1Char(' ')).simplified();
        if (joined.isEmpty()) continue;

        const QString fromNumber = detectInvoiceStoreFromDocumentNumber(extractInvoiceMetaNumberFromText(joined));
        if (!fromNumber.isEmpty()) return fromNumber;

        const QString folded = joined.toCaseFolded();
        if (folded.contains(QString::fromUtf8("автонова-д")) ||
            (folded.contains(QString::fromUtf8("автонова")) && folded.contains(QString::fromUtf8("д")))) {
            return QString::fromUtf8("Автонова-D");
        }
        if (folded.contains(QString::fromUtf8("автонова"))) {
            return QString::fromUtf8("Автонова-D");
        }
        if (folded.contains(QString::fromUtf8("автотехнікс")) ||
            folded.contains(QString::fromUtf8("автотехникс")) ||
            folded.contains(QStringLiteral("autotechnics")) ||
            folded.contains(QStringLiteral("autotehnix"))) {
            return QString::fromUtf8("Автотехнікс");
        }
        if (folded.contains(QString::fromUtf8("еліт")) || folded.contains(QString::fromUtf8("элит"))) {
            return QString::fromUtf8("Еліт");
        }
        // Юнік-трейд / Юнікс Трейд Ко XLSX invoices expose the supplier only in
        // the document header, while item rows do not carry a store column.
        // Detect it here so imported rows receive the correct shop value and
        // downstream markup/report logic sees the same canonical store name.
        const bool looksLikeUnikTrade =
            (folded.contains(QString::fromUtf8("юнік")) ||
             folded.contains(QString::fromUtf8("юник")) ||
             folded.contains(QString::fromUtf8("юнікс")) ||
             folded.contains(QString::fromUtf8("юникс")) ||
             folded.contains(QStringLiteral("unik")) ||
             folded.contains(QStringLiteral("unix")) ||
             folded.contains(QStringLiteral("uniks")) ||
             folded.contains(QStringLiteral("unique"))) &&
            (folded.contains(QString::fromUtf8("трейд")) || folded.contains(QStringLiteral("trade")));
        if (looksLikeUnikTrade) {
            return QString::fromUtf8("Юнік-трейд");
        }
        if (folded.contains(QString::fromUtf8("ямато"))) {
            return QString::fromUtf8("Ямато");
        }
        if (folded.contains(QString::fromUtf8("база"))) {
            return QString::fromUtf8("База");
        }
    }

    InvoiceImportMeta meta = detectInvoiceImportMeta(rows, {});
    const QString fromMetaNumber = detectInvoiceStoreFromDocumentNumber(meta.documentNumber);
    if (!fromMetaNumber.isEmpty()) return fromMetaNumber;

    return QString();
}


InvoiceImportMeta detectInvoiceImportMeta(const QList<QStringList>& rows, const QSet<int>& highlightedColumns);

static void appendPdfInvoiceRow(ExcelReadResult& result,
                                const QString& store,
                                const QString& oem,
                                const QString& name,
                                const QString& qty,
                                const QString& price,
                                const QString& amount);
static QString extractLeadingPdfInvoiceCode(const QStringList& tokens,
                                            int startIndex,
                                            int* nextIndex);
static bool isElitePdfTableHeaderAnchor(const QString& normalized);
static bool isElitePdfTableHeaderSignal(const QString& normalized);
static bool isPdfInvoiceLikelyStopLine(const QString& normalized);
static bool isPdfInvoiceLikelyCode(const QString& value);
static bool isPdfInvoiceLikelyUnit(const QString& token);
static QString stripLeadingPdfInvoiceUnit(QString text);
static bool isPdfInvoiceLikelyNameLine(const QString& value);
static bool tryParseEliteInvoiceTrailingCodeLine(const QString& rawLine,
                                                 QString* oem,
                                                 QString* name,
                                                 QString* qty,
                                                 QString* price,
                                                 QString* amount);
static bool tryParseEliteInvoiceFlattenedLine(const QString& line,
                                              QString* oem,
                                              QString* name,
                                              QString* qty,
                                              QString* price,
                                              QString* amount);
static bool tryParseEliteInvoiceGenericPartsRow(const QString& rawLine,
                                                 QString* oem,
                                                 QString* name,
                                                 QString* qty,
                                                 QString* price,
                                                 QString* amount);
static bool tryParseEliteInvoiceCompactMergedRow(const QString& rawLine,
                                                 QString* oem,
                                                 QString* name,
                                                 QString* qty,
                                                 QString* price,
                                                 QString* amount);
static bool tryParseEliteInvoiceDenseCompactLine(const QString& rawLine,
                                                 QString* oem,
                                                 QString* name,
                                                 QString* qty,
                                                 QString* price,
                                                 QString* amount);
static bool tryParseEliteInvoiceDenseCompactTable(const QStringList& lines, ExcelReadResult& result);
static bool tryParseEliteInvoiceDirectCompactRows(const QStringList& lines, ExcelReadResult& result);
static bool tryParseEliteInvoiceVatWarehouseRow(const QString& rawLine,
                                                QString* oem,
                                                QString* name,
                                                QString* qty,
                                                QString* price,
                                                QString* amount);
static bool tryParseEliteInvoiceVatWarehouseTable(const QStringList& lines, ExcelReadResult& result);
static bool tryParseEliteInvoiceWideVatLayoutRow(const QString& rawLine,
                                                 QString* oem,
                                                 QString* name,
                                                 QString* qty,
                                                 QString* price,
                                                 QString* amount);
static bool tryParseEliteInvoiceWideVatLayoutTable(const QStringList& lines, ExcelReadResult& result);
static bool tryParseEliteInvoiceStandardTextRow(const QString& rawLine,
                                               QString* oem,
                                               QString* name,
                                               QString* qty,
                                               QString* price,
                                               QString* amount);
static bool tryParseEliteInvoiceStandardTextTable(const QStringList& lines, ExcelReadResult& result);
static bool isElitePdfLikelyItemCode(const QString& value);
static bool splitElitePdfTrailingItemCode(const QString& body, QString* nameOut, QString* oemOut);
static int findElitePdfTableHeaderStart(const QStringList& lines);
static QStringList elitePdfFallbackCandidateLines(const QStringList& lines);


static QString normalizePdfImportedTextCase(const QString& text)
{
    const QString simplified = text.simplified();
    if (simplified.isEmpty()) return QString();
    QStringList parts = simplified.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (QString& part : parts) {
        if (part.isEmpty()) continue;
        const QString upper = part.toUpper();
        const bool allUpper = (part == upper);
        if (allUpper && part.size() > 1) {
            part = part.left(1).toUpper() + part.mid(1).toLower();
        }
    }
    return parts.join(QLatin1Char(' '));
}

static QString normalizePdfImportedBrandToken(const QString& token)
{
    const QString trimmed = token.trimmed();
    if (trimmed.isEmpty()) return QString();
    QString cleaned = trimmed;
    cleaned.remove(QRegularExpression(QStringLiteral(R"(^[^\p{L}\p{N}]+|[^\p{L}\p{N}]+$)")));
    if (cleaned.isEmpty()) return QString();
    if (isPdfInvoiceLikelyUnit(cleaned)) return QString();
    const QString upper = cleaned.toUpper();
    const bool latinLike = upper.contains(QRegularExpression(QStringLiteral(R"([A-ZА-ЯІЇЄҐ])")));
    if (!latinLike) return QString();
    if (cleaned.size() < 2 || cleaned.size() > 24) return QString();
    if (cleaned == upper) return upper;
    return normalizePdfImportedTextCase(cleaned);
}

static void splitPdfImportedNameAndBrand(const QString& rawName,
                                         const QString& oem,
                                         QString* cleanName,
                                         QString* brand)
{
    if (cleanName) cleanName->clear();
    if (brand) brand->clear();

    QString text = rawName.simplified();
    if (text.isEmpty()) return;

    const QString trimmedOem = oem.simplified();
    if (!trimmedOem.isEmpty()) {
        const QRegularExpression leadingOemRx(QStringLiteral(R"(^%1(?:\s+|$))")
                                              .arg(QRegularExpression::escape(trimmedOem)),
                                              QRegularExpression::CaseInsensitiveOption);
        text.remove(leadingOemRx);
        text = text.trimmed();
    }

    text = stripLeadingPdfInvoiceUnit(text);
    text.replace(QRegularExpression(QStringLiteral(R"((?:\s+|^)(шт|шт\.|штука|од|од\.|компл|компл\.|к-т|л|кг|г|м|см|уп|уп\.|упак|пара|пар)\s*$)"),
                                    QRegularExpression::CaseInsensitiveOption), QString());
    text = text.simplified();
    if (text.isEmpty()) return;

    QStringList parts = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.size() >= 2) {
        const QString maybeBrand = normalizePdfImportedBrandToken(parts.last());
        if (!maybeBrand.isEmpty()) {
            parts.removeLast();
            if (brand) *brand = maybeBrand;
        }
    }

    text = parts.join(QLatin1Char(' ')).simplified();
    if (cleanName) *cleanName = normalizePdfImportedTextCase(text);
}

static bool tryParseEliteInvoiceDenseCompactLine(const QString& rawLine,
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
    if (!line.contains(QRegularExpression(QStringLiteral(R"(^(?:[0-9][0-9\s]*[\.,][0-9]{2}))")))) return false;
    if (!line.contains(QRegularExpression(QStringLiteral(R"(X\s*[0-9]{8,}\s*$)"),
                                           QRegularExpression::CaseInsensitiveOption))) return false;

    const QString moneyPattern = QStringLiteral(R"((\d[\d\s]*[\.,]\d{2}))");
    const QString unitPattern = QStringLiteral(R"((шт|штука|шт\.|од|од\.|компл|компл\.|к-т|л|кг|г|м|см|уп|уп\.|упак|пара|пар))");
    const QRegularExpression amountRx(QStringLiteral(R"(^%1)").arg(moneyPattern));
    const QRegularExpression leadingDigitsRx(QStringLiteral(R"(^([0-9]{1,4}))"));
    const QRegularExpression priceRx(QStringLiteral(R"(^%1)").arg(moneyPattern));
    const QRegularExpression unitRx(QStringLiteral(R"(^%1(.*)$)").arg(unitPattern), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression tailCodeRx(QStringLiteral(R"(^(.*)\s*X\s*([0-9]{8,})\s*$)"),
                                        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression trailingMoneyRx(QStringLiteral(R"((\d[\d\s]*[\.,]\d{2}))"));

    const auto amountMatch = amountRx.match(line);
    if (!amountMatch.hasMatch()) return false;

    const QString parsedAmount = amountMatch.captured(1).trimmed();
    bool amountOk = false;
    const double amountValue = parseLooseNumberFallback(parsedAmount, &amountOk);
    if (!amountOk || amountValue <= 0.0) return false;

    QString afterAmount = line.mid(amountMatch.capturedLength()).trimmed();
    if (afterAmount.isEmpty()) return false;

    const auto leadingDigitsMatch = leadingDigitsRx.match(afterAmount);
    if (!leadingDigitsMatch.hasMatch()) return false;
    const QString leadingDigits = leadingDigitsMatch.captured(1);

    QString bestOem;
    QString bestName;
    QString bestQty;
    QString bestPrice;
    double bestDelta = (std::numeric_limits<double>::max)();

    for (int qtyDigits = 1; qtyDigits <= leadingDigits.size(); ++qtyDigits) {
        const QString parsedQty = leadingDigits.left(qtyDigits).trimmed();
        QString afterQty = afterAmount.mid(qtyDigits).trimmed();
        if (parsedQty.isEmpty() || afterQty.isEmpty()) continue;

        const auto priceMatch = priceRx.match(afterQty);
        if (!priceMatch.hasMatch()) continue;
        const QString parsedPrice = priceMatch.captured(1).trimmed();
        QString afterPrice = afterQty.mid(priceMatch.capturedLength()).trimmed();
        if (afterPrice.isEmpty()) continue;

        const auto unitMatch = unitRx.match(afterPrice);
        if (!unitMatch.hasMatch()) continue;
        QString afterUnit = unitMatch.captured(2).trimmed();
        if (afterUnit.isEmpty()) continue;

        const auto tailCodeMatch = tailCodeRx.match(afterUnit);
        if (!tailCodeMatch.hasMatch()) continue;
        const QString bodyAndVat = tailCodeMatch.captured(1).trimmed();
        if (bodyAndVat.isEmpty()) continue;

        int lastMoneyStart = -1;
        int lastMoneyLength = 0;
        QRegularExpressionMatchIterator moneyIt = trailingMoneyRx.globalMatch(bodyAndVat);
        while (moneyIt.hasNext()) {
            const QRegularExpressionMatch moneyMatch = moneyIt.next();
            if (!moneyMatch.hasMatch()) continue;
            lastMoneyStart = moneyMatch.capturedStart(1);
            lastMoneyLength = moneyMatch.capturedLength(1);
        }
        if (lastMoneyStart < 0 || lastMoneyLength <= 0) continue;

        QString body = bodyAndVat.left(lastMoneyStart).trimmed();
        if (body.isEmpty()) continue;

        QString parsedName;
        QString parsedOem;
        if (!splitElitePdfTrailingItemCode(body, &parsedName, &parsedOem)) continue;
        if (parsedName.isEmpty() || parsedOem.isEmpty() || !isElitePdfLikelyItemCode(parsedOem)) continue;

        bool qtyOk = false;
        bool priceOk = false;
        const double qtyValue = parseLooseNumberFallback(parsedQty, &qtyOk);
        const double priceValue = parseLooseNumberFallback(parsedPrice, &priceOk);
        if (!qtyOk || !priceOk || qtyValue <= 0.0 || priceValue <= 0.0) continue;

        const double delta = qAbs(priceValue * qtyValue - amountValue);
        const double tolerance = qMax(0.05, qAbs(amountValue) * 0.01);
        if (delta > tolerance) continue;

        if (delta < bestDelta) {
            bestDelta = delta;
            bestOem = parsedOem;
            bestName = parsedName;
            bestQty = parsedQty;
            bestPrice = parsedPrice;
        }
    }

    if (bestOem.isEmpty() || bestName.isEmpty() || bestQty.isEmpty() || bestPrice.isEmpty()) return false;
    if (oem) *oem = bestOem;
    if (name) *name = bestName;
    if (qty) *qty = bestQty;
    if (price) *price = bestPrice;
    if (amount) *amount = parsedAmount;
    return true;
}

static bool tryParseEliteInvoiceDirectCompactRows(const QStringList& lines, ExcelReadResult& result)
{
    int parsedCount = 0;
    for (const QString& rawLine : lines) {
        const QString simplified = rawLine.simplified();
        if (simplified.isEmpty()) continue;
        if (!simplified.contains(QRegularExpression(QStringLiteral(R"(^(?:[0-9][0-9\s]*[\.,][0-9]{2}))")))) continue;
        if (!simplified.contains(QRegularExpression(QStringLiteral(R"(X\s*[0-9]{8,}\s*$)"),
                                                    QRegularExpression::CaseInsensitiveOption))) continue;

        QString parsedOem;
        QString parsedName;
        QString parsedQty;
        QString parsedPrice;
        QString parsedAmount;
        if (!tryParseEliteInvoiceDenseCompactLine(simplified,
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

static bool tryParseEliteInvoiceDenseCompactTable(const QStringList& lines, ExcelReadResult& result)
{
    int headerStart = findElitePdfTableHeaderStart(lines);
    if (headerStart < 0) {
        bool hasDenseCandidate = false;
        for (const QString& rawLine : lines) {
            const QString line = rawLine.simplified();
            if (line.isEmpty()) continue;
            if (line.contains(QRegularExpression(QStringLiteral(R"(^(?:[0-9][0-9\s]*[\.,][0-9]{2}))"))) &&
                line.contains(QRegularExpression(QStringLiteral(R"(X\s*[0-9]{8,}\s*$)"),
                                                 QRegularExpression::CaseInsensitiveOption))) {
                hasDenseCandidate = true;
                break;
            }
        }
        if (!hasDenseCandidate) return false;
        headerStart = 0;
    }

    bool bodyStarted = false;
    int parsedCount = 0;
    for (int i = headerStart; i < lines.size(); ++i) {
        const QString rawLine = lines.at(i).trimmed();
        const QString normalized = normalizedImportHeader(rawLine);
        if (rawLine.isEmpty()) continue;
        if (isElitePdfTableHeaderSignal(normalized)) continue;

        QString parsedOem;
        QString parsedName;
        QString parsedQty;
        QString parsedPrice;
        QString parsedAmount;
        const bool rowParsed = tryParseEliteInvoiceDenseCompactLine(rawLine,
                                                                    &parsedOem,
                                                                    &parsedName,
                                                                    &parsedQty,
                                                                    &parsedPrice,
                                                                    &parsedAmount);
        if (!rowParsed) {
            if (!bodyStarted) {
                continue;
            }
            if (isPdfInvoiceLikelyStopLine(normalized)) break;
            continue;
        }

        bodyStarted = true;
        appendPdfInvoiceRow(result, QString::fromUtf8("Еліт"), parsedOem, parsedName, parsedQty, parsedPrice, parsedAmount);
        ++parsedCount;
    }

    return parsedCount > 0;
}

static bool tryParseEliteInvoiceHeaderWindow(const QStringList& lines,
                                             QString* oem,
                                             QString* name,
                                             QString* qty,
                                             QString* price,
                                             QString* amount);
static bool tryParseEliteInvoiceRowBlock(const QStringList& parts,
                                         QString* oem,
                                         QString* name,
                                         QString* qty,
                                         QString* price,
                                         QString* amount);


static int findElitePdfTableHeaderStart(const QStringList& lines);
static QStringList extractElitePdfTableLines(const QStringList& lines, int* headerStartOut);


static QString invoiceImportLogPath()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.trimmed().isEmpty()) return QString();
    QDir dir(base);
    if (!dir.exists()) dir.mkpath(QStringLiteral("."));
    if (!dir.exists(QStringLiteral("logs"))) dir.mkpath(QStringLiteral("logs"));
    return dir.filePath(QStringLiteral("logs/invoice_import.log"));
}

static bool invoiceImportDeepDebugEnabled()
{
    return qEnvironmentVariableIntValue("CS_DEEP_DEBUG") > 0 ||
           qEnvironmentVariableIntValue("CS_INVOICE_IMPORT_DEBUG") > 0;
}

static bool invoiceImportLogRateLimitAllows(const QString& message, qint64 windowMs = 5000)
{
    if (invoiceImportDeepDebugEnabled()) return true;

    static QMutex rateLimitMutex;
    static QHash<QString, qint64> lastSeenMs;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QString key = message.left(240);

    QMutexLocker locker(&rateLimitMutex);
    const auto it = lastSeenMs.constFind(key);
    if (it != lastSeenMs.constEnd() && now - *it < windowMs) {
        return false;
    }
    if (lastSeenMs.size() > 512) {
        lastSeenMs.clear();
    }
    lastSeenMs.insert(key, now);
    return true;
}

static void appendInvoiceImportLog(const QString& message)
{
    // P0/P2 performance: invoice_import.log is written from hot fallback paths.
    // In production, keep the first occurrence and suppress identical bursts;
    // full per-stage logging stays available with CS_DEEP_DEBUG=1.
    if (!invoiceImportLogRateLimitAllows(message)) return;

    const QString logPath = invoiceImportLogPath();
    if (logPath.trimmed().isEmpty()) return;
    QFile file(logPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) return;
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
           << QStringLiteral(" | ") << message << Qt::endl;
}

static bool rejectBlockingImportProcessOnUiThread(const QString& operation, QString* errorText)
{
    QCoreApplication* app = QCoreApplication::instance();
    if (!app || QThread::currentThread() != app->thread()) {
        return false;
    }

    const QString message = QString::fromUtf8(
        "P0 import p4: блокуюче очікування зовнішнього процесу імпорту '%1' у UI-потоці заборонено. "
        "Запустіть імпорт через InvoiceImport::ImportJob.")
        .arg(operation);
    if (errorText) {
        *errorText = message;
    }
    const QString logMessage = QStringLiteral("ui-thread process wait denied | operation=%1 | reason=P0_import_p4_no_blocking_process_wait_on_ui_thread")
        .arg(operation);
    appendInvoiceImportLog(logMessage);
    if (invoiceImportLogRateLimitAllows(QStringLiteral("warn:%1").arg(logMessage), 10000)) {
        qWarning().noquote() << QStringLiteral("INVOICE_IMPORT_UI_THREAD_WAIT_DENIED operation=%1").arg(operation);
    }
    return true;
}


struct IncrementalImportProcessResult {
    bool started = false;
    bool timedOut = false;
    bool cancelled = false;
    bool stdOutTruncated = false;
    bool stdErrTruncated = false;
    QProcess::ExitStatus exitStatus = QProcess::CrashExit;
    int exitCode = -1;
    QByteArray stdOut;
    QByteArray stdErr;
    QString errorString;
};

constexpr qsizetype kMaxImportProcessChannelBytes = 2 * 1024 * 1024;

static void appendBoundedImportProcessBytes(QByteArray& target,
                                            const QByteArray& chunk,
                                            bool& truncated)
{
    if (chunk.isEmpty()) return;
    const qsizetype remaining = kMaxImportProcessChannelBytes - target.size();
    if (remaining <= 0) {
        truncated = true;
        return;
    }
    if (chunk.size() > remaining) {
        target.append(chunk.constData(), remaining);
        truncated = true;
        return;
    }
    target.append(chunk);
}

static QString importProcessLogPreview(const QByteArray& bytes, int maxLen = 800)
{
    return QString::fromUtf8(bytes)
        .left(maxLen)
        .replace(QRegularExpression(QStringLiteral(R"([\r\n]+)")), QStringLiteral(" "));
}

static void drainImportProcessChannels(QProcess& process, IncrementalImportProcessResult& result)
{
    const QByteArray out = process.readAllStandardOutput();
    appendBoundedImportProcessBytes(result.stdOut, out, result.stdOutTruncated);

    const QByteArray err = process.readAllStandardError();
    appendBoundedImportProcessBytes(result.stdErr, err, result.stdErrTruncated);
}

static IncrementalImportProcessResult runImportProcessIncrementally(QProcess& process,
                                                                    int startTimeoutMs,
                                                                    int finishTimeoutMs,
                                                                    const QString& operation)
{
    IncrementalImportProcessResult result;
    appendInvoiceImportLog(QStringLiteral("incremental process begin | operation=%1 | program=%2 | timeoutMs=%3")
                               .arg(operation, process.program())
                               .arg(finishTimeoutMs));

    process.start();
    if (!process.waitForStarted(startTimeoutMs)) {
        drainImportProcessChannels(process, result);
        result.errorString = process.errorString().trimmed();
        appendInvoiceImportLog(QStringLiteral("incremental process failedToStart | operation=%1 | program=%2 | err=%3 | stdoutBytes=%4 | stderrBytes=%5")
                                   .arg(operation, process.program(), result.errorString)
                                   .arg(result.stdOut.size())
                                   .arg(result.stdErr.size()));
        return result;
    }

    result.started = true;
    QElapsedTimer timer;
    timer.start();

    while (process.state() != QProcess::NotRunning) {
        drainImportProcessChannels(process, result);

        if (QThread::currentThread() && QThread::currentThread()->isInterruptionRequested()) {
            result.cancelled = true;
            result.errorString = QStringLiteral("cancelled");
            appendInvoiceImportLog(QStringLiteral("incremental process cancelled | operation=%1 | program=%2 | stdoutBytes=%3 | stderrBytes=%4")
                                       .arg(operation, process.program())
                                       .arg(result.stdOut.size())
                                       .arg(result.stdErr.size()));
            process.kill();
            for (int i = 0; i < 20 && process.state() != QProcess::NotRunning; ++i) {
                process.waitForReadyRead(100);
                drainImportProcessChannels(process, result);
            }
            break;
        }

        if (timer.elapsed() >= finishTimeoutMs) {
            result.timedOut = true;
            result.errorString = QStringLiteral("timeout");
            appendInvoiceImportLog(QStringLiteral("incremental process timeout | operation=%1 | program=%2 | stdoutBytes=%3 | stderrBytes=%4")
                                       .arg(operation, process.program())
                                       .arg(result.stdOut.size())
                                       .arg(result.stdErr.size()));
            process.kill();
            for (int i = 0; i < 20 && process.state() != QProcess::NotRunning; ++i) {
                process.waitForReadyRead(100);
                drainImportProcessChannels(process, result);
            }
            break;
        }

        const qint64 remaining = qMax<qint64>(1, finishTimeoutMs - timer.elapsed());
        const int waitChunkMs = int(qMin<qint64>(250, remaining));
        process.waitForReadyRead(waitChunkMs);
    }

    drainImportProcessChannels(process, result);
    result.exitStatus = process.exitStatus();
    result.exitCode = process.exitCode();
    if (result.errorString.trimmed().isEmpty()) result.errorString = process.errorString().trimmed();

    appendInvoiceImportLog(QStringLiteral("incremental process finished | operation=%1 | exitStatus=%2 | exitCode=%3 | stdoutBytes=%4 | stderrBytes=%5 | stdoutTruncated=%6 | stderrTruncated=%7 | stdout=%8 | stderr=%9")
                               .arg(operation)
                               .arg(int(result.exitStatus))
                               .arg(result.exitCode)
                               .arg(result.stdOut.size())
                               .arg(result.stdErr.size())
                               .arg(result.stdOutTruncated ? 1 : 0)
                               .arg(result.stdErrTruncated ? 1 : 0)
                               .arg(importProcessLogPreview(result.stdOut), importProcessLogPreview(result.stdErr)));
    return result;
}

static QString classifyPdfTextQuality(const QString& text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return QStringLiteral("ScannedImagePdf");
    int printable = 0;
    for (const QChar ch : trimmed) {
        if (ch.isPrint() && !ch.isNull()) ++printable;
    }
    if (printable < 40) return QStringLiteral("ScannedImagePdf");
    const double ratio = trimmed.isEmpty() ? 0.0 : (double(printable) / double(trimmed.size()));
    if (ratio > 0.90 && printable > 300) return QStringLiteral("DigitalTextPdf");
    if (ratio > 0.55) return QStringLiteral("HybridPdf");
    return QStringLiteral("UnknownPdf");
}

static QString extractPdfTextViaQtPdf(const QString& filePath, QString* errorText = nullptr)
{
#if defined(CS_HAS_QT_PDF) && CS_HAS_QT_PDF
    QPdfDocument document;
    const auto loadError = document.load(filePath);
    if (loadError != QPdfDocument::Error::None) {
        if (errorText) *errorText = QString::fromUtf8("QtPdf не зміг відкрити PDF-файл.");
        return {};
    }

    QStringList pages;
    pages.reserve(document.pageCount());
    for (int page = 0; page < document.pageCount(); ++page) {
        const QString pageText = document.getAllText(page).text().trimmed();
        if (!pageText.isEmpty()) pages << pageText;
    }
    const QString text = pages.join(QStringLiteral("\n\n"));
    if (text.trimmed().isEmpty()) {
        if (errorText) *errorText = QString::fromUtf8("QtPdf відкрив PDF, але не знайшов текстового шару.");
        return {};
    }
    return text;
#else
    Q_UNUSED(filePath);
    if (errorText) *errorText = QString();
    return {};
#endif
}


static QString renderPdfPagesViaQtPdfForOcr(const QString& filePath,
                                            const QString& outputDirPath,
                                            QString* errorText = nullptr)
{
#if defined(CS_HAS_QT_PDF) && CS_HAS_QT_PDF
    QPdfDocument document;
    const auto loadError = document.load(filePath);
    if (loadError != QPdfDocument::Error::None) {
        if (errorText) *errorText = QString::fromUtf8("QtPdf не зміг відкрити PDF для OCR-рендерингу.");
        return {};
    }

    QDir outputDir(outputDirPath);
    if (!outputDir.exists() && !outputDir.mkpath(QStringLiteral("."))) {
        if (errorText) *errorText = QString::fromUtf8("Не вдалося створити тимчасову папку для OCR-зображень PDF.");
        return {};
    }

    constexpr double kTargetDpi = 300.0;
    constexpr int kMaxSidePx = 3600;
    constexpr int kMinWidthPx = 1400;
    int renderedPages = 0;

    for (int page = 0; page < document.pageCount(); ++page) {
        QSizeF pointSize = document.pagePointSize(page);
        if (!pointSize.isValid() || pointSize.width() <= 0.0 || pointSize.height() <= 0.0) {
            pointSize = QSizeF(595.0, 842.0); // A4 fallback in PostScript points.
        }

        QSize pixelSize(qMax(1, int(std::round(pointSize.width() * kTargetDpi / 72.0))),
                        qMax(1, int(std::round(pointSize.height() * kTargetDpi / 72.0))));
        if (pixelSize.width() < kMinWidthPx) {
            pixelSize.scale(kMinWidthPx, kMaxSidePx, Qt::KeepAspectRatio);
        }
        if (pixelSize.width() > kMaxSidePx || pixelSize.height() > kMaxSidePx) {
            pixelSize.scale(kMaxSidePx, kMaxSidePx, Qt::KeepAspectRatio);
        }

        QImage image = document.render(page, pixelSize);
        if (image.isNull()) {
            appendInvoiceImportLog(QStringLiteral("qt pdf render ocr page skipped | page=%1 | reason=null_image").arg(page + 1));
            continue;
        }
        if (image.format() != QImage::Format_RGB32 && image.format() != QImage::Format_ARGB32) {
            image = image.convertToFormat(QImage::Format_RGB32);
        }

        const QString imagePath = outputDir.filePath(QStringLiteral("page_%1.png").arg(page + 1, 4, 10, QLatin1Char('0')));
        if (image.save(imagePath, "PNG")) {
            ++renderedPages;
        } else {
            appendInvoiceImportLog(QStringLiteral("qt pdf render ocr page skipped | page=%1 | reason=save_failed | path=%2")
                                       .arg(page + 1)
                                       .arg(imagePath));
        }
    }

    if (renderedPages <= 0) {
        if (errorText) *errorText = QString::fromUtf8("QtPdf не зміг підготувати сторінки PDF для Windows OCR.");
        return {};
    }
    if (errorText) errorText->clear();
    appendInvoiceImportLog(QStringLiteral("qt pdf render ocr pages ready | pages=%1 | dir=%2")
                               .arg(renderedPages)
                               .arg(outputDir.absolutePath()));
    return outputDir.absolutePath();
#else
    Q_UNUSED(filePath);
    Q_UNUSED(outputDirPath);
    if (errorText) *errorText = QString::fromUtf8("QtPdf renderer недоступний у цій збірці.");
    return {};
#endif
}


static QStringList pdfImportRuntimeRoots(const QString& appDir)
{
    QStringList roots;
    auto addRoot = [&](const QString& path) {
        if (path.trimmed().isEmpty()) return;
        const QString clean = QDir::cleanPath(path);
        if (!clean.trimmed().isEmpty() && !roots.contains(clean)) roots << clean;
    };

    const QString parent = QFileInfo(appDir).dir().absolutePath();
    const QString grand = QFileInfo(parent).dir().absolutePath();
    for (const QString& root : QStringList{appDir, parent, grand}) {
        if (root.trimmed().isEmpty()) continue;
        addRoot(root);
        addRoot(QDir(root).filePath(QStringLiteral("resources/windows/ocr-layer")));
        addRoot(QDir(root).filePath(QStringLiteral("ocr-layer")));
        addRoot(QDir(root).filePath(QStringLiteral("ocr")));
        addRoot(QDir(root).filePath(QStringLiteral("runtime/ocr")));
        addRoot(QDir(root).filePath(QStringLiteral("tools/ocr")));
        addRoot(QDir(root).filePath(QStringLiteral("support/ocr")));
    }

    const QString envLayer = qEnvironmentVariable("CS_PDF_OCR_LAYER").trimmed();
    if (!envLayer.isEmpty()) addRoot(envLayer);
    return roots;
}

static QStringList pythonVendorPathsForPdfImport(const QString& appDir)
{
    QStringList roots;
    auto addIfExists = [&](const QString& path) {
        if (path.trimmed().isEmpty()) return;
        const QString clean = QDir::cleanPath(path);
        if (QDir(clean).exists() && !roots.contains(clean)) roots << clean;
    };

    for (const QString& root : pdfImportRuntimeRoots(appDir)) {
        if (root.trimmed().isEmpty()) continue;
        addIfExists(QDir(root).filePath(QStringLiteral("resources/python_vendor")));
        addIfExists(QDir(root).filePath(QStringLiteral("resources/python")));
        addIfExists(QDir(root).filePath(QStringLiteral("python_vendor")));
        addIfExists(QDir(root).filePath(QStringLiteral("python")));
        addIfExists(QDir(root).filePath(QStringLiteral("python/Lib/site-packages")));
        addIfExists(QDir(root).filePath(QStringLiteral("python/site-packages")));
        addIfExists(QDir(root).filePath(QStringLiteral("site-packages")));
    }
    return roots;
}


static QStringList pdfImportRuntimeToolSearchDirs(const QString& appDir)
{
    QStringList dirs;
    auto addDir = [&](const QString& path) {
        if (path.trimmed().isEmpty()) return;
        const QString clean = QDir::cleanPath(path);
        if (QDir(clean).exists() && !dirs.contains(clean)) dirs << clean;
    };

    for (const QString& root : pdfImportRuntimeRoots(appDir)) {
        if (root.trimmed().isEmpty()) continue;
        addDir(root);
        for (const QString& rel : QStringList{
                 QStringLiteral("bin"), QStringLiteral("tools"), QStringLiteral("poppler/bin"),
                 QStringLiteral("poppler/Library/bin"), QStringLiteral("mupdf"), QStringLiteral("mupdf/bin"),
                 QStringLiteral("xpdf/bin"), QStringLiteral("pdfium"), QStringLiteral("pdfium/bin"),
                 QStringLiteral("ghostscript"), QStringLiteral("ghostscript/bin"), QStringLiteral("ghostscript/gs10.07.0/bin"), QStringLiteral("ghostscript/gs10.07.1/bin"), QStringLiteral("ghostscript/gs10.06.0/bin"), QStringLiteral("gs"), QStringLiteral("gs/bin"),
                 QStringLiteral("xpdf/bin"), QStringLiteral("tesseract"), QStringLiteral("tesseract/bin"), QStringLiteral("Tesseract-OCR"),
                 QStringLiteral("Tesseract-OCR/bin"), QStringLiteral("naps2"), QStringLiteral("NAPS2"),
                 QStringLiteral("NAPS2/bin"), QStringLiteral("NAPS2/App"), QStringLiteral("NAPS2/App/bin"),
                 QStringLiteral("ocrmypdf"), QStringLiteral("ocrmypdf/bin")}) {
            addDir(QDir(root).filePath(rel));
        }
    }
    return dirs;
}

static QStringList candidatePdfRuntimeExecutables(const QString& appDir, const QStringList& executableNames)
{
    QStringList candidates;
    auto addCandidate = [&](const QString& path) {
        if (path.trimmed().isEmpty()) return;
        const QString clean = QDir::toNativeSeparators(QDir::cleanPath(path));
        if (!QFileInfo::exists(clean)) return;
        if (!candidates.contains(clean)) candidates << clean;
    };

    for (const QString& dir : pdfImportRuntimeToolSearchDirs(appDir)) {
        for (const QString& exe : executableNames) addCandidate(QDir(dir).filePath(exe));
    }
    for (const QString& base : QStringList{qEnvironmentVariable("ProgramFiles"), qEnvironmentVariable("ProgramFiles(x86)")}) {
        if (base.trimmed().isEmpty()) continue;
        for (const QString& sub : QStringList{QStringLiteral("Tesseract-OCR"), QStringLiteral("Poppler/bin"), QStringLiteral("MuPDF"), QStringLiteral("Ghostscript/bin"), QStringLiteral("gs/bin"), QStringLiteral("PDFium/bin"), QStringLiteral("Xpdf/bin")}) {
            for (const QString& exe : executableNames) addCandidate(QDir(QDir(base).filePath(sub)).filePath(exe));
        }
    }
    for (const QString& exe : executableNames) addCandidate(QStandardPaths::findExecutable(exe));
    return candidates;
}

static QString firstExistingPdfRuntimeDir(const QString& appDir, const QStringList& relativeDirs)
{
    for (const QString& root : pdfImportRuntimeRoots(appDir)) {
        for (const QString& rel : relativeDirs) {
            const QString candidate = QDir(root).filePath(rel);
            if (QDir(candidate).exists()) return QDir::toNativeSeparators(QDir::cleanPath(candidate));
        }
    }
    return {};
}

static QString extractPdfTextViaLocalRuntimeTool(const QString& filePath,
                                                 const QString& appDir,
                                                 QString* engineOut = nullptr,
                                                 QString* errorText = nullptr)
{
#if defined(Q_OS_WIN)
    if (rejectBlockingImportProcessOnUiThread(QStringLiteral("pdf_local_runtime_text"), errorText)) return {};

    struct ToolAttempt { QString engine; QStringList names; QStringList args; };
    const QList<ToolAttempt> attempts = {
        {QStringLiteral("pdftotext-local"), {QStringLiteral("pdftotext.exe"), QStringLiteral("pdftotext")},
         {QStringLiteral("-layout"), QStringLiteral("-enc"), QStringLiteral("UTF-8"), filePath, QStringLiteral("-")}},
        {QStringLiteral("mutool-local"), {QStringLiteral("mutool.exe"), QStringLiteral("mutool")},
         {QStringLiteral("draw"), QStringLiteral("-F"), QStringLiteral("txt"), QStringLiteral("-o"), QStringLiteral("-"), filePath}},
        {QStringLiteral("ghostscript-txtwrite-local"), {QStringLiteral("gswin64c.exe"), QStringLiteral("gswin32c.exe"), QStringLiteral("gs.exe")},
         {QStringLiteral("-q"), QStringLiteral("-dBATCH"), QStringLiteral("-dNOPAUSE"), QStringLiteral("-sDEVICE=txtwrite"), QStringLiteral("-sOutputFile=-"), filePath}},
        {QStringLiteral("pdftohtml-xml-local"), {QStringLiteral("pdftohtml.exe"), QStringLiteral("pdftohtml")},
         {QStringLiteral("-xml"), QStringLiteral("-stdout"), QStringLiteral("-enc"), QStringLiteral("UTF-8"), filePath}},
        {QStringLiteral("pdfium-pdfium_test-local"), {QStringLiteral("pdfium_test.exe")},
         {QStringLiteral("--txt"), filePath}}
    };

    for (const ToolAttempt& attempt : attempts) {
        const QStringList executables = candidatePdfRuntimeExecutables(appDir, attempt.names);
        appendInvoiceImportLog(QStringLiteral("local pdf text tool candidates | engine=%1 | candidates=%2")
                                   .arg(attempt.engine, executables.join(QStringLiteral(" || "))));
        for (const QString& exe : executables) {
            QProcess process;
            process.setProgram(exe);
            process.setArguments(attempt.args);
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            const QStringList runtimeDirs = pdfImportRuntimeToolSearchDirs(appDir);
            if (!runtimeDirs.isEmpty()) {
                const QString oldPath = env.value(QStringLiteral("PATH"));
                env.insert(QStringLiteral("PATH"), runtimeDirs.join(QDir::listSeparator()) + QDir::listSeparator() + oldPath);
            }
            process.setProcessEnvironment(env);
            const IncrementalImportProcessResult pr = runImportProcessIncrementally(process, 10000, 60000, QStringLiteral("pdf_local_runtime_text"));
            appendInvoiceImportLog(QStringLiteral("local pdf text tool finished | engine=%1 | exe=%2 | started=%3 | exitStatus=%4 | exitCode=%5 | stdout=%6 | stderr=%7")
                                       .arg(attempt.engine, exe)
                                       .arg(pr.started ? 1 : 0)
                                       .arg(int(pr.exitStatus))
                                       .arg(pr.exitCode)
                                       .arg(importProcessLogPreview(pr.stdOut, 400), importProcessLogPreview(pr.stdErr, 400)));
            if (!pr.started || pr.timedOut || pr.exitStatus != QProcess::NormalExit || pr.exitCode != 0) continue;
            const QString text = QString::fromUtf8(pr.stdOut).trimmed();
            if (!text.isEmpty()) {
                if (engineOut) *engineOut = attempt.engine;
                if (errorText) errorText->clear();
                return text;
            }
        }
    }
    if (errorText) *errorText = QString::fromUtf8("App-local Poppler/MuPDF рушії не знайдені або не повернули текст PDF.");
#else
    Q_UNUSED(filePath); Q_UNUSED(appDir); Q_UNUSED(engineOut); if (errorText) errorText->clear();
#endif
    return {};
}

static QString renderPdfPagesViaLocalOcrTools(const QString& filePath,
                                              const QString& targetDir,
                                              const QString& appDir,
                                              QString* engineOut = nullptr,
                                              QString* errorText = nullptr)
{
#if defined(Q_OS_WIN)
    if (rejectBlockingImportProcessOnUiThread(QStringLiteral("pdf_local_render_for_ocr"), errorText)) return {};
    QDir().mkpath(targetDir);
    if (!QDir(targetDir).exists()) {
        if (errorText) *errorText = QString::fromUtf8("Не вдалося створити тимчасову папку для app-local OCR render.");
        return {};
    }

    struct RenderAttempt { QString engine; QStringList names; QStringList args; };
    const QString ppmPrefix = QDir(targetDir).filePath(QStringLiteral("page"));
    const QString mutoolPattern = QDir(targetDir).filePath(QStringLiteral("page-%03d.png"));
    const QString gsPattern = QDir(targetDir).filePath(QStringLiteral("page-%03d.png"));
    const QList<RenderAttempt> attempts = {
        {QStringLiteral("pdftoppm-local-render"), {QStringLiteral("pdftoppm.exe"), QStringLiteral("pdftoppm")},
         {QStringLiteral("-r"), QStringLiteral("300"), QStringLiteral("-png"), filePath, ppmPrefix}},
        {QStringLiteral("mutool-local-render"), {QStringLiteral("mutool.exe"), QStringLiteral("mutool")},
         {QStringLiteral("draw"), QStringLiteral("-r"), QStringLiteral("300"), QStringLiteral("-o"), mutoolPattern, filePath}},
        {QStringLiteral("ghostscript-local-render"), {QStringLiteral("gswin64c.exe"), QStringLiteral("gswin32c.exe"), QStringLiteral("gs.exe")},
         {QStringLiteral("-q"), QStringLiteral("-dBATCH"), QStringLiteral("-dNOPAUSE"), QStringLiteral("-r300"), QStringLiteral("-sDEVICE=png16m"), QStringLiteral("-sOutputFile=") + gsPattern, filePath}}
    };

    for (const RenderAttempt& attempt : attempts) {
        const QStringList executables = candidatePdfRuntimeExecutables(appDir, attempt.names);
        appendInvoiceImportLog(QStringLiteral("local OCR render candidates | engine=%1 | candidates=%2")
                                   .arg(attempt.engine, executables.join(QStringLiteral(" || "))));
        for (const QString& exe : executables) {
            QProcess process;
            process.setProgram(exe);
            process.setArguments(attempt.args);
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            const QStringList runtimeDirs = pdfImportRuntimeToolSearchDirs(appDir);
            if (!runtimeDirs.isEmpty()) env.insert(QStringLiteral("PATH"), runtimeDirs.join(QDir::listSeparator()) + QDir::listSeparator() + env.value(QStringLiteral("PATH")));
            process.setProcessEnvironment(env);
            const IncrementalImportProcessResult pr = runImportProcessIncrementally(process, 10000, 120000, QStringLiteral("pdf_local_render_for_ocr"));
            const QFileInfoList images = QDir(targetDir).entryInfoList({QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"), QStringLiteral("*.tif"), QStringLiteral("*.tiff")}, QDir::Files, QDir::Name);
            appendInvoiceImportLog(QStringLiteral("local OCR render finished | engine=%1 | exe=%2 | started=%3 | exitStatus=%4 | exitCode=%5 | images=%6 | stderr=%7")
                                       .arg(attempt.engine, exe)
                                       .arg(pr.started ? 1 : 0)
                                       .arg(int(pr.exitStatus))
                                       .arg(pr.exitCode)
                                       .arg(images.size())
                                       .arg(importProcessLogPreview(pr.stdErr, 400)));
            if (pr.started && !pr.timedOut && pr.exitStatus == QProcess::NormalExit && pr.exitCode == 0 && !images.isEmpty()) {
                if (engineOut) *engineOut = attempt.engine;
                if (errorText) errorText->clear();
                return targetDir;
            }
        }
    }
    if (errorText) *errorText = QString::fromUtf8("App-local PDF render рушії для OCR не знайдені або не створили сторінки.");
#else
    Q_UNUSED(filePath); Q_UNUSED(targetDir); Q_UNUSED(appDir); Q_UNUSED(engineOut); if (errorText) errorText->clear();
#endif
    return {};
}

static QString extractPdfTextViaLocalTesseractOcr(const QString& renderedImageDir,
                                                  const QString& appDir,
                                                  QString* engineOut = nullptr,
                                                  QString* errorText = nullptr)
{
#if defined(Q_OS_WIN)
    if (renderedImageDir.trimmed().isEmpty() || !QDir(renderedImageDir).exists()) {
        if (errorText) *errorText = QString::fromUtf8("Немає підготовлених зображень сторінок PDF для app-local OCR.");
        return {};
    }
    if (rejectBlockingImportProcessOnUiThread(QStringLiteral("pdf_local_tesseract_ocr"), errorText)) return {};

    const QStringList tesseracts = candidatePdfRuntimeExecutables(appDir, {QStringLiteral("tesseract.exe"), QStringLiteral("tesseract")});
    appendInvoiceImportLog(QStringLiteral("local tesseract candidates | candidates=%1").arg(tesseracts.join(QStringLiteral(" || "))));
    if (tesseracts.isEmpty()) {
        if (errorText) *errorText = QString::fromUtf8("App-local Tesseract OCR не знайдено.");
        return {};
    }

    const QFileInfoList images = QDir(renderedImageDir).entryInfoList({QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"), QStringLiteral("*.tif"), QStringLiteral("*.tiff")},
                                                                       QDir::Files, QDir::Name);
    if (images.isEmpty()) {
        if (errorText) *errorText = QString::fromUtf8("App-local OCR не має зображень сторінок для розпізнавання.");
        return {};
    }

    const QString tessdata = firstExistingPdfRuntimeDir(appDir, {QStringLiteral("tessdata"), QStringLiteral("Tesseract-OCR/tessdata"), QStringLiteral("tesseract/tessdata"), QStringLiteral("tesseract/share/tessdata")});
    for (const QString& tesseract : tesseracts) {
        QStringList parts;
        for (const QFileInfo& img : images) {
            QProcess process;
            process.setProgram(tesseract);
            QStringList args{img.absoluteFilePath(), QStringLiteral("stdout")};
            if (!tessdata.isEmpty()) args << QStringLiteral("--tessdata-dir") << tessdata;
            args << QStringLiteral("-l") << QStringLiteral("ukr+rus+eng") << QStringLiteral("--psm") << QStringLiteral("6");
            process.setArguments(args);
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            const QStringList runtimeDirs = pdfImportRuntimeToolSearchDirs(appDir);
            if (!runtimeDirs.isEmpty()) env.insert(QStringLiteral("PATH"), runtimeDirs.join(QDir::listSeparator()) + QDir::listSeparator() + env.value(QStringLiteral("PATH")));
            if (!tessdata.isEmpty()) env.insert(QStringLiteral("TESSDATA_PREFIX"), QFileInfo(tessdata).absolutePath());
            process.setProcessEnvironment(env);
            const IncrementalImportProcessResult pr = runImportProcessIncrementally(process, 10000, 90000, QStringLiteral("pdf_local_tesseract_ocr"));
            appendInvoiceImportLog(QStringLiteral("local tesseract page finished | exe=%1 | image=%2 | started=%3 | exitStatus=%4 | exitCode=%5 | stdout=%6 | stderr=%7")
                                       .arg(tesseract, img.fileName())
                                       .arg(pr.started ? 1 : 0)
                                       .arg(int(pr.exitStatus))
                                       .arg(pr.exitCode)
                                       .arg(importProcessLogPreview(pr.stdOut, 300), importProcessLogPreview(pr.stdErr, 300)));
            if (pr.started && !pr.timedOut && pr.exitStatus == QProcess::NormalExit && pr.exitCode == 0) {
                const QString pageText = QString::fromUtf8(pr.stdOut).trimmed();
                if (!pageText.isEmpty()) parts << pageText;
            }
        }
        const QString text = parts.join(QStringLiteral("\n"));
        if (!text.trimmed().isEmpty()) {
            if (engineOut) *engineOut = QStringLiteral("tesseract-local-qt-render");
            if (errorText) errorText->clear();
            return text;
        }
    }
    if (errorText) *errorText = QString::fromUtf8("App-local Tesseract OCR не повернув текст PDF.");
#else
    Q_UNUSED(renderedImageDir); Q_UNUSED(appDir); Q_UNUSED(engineOut); if (errorText) errorText->clear();
#endif
    return {};
}

static QStringList candidatePythonExecutablesForPdfImport(const QString& appDir)
{
    QStringList candidates;
    auto add = [&](const QString& path) {
        if (path.trimmed().isEmpty()) return;
        const QString clean = QDir::toNativeSeparators(QDir::cleanPath(path));
        if (!QFileInfo::exists(clean)) return;
        if (clean.toLower().contains(QStringLiteral("\\windowsapps\\"))) return;
        if (!candidates.contains(clean)) candidates << clean;
    };

    // First check the application-private OCR/runtime layer. This keeps invoice import
    // stable after installation even when the user's PATH has no Python or has a
    // Microsoft Store shim that cannot run non-interactively.
    for (const QString& root : pdfImportRuntimeRoots(appDir)) {
        if (root.trimmed().isEmpty()) continue;
        add(QDir(root).filePath(QStringLiteral("python/python.exe")));
