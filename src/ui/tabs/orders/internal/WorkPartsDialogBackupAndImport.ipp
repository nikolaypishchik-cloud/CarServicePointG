// Extracted from src/ui/tabs/orders/WorkPartsDialogSupport.cpp for P1/P2 monolith reduction.
// Included exactly once by the original translation unit/header; do not compile separately.

static bool isOrderSaveBackupSuppressed()
{
    return qApp && qApp->property("csDisableOrderSaveBackup").toBool();
}

void publishWorkPartsBackupEvent(const QString& page,
                                 const QString& entityType,
                                 const QString& entityId,
                                 const QString& orderUid,
                                 int orderNumber,
                                 const QString& operationType,
                                 const QString& description,
                                 const QStringList& changedTables)
{
    if (isOrderSaveBackupSuppressed()) return;

    BackupCommitPublisher::publish(
        BackupCommitPublisher::makeEvent(QStringLiteral("WorkPartsDialog"),
                                         page,
                                         entityType,
                                         entityId,
                                         orderUid,
                                         orderNumber,
                                         operationType,
                                         description,
                                         changedTables),
        nullptr);
}
double parseLooseNumber(QString text, bool* ok);
QString normalizeSentence(const QString& s);
QTableWidgetItem* makePartIndexItem(const QString& text);

QString clippedImportLogText(QString value, int maxLen)
{
    value.replace(QChar('\r'), QLatin1Char(' '));
    value.replace(QChar('\n'), QLatin1Char(' '));
    value.replace(QChar('\t'), QLatin1Char(' '));
    value = value.simplified();

    if (maxLen <= 0) {
        return QString();
    }
    if (value.size() <= maxLen) {
        return value;
    }
    if (maxLen <= 1) {
        return QStringLiteral("…");
    }
    return value.left(maxLen - 1) + QStringLiteral("…");
}

static QString normalizedWorkOrderStatus(QString value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("open") || normalized == QString::fromUtf8("відкритий") || normalized == QString::fromUtf8("відкрито")) {
        return QStringLiteral("open");
    }
    if (normalized == QStringLiteral("in_progress") || normalized == QString::fromUtf8("в роботі") || normalized == QString::fromUtf8("у роботі") || normalized == QString::fromUtf8("в процесі")) {
        return QStringLiteral("in_progress");
    }
    if (normalized == QStringLiteral("closed") || normalized == QString::fromUtf8("закритий") || normalized == QString::fromUtf8("закрито")) {
        return QStringLiteral("closed");
    }
    if (normalized == QStringLiteral("cancelled") || normalized == QStringLiteral("canceled") || normalized == QString::fromUtf8("скасований") || normalized == QString::fromUtf8("скасовано")) {
        return QStringLiteral("cancelled");
    }
    return normalized;
}

QString localizedWorkOrderStatusDisplay(const QString& value)
{
    const QString normalized = normalizedWorkOrderStatus(value);
    if (normalized == QStringLiteral("open")) return QString::fromUtf8("Відкритий");
    if (normalized == QStringLiteral("in_progress")) return QString::fromUtf8("В роботі");
    if (normalized == QStringLiteral("closed")) return QString::fromUtf8("Закритий");
    if (normalized == QStringLiteral("cancelled")) return QString::fromUtf8("Скасований");
    return value;
}

bool promoteOrderStatusToInProgressIfNeeded(const QString& connectionName, int orderNumber, QString* errorText)
{
    if (connectionName.trimmed().isEmpty()) {
        return OrderStatusService::promoteToInProgressIfNeeded(orderNumber, errorText);
    }
    return OrderStatusService::promoteToInProgressIfNeeded(connectionName, orderNumber, errorText);
}


static QString uppercaseFirstLetterOnly(QString text)
{
    for (int i = 0; i < text.size(); ++i) {
        if (text[i].isLetter()) {
            text[i] = text[i].toUpper();
            break;
        }
    }
    return text;
}

