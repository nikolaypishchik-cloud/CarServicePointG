// Extracted from src/ui/tabs/orders/WorkPartsDialogSupport.cpp for P1/P2 monolith reduction.
// Included exactly once by the original translation unit/header; do not compile separately.

                                      int row,
                                      int col,
                                      int orderNumber,
                                      const QString& orderUid,
                                      const QString& extra)
{
    QJsonObject root;
    root.insert(QStringLiteral("stage"), stage);
    root.insert(QStringLiteral("orderNumber"), orderNumber);
    root.insert(QStringLiteral("orderUid"), orderUid);
    root.insert(QStringLiteral("row"), row);
    root.insert(QStringLiteral("col"), col);
    root.insert(QStringLiteral("partId"), partCrashCellId(table, row));
    root.insert(QStringLiteral("imported"), partRowHasImportedInvoiceMarker(table, row));
    root.insert(QStringLiteral("traceId"), importedPartTraceId(table, row));
    root.insert(QStringLiteral("invoiceNumber"), partCrashCellText(table, row, PartColInvoiceNumber));
    root.insert(QStringLiteral("invoiceDate"), partCrashCellDataText(table, row, PartColAttachment, RoleAttachmentDocumentDate));
    root.insert(QStringLiteral("orderedAt"), partCrashCellText(table, row, PartColOrderedAt));
    root.insert(QStringLiteral("deliveryDate"), partCrashCellDataText(table, row, PartColDelivered, RoleDeliveryDate));
    root.insert(QStringLiteral("store"), partCrashCellText(table, row, PartColStore));
    root.insert(QStringLiteral("brand"), partManufacturerText(table, row));
    root.insert(QStringLiteral("oem"), partCrashCellText(table, row, PartColOem));
    root.insert(QStringLiteral("name"), partCrashCellText(table, row, PartColName));
    root.insert(QStringLiteral("qty"), partCrashCellText(table, row, PartColQty));
    root.insert(QStringLiteral("price"), partCrashCellText(table, row, PartColPrice));
    root.insert(QStringLiteral("threadId"), QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId())));
    root.insert(QStringLiteral("extra"), extra);

    const QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/logs/crash_snapshots");
    QDir().mkpath(baseDir);
    const QString fileName = QStringLiteral("part_%1_order_%2_row_%3_%4.json")
        .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz")))
        .arg(orderNumber)
        .arg(row)
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
    const QString path = baseDir + QLatin1Char('/') + fileName;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning().noquote() << QStringLiteral("CRASH_GUARD snapshot_open_failed path=") << path
                             << QStringLiteral(" error=") << file.errorString();
        return QString();
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        qWarning().noquote() << QStringLiteral("CRASH_GUARD snapshot_commit_failed path=") << path
                             << QStringLiteral(" error=") << file.errorString();
        return QString();
    }
    qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("CRASH_GUARD snapshot_written path=") << path
                      << QStringLiteral(" stage=") << stage
                      << QStringLiteral(" order=") << orderNumber
                      << QStringLiteral(" row=") << row;
    return path;
}

bool partRowHasImportedInvoiceMarker(QTableWidget* table, int row)
{
    if (!table || row < 0 || row >= table->rowCount()) return false;
    for (int col : {PartColName, PartColOrderedAt, PartColOem, PartColQty, PartColPrice, PartColStore, PartColBrand, PartColInvoiceNumber, PartColAttachment}) {
        if (auto* item = table->item(row, col)) {
            if (item->data(RoleImportedInvoiceRow).toBool()) return true;
        }
    }
    return false;
}

void markImportedInvoiceRow(QTableWidget* table, int row, const QString& traceId)
{
    if (!table || row < 0 || row >= table->rowCount()) return;
    const QSignalBlocker blocker(table);
    for (int col = 0; col < table->columnCount(); ++col) {
        if (auto* item = table->item(row, col)) {
            item->setData(RoleImportedInvoiceRow, true);
            item->setData(RoleImportedInvoiceTrace, traceId);
        }
    }
}

