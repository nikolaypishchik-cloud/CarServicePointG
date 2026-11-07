// Extracted from InvoiceImportPersistenceStage.ipp to keep each import fragment within the monolith gate line budget.
static QString normalizeImportedQtyText(QString raw, const ParseLooseNumberFn& parseLooseNumber)
{
    raw = raw.trimmed();
    if (raw.isEmpty()) return QString();
    bool ok = false;
    const double value = parseLooseNumber(raw, &ok);
    return ok && value > 0.0 ? FormatUtils::smartQty(value, 2) : QString();
}

static QString normalizeImportedMoneyText(QString raw,
                                          const ParseLooseNumberFn& parseLooseNumber,
                                          const MoneyTextFn& moneyText)
{
    raw = raw.trimmed();
    if (raw.isEmpty()) return QString();
    bool ok = false;
    const double value = parseLooseNumber(raw, &ok);
    return ok && value >= 0.0 ? moneyText(value) : QString();
}

static QString normalizedImportCellDateDisplay(QString raw)
{
    raw = raw.trimmed();
    if (raw.isEmpty()) return QString();

    const QString direct = PartImportTextSupport::normalizeImportedDateDisplay(raw).trimmed();
    if (!direct.isEmpty() && direct != raw) return direct;

    const QString iso = normalizeInvoiceMetaDateToIso(raw);
    if (!iso.isEmpty()) {
        const QDate parsed = DateFormats::parseDate(iso);
        if (parsed.isValid()) return parsed.toString(QStringLiteral("dd.MM.yy"));
    }

    QString embeddedIso = extractInvoiceMetaDateFromText(raw);
    if (embeddedIso.isEmpty()) {
        static const QRegularExpression kAnyDateRx(
            QStringLiteral(R"((\d{1,2}[./-]\d{1,2}[./-]\d{2,4})|(\d{4}[./-]\d{1,2}[./-]\d{1,2}))"));
        const QRegularExpressionMatch match = kAnyDateRx.match(raw);
        if (match.hasMatch()) {
            const QString candidate = match.captured(1).isEmpty() ? match.captured(2) : match.captured(1);
            embeddedIso = normalizeInvoiceMetaDateToIso(candidate);
        }
    }
    if (!embeddedIso.isEmpty()) {
        const QDate parsed = DateFormats::parseDate(embeddedIso);
        if (parsed.isValid()) return parsed.toString(QStringLiteral("dd.MM.yy"));
    }

    static const QRegularExpression kDisplayDateRx(
        QStringLiteral(R"(^\d{1,2}[./-]\d{1,2}[./-](?:\d{2}|\d{4})$)"));
    if (direct.contains(kDisplayDateRx)) return direct;
    return QString();
}

static bool isExcelImportFooterRow(const QStringList& row)
{
    QString joined;
    for (const QString& cell : row) {
        if (!joined.isEmpty()) joined += QLatin1Char(' ');
        joined += cell.trimmed();
    }

    const QString normalized = normalizedImportHeader(joined);
    if (normalized.isEmpty()) return false;

    return normalized.contains(QStringLiteral("всьогодосплати")) ||
           normalized.contains(QStringLiteral("всегодосплате")) ||
           normalized.contains(QStringLiteral("відпустив")) ||
           normalized.contains(QStringLiteral("отпустил")) ||
           normalized.contains(QStringLiteral("отримав")) ||
           normalized.contains(QStringLiteral("получил")) ||
           normalized.contains(QStringLiteral("своїмпідписом")) ||
           normalized.contains(QStringLiteral("своейподписью")) ||
           normalized.contains(QStringLiteral("замовникпідтверджує")) ||
           normalized.contains(QStringLiteral("заказчикподтверждает"));
}

static bool parseInvoiceImportNumber(QString raw,
                                     const ParseLooseNumberFn& parseLooseNumber,
                                     double* valueOut)
{
    raw = raw.trimmed();
    if (raw.isEmpty()) return false;
    bool ok = false;
    const double value = parseLooseNumber(raw, &ok);
    if (!ok || !std::isfinite(value)) return false;
    if (valueOut) *valueOut = value;
    return true;
}