static bool isCyrillicWorkPartsLetter(QChar ch)
{
    if (!ch.isLetter()) return false;
    const char32_t u = ch.unicode();
    return (u >= 0x0400 && u <= 0x052F) ||
           (u >= 0x2DE0 && u <= 0x2DFF) ||
           (u >= 0xA640 && u <= 0xA69F) ||
           (u >= 0x1C80 && u <= 0x1C8F);
}

static bool isLatinWorkPartsLetter(QChar ch)
{
    if (!ch.isLetter()) return false;
    const char32_t u = ch.unicode();
    return (u >= 0x0041 && u <= 0x005A) ||
           (u >= 0x0061 && u <= 0x007A) ||
           (u >= 0x00C0 && u <= 0x024F);
}

static bool tokenHasLettersWorkParts(const QString& token)
{
    for (const QChar ch : token) {
        if (ch.isLetter()) return true;
    }
    return false;
}

static const QList<QPair<QString, QString>>& workPartsAcronymReplacements()
{
    static const QList<QPair<QString, QString>> acronyms = {
        {QString::fromUtf8("то1"), QString::fromUtf8("ТО-1")},
        {QString::fromUtf8("то2"), QString::fromUtf8("ТО-2")},
        {QString::fromUtf8("то3"), QString::fromUtf8("ТО-3")},
        {QString::fromUtf8("то4"), QString::fromUtf8("ТО-4")},
        {QString::fromUtf8("сто"), QString::fromUtf8("СТО")},
        {QString::fromUtf8("то"), QString::fromUtf8("ТО")},
        {QString::fromUtf8("тр"), QString::fromUtf8("ТР")},
        {QString::fromUtf8("грм"), QString::fromUtf8("ГРМ")},
        {QString::fromUtf8("гбц"), QString::fromUtf8("ГБЦ")},
        {QString::fromUtf8("двс"), QString::fromUtf8("ДВС")},
        {QString::fromUtf8("двз"), QString::fromUtf8("ДВЗ")},
        {QString::fromUtf8("кпп"), QString::fromUtf8("КПП")},
        {QString::fromUtf8("акпп"), QString::fromUtf8("АКПП")},
        {QString::fromUtf8("мкпп"), QString::fromUtf8("МКПП")},
        {QString::fromUtf8("ркпп"), QString::fromUtf8("РКПП")},
        {QString::fromUtf8("гур"), QString::fromUtf8("ГУР")},
        {QString::fromUtf8("егур"), QString::fromUtf8("ЕГУР")},
        {QString::fromUtf8("еур"), QString::fromUtf8("ЕУР")},
        {QString::fromUtf8("шрус"), QString::fromUtf8("ШРУС")},
        {QString::fromUtf8("тнвд"), QString::fromUtf8("ТНВД")},
        {QString::fromUtf8("тннд"), QString::fromUtf8("ТННД")},
        {QString::fromUtf8("пнвт"), QString::fromUtf8("ПНВТ")},
        {QString::fromUtf8("пннд"), QString::fromUtf8("ПННД")},
        {QString::fromUtf8("ебу"), QString::fromUtf8("ЕБУ")},
        {QString::fromUtf8("дпкв"), QString::fromUtf8("ДПКВ")},
        {QString::fromUtf8("дпрв"), QString::fromUtf8("ДПРВ")},
        {QString::fromUtf8("дмрв"), QString::fromUtf8("ДМРВ")},
        {QString::fromUtf8("дпдз"), QString::fromUtf8("ДПДЗ")},
        {QString::fromUtf8("рхх"), QString::fromUtf8("РХХ")},
        {QString::fromUtf8("гбо"), QString::fromUtf8("ГБО")},
        {QStringLiteral("abs"), QStringLiteral("ABS")},
        {QStringLiteral("ebd"), QStringLiteral("EBD")},
        {QStringLiteral("eba"), QStringLiteral("EBA")},
        {QStringLiteral("esp"), QStringLiteral("ESP")},
        {QStringLiteral("esc"), QStringLiteral("ESC")},
        {QStringLiteral("asr"), QStringLiteral("ASR")},
        {QStringLiteral("tcs"), QStringLiteral("TCS")},
        {QStringLiteral("srs"), QStringLiteral("SRS")},
        {QStringLiteral("tpms"), QStringLiteral("TPMS")},
        {QStringLiteral("adas"), QStringLiteral("ADAS")},
        {QStringLiteral("egr"), QStringLiteral("EGR")},
        {QStringLiteral("dpf"), QStringLiteral("DPF")},
        {QStringLiteral("fap"), QStringLiteral("FAP")},
        {QStringLiteral("evap"), QStringLiteral("EVAP")},
        {QStringLiteral("vin"), QStringLiteral("VIN")},
        {QStringLiteral("oem"), QStringLiteral("OEM")},
        {QStringLiteral("ecu"), QStringLiteral("ECU")},
        {QStringLiteral("ecm"), QStringLiteral("ECM")},
        {QStringLiteral("tcm"), QStringLiteral("TCM")},
        {QStringLiteral("bcm"), QStringLiteral("BCM")},
        {QStringLiteral("pcm"), QStringLiteral("PCM")},
        {QStringLiteral("maf"), QStringLiteral("MAF")},
        {QStringLiteral("tps"), QStringLiteral("TPS")},
        {QStringLiteral("cvt"), QStringLiteral("CVT")},
        {QStringLiteral("dsg"), QStringLiteral("DSG")},
        {QStringLiteral("atf"), QStringLiteral("ATF")},
        {QStringLiteral("mtf"), QStringLiteral("MTF")},
        {QStringLiteral("awd"), QStringLiteral("AWD")},
        {QStringLiteral("fwd"), QStringLiteral("FWD")},
        {QStringLiteral("rwd"), QStringLiteral("RWD")},
        {QStringLiteral("4wd"), QStringLiteral("4WD")},
        {QStringLiteral("4x4"), QStringLiteral("4x4")},
        {QStringLiteral("obd"), QStringLiteral("OBD")},
        {QStringLiteral("obd2"), QStringLiteral("OBD2")},
        {QStringLiteral("obdii"), QStringLiteral("OBD-II")}
    };
    return acronyms;
}