void applyImportedCellValueLikeManualInput(QTableWidget* table, int row, int col, const QString& rawValue)
{
    if (!table || row < 0 || row >= table->rowCount()) return;
    QTableWidgetItem* item = table->item(row, col);
    if (!item) return;

    QString value = rawValue.trimmed();
    if (col == PartColName) value = normalizePartSentence(value);
    else if (col == PartColStore || col == PartColBrand) value = normalizeSentence(value);
    else if (col == PartColOrderedAt && !value.isEmpty()) value = displayDateFromStored(storedDateFromDisplay(value));

    {
        const QSignalBlocker blocker(table);
        item->setData(RoleIsHint, false);
        item->setText(value);
    }

    if (col == PartColBrand || col == PartColName) {
        syncPartManufacturerRoleFromRow(table, row);
    }
    if (col == PartColQty || col == PartColName || col == PartColStore || col == PartColBrand) {
        refreshPartRowClientPricing(table, row, col);
    }
}

static QDate parseUiDisplayDate(QString raw)
{
    raw = raw.trimmed();
    if (raw.isEmpty()) return {};
    QDate d = DateFormats::parseDate(raw);
    if (d.isValid()) return d;
    d = QDate::fromString(raw, QStringLiteral("dd.MM.yyyy"));
    if (d.isValid()) return d;
    d = QDate::fromString(raw, QStringLiteral("dd/MM/yyyy"));
    if (d.isValid()) return d;

    const QChar sep = raw.contains(QLatin1Char('.')) ? QLatin1Char('.') : (raw.contains(QLatin1Char('/')) ? QLatin1Char('/') : QChar());
    if (!sep.isNull()) {
        const QStringList parts = raw.split(sep);
        if (parts.size() == 3) {
            bool okDay = false;
            bool okMonth = false;
            bool okYear = false;
            const int day = parts.at(0).toInt(&okDay);
            const int month = parts.at(1).toInt(&okMonth);
            int year = parts.at(2).toInt(&okYear);
            if (okDay && okMonth && okYear) {
                if (parts.at(2).size() <= 2) year += (year >= 70 ? 1900 : 2000);
                const QDate custom(year, month, day);
                if (custom.isValid()) return custom;
            }
        }
    }

    return {};
}

QString displayDateFromStored(const QString& raw)
{
    const QString t = raw.trimmed();
    if (t.isEmpty()) return currentOrderDateDisplay();
    const QDate d = parseUiDisplayDate(t);
    return d.isValid() ? d.toString(QStringLiteral("dd.MM.yy")) : t;
}

QString storedDateFromDisplay(const QString& raw)
{
    const QString t = raw.trimmed();
    if (t.isEmpty()) return QDate::currentDate().toString(Qt::ISODate);
    const QDate d = parseUiDisplayDate(t);
    return d.isValid() ? d.toString(Qt::ISODate) : QDate::currentDate().toString(Qt::ISODate);
}

static QString normalizedColumnType(QString raw)
{
    raw = raw.trimmed().toUpper();
    const int paren = raw.indexOf(QLatin1Char('('));
    if (paren > 0) raw = raw.left(paren).trimmed();
    return raw;
}

static QString nonNullUiText(const QString& value)
{
    return value.isNull() ? QStringLiteral("") : value;
}

struct CanonicalPartPayload {
    QString orderedAt;
    QString store;
    QString partNumber;
    QString name;
    QString note;
    QString manufacturer;
    double qty = 1.0;
    double clientPrice = 0.0;
    double clientAmount = 0.0;
    double purchasePrice = 0.0;
    double purchaseAmount = 0.0;
    double markupPercent = 0.0;
    int serviceId = 0;
};