static bool invoiceAmountMatchesVatPrice(double qty, double priceWithVat, double totalWithVat)
{
    if (!std::isfinite(qty) || !std::isfinite(priceWithVat) || !std::isfinite(totalWithVat)) return false;
    if (qty <= 0.0 || priceWithVat < 0.0 || totalWithVat < 0.0) return false;
    const double expected = qty * priceWithVat;
    const double tolerance = qMax(0.05, qMax(std::fabs(totalWithVat), std::fabs(expected)) * 0.035);
    return std::fabs(expected - totalWithVat) <= tolerance;
}

struct InvoiceColumnConfidenceReport {
    int supplierScore = 0;
    int headerScore = 0;
    int dataTypeScore = 0;
    int numericConsistencyScore = 0;
    int vatPriceScore = 0;
    int totalConfidence = 0;
    int inspectedRows = 0;
    int amountCheckedRows = 0;
    int amountMatchedRows = 0;
    bool automaticAllowed = false;
    QStringList warnings;
};

static int evaluateInvoiceSupplierScore(const QList<QStringList>& rows, const ExcelImportColumnMap& map)
{
    const QString supplierName = detectInvoiceSourceStore(rows).trimmed();
    const QString supplierKey = supplierInvoiceTemplateKey(supplierName);
    if (!supplierKey.isEmpty()) {
        if (supplierKey == QStringLiteral("avtonova_d") ||
            supplierKey == QStringLiteral("elit") ||
            supplierKey == QStringLiteral("unik_trade")) {
            return 100;
        }
        return 85;
    }
    return map.store >= 0 ? 65 : 0;
}

static InvoiceColumnConfidenceReport evaluateInvoiceColumnConfidence(const QList<QStringList>& rows,
                                                                     int headerRow,
                                                                     const ExcelImportColumnMap& map,
                                                                     const ParseLooseNumberFn& parseLooseNumber)
{
    InvoiceColumnConfidenceReport report;
    report.supplierScore = evaluateInvoiceSupplierScore(rows, map);
    report.vatPriceScore = map.hasVatConfirmedPrice() && isVatSafePriceColumn(rows, headerRow, map) ? 100 : 0;
    report.headerScore = qBound(0, map.headerScore * 5, 100);

    int usableRows = 0;
    int nameRows = 0;
    int qtyRows = 0;
    int priceRows = 0;
    int totalRows = 0;
    int totalMatches = 0;

    const int startRow = headerRow >= 0 ? headerRow + 1 : 0;
    for (int rowIndex = qMax(0, startRow); rowIndex < rows.size() && usableRows < 160; ++rowIndex) {
        const QStringList& row = rows.at(rowIndex);
        if (isExcelImportFooterRow(row)) continue;

        const QString name = valueAtOrEmpty(row, map.name);
        const QString oem = valueAtOrEmpty(row, map.oem);
        const QString qtyText = valueAtOrEmpty(row, map.qty);
        const QString priceText = valueAtOrEmpty(row, map.price);
        const QString amountText = valueAtOrEmpty(row, map.amount);
        if (name.trimmed().isEmpty() && oem.trimmed().isEmpty() && qtyText.trimmed().isEmpty() && priceText.trimmed().isEmpty()) continue;

        ++usableRows;
        if (!name.trimmed().isEmpty()) ++nameRows;

        double qty = 0.0;
        double price = 0.0;
        double amount = 0.0;
        const bool qtyOk = parseInvoiceImportNumber(qtyText, parseLooseNumber, &qty) && qty > 0.0;
        const bool priceOk = parseInvoiceImportNumber(priceText, parseLooseNumber, &price) && price >= 0.0;
        const bool amountOk = map.amount >= 0 && parseInvoiceImportNumber(amountText, parseLooseNumber, &amount) && amount >= 0.0;
        if (qtyOk) ++qtyRows;
        if (priceOk) ++priceRows;
        if (amountOk) ++totalRows;
        if (qtyOk && priceOk && amountOk && invoiceAmountMatchesVatPrice(qty, price, amount)) ++totalMatches;
    }

    report.inspectedRows = usableRows;
    report.amountCheckedRows = totalRows;
    report.amountMatchedRows = totalMatches;
    if (usableRows > 0) {
        const int nameScore = (nameRows * 100) / usableRows;
        const int qtyScore = (qtyRows * 100) / usableRows;
        const int priceScore = (priceRows * 100) / usableRows;
        report.dataTypeScore = qBound(0, (nameScore + qtyScore + priceScore) / 3, 100);
    }

    if (totalRows > 0) {
        report.numericConsistencyScore = qBound(0, (totalMatches * 100) / totalRows, 100);
    } else {
        report.numericConsistencyScore = (usableRows > 0 && qtyRows > 0 && priceRows > 0) ? 70 : 0;
    }

    report.totalConfidence = qBound(0,
                                    (report.vatPriceScore * 35 +
                                     report.supplierScore * 10 +
                                     report.headerScore * 15 +
                                     report.dataTypeScore * 20 +
                                     report.numericConsistencyScore * 20) / 100,
                                    100);

    if (report.vatPriceScore < 100) {
        report.warnings << QString::fromUtf8("не підтверджено колонку ціни саме з ПДВ");
    }
    if (report.supplierScore <= 0) {
        report.warnings << QString::fromUtf8("постачальника не визначено автоматично");
    }
    if (usableRows <= 0) {
        report.warnings << QString::fromUtf8("не знайдено рядків запчастин для перевірки");
    }
    if (report.dataTypeScore < 70) {
        report.warnings << QString::fromUtf8("низька впевненість у типах колонок");
    }
    if (totalRows > 0 && report.numericConsistencyScore < 65) {
        report.warnings << QString::fromUtf8("сума з ПДВ не сходиться з кількістю та ціною з ПДВ");
    }

    report.automaticAllowed = report.vatPriceScore == 100 &&
                              report.totalConfidence >= 85 &&
                              report.dataTypeScore >= 70 &&
                              report.numericConsistencyScore >= 65;
    return report;
}