static QString workPartsCanonicalAcronym(const QString& token)
{
    const QString trimmed = token.trimmed();
    if (trimmed.isEmpty()) return {};
    for (const auto& pair : workPartsAcronymReplacements()) {
        if (trimmed.compare(pair.first, Qt::CaseInsensitive) == 0) return pair.second;
    }
    return {};
}

static QString normalizeSentenceCaseWorkPartsToken(QString token, bool capitalizeFirstMeaningful)
{
    if (token.isEmpty() || !tokenHasLettersWorkParts(token)) return token;

    if (const QString acronym = workPartsCanonicalAcronym(token); !acronym.isEmpty()) return acronym;

    bool hasCyrillic = false;
    bool hasLatin = false;
    bool allLettersUpper = true;
    int letterCount = 0;
    for (const QChar ch : token) {
        if (!ch.isLetter()) continue;
        ++letterCount;
        if (isCyrillicWorkPartsLetter(ch)) hasCyrillic = true;
        if (isLatinWorkPartsLetter(ch)) hasLatin = true;
        if (!ch.isUpper()) allLettersUpper = false;
    }

    if (letterCount > 1 && allLettersUpper && hasLatin && !hasCyrillic) return token;

    QString normalized = token.toLower();
    if (hasLatin && !hasCyrillic) return uppercaseFirstLetterOnly(normalized);
    if (hasCyrillic) return capitalizeFirstMeaningful ? uppercaseFirstLetterOnly(normalized) : normalized;
    return capitalizeFirstMeaningful ? uppercaseFirstLetterOnly(normalized) : normalized;
}