QTableWidgetItem* makePartIndexItem(const QString& text)
{
    auto* item = new QTableWidgetItem(text);
    item->setTextAlignment(Qt::AlignCenter);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

QTableWidgetItem* makePartDeliveredItem(bool checked, int partId, bool enabled)
{
    auto* item = new QTableWidgetItem();
    item->setData(RoleId, partId);
    item->setTextAlignment(Qt::AlignCenter);
    Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (enabled) flags |= Qt::ItemIsUserCheckable;
    item->setFlags(flags);
    item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    return item;
}

bool ensureOrderReadyForEditing(int orderNumber, QString* errorText)
{
    return OrderWorkspaceService::ensureEditableOrder(orderNumber, errorText);
}

double parseLooseNumber(QString text, bool* ok)
{
    text = text.trimmed();
    text.remove(' ');
    text.replace(',', '.');
    const double v = text.toDouble(ok);
    return v;
}

static QString foldForMatch(QString s)
{
    s = s.toLower().trimmed();
    const QStringList replFrom = {QString::fromUtf8("ё"), QString::fromUtf8("і"), QString::fromUtf8("ї"), QString::fromUtf8("є")};
    const QStringList replTo   = {QString::fromUtf8("е"), QString::fromUtf8("и"), QString::fromUtf8("и"), QString::fromUtf8("е")};
    for (int i = 0; i < replFrom.size(); ++i) s.replace(replFrom[i], replTo[i]);
    QString out;
    out.reserve(s.size());
    for (const QChar ch : s) {
        if (ch.isLetterOrNumber() || ch.isSpace()) out += ch;
        else out += QLatin1Char(' ');
    }
    return out.simplified();
}

static bool containsAnyRoot(const QString& text, const QStringList& roots)
{
    for (const QString& root : roots) {
        if (text.contains(root)) return true;
    }
    return false;
}

static bool containsWholeWordCaseInsensitive(const QString& text, const QString& word)
{
    QRegularExpression rx(QStringLiteral("(?<!\\p{L}|\\p{N})%1(?!\\p{L}|\\p{N})")
                              .arg(QRegularExpression::escape(word)),
                          QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    return rx.match(text).hasMatch();
}

static int overlapScore(const QString& a, const QString& b, const QStringList& roots)
{
    int score = 0;
    for (const QString& root : roots) {
        if (a.contains(root) && b.contains(root)) ++score;
    }
    return score;
}

static QString canonicalMatchToken(QString token)
{
    token = foldForMatch(token);
    if (token.isEmpty()) return QString();

    if (token.contains(QString::fromUtf8("замен")) || token.contains(QString::fromUtf8("замин")) ||
        token.contains(QString::fromUtf8("ремонт")) || token.contains(QString::fromUtf8("установ")) ||
        token.contains(QString::fromUtf8("снят")) || token.contains(QString::fromUtf8("знят")) ||
        token.contains(QString::fromUtf8("робот")) || token.contains(QString::fromUtf8("работ")) ||
        token.contains(QString::fromUtf8("запчаст")) || token.contains(QString::fromUtf8("детал")) ||
        token.contains(QString::fromUtf8("издел")) || token == QString::fromUtf8("по") ||
        token == QString::fromUtf8("для") || token == QString::fromUtf8("та") ||
        token == QString::fromUtf8("и") || token == QString::fromUtf8("й")) {
        return QString();
    }

    if (token.contains(QString::fromUtf8("рычаг")) || token.contains(QString::fromUtf8("ричаг"))) return QString::fromUtf8("рычаг");
    if (token.contains(QString::fromUtf8("подшип")) || token.contains(QString::fromUtf8("підшип"))) return QString::fromUtf8("подшип");
    if (token.contains(QString::fromUtf8("ступиц")) || token.contains(QString::fromUtf8("ступич"))) return QString::fromUtf8("ступиц");
    if (token.contains(QString::fromUtf8("патруб"))) return QString::fromUtf8("патруб");
    if (token.contains(QString::fromUtf8("шланг"))) return QString::fromUtf8("шланг");
    if (token.contains(QString::fromUtf8("провод")) || token.contains(QString::fromUtf8("дрот"))) return QString::fromUtf8("провод");
    if (token.contains(QString::fromUtf8("болт"))) return QString::fromUtf8("болт");
    if (token.contains(QString::fromUtf8("гайк"))) return QString::fromUtf8("гайк");
    if (token.contains(QString::fromUtf8("подрам"))) return QString::fromUtf8("подрам");
    if (token.contains(QString::fromUtf8("колес")) || token.contains(QString::fromUtf8("коліс"))) return QString::fromUtf8("колес");
    if (token.contains(QString::fromUtf8("проклад"))) return QString::fromUtf8("проклад");
    if (token.contains(QString::fromUtf8("головк"))) return QString::fromUtf8("головк");
    if (token.contains(QString::fromUtf8("гбц"))) return QString::fromUtf8("гбц");
    if (token.contains(QString::fromUtf8("колод"))) return QString::fromUtf8("колод");
    if (token.contains(QString::fromUtf8("тормоз")) || token.contains(QString::fromUtf8("гальм"))) return QString::fromUtf8("тормоз");
    if (token.contains(QString::fromUtf8("супорт"))) return QString::fromUtf8("супорт");
    if (token.contains(QString::fromUtf8("ремен"))) return QString::fromUtf8("ремен");
    if (token.contains(QString::fromUtf8("генератор"))) return QString::fromUtf8("генератор");
    if (token.contains(QString::fromUtf8("грм")) || token.contains(QStringLiteral("timing"))) return QString::fromUtf8("грм");
    if (token.contains(QString::fromUtf8("ролик"))) return QString::fromUtf8("ролик");
    if (token.contains(QString::fromUtf8("помп"))) return QString::fromUtf8("помп");
    if (token.contains(QString::fromUtf8("цеп")) || token.contains(QString::fromUtf8("ланцюг"))) return QString::fromUtf8("цеп");
    if (token.contains(QString::fromUtf8("сайлент"))) return QString::fromUtf8("сайлент");
    if (token.contains(QString::fromUtf8("наконеч"))) return QString::fromUtf8("наконеч");
    if (token.contains(QString::fromUtf8("шаров"))) return QString::fromUtf8("шаров");
    if (token.contains(QString::fromUtf8("амортиз"))) return QString::fromUtf8("амортиз");
    if (token.contains(QString::fromUtf8("масл")) || token.contains(QString::fromUtf8("мастил")) || token == QStringLiteral("oil")) return QString::fromUtf8("масло");
    if (token.contains(QString::fromUtf8("фильтр")) || token.contains(QString::fromUtf8("фільтр")) || token == QStringLiteral("filter")) return QString::fromUtf8("фильтр");
    if (token.contains(QString::fromUtf8("салон"))) return QString::fromUtf8("салон");
    if (token.contains(QString::fromUtf8("повітр")) || token.contains(QString::fromUtf8("воздуш")) || token.contains(QString::fromUtf8("воздух")) || token == QStringLiteral("air")) return QString::fromUtf8("воздух");
    if (token.contains(QString::fromUtf8("свеч")) || token.contains(QString::fromUtf8("свіч"))) return QString::fromUtf8("свеч");
    if (token.contains(QString::fromUtf8("форсунк")) || token.contains(QString::fromUtf8("инжект")) || token.contains(QString::fromUtf8("інжект"))) return QString::fromUtf8("форсунк");
    if (token.contains(QString::fromUtf8("колектор")) || token.contains(QString::fromUtf8("коллектор"))) return QString::fromUtf8("колектор");
    if (token.contains(QString::fromUtf8("впуск"))) return QString::fromUtf8("впуск");
    if (token.contains(QString::fromUtf8("выпуск")) || token.contains(QString::fromUtf8("випуск"))) return QString::fromUtf8("выпуск");
    if (token.contains(QString::fromUtf8("стабилиз")) || token.contains(QString::fromUtf8("стабіліз"))) return QString::fromUtf8("стабилиз");
    if (token.contains(QString::fromUtf8("втулк"))) return QString::fromUtf8("втулк");
    if (token.contains(QString::fromUtf8("стойк")) || token.contains(QString::fromUtf8("стійк")) || token.contains(QString::fromUtf8("линк")) || token.contains(QString::fromUtf8("лінк"))) return QString::fromUtf8("стойк");
    if (token.contains(QString::fromUtf8("вентилят"))) return QString::fromUtf8("вентилят");
    if (token.contains(QString::fromUtf8("печк")) || token.contains(QString::fromUtf8("пічк"))) return QString::fromUtf8("печк");
    if (token.contains(QString::fromUtf8("моторчик"))) return QString::fromUtf8("моторчик");
    if (token.contains(QString::fromUtf8("турбин")) || token.contains(QString::fromUtf8("турбін"))) return QString::fromUtf8("турбин");

    if (token.contains(QString::fromUtf8("передн"))) return QString::fromUtf8("перед");
    if (token.contains(QString::fromUtf8("задн"))) return QString::fromUtf8("зад");
    if (token.contains(QString::fromUtf8("лев"))) return QString::fromUtf8("лев");
    if (token.contains(QString::fromUtf8("прав"))) return QString::fromUtf8("прав");
    if (token.contains(QString::fromUtf8("капот"))) return QString::fromUtf8("капот");

    return token;
}

static QSet<QString> buildMatchTokenSet(const QString& text)
{
    QSet<QString> out;
    const QStringList rawTokens = foldForMatch(text).split(QChar(' '), Qt::SkipEmptyParts);
    for (const QString& rawToken : rawTokens) {
        const QString canonical = canonicalMatchToken(rawToken);
        if (!canonical.isEmpty()) out.insert(canonical);
    }
    return out;
}

static QString normalizedCoreMatchPhrase(const QString& text)
{
    QStringList tokens = buildMatchTokenSet(text).values();
    std::sort(tokens.begin(), tokens.end());
    return tokens.join(QChar(' '));
}

static int matchTokenWeight(const QString& token)
{
    if (token == QString::fromUtf8("перед") || token == QString::fromUtf8("зад") ||
        token == QString::fromUtf8("лев") || token == QString::fromUtf8("прав")) {
        return 12;
    }
    if (token == QString::fromUtf8("проклад") || token == QString::fromUtf8("болт") ||
        token == QString::fromUtf8("гайк") || token == QString::fromUtf8("фильтр")) {
        return 26;
    }
    if (token == QString::fromUtf8("масло") || token == QString::fromUtf8("колес") ||
        token == QString::fromUtf8("капот") || token == QString::fromUtf8("салон") ||
        token == QString::fromUtf8("воздух") || token == QString::fromUtf8("тормоз")) {
        return 22;
    }
    return 40;
}

static bool sameMatchCategory(const QSet<QString>& serviceTokens,
                              const QSet<QString>& partTokens,
                              const std::initializer_list<QString>& categoryTokens)
{
    bool serviceHit = false;
    bool partHit = false;
    for (const QString& token : categoryTokens) {
        if (serviceTokens.contains(token)) serviceHit = true;
        if (partTokens.contains(token)) partHit = true;
        if (serviceHit && partHit) return true;
    }
    return false;
}

static int localPartServiceMatchScore(const QString& serviceTitle, const QString& partName)
{
    const QString s = foldForMatch(serviceTitle);
    const QString p = foldForMatch(partName);
    if (s.isEmpty() || p.isEmpty()) return 0;

    const auto oilKind = OilServiceClassifier::classifyOilWork(serviceTitle);
    if (oilKind != OilServiceClassifier::OilWorkKind::None) {
        if (OilServiceClassifier::matchesOilWorkKindPart(oilKind, partName)) {
            return oilKind == OilServiceClassifier::OilWorkKind::EngineOilService ? 420 : 360;
        }
        return 0;
    }

    const QSet<QString> serviceTokens = buildMatchTokenSet(serviceTitle);
    const QSet<QString> partTokens = buildMatchTokenSet(partName);
    if (serviceTokens.isEmpty() || partTokens.isEmpty()) return 0;

    const QString serviceCore = normalizedCoreMatchPhrase(serviceTitle);
    const QString partCore = normalizedCoreMatchPhrase(partName);

    int score = 0;
    bool anchored = false;

    if (!serviceCore.isEmpty() && !partCore.isEmpty()) {
        if (serviceCore == partCore) {
            score += 320;
            anchored = true;
        } else if (serviceCore.contains(partCore) || partCore.contains(serviceCore)) {
            score += 220;
            anchored = true;
        }
    }

    int overlapCount = 0;
    int strongOverlapCount = 0;
    for (const QString& token : serviceTokens) {
        if (!partTokens.contains(token)) continue;
        const int weight = matchTokenWeight(token);
        if (weight <= 0) continue;
        score += weight;
        ++overlapCount;
        if (weight >= 30) {
            ++strongOverlapCount;
            anchored = true;
        }
    }
    if (overlapCount >= 2) score += 60;
    if (strongOverlapCount >= 2) score += 100;
    if (strongOverlapCount >= 3) score += 60;

    if (sameMatchCategory(serviceTokens, partTokens, {QString::fromUtf8("рычаг")})) score += 120, anchored = true;
    if (sameMatchCategory(serviceTokens, partTokens, {QString::fromUtf8("подшип"), QString::fromUtf8("ступиц")})) score += 130, anchored = true;
    if (sameMatchCategory(serviceTokens, partTokens, {QString::fromUtf8("патруб")})) score += 130, anchored = true;
    if (sameMatchCategory(serviceTokens, partTokens, {QString::fromUtf8("шланг")})) score += 130, anchored = true;
    if (sameMatchCategory(serviceTokens, partTokens, {QString::fromUtf8("провод")})) score += 130, anchored = true;
    if (sameMatchCategory(serviceTokens, partTokens, {QString::fromUtf8("болт"), QString::fromUtf8("подрам")})) score += 150, anchored = true;
    if (sameMatchCategory(serviceTokens, partTokens, {QString::fromUtf8("болт"), QString::fromUtf8("колес")})) score += 150, anchored = true;
    if (sameMatchCategory(serviceTokens, partTokens, {QString::fromUtf8("гайк")})) score += 120, anchored = true;
    if (sameMatchCategory(serviceTokens, partTokens, {QString::fromUtf8("проклад"), QString::fromUtf8("гбц")})) score += 170, anchored = true;
    if (sameMatchCategory(serviceTokens, partTokens, {QString::fromUtf8("проклад"), QString::fromUtf8("головк")})) score += 170, anchored = true;
    if (sameMatchCategory(serviceTokens, partTokens, {QString::fromUtf8("проклад"), QString::fromUtf8("колектор")})) score += 170, anchored = true;
    if (sameMatchCategory(serviceTokens, partTokens, {QString::fromUtf8("проклад"), QString::fromUtf8("турбин")})) score += 170, anchored = true;
    if (sameMatchCategory(serviceTokens, partTokens, {QString::fromUtf8("колод"), QString::fromUtf8("тормоз")})) score += 180, anchored = true;
    if (sameMatchCategory(serviceTokens, partTokens, {QString::fromUtf8("супорт")})) score += 170, anchored = true;
    if (sameMatchCategory(serviceTokens, partTokens, {QString::fromUtf8("ремен"), QString::fromUtf8("генератор")})) score += 180, anchored = true;
    if (sameMatchCategory(serviceTokens, partTokens, {QString::fromUtf8("грм"), QString::fromUtf8("ролик")})) score += 180, anchored = true;
    if (sameMatchCategory(serviceTokens, partTokens, {QString::fromUtf8("грм"), QString::fromUtf8("помп")})) score += 180, anchored = true;
    if (sameMatchCategory(serviceTokens, partTokens, {QString::fromUtf8("цеп")})) score += 170, anchored = true;
    if (sameMatchCategory(serviceTokens, partTokens, {QString::fromUtf8("сайлент")})) score += 170, anchored = true;
    if (sameMatchCategory(serviceTokens, partTokens, {QString::fromUtf8("наконеч")})) score += 170, anchored = true;
    if (sameMatchCategory(serviceTokens, partTokens, {QString::fromUtf8("шаров")})) score += 170, anchored = true;
    if (sameMatchCategory(serviceTokens, partTokens, {QString::fromUtf8("амортиз")})) score += 170, anchored = true;
    if (sameMatchCategory(serviceTokens, partTokens, {QString::fromUtf8("свеч")})) score += 170, anchored = true;
    if (sameMatchCategory(serviceTokens, partTokens, {QString::fromUtf8("форсунк")})) score += 170, anchored = true;
    if (sameMatchCategory(serviceTokens, partTokens, {QString::fromUtf8("вентилят")})) score += 170, anchored = true;
    if (sameMatchCategory(serviceTokens, partTokens, {QString::fromUtf8("печк")})) score += 170, anchored = true;

    const bool frontRearConflict = (serviceTokens.contains(QString::fromUtf8("перед")) && partTokens.contains(QString::fromUtf8("зад"))) ||
                                   (serviceTokens.contains(QString::fromUtf8("зад")) && partTokens.contains(QString::fromUtf8("перед")));
    const bool sideConflict = (serviceTokens.contains(QString::fromUtf8("лев")) && partTokens.contains(QString::fromUtf8("прав"))) ||
                              (serviceTokens.contains(QString::fromUtf8("прав")) && partTokens.contains(QString::fromUtf8("лев")));
    if (frontRearConflict) score -= 90;
    if (sideConflict) score -= 110;

    if (!anchored) return 0;
    return qMax(0, score);
}

void restoreHeaderState(QHeaderView* header, const QString& key)
{
    if (!header || key.trimmed().isEmpty()) return;
    const QString raw = UiStateSettingsService::loadString(key, QString());
    if (raw.trimmed().isEmpty()) return;
    header->restoreState(QByteArray::fromBase64(raw.toUtf8()));
}

void persistHeaderState(QHeaderView* header, const QString& key)
{
    if (!header || key.trimmed().isEmpty()) return;
    UiStateSettingsService::saveString(key, QString::fromLatin1(header->saveState().toBase64()));
}

static void restoreTableColumnWidths(QTableWidget* table, const QString& key)
{
    if (!table || key.trimmed().isEmpty()) return;
    const QString raw = UiStateSettingsService::loadString(key, QStringLiteral("{}")).trimmed();
    if (raw.isEmpty()) return;
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
    if (!doc.isObject()) return;
    const QJsonObject obj = doc.object();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        bool ok = false;
        const int logicalIndex = it.key().toInt(&ok);
        if (!ok) continue;
        if (logicalIndex < 0 || logicalIndex >= table->columnCount()) continue;
        const int width = it.value().toInt();
        if (width > 0) table->setColumnWidth(logicalIndex, width);
    }
}

void persistTableColumnWidths(QTableWidget* table, const QString& key)
{
    if (!table || key.trimmed().isEmpty()) return;
    QJsonObject obj;
    for (int logicalIndex = 0; logicalIndex < table->columnCount(); ++logicalIndex) {
        obj.insert(QString::number(logicalIndex), table->columnWidth(logicalIndex));
    }
    UiStateSettingsService::saveString(key, QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
}


static void restoreTableColumnWidthsDeferred(QTableWidget* table, const QString& key, int delayMs = 0)
{
    if (!table || key.trimmed().isEmpty()) return;
    QPointer<QTableWidget> safeTable(table);
    QTimer::singleShot(delayMs, table, [safeTable, key]() {
        if (!safeTable) return;
        restoreTableColumnWidths(safeTable, key);
    });
}

void enforcePersistentInteractiveColumnWidths(QTableWidget* table, const QString& key)
{
    if (!table || key.trimmed().isEmpty() || !table->horizontalHeader()) return;
    restoreTableColumnWidths(table, key);
    restoreTableColumnWidthsDeferred(table, key, 0);
    restoreTableColumnWidthsDeferred(table, key, 80);
    restoreTableColumnWidthsDeferred(table, key, 180);
}

void persistSplitterState(QSplitter* split, const QString& key)
{
    if (!split || key.trimmed().isEmpty()) return;
    UiStateSettingsService::saveString(key, QString::fromLatin1(split->saveState().toBase64()));
}

void bindTableColumnWidthPersistence(QTableWidget* table, const QString& key)
{
    if (!table || key.trimmed().isEmpty() || !table->horizontalHeader()) return;
    auto* header = table->horizontalHeader();
    QObject::connect(header, &QHeaderView::sectionResized, table, [table, key](int, int, int) {
        persistTableColumnWidths(table, key);
    });
    QObject::connect(header, &QHeaderView::sectionMoved, table, [table, key](int, int, int) {
        persistTableColumnWidths(table, key);
    });
}

void bindHeaderStatePersistence(QHeaderView* header, const QString& key)
{
    if (!header || key.trimmed().isEmpty()) return;
    QObject::connect(header, &QHeaderView::sectionResized, header, [header, key](int, int, int) {
        UiStateSettingsService::saveString(key, QString::fromLatin1(header->saveState().toBase64()));
    });
    QObject::connect(header, &QHeaderView::sectionMoved, header, [header, key](int, int, int) {
        UiStateSettingsService::saveString(key, QString::fromLatin1(header->saveState().toBase64()));
    });
    QObject::connect(header, &QHeaderView::sortIndicatorChanged, header, [header, key](int, Qt::SortOrder) {
        UiStateSettingsService::saveString(key, QString::fromLatin1(header->saveState().toBase64()));
    });
}

void restoreHeaderFixedWidth(QHeaderView* header, const QString& key, int defaultWidth)
{
    if (!header || key.trimmed().isEmpty()) return;
    const int savedWidth = UiStateSettingsService::loadValue(key, defaultWidth).toInt();
    header->resize(qMax(header->minimumWidth(), savedWidth), header->height());
}

void bindHeaderFixedWidthPersistence(QHeaderView* header, const QString& key)
{
    if (!header || key.trimmed().isEmpty()) return;
    QObject::connect(header, &QHeaderView::geometriesChanged, header, [header, key]() {
        UiStateSettingsService::saveValue(key, header->width());
    });
}

void restoreSplitterState(QSplitter* split, const QString& key)
{
    if (!split || key.trimmed().isEmpty()) return;
    const QString raw = UiStateSettingsService::loadString(key, QString());
    if (raw.trimmed().isEmpty()) return;
    split->restoreState(QByteArray::fromBase64(raw.toUtf8()));
}

void bindSplitterStatePersistence(QSplitter* split, const QString& key)
{
    if (!split || key.trimmed().isEmpty()) return;
    QObject::connect(split, &QSplitter::splitterMoved, split, [split, key](int, int) {
        UiStateSettingsService::saveString(key, QString::fromLatin1(split->saveState().toBase64()));
    });
}