static QString invoiceColumnConfidenceErrorText(const InvoiceColumnConfidenceReport& report)
{
    QString message = QString::fromUtf8("Не вдалося автоматично підтвердити імпорт накладної. Для імпорту запчастин обов'язкова ціна саме з ПДВ.");
    message += QString::fromUtf8("\nВпевненість: %1%. Постачальник: %2%. Заголовки: %3%. Дані: %4%. Перевірка суми: %5%.")
                   .arg(report.totalConfidence)
                   .arg(report.supplierScore)
                   .arg(report.headerScore)
                   .arg(report.dataTypeScore)
                   .arg(report.numericConsistencyScore);
    if (!report.warnings.isEmpty()) {
        message += QString::fromUtf8("\nПричини: %1.").arg(report.warnings.join(QStringLiteral("; ")));
    }
    message += QString::fromUtf8("\nВідкрийте попередній перегляд і вручну підтвердьте колонку 'Ціна з ПДВ' або додайте шаблон постачальника.");
    return message;
}



static InvoiceColumnMappingPreview buildInvoiceColumnMappingPreview(const QList<QStringList>& rows,
                                                                    int headerRow,
                                                                    const ExcelImportColumnMap& map,
                                                                    const InvoiceColumnConfidenceReport& confidence)
{
    InvoiceColumnMappingPreview preview;
    preview.headerRow = headerRow;
    preview.dateColumn = map.date;
    preview.articleColumn = map.oem;
    preview.partNameColumn = map.name;
    preview.brandColumn = map.brand;
    preview.quantityColumn = map.qty;
    preview.priceWithVatColumn = map.price;
    preview.totalWithVatColumn = map.amount;
    preview.supplierColumn = map.store;
    preview.supplierScore = confidence.supplierScore;
    preview.headerScore = confidence.headerScore;
    preview.dataTypeScore = confidence.dataTypeScore;
    preview.numericConsistencyScore = confidence.numericConsistencyScore;
    preview.vatPriceScore = confidence.vatPriceScore;
    preview.totalConfidence = confidence.totalConfidence;
    preview.priceWithVatConfirmed = map.hasVatConfirmedPrice() && isVatSafePriceColumn(rows, headerRow, map);
    preview.automaticAllowed = confidence.automaticAllowed;
    preview.manualPreviewRequired = !confidence.automaticAllowed;
    preview.manualColumnSelectionRequired = confidence.totalConfidence < 60 || !preview.priceWithVatConfirmed;
    preview.supplierDisplayName = detectInvoiceSourceStore(rows);
    preview.supplierKey = supplierInvoiceTemplateKey(preview.supplierDisplayName);
    preview.warnings = confidence.warnings;
    const int documentDateConfidence = map.date >= 0 ? confidence.headerScore : 0;
    const int partNameConfidence = map.name >= 0 ? confidence.dataTypeScore : 0;
    const int articleConfidence = map.oem >= 0 ? confidence.headerScore : 0;
    const int brandConfidence = map.brand >= 0 ? confidence.headerScore : 0;
    const int quantityConfidence = map.qty >= 0 ? confidence.dataTypeScore : 0;
    const int priceConfidence = preview.priceWithVatConfirmed ? confidence.vatPriceScore : 0;
    const int totalConfidence = map.amount >= 0 ? confidence.numericConsistencyScore : 0;
    const int supplierConfidence = map.store >= 0 ? qMax(confidence.supplierScore, confidence.headerScore) : confidence.supplierScore;

    preview.columnConfidence.insert(QStringLiteral("documentDate"), documentDateConfidence);
    preview.columnConfidence.insert(QStringLiteral("partName"), partNameConfidence);
    preview.columnConfidence.insert(QStringLiteral("article"), articleConfidence);
    preview.columnConfidence.insert(QStringLiteral("brand"), brandConfidence);
    preview.columnConfidence.insert(QStringLiteral("quantity"), quantityConfidence);
    preview.columnConfidence.insert(QStringLiteral("priceWithVat"), priceConfidence);
    preview.columnConfidence.insert(QStringLiteral("totalWithVat"), totalConfidence);
    preview.columnConfidence.insert(QStringLiteral("supplierName"), supplierConfidence);

    auto addPhysicalConfidence = [&preview](int column, int score, const QString& warning = QString()) {
        if (column < 0) return;
        const int bounded = qBound(0, score, 100);
        if (!preview.physicalColumnConfidence.contains(column) || bounded > preview.physicalColumnConfidence.value(column)) {
            preview.physicalColumnConfidence.insert(column, bounded);
        }
        if (!warning.trimmed().isEmpty()) {
            preview.physicalColumnWarnings.insert(column, warning.trimmed());
        }
    };

    addPhysicalConfidence(map.date, documentDateConfidence);
    addPhysicalConfidence(map.oem, articleConfidence);
    addPhysicalConfidence(map.name, partNameConfidence,
                          partNameConfidence < 60 ? QString::fromUtf8("сумнівна або відсутня назва запчастини") : QString());
    addPhysicalConfidence(map.brand, brandConfidence);
    addPhysicalConfidence(map.qty, quantityConfidence,
                          quantityConfidence < 60 ? QString::fromUtf8("сумнівна колонка кількості") : QString());
    addPhysicalConfidence(map.amount, totalConfidence,
                          (map.amount >= 0 && totalConfidence < 65) ? QString::fromUtf8("сума з ПДВ не підтверджена формулою") : QString());
    addPhysicalConfidence(map.store, supplierConfidence);

    if (map.price >= 0) {
        addPhysicalConfidence(map.price, preview.priceWithVatConfirmed ? confidence.vatPriceScore : 25,
                              preview.priceWithVatConfirmed ? QString() : QString::fromUtf8("потрібне ручне підтвердження ціни з ПДВ"));
    }
    if (!preview.priceWithVatConfirmed) {
        preview.warnings << QString::fromUtf8("колонка ціни має бути вручну підтверджена як 'Ціна з ПДВ'");
    }
    if (map.name < 0) {
        preview.warnings << QString::fromUtf8("не знайдено обов'язкову колонку назви запчастини");
    }
    return preview;
}