static QString sentenceCaseWorkPartsText(QString text)
{
    text = text.trimmed().simplified();
    if (text.isEmpty()) return text;

    QString out;
    out.reserve(text.size());
    QString token;
    bool capitalizeFirstMeaningful = true;

    auto flushToken = [&]() {
        if (token.isEmpty()) return;
        out.append(normalizeSentenceCaseWorkPartsToken(token, capitalizeFirstMeaningful));
        if (tokenHasLettersWorkParts(token)) capitalizeFirstMeaningful = false;
        token.clear();
    };

    auto isTokenChar = [](QChar ch) {
        return ch.isLetterOrNumber() || ch == QLatin1Char('\'') || ch == QChar(0x2019) || ch == QChar(0x02BC);
    };

    for (const QChar ch : text) {
        if (isTokenChar(ch)) token.append(ch);
        else {
            flushToken();
            out.append(ch);
        }
    }
    flushToken();
    return out;
}

void installSentenceCaseAutoCap(QLineEdit* edit)
{
    if (!edit) return;
    QObject::connect(edit, &QLineEdit::textEdited, edit, [edit](const QString&) {
        const QString fixed = uppercaseFirstLetterOnly(edit->text());
        if (fixed == edit->text()) return;
        const int cursor = edit->cursorPosition();
        const QSignalBlocker blocker(edit);
        edit->setText(fixed);
        edit->setCursorPosition(qMin(cursor, fixed.size()));
    });
    QObject::connect(edit, &QLineEdit::editingFinished, edit, [edit]() {
        const QString normalized = normalizeSentence(edit->text());
        if (normalized == edit->text()) return;
        const QSignalBlocker blocker(edit);
        edit->setText(normalized);
    });
}

static void preparePopupMenu(QMenu& menu)
{
    ContextMenuAppearance::apply(menu);
}

static QString serviceHintText()
{
    return QString();
}

static QString partHintText()
{
    return QString();
}

QString normalizeInvoiceDisplayNumber(QString raw)
{
    raw = raw.trimmed();
    if (raw.isEmpty()) return {};

    raw.replace(QChar(0x00A0), QLatin1Char(' '));
    raw = raw.simplified();

    const QList<QRegularExpression> patterns = {
        QRegularExpression(QStringLiteral(R"((?:видаткова\s+накладна|накладна|документ)\s*(?:№|N|No|Номер)?\s*[:\-]?\s*([A-Za-zА-Яа-яІіЇїЄєҐґ0-9][A-Za-zА-Яа-яІіЇїЄєҐґ0-9/\\_.\-]{1,47}))"), QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:номер\s+накладн(?:ої|ой)?|номер\s+документа|номер|документ\s*№)\s*[:\-]?\s*([A-Za-zА-Яа-яІіЇїЄєҐґ0-9][A-Za-zА-Яа-яІіЇїЄєҐґ0-9/\\_.\-]{1,47}))"), QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:^|\s)№\s*([A-Za-zА-Яа-яІіЇїЄєҐґ0-9][A-Za-zА-Яа-яІіЇїЄєҐґ0-9/\\_.\-]{1,47}))"), QRegularExpression::CaseInsensitiveOption)
    };

    for (const QRegularExpression& rx : patterns) {
        const QRegularExpressionMatch match = rx.match(raw);
        if (match.hasMatch()) {
            QString candidate = match.captured(1).trimmed();
            candidate.remove(QRegularExpression(QStringLiteral(R"(^[№#:\-\s]+)")));
            candidate.remove(QRegularExpression(QStringLiteral(R"([,;:.\s]+$)")));
            if (!candidate.isEmpty()) return candidate;
        }
    }

    raw.remove(QRegularExpression(QStringLiteral(R"(^[№#:\-\s]+)")));
    raw.remove(QRegularExpression(QStringLiteral(R"([,;:.\s]+$)")));
    return raw.trimmed();
}