static ExcelImportColumnMap excelImportMapFromConfirmedPreview(const InvoiceColumnMappingPreview& preview)
{
    ExcelImportColumnMap map;
    map.date = preview.dateColumn;
    map.oem = preview.articleColumn;
    map.name = preview.partNameColumn;
    map.brand = preview.brandColumn;
    map.qty = preview.quantityColumn;
    map.price = preview.priceWithVatColumn;
    map.amount = preview.totalWithVatColumn;
    map.store = preview.supplierColumn;
    map.headerScore = qMax(0, preview.headerScore);
    map.priceHeaderScore = qMax(0, preview.vatPriceScore);
    map.amountHeaderScore = qMax(0, preview.numericConsistencyScore);
    map.priceVatExplicit = preview.priceWithVatConfirmed;
    map.priceVatExcludedCandidate = false;
    map.priceFromSupplierTemplate = false;
    map.amountFromSupplierTemplate = preview.totalWithVatColumn >= 0;
    map.detectionSupplierKey = preview.supplierKey;
    return map;
}

static void persistSupplierInvoiceTemplateFromManualConfirmation(const QList<QStringList>& rows,
                                                                 const InvoiceColumnMappingPreview& preview)
{
    if (!preview.priceWithVatConfirmed || preview.priceWithVatColumn < 0 || preview.quantityColumn < 0 ||
        preview.partNameColumn < 0) {
        return;
    }
    ExcelImportColumnMap map = excelImportMapFromConfirmedPreview(preview);
    const int confidence = qMax(85, preview.totalConfidence);
    persistSupplierInvoiceTemplateIfUseful(rows, map, confidence);
}

InvoiceColumnMappingPreview detectInvoiceColumnMappingPreview(const QList<QStringList>& rows,
                                                            const QSet<int>& highlightedColumns,
                                                            const ParseLooseNumberFn& parseLooseNumber)
{
    int headerRow = -1;
    ExcelImportColumnMap map = detectExcelImportColumns(rows, &headerRow, highlightedColumns);
    const InvoiceColumnConfidenceReport confidence = evaluateInvoiceColumnConfidence(rows, headerRow, map, parseLooseNumber);
    return buildInvoiceColumnMappingPreview(rows, headerRow, map, confidence);
}

QList<ImportedPartDraft> parseImportedPartDraftsWithConfirmedVatMapping(const QList<QStringList>& rows,
                                                                        const InvoiceColumnMappingPreview& confirmedMapping,
                                                                        const NormalizeTextFn& normalizeSentence,
                                                                        const ParseLooseNumberFn& parseLooseNumber,
                                                                        const MoneyTextFn& moneyText,
                                                                        QString* errorText,
                                                                        const QString& documentDate)
{
    QList<ImportedPartDraft> result;
    if (rows.isEmpty()) {
        if (errorText) *errorText = QString::fromUtf8("Файл не містить рядків для імпорту.");
        return result;
    }
    if (!confirmedMapping.priceWithVatConfirmed || confirmedMapping.priceWithVatColumn < 0) {
        if (errorText) *errorText = QString::fromUtf8("Ручний імпорт заблоковано: підтвердьте колонку 'Ціна з ПДВ'.");
        return result;
    }

    ExcelImportColumnMap map = excelImportMapFromConfirmedPreview(confirmedMapping);
    if (!map.hasCoreColumns() || map.qty < 0) {
        if (errorText) *errorText = QString::fromUtf8("Ручний mapping неповний: потрібні назва запчастини, кількість і ціна з ПДВ.");
        return result;
    }

    if (!confirmedMapping.templateKey.isEmpty()
        && !InvoiceRecognitionProfiles::validateRows(rows, confirmedMapping, parseLooseNumber, errorText)) return result;
    const QString fallbackStore = confirmedMapping.supplierDisplayName.trimmed().isEmpty()
        ? detectInvoiceSourceStore(rows) : confirmedMapping.supplierDisplayName.trimmed();
    const int startRow = confirmedMapping.headerRow >= 0 ? confirmedMapping.headerRow + 1 : 0;
    for (int rowIndex = qMax(0, startRow); rowIndex < rows.size(); ++rowIndex) {
        const QStringList& row = rows.at(rowIndex);
        if (isExcelImportFooterRow(row)) continue;
        const QString name = PartImportTextSupport::normalizePartName(valueAtOrEmpty(row, map.name));
        const QString oem = valueAtOrEmpty(row, map.oem);
        const QString store = normalizeSentence(valueAtOrEmpty(row, map.store));
        const QString brandName = normalizeSentence(valueAtOrEmpty(row, map.brand));
        const QString qtyRaw = valueAtOrEmpty(row, map.qty);
        const QString priceRaw = valueAtOrEmpty(row, map.price);
        const QString amountRaw = valueAtOrEmpty(row, map.amount);
        const QString dateRaw = valueAtOrEmpty(row, map.date);
        const bool emptyRow = name.isEmpty() && oem.isEmpty() && brandName.isEmpty() && store.isEmpty() && qtyRaw.isEmpty() && priceRaw.isEmpty() && amountRaw.isEmpty();
        if (emptyRow) continue;

        if (map.amount >= 0) {
            double qtyValue = 0.0;
            double priceValue = 0.0;
            double amountValue = 0.0;
            const bool qtyOkForCheck = parseInvoiceImportNumber(qtyRaw, parseLooseNumber, &qtyValue);
            const bool priceOkForCheck = parseInvoiceImportNumber(priceRaw, parseLooseNumber, &priceValue);
            const bool amountOkForCheck = parseInvoiceImportNumber(amountRaw, parseLooseNumber, &amountValue);
            if (qtyOkForCheck && priceOkForCheck && amountOkForCheck &&
                !invoiceAmountMatchesVatPrice(qtyValue, priceValue, amountValue)) {
                continue;
            }
        }

        ImportedPartDraft draft;
        draft.orderedAt = normalizedImportCellDateDisplay(dateRaw);
        if (draft.orderedAt.isEmpty()) draft.orderedAt = normalizedImportCellDateDisplay(documentDate);
        draft.oemNumber = oem;
        draft.name = name;
        draft.brandName = brandName;
        if (confirmedMapping.splitCombinedDescription) {
            InvoiceRecognitionProfiles::splitDescription(valueAtOrEmpty(row, map.name), &draft);
            draft.name = PartImportTextSupport::normalizePartName(draft.name);
        }
        draft.qtyText = normalizeImportedQtyText(qtyRaw, parseLooseNumber);
        draft.priceText = normalizeImportedMoneyText(priceRaw, parseLooseNumber, moneyText);
        draft.amountText = normalizeImportedMoneyText(amountRaw, parseLooseNumber, moneyText);
        draft.store = store.isEmpty() ? fallbackStore : store;
        if (draft.amountText.isEmpty() && !draft.qtyText.trimmed().isEmpty() && !draft.priceText.trimmed().isEmpty()) {
            bool qtyOk = false;
            bool priceOk = false;
            const double qty = parseLooseNumber(draft.qtyText, &qtyOk);
            const double price = parseLooseNumber(draft.priceText, &priceOk);
            if (qtyOk && priceOk) draft.amountText = moneyText(qMax(0.0, qty) * qMax(0.0, price));
        }
        if (draft.name.isEmpty()) continue;
        if (draft.qtyText.trimmed().isEmpty() || draft.priceText.trimmed().isEmpty()) continue;
        result.append(draft);
    }
    if (result.isEmpty()) {
        if (errorText) *errorText = QString::fromUtf8("Після ручного mapping не знайдено коректних рядків з ціною з ПДВ.");
        return result;
    }
    if (!confirmedMapping.templateApplied) {
        if (!InvoiceRecognitionProfiles::save(rows, confirmedMapping, errorText)) return {};
        persistSupplierInvoiceTemplateFromManualConfirmation(rows, confirmedMapping);
    }
    return result;
}