QString attachmentToolTipText(const QString& path,
                                      const QString& invoiceNumber,
                                      const QString& invoiceDate)
{
    QStringList lines;
    lines << (path.trimmed().isEmpty()
                ? QString::fromUtf8("Натисніть, щоб додати файл")
                : QString::fromUtf8("Натисніть, щоб переглянути або відкрити файл"));
    if (!invoiceNumber.trimmed().isEmpty()) lines << QString::fromUtf8("Номер накладної: %1").arg(invoiceNumber.trimmed());
    if (!invoiceDate.trimmed().isEmpty()) lines << QString::fromUtf8("Дата накладної: %1").arg(invoiceDate.trimmed());
    return lines.join(QLatin1Char('\n'));
}

QTableWidgetItem* makeAttachmentItem(const QString& path, const QString& displayName)
{
    auto* item = new QTableWidgetItem(path.trimmed().isEmpty() ? QString() : QString::fromUtf8("📎"));
    QFont f = item->font();
    f.setPointSizeF(qMax<qreal>(8.0, (f.pointSizeF() > 0.0 ? f.pointSizeF() : 10.0) + 3.0));
    item->setFont(f);
    item->setTextAlignment(Qt::AlignCenter);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    item->setData(RoleAttachmentPath, path);
    item->setData(RoleAttachmentName, displayName);
    item->setData(RoleAttachmentSource, QString());
    item->setData(RoleAttachmentDocumentNumber, QString());
    item->setData(RoleAttachmentDocumentDate, QString());
    item->setToolTip(attachmentToolTipText(path));
    return item;
}

QTableWidgetItem* makeInvoiceNumberItem(const QString& invoiceNumber,
                                               const QString& path,
                                               const QString& invoiceDate)
{
    const QString normalizedInvoiceNumber = normalizeInvoiceDisplayNumber(invoiceNumber);
    auto* item = new QTableWidgetItem(normalizedInvoiceNumber);
    item->setTextAlignment(Qt::AlignCenter);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
    item->setData(RoleAttachmentPath, path);
    item->setData(RoleAttachmentDocumentNumber, normalizedInvoiceNumber);
    item->setData(RoleAttachmentDocumentDate, invoiceDate.trimmed());
    item->setToolTip(attachmentToolTipText(path, normalizedInvoiceNumber, invoiceDate));
    QFont f = item->font();
    if (!normalizedInvoiceNumber.isEmpty()) {
        f.setUnderline(true);
        item->setForeground(QColor(QStringLiteral("#1d4ed8")));
    }
    item->setFont(f);
    return item;
}


bool isServiceHintValue(const QString& text)
{
    const QString t = text.simplified().trimmed();
    return t.isEmpty() || t.startsWith(QString::fromUtf8("+ Додати роботу"));
}

bool isPartHintValue(const QString& text)
{
    const QString t = text.simplified().trimmed();
    return t.isEmpty() || t.startsWith(QString::fromUtf8("+ Додати запчастину"));
}

static QDate partEditorBusinessDateFromDisplay(const QString& raw)
{
    const QString text = raw.simplified().trimmed();
    if (text.isEmpty()) return {};
    const QString stored = storedDateFromDisplay(text).trimmed();
    QDate d = DateFormats::parseDate(stored);
    if (!d.isValid()) d = DateFormats::parseDate(text);
    if (!d.isValid()) d = QDate::fromString(text, QStringLiteral("dd.MM.yyyy"));
    if (!d.isValid()) d = QDate::fromString(text, QStringLiteral("dd.MM.yy"));
    return d;
}

static int partEditorOverdueSeverity(QTableWidget* table, int row)
{
    if (!table || row < 0 || row >= table->rowCount()) return 0;
    if (table->objectName() != QStringLiteral("orderPartsTable")) return 0;
    if (!rowHasMeaningfulPartData(table, row)) return 0;

    const auto* deliveredItem = table->item(row, PartColDelivered);
    const bool installed = deliveredItem
        && (deliveredItem->checkState() == Qt::Checked
            || !deliveredItem->data(RoleDeliveryDate).toString().trimmed().isEmpty());
    if (installed) return 0;

    const QString dateText = table->item(row, PartColOrderedAt) ? table->item(row, PartColOrderedAt)->text() : QString();
    const QDate anchor = partEditorBusinessDateFromDisplay(dateText);
    if (!anchor.isValid()) return 0;

    const int days = anchor.daysTo(QDate::currentDate());
    if (days >= 5) return 2;
    if (days >= 3) return 1;
    return 0;
}

void setRowEditingVisual(QTableWidget* table, int row)
{
    if (!table || row < 0 || row >= table->rowCount()) return;
    const int overdueSeverity = partEditorOverdueSeverity(table, row);
    const QColor normalColor(QStringLiteral("#111111"));
    const QColor overdueBg = overdueSeverity >= 2
        ? QColor(255, 160, 160)
        : (overdueSeverity == 1 ? QColor(255, 235, 120) : QColor());
    for (int c = 0; c < table->columnCount(); ++c) {
        if (auto* item = table->item(row, c)) {
            item->setForeground(normalColor);
            item->setData(Qt::BackgroundRole, overdueBg.isValid() ? QVariant(overdueBg) : QVariant());
        }
    }
}

void clearRowHintState(QTableWidget* table, int row)
{
    if (!table || row < 0 || row >= table->rowCount()) return;
    for (int c = 0; c < table->columnCount(); ++c) {
        if (auto* item = table->item(row, c)) {
            item->setData(RoleIsHint, false);
        }
    }
}

bool isSpacerItem(const QTableWidgetItem* item)
{
    return item && item->data(RoleSpacer).toBool();
}

bool rowHasMeaningfulServiceData(QTableWidget* table, int row)
{
    if (!table || row < 0 || row >= table->rowCount()) return false;
    auto* item = table->item(row, ServiceColTitle);
    if (!item || isSpacerItem(item)) return false;
    auto meaningful = [](const QString& value) {
        const QString t = value.simplified().trimmed();
        return !t.isEmpty() && !isServiceHintValue(t);
    };
    return meaningful(item->text());
}

bool rowHasMeaningfulPartData(QTableWidget* table, int row)
{
    if (!table || row < 0 || row >= table->rowCount()) return false;
    auto meaningful = [](const QString& value) {
        const QString t = value.simplified().trimmed();
        return !t.isEmpty() && !isPartHintValue(t);
    };
    return (table->item(row, PartColName) && meaningful(table->item(row, PartColName)->text())) ||
           (table->item(row, PartColOem) && !table->item(row, PartColOem)->text().simplified().trimmed().isEmpty()) ||
           (table->item(row, PartColStore) && !table->item(row, PartColStore)->text().simplified().trimmed().isEmpty());
}

bool rowIsRemovableBlankPartRow(QTableWidget* table, int row)
{
    if (!table || row < 0 || row >= table->rowCount()) return false;
    if (rowIsPureViewportFill(table, row) || rowHasHintMarker(table, row) || rowHasMeaningfulPartData(table, row)) return false;
    auto nonEmpty = [](QTableWidgetItem* item) {
        return item && !isSpacerItem(item) && !item->data(RoleViewportFill).toBool() && !item->text().simplified().trimmed().isEmpty();
    };
    if (auto* nameItem = table->item(row, PartColName); nameItem && nameItem->data(RoleId).toInt() > 0) return false;
    if (auto* attachmentItem = table->item(row, PartColAttachment)) {
        if (!attachmentItem->data(RoleAttachmentPath).toString().trimmed().isEmpty() ||
            !attachmentItem->data(RoleAttachmentSource).toString().trimmed().isEmpty() ||
            !attachmentItem->data(RoleAttachmentDocumentNumber).toString().trimmed().isEmpty()) return false;
    }
    const int cols[] = {PartColOrderedAt, PartColOem, PartColName, PartColQty, PartColPrice, PartColAmount, PartColStore, PartColBrand, PartColInvoiceNumber};
    for (int c : cols) { if (nonEmpty(table->item(row, c))) return false; }
    return true;
}