QList<ImportedPartDraft> parseImportedPartDrafts(const QList<QStringList>& rows,
                                                 const QSet<int>& highlightedColumns,
                                                 const NormalizeTextFn& normalizeSentence,
                                                 const ParseLooseNumberFn& parseLooseNumber,
                                                 const MoneyTextFn& moneyText,
                                                 QString* errorText,
                                                 const QString& documentDate)
{
    QList<ImportedPartDraft> result;
    if (rows.isEmpty()) {
        if (errorText) *errorText = QString::fromUtf8("Файл Excel/PDF не містить рядків для імпорту.");
        return result;
    }

    int headerRow = -1;
    const ExcelImportColumnMap map = detectExcelImportColumns(rows, &headerRow, highlightedColumns);
    const InvoiceColumnConfidenceReport columnConfidence = evaluateInvoiceColumnConfidence(rows, headerRow, map, parseLooseNumber);
    if (!map.hasCoreColumns()) {
        if (errorText) *errorText = QString::fromUtf8("Не вдалося визначити колонки Excel/PDF для імпорту запчастин.");
        return result;
    }
    if (!columnConfidence.automaticAllowed) {
        if (errorText) *errorText = invoiceColumnConfidenceErrorText(columnConfidence);
        return result;
    }
    if (!map.hasVatConfirmedPrice() || !isVatSafePriceColumn(rows, headerRow, map)) {
        if (errorText) *errorText = QString::fromUtf8("Імпорт заблоковано: для запчастин потрібна підтверджена колонка 'Ціна з ПДВ'.");
        return result;
    }

    const QString fallbackStore = detectInvoiceSourceStore(rows);
    const int startRow = headerRow >= 0 ? headerRow + 1 : 0;
    for (int rowIndex = qMax(0, startRow); rowIndex < rows.size(); ++rowIndex) {
        const QStringList& row = rows.at(rowIndex);
        if (isExcelImportFooterRow(row)) continue;

        const QString name = PartImportTextSupport::normalizePartName(valueAtOrEmpty(row, map.name));
        const QString oem = valueAtOrEmpty(row, map.oem);
        const QString store = normalizeSentence(valueAtOrEmpty(row, map.store));
        const QString brandName = normalizeSentence(valueAtOrEmpty(row, map.brand));
        const QString qtyRaw = valueAtOrEmpty(row, map.qty);
        const QString priceRaw = valueAtOrEmpty(row, map.price);
        const QString amountRaw = valueAtOrEmpty(row, map.amount);
        const QString dateRaw = valueAtOrEmpty(row, map.date);
        const bool emptyRow = name.isEmpty() && oem.isEmpty() && brandName.isEmpty() && store.isEmpty() &&
                              qtyRaw.isEmpty() && priceRaw.isEmpty() && amountRaw.isEmpty();
        if (emptyRow) continue;

        if (map.amount >= 0) {
            double qtyValue = 0.0;
            double priceValue = 0.0;
            double amountValue = 0.0;
            const bool qtyOkForCheck = parseInvoiceImportNumber(qtyRaw, parseLooseNumber, &qtyValue);
            const bool priceOkForCheck = parseInvoiceImportNumber(priceRaw, parseLooseNumber, &priceValue);
            const bool amountOkForCheck = parseInvoiceImportNumber(amountRaw, parseLooseNumber, &amountValue);
            if (qtyOkForCheck && priceOkForCheck && amountOkForCheck &&
                !invoiceAmountMatchesVatPrice(qtyValue, priceValue, amountValue)) {
                continue;
            }
        }

        ImportedPartDraft draft;
        draft.orderedAt = normalizedImportCellDateDisplay(dateRaw);
        if (draft.orderedAt.isEmpty()) draft.orderedAt = normalizedImportCellDateDisplay(documentDate);
        draft.oemNumber = oem;
        draft.name = name;
        draft.brandName = brandName;
        InvoiceRecognitionProfiles::splitDescription(valueAtOrEmpty(row, map.name), &draft);
        draft.name = PartImportTextSupport::normalizePartName(draft.name);
        draft.qtyText = normalizeImportedQtyText(qtyRaw, parseLooseNumber);
        draft.priceText = normalizeImportedMoneyText(priceRaw, parseLooseNumber, moneyText);
        draft.amountText = normalizeImportedMoneyText(amountRaw, parseLooseNumber, moneyText);
        draft.store = store.isEmpty() ? fallbackStore : store;

        if (draft.amountText.isEmpty() && !draft.qtyText.trimmed().isEmpty() && !draft.priceText.trimmed().isEmpty()) {
            bool qtyOk = false;
            bool priceOk = false;
            const double qty = parseLooseNumber(draft.qtyText, &qtyOk);
            const double price = parseLooseNumber(draft.priceText, &priceOk);
            if (qtyOk && priceOk) draft.amountText = moneyText(qMax(0.0, qty) * qMax(0.0, price));
        }

        if (draft.name.isEmpty()) continue;
        if (draft.qtyText.trimmed().isEmpty() || draft.priceText.trimmed().isEmpty()) continue;
        result.append(draft);
    }

    if (result.isEmpty()) {
        if (errorText) *errorText = QString::fromUtf8("Не знайдено коректних рядків з підтвердженою ціною з ПДВ.");
        return result;
    }

    persistSupplierInvoiceTemplateIfUseful(rows, map, columnConfidence.totalConfidence);
    return result;
}