bool shouldShowPartDeliveredCheckbox(QTableWidget* table, int row)
{
    if (!table || row < 0 || row >= table->rowCount()) return false;
    auto* item = table->item(row, PartColDelivered);
    if (!item || isSpacerItem(item) || item->data(RoleViewportFill).toBool()) return false;
    return rowHasMeaningfulPartData(table, row);
}

void syncPartDeliveredCellState(QTableWidget* table, int row)
{
    if (!table || row < 0 || row >= table->rowCount()) return;
    auto* item = table->item(row, PartColDelivered);
    if (!item || isSpacerItem(item) || item->data(RoleViewportFill).toBool()) return;

    const bool showCheckbox = shouldShowPartDeliveredCheckbox(table, row);
    // A freshly typed part row has RoleId == 0 until the order/details dialog is
    // committed.  The checkbox is still valid UI state: keep it locally and let
    // savePartRow persist installed_at during the insert.  Existing rows continue
    // to update the DB immediately from itemChanged.
    const bool canToggle = showCheckbox;

    Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (canToggle) flags |= Qt::ItemIsUserCheckable;
    item->setFlags(flags);
}

static QString applyUppercaseAcronyms(QString text)
{
    for (const auto& pair : workPartsAcronymReplacements()) {
        QRegularExpression rx(QStringLiteral("(?<!\\p{L}|\\p{N})%1(?!\\p{L}|\\p{N})")
                                  .arg(QRegularExpression::escape(pair.first)),
                              QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
        text.replace(rx, pair.second);
    }
    return text;
}

static QString canonicalizeOilGradesInText(QString text)
{
    static const QRegularExpression oilRx(
        QStringLiteral(R"((?<!\d)(\d{1,2})\s*([WwВв])\s*[-/]?\s*(\d{2})(?!\d))"),
        QRegularExpression::UseUnicodePropertiesOption);
    text.replace(oilRx, QStringLiteral("\\1W\\3"));
    return text;
}

bool isOilServiceTitle(const QString& serviceTitle);
bool isOilMaintenancePart(const QString& partName);

static QString normalizeWorkPartsTypingMistakes(QString text)
{
    static const QList<QPair<QString, QString>> typoRules = {
        {QString::fromUtf8("замина"), QString::fromUtf8("заміна")},
        {QString::fromUtf8("замена"), QString::fromUtf8("заміна")},
        {QString::fromUtf8("замены"), QString::fromUtf8("заміни")},
        {QString::fromUtf8("замінна"), QString::fromUtf8("заміна")},
        {QString::fromUtf8("устанвока"), QString::fromUtf8("установка")},
        {QString::fromUtf8("устновка"), QString::fromUtf8("установка")},
        {QString::fromUtf8("снятіе"), QString::fromUtf8("снятие")},
        {QString::fromUtf8("фильтер"), QString::fromUtf8("фильтр")},
        {QString::fromUtf8("філтер"), QString::fromUtf8("фільтр")},
        {QString::fromUtf8("філтр"), QString::fromUtf8("фільтр")},
        {QString::fromUtf8("проведеня"), QString::fromUtf8("проведення")},
        {QString::fromUtf8("проведенння"), QString::fromUtf8("проведення")},
        {QString::fromUtf8("двс"), QString::fromUtf8("двз")}
    };
    for (const auto& rule : typoRules) {
        QRegularExpression rx(QStringLiteral("(?<!\\p{L}|\\p{N})%1(?!\\p{L}|\\p{N})")
                                  .arg(QRegularExpression::escape(rule.first)),
                              QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
        text.replace(rx, rule.second);
    }
    return text.simplified();
}

static QString foldedWorkPartsPhrase(QString value)
{
    value = canonicalizeOilGradesInText(value.simplified().trimmed()).toLower();
    value.replace(QRegularExpression(QStringLiteral(R"([^\p{L}\p{N}]+)"),
                                     QRegularExpression::UseUnicodePropertiesOption),
                  QStringLiteral(" "));
    return value.simplified();
}

static QString canonicalizeKnownPartName(QString value)
{
