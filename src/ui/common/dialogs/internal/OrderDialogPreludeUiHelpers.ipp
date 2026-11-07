// Extracted from src/ui/common/dialogs/OrderDialogModulePrelude.h for P2 prelude monolith reduction.
// Included exactly once by the owning header; do not compile separately.

    const QString normalized = PlateUtils::normalizePlate(raw);
    const QString display = PlateUtils::formatPlateDisplay(normalized);
    return display.isEmpty() ? formatPlateProgressive(raw) : display;
}

static QString comparablePlateText(const QLineEdit* edit)
{
    const QString raw = edit ? edit->text() : QString();
    const QString normalized = PlateUtils::normalizePlate(raw);
    const QString display = PlateUtils::formatPlateDisplay(normalized);
    return display.isEmpty() ? formatPlateProgressive(raw) : display;
}

static QString comparableVinText(const QLineEdit* edit)
{
    return edit ? edit->text().trimmed().toUpper() : QString();
}

static QString comparableCarText(const QLineEdit* edit)
{
    return edit ? VehicleNameUtils::normalizeVehicleFreeText(edit->text()) : QString();
}

static QString comparableColorText(const QLineEdit* edit)
{
    return edit ? capitalizeSentence(edit->text()) : QString();
}

static QString comparableClientText(const QLineEdit* edit)
{
    return edit ? capitalizeSentence(edit->text()) : QString();
}

static QString statusDisplayFromValue(QString value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("closed") || normalized == QString::fromUtf8("закритий") || normalized == QString::fromUtf8("закрито")) {
        return QString::fromUtf8("Закритий");
    }
    if (normalized == QStringLiteral("in_progress") || normalized == QString::fromUtf8("в роботі") || normalized == QString::fromUtf8("у роботі") || normalized == QString::fromUtf8("в процесі")) {
        return QString::fromUtf8("В роботі");
    }
    return QString::fromUtf8("Відкритий");
}

static QString statusValueFromText(QString value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("closed") || normalized == QString::fromUtf8("закритий") || normalized == QString::fromUtf8("закрито")) {
        return QStringLiteral("closed");
    }
    if (normalized == QStringLiteral("in_progress") || normalized == QString::fromUtf8("в роботі") || normalized == QString::fromUtf8("у роботі") || normalized == QString::fromUtf8("в процесі")) {
        return QStringLiteral("in_progress");
    }
    return QStringLiteral("open");
}

static int statusComboIndexByValue(QComboBox* combo, const QString& value)
{
    if (!combo) return -1;
    const QString normalizedValue = statusValueFromText(value);
    int idx = combo->findData(normalizedValue, Qt::UserRole, Qt::MatchFixedString);
    if (idx >= 0) return idx;
    idx = combo->findText(statusDisplayFromValue(normalizedValue), Qt::MatchFixedString);
    if (idx >= 0) return idx;
    return combo->findText(value, Qt::MatchFixedString);
}

static QString currentStatusValue(const QComboBox* combo)
{
    if (!combo) return QStringLiteral("open");
    const QString dataValue = combo->currentData().toString().trimmed();
    if (!dataValue.isEmpty()) return statusValueFromText(dataValue);
    return statusValueFromText(combo->currentText());
}


static QString stripPlanningMarkersForOrder(QString text)
{
    text.remove(QString::fromUtf8("✕"));
    text.remove(QString::fromUtf8("❌"));
    text.remove(QString::fromUtf8("✓"));
    text.remove(QString::fromUtf8("✅"));
    text = text.simplified();

    static const QRegularExpression leadingTimeRe(QStringLiteral(R"(^\s*((?:[01]?\d|2[0-3]):[0-5]\d)\s+)"));
    const auto leading = leadingTimeRe.match(text);
    if (leading.hasMatch()) {
        text.remove(0, leading.capturedLength(1));
    }
    return text.simplified();
}

struct PlanningOrderPrefillData
{
    QString plateDisplay;
    QString carDisplay;
    QString clientName;
    QString clientPhone;
    QString carYear;
    QString fuelType;
    QString engineVolume;
    QString odometer;
    QString serviceText;
    QString commentText;
};

static QStringList planningSegmentsForOrder(const QString& rawText)
{
    QString clean = stripPlanningMarkersForOrder(rawText);
    QStringList out;
    const QStringList parts = clean.split(QStringLiteral("|"), Qt::SkipEmptyParts);
    for (QString part : parts) {
        part = part.trimmed();
        if (part.isEmpty()) continue;
        out << part;
    }
    if (out.isEmpty() && !clean.trimmed().isEmpty()) out << clean.trimmed();
    return out;
}

static QString normalizePlanningLabel(QString label)
{
    label = label.trimmed().toLower();
    label.replace(QString::fromUtf8("’"), QStringLiteral("'"));
    label.remove(QRegularExpression(QStringLiteral(R"([\s\.\-_/\\'`]+)")));
    return label;
}

static bool planningLabelMatches(const QString& rawLabel, const QStringList& labels)
{
    const QString normalized = normalizePlanningLabel(rawLabel);
    for (const QString& label : labels) {
        if (normalized == normalizePlanningLabel(label)) return true;
    }
    return false;
}

static bool planningSegmentHasKnownLabel(const QString& segment)
{
    const int colon = segment.indexOf(QLatin1Char(':'));
    const int equals = segment.indexOf(QLatin1Char('='));
    int sep = -1;
    if (colon >= 0 && equals >= 0) sep = qMin(colon, equals);
    else sep = qMax(colon, equals);
    if (sep <= 0) return false;

    static const QStringList labels = {
        QString::fromUtf8("Клієнт"), QString::fromUtf8("Телефон"), QString::fromUtf8("Тел"), QStringLiteral("Phone"),
        QString::fromUtf8("Авто"), QString::fromUtf8("Марка"), QString::fromUtf8("Модель"),
        QString::fromUtf8("Рік"), QString::fromUtf8("Паливо"), QString::fromUtf8("Об'єм"), QString::fromUtf8("Об’єм"),
        QString::fromUtf8("Двигун"), QString::fromUtf8("Одометр"), QString::fromUtf8("Пробіг"),
        QString::fromUtf8("Номер"), QString::fromUtf8("Держ номер"), QString::fromUtf8("Номер авто"),
        QString::fromUtf8("Робота"), QString::fromUtf8("Послуга"), QString::fromUtf8("Коментар")
    };
    return planningLabelMatches(segment.left(sep), labels);
}

static QString extractPlanningLabeledValue(const QStringList& segments, const QStringList& labels)
{
    for (const QString& segment : segments) {
        const int colon = segment.indexOf(QLatin1Char(':'));
        const int equals = segment.indexOf(QLatin1Char('='));
        int sep = -1;
        if (colon >= 0 && equals >= 0) sep = qMin(colon, equals);
        else sep = qMax(colon, equals);
        if (sep <= 0) continue;
        if (planningLabelMatches(segment.left(sep), labels)) {
            QString value = segment.mid(sep + 1).trimmed();
            value.remove(QRegularExpression(QStringLiteral(R"(^[\s:=-]+)")));
            return value.simplified();
        }
    }
    return QString();
}

static QString cleanOnlineMarker(QString text)
{
    text.remove(QRegularExpression(QStringLiteral(R"(\[\s*online\s*\])"), QRegularExpression::CaseInsensitiveOption));
    text.remove(QRegularExpression(QStringLiteral(R"(^\s*online\s*$)"), QRegularExpression::CaseInsensitiveOption));
    return text.simplified();
}

static bool isPlanningTimeSegment(const QString& text)
{
    static const QRegularExpression timeRe(QStringLiteral(R"(^\s*(?:[01]?\d|2[0-3]):[0-5]\d(?:\s*[-–—]\s*(?:[01]?\d|2[0-3]):[0-5]\d)?\s*$)"));
    return timeRe.match(text).hasMatch();
}

static bool isCompactOnlinePlanningSummary(const QString& rawText)
{
    QString text = rawText.trimmed();
    if (text.isEmpty()) return false;

    const bool hasOnlineMarker = QRegularExpression(
        QStringLiteral(R"(\[\s*online\s*\])"),
        QRegularExpression::CaseInsensitiveOption).match(text).hasMatch();
    const bool hasTimeRange = QRegularExpression(
        QStringLiteral(R"((?:[01]?\d|2[0-3]):[0-5]\d\s*[-–—]\s*(?:[01]?\d|2[0-3]):[0-5]\d)")).match(text).hasMatch();
    const bool hasPlate = QRegularExpression(
        QStringLiteral(R"((?<!\S)([A-Za-zА-Яа-яІіЇїЄєҐґ]{2}[\s\-]?\d{4,5}[\s\-]?[A-Za-zА-Яа-яІіЇїЄєҐґ]{2})(?!\S))"))
        .match(text).hasMatch();

    // Compact online rows deliberately contain only: [Online] time, car, plate, service.
    // They must never be interpreted as the client name; the full client data is kept
    // in online_bookings and is merged by enrichPlanningDataFromStoredOnlineBooking().
    return hasOnlineMarker && hasTimeRange && hasPlate;
}

static int planningPositiveIntFromText(const QString& text)
{
    QString digits = text;
    digits.remove(QRegularExpression(QStringLiteral(R"([^0-9])")));
    if (digits.isEmpty()) return -1;
    bool ok = false;
    const int value = digits.toInt(&ok);
    return ok && value > 0 ? value : -1;
}

static QString normalizePlanningEngineVolume(QString text)
{
    text = text.trimmed();
    text.remove(QRegularExpression(QStringLiteral(R"(\s*(?:л|l|литр(?:а|ів)?)\s*$)"), QRegularExpression::CaseInsensitiveOption));
    text.replace(QLatin1Char(','), QLatin1Char('.'));
    return text.simplified();
}

static QString extractPlanningPlateDisplayForOrder(const QString& rawText)
{
    static const QRegularExpression plateRe(
        QStringLiteral(R"((?<!\S)([A-Za-zА-Яа-яІіЇїЄєҐґ]{2}[\s\-]?\d{4,5}[\s\-]?[A-Za-zА-Яа-яІіЇїЄєҐґ]{2})(?!\S))"));

    const QString clean = stripPlanningMarkersForOrder(rawText);
    const auto it = plateRe.globalMatch(clean);
    QRegularExpressionMatchIterator matchIt = it;
    while (matchIt.hasNext()) {
        const auto match = matchIt.next();
        const QString norm = PlateUtils::normalizePlate(match.captured(1));
        const QString display = PlateUtils::formatPlateDisplay(norm);
        if (!display.isEmpty()) return display;
        const QString fallback = formatPlateProgressive(match.captured(1));
        if (!fallback.isEmpty()) return fallback;
    }

    if (PlateUtils::isLikelyPlateInput(clean)) {
        const QString norm = PlateUtils::normalizePlate(clean);
        const QString display = PlateUtils::formatPlateDisplay(norm);
        return display.isEmpty() ? formatPlateProgressive(clean) : display;
    }
    return QString();
}

static QString sanitizePlanningCarDisplayForOrder(QString candidate)
{
    candidate = cleanOnlineMarker(candidate).simplified();
    static const QRegularExpression leadingTimeRangeRe(
        QStringLiteral(R"(^\s*(?:[01]?\d|2[0-3]):[0-5]\d(?:\s*[-–—]\s*(?:[01]?\d|2[0-3]):[0-5]\d)?\s+)")
    );
    candidate.remove(leadingTimeRangeRe);
    candidate = candidate.simplified();
    if (candidate.isEmpty()) return QString();

    const QList<QRegularExpression> tailPatterns = {
        QRegularExpression(
            QStringLiteral(R"(\s+(?:на|для)\s+(?:проведення|проходження|планового|позапланового|технічного)?\s*(?:то|т\.\s*о\.?|техобслуговування|технічного\s+обслуговування)\b.*$)"),
            QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(
            QStringLiteral(R"(\s+(?:на|для)\s+(?:діагностики|діагностику|ремонту|ремонт|обслуговування|техобслуговування|технічного\s+обслуговування|заміни|заміну|перевірки|перевірку|огляду)\b.*$)"),
            QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(
            QStringLiteral(R"(\s*[-|,:]\s*(?:то|т\.\s*о\.?|діагностика|ремонт|обслуговування|заміна|перевірка|огляд)\b.*$)"),
            QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(
            QStringLiteral(R"(\s+(?:та|і)\s+(?:то|діагностика|ремонт|обслуговування|заміна|перевірка|огляд)\b.*$)"),
            QRegularExpression::CaseInsensitiveOption)
    };

    for (const auto& re : tailPatterns) {
        candidate.replace(re, QString());
    }

    candidate.remove(QRegularExpression(QStringLiteral(R"([\s\-|,:;]+$)")));
    candidate = candidate.simplified();
    return candidate;
}

static QString extractPlanningCarDisplayForOrder(const QString& rawText)
{
    static const QRegularExpression plateRe(
        QStringLiteral(R"((?<!\S)([A-Za-zА-Яа-яІіЇїЄєҐґ]{2}[\s\-]?\d{4,5}[\s\-]?[A-Za-zА-Яа-яІіЇїЄєҐґ]{2})(?!\S))"));

    const QStringList segments = planningSegmentsForOrder(rawText);
    QString candidate = extractPlanningLabeledValue(segments, {QString::fromUtf8("Авто"), QStringLiteral("Car")});
    if (candidate.isEmpty()) {
        const QString make = extractPlanningLabeledValue(segments, {QString::fromUtf8("Марка"), QStringLiteral("Make")});
        const QString model = extractPlanningLabeledValue(segments, {QString::fromUtf8("Модель"), QStringLiteral("Model")});
        candidate = QStringLiteral("%1 %2").arg(make, model).simplified();
    }

    if (candidate.isEmpty()) {
        QStringList freeSegments;
        for (QString segment : segments) {
            segment = cleanOnlineMarker(segment);
            if (segment.isEmpty() || isPlanningTimeSegment(segment) || segment.startsWith(QStringLiteral("booking:"), Qt::CaseInsensitive) || planningSegmentHasKnownLabel(segment)) continue;
            freeSegments << segment;
        }
        if (freeSegments.size() >= 5) candidate = freeSegments.value(2).trimmed();
    }

    if (candidate.isEmpty()) {
        QString clean = stripPlanningMarkersForOrder(rawText);
        const auto match = plateRe.match(clean);
        candidate = clean;
        if (match.hasMatch()) {
            candidate = clean.left(match.capturedStart(1)).trimmed();
        }
    }

    candidate = sanitizePlanningCarDisplayForOrder(candidate);
    return candidate.isEmpty() ? QString() : titleCaseWords(candidate);
}

static PlanningOrderPrefillData extractPlanningOrderPrefillData(const QString& rawText)
{
    PlanningOrderPrefillData data;
    const QStringList segments = planningSegmentsForOrder(rawText);

    data.plateDisplay = extractPlanningPlateDisplayForOrder(rawText);
    data.carDisplay = extractPlanningCarDisplayForOrder(rawText);
    data.clientName = extractPlanningLabeledValue(segments, {QString::fromUtf8("Клієнт"), QString::fromUtf8("ПІБ"), QStringLiteral("Client")});
    data.clientPhone = extractPlanningLabeledValue(segments, {QString::fromUtf8("Телефон"), QString::fromUtf8("Тел"), QStringLiteral("Phone")});
    data.carYear = extractPlanningLabeledValue(segments, {QString::fromUtf8("Рік"), QStringLiteral("Year")});
    data.fuelType = extractPlanningLabeledValue(segments, {QString::fromUtf8("Паливо"), QStringLiteral("Fuel")});
    data.engineVolume = normalizePlanningEngineVolume(extractPlanningLabeledValue(segments, {QString::fromUtf8("Об'єм"), QString::fromUtf8("Об’єм"), QString::fromUtf8("Двигун"), QStringLiteral("Engine")}));
    data.odometer = extractPlanningLabeledValue(segments, {QString::fromUtf8("Одометр"), QString::fromUtf8("Пробіг"), QStringLiteral("Odometer")});
    data.serviceText = extractPlanningLabeledValue(segments, {QString::fromUtf8("Робота"), QString::fromUtf8("Послуга"), QStringLiteral("Service")});
    data.commentText = extractPlanningLabeledValue(segments, {QString::fromUtf8("Коментар"), QStringLiteral("Comment")});

    if (data.serviceText.isEmpty() && !data.plateDisplay.isEmpty()) {
        static const QRegularExpression plateRe(
            QStringLiteral(R"((?<!\S)([A-Za-zА-Яа-яІіЇїЄєҐґ]{2}[\s\-]?\d{4,5}[\s\-]?[A-Za-zА-Яа-яІіЇїЄєҐґ]{2})(?!\S))"));
        QString clean = stripPlanningMarkersForOrder(rawText);
        clean = cleanOnlineMarker(clean);
        const auto match = plateRe.match(clean);
        if (match.hasMatch()) {
            QString service = clean.mid(match.capturedEnd(1)).trimmed();
            service.remove(QRegularExpression(QStringLiteral(R"(^[\s\-|,:;.]+)")));
            service.remove(QRegularExpression(QStringLiteral(R"([\s\-|,:;]+$)")));
            data.serviceText = service.simplified();
        }
    }

    if (data.clientName.isEmpty() || data.clientPhone.isEmpty() || data.carDisplay.isEmpty()) {
        const bool compactOnlineSummary = isCompactOnlinePlanningSummary(rawText);
        QStringList freeSegments;
        for (QString segment : segments) {
            segment = cleanOnlineMarker(segment);
            if (segment.isEmpty() || isPlanningTimeSegment(segment) || segment.startsWith(QStringLiteral("booking:"), Qt::CaseInsensitive) || planningSegmentHasKnownLabel(segment)) continue;
            freeSegments << segment;
        }

        // For old long rows the positional order was: client, phone, car, plate, service, comment.
        // For new compact rows the single free segment is: time + car + plate + service, so do
        // not copy it into the client/phone fields. Full client data is resolved from storage.
        if (!compactOnlineSummary) {
            if (data.clientName.isEmpty() && freeSegments.size() >= 1) data.clientName = freeSegments.value(0).trimmed();
            if (data.clientPhone.isEmpty() && freeSegments.size() >= 2) data.clientPhone = freeSegments.value(1).trimmed();
            if (data.carDisplay.isEmpty() && freeSegments.size() >= 3) data.carDisplay = titleCaseWords(sanitizePlanningCarDisplayForOrder(freeSegments.value(2)));
            if (data.plateDisplay.isEmpty() && freeSegments.size() >= 4) {
                const QString norm = PlateUtils::normalizePlate(freeSegments.value(3));
                data.plateDisplay = PlateUtils::formatPlateDisplay(norm);
            }
            if (data.serviceText.isEmpty() && freeSegments.size() >= 5) data.serviceText = freeSegments.value(4).trimmed();
            if (data.commentText.isEmpty() && freeSegments.size() >= 6) data.commentText = freeSegments.value(5).trimmed();
        }
    }

    data.clientName = titleCaseWords(data.clientName);
    data.carDisplay = titleCaseWords(data.carDisplay);
    return data;
}


static void enrichPlanningDataFromStoredOnlineBooking(PlanningOrderPrefillData* data, const QString& rawText, const QDate& plannedDate, const QTime& plannedTime)
{
    if (!data || !plannedDate.isValid() || !plannedTime.isValid()) return;

    QString ignoredError;
    const auto stored = BookingRepository().findByDateTimePlate(plannedDate, plannedTime, data->plateDisplay, &ignoredError);
    if (!stored.has_value()) {
        Q_UNUSED(rawText);
        return;
    }

    const BookingRecord& booking = stored.value();
    const QString rowCarDisplay = !booking.carMakeModel.trimmed().isEmpty()
        ? booking.carMakeModel.trimmed()
        : QStringLiteral("%1 %2").arg(booking.carMake.trimmed(), booking.carModel.trimmed()).simplified();

    // When a compact online row is displayed in Planning, it intentionally omits the
    // client and other detailed fields. The local online_bookings row is the source of
    // truth for creating a new order, so prefer stored values whenever they exist.
    if (!booking.clientName.trimmed().isEmpty()) data->clientName = titleCaseWords(booking.clientName);
    if (!booking.clientPhone.trimmed().isEmpty()) data->clientPhone = booking.clientPhone.trimmed();
    if (!rowCarDisplay.isEmpty()) data->carDisplay = titleCaseWords(rowCarDisplay);
    if (!booking.carNumber.trimmed().isEmpty()) {
        const QString norm = PlateUtils::normalizePlate(booking.carNumber);
        data->plateDisplay = PlateUtils::formatPlateDisplay(norm);
        if (data->plateDisplay.isEmpty()) data->plateDisplay = formatPlateProgressive(booking.carNumber);
    }
    if (!booking.carYear.trimmed().isEmpty()) data->carYear = booking.carYear.trimmed();
    if (!booking.fuelType.trimmed().isEmpty()) data->fuelType = booking.fuelType.trimmed();
    if (!booking.engineVolumeL.trimmed().isEmpty()) data->engineVolume = normalizePlanningEngineVolume(booking.engineVolumeL);
    if (!booking.odometerKm.trimmed().isEmpty()) data->odometer = booking.odometerKm.trimmed();
    if (!booking.serviceType.trimmed().isEmpty()) data->serviceText = booking.serviceType.trimmed();
    if (!booking.comment.trimmed().isEmpty()) data->commentText = booking.comment.trimmed();
}

static QString labelStyle()
{
    const bool lightUi = ThemeManager::currentTheme() == QStringLiteral("light");
    const QString color = lightUi ? QStringLiteral("#111111") : QStringLiteral("#f3f6fa");
    return QString::fromUtf8("font-weight:700; color:%1; padding-right:8px;").arg(color);
}

static QDateTime parseStoredDateTimeFlexible(const QString& raw)
{
    const QString t = raw.trimmed();
    if (t.isEmpty()) return {};
    for (const QString& fmt : {QString(), QStringLiteral("yyyy-MM-dd HH:mm:ss"), QStringLiteral("yyyy-MM-ddTHH:mm:ss"), QStringLiteral("dd.MM.yy HH:mm"), QStringLiteral("dd.MM.yyyy HH:mm")}) {
        QDateTime dt = fmt.isEmpty() ? DateFormats::parseDateTime(t) : QDateTime::fromString(t, fmt);
        if (dt.isValid()) return dt;
    }
    return {};
}

static QString editorPlaceholderStyle()
{
    return QString::fromUtf8(
        "QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QDateEdit, QTimeEdit { min-height: 28px; padding: 4px 12px; background:qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #f1f3f6, stop:0.45 #fafbfc, stop:0.55 #ffffff, stop:1 #e4e8ed); border:1px solid rgba(70,78,90,0.55); border-radius:12px; color:#101214; selection-background-color:#cfe3ff; }"
        " QLineEdit::placeholder, QAbstractSpinBox::placeholderText { color:#7a828d; }"
        " QCheckBox { spacing: 6px; }"
    );
}

static QString volumetricButtonStyle()
{
    return QString::fromUtf8(
        "QPushButton {"
        " color:#f8fbff; font-weight:800; font-size:15px;"
        " background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #566474, stop:0.12 #8390a0, stop:0.46 #34404d, stop:0.52 #131b28, stop:1 #09111a);"
        " border:1px solid rgba(255,255,255,0.64); border-radius:16px;"
        " padding:8px 22px 9px 22px;"
        "}"
        "QPushButton:hover { border-color: rgba(255,255,255,0.84); background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #627081, stop:0.14 #94a1af, stop:0.48 #404b59, stop:0.54 #18212f, stop:1 #0b1320); }"
        "QPushButton:pressed { padding-top:11px; padding-bottom:6px; padding-left:24px; padding-right:20px; background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #111823, stop:0.45 #1f2a39, stop:1 #49586a); }"
    );
}

static QString flatOrderFieldStyle()
{
    return QString::fromUtf8(
        "QComboBox::drop-down { width: 0px; border: none; }"
        "QComboBox::down-arrow { image: none; width: 0px; height: 0px; }"
        "QDateEdit::drop-down, QTimeEdit::drop-down { subcontrol-origin: padding; subcontrol-position: right center; width: 22px; border-left: 1px solid rgba(70,78,90,0.20); }"
        "QDateEdit::down-arrow, QTimeEdit::down-arrow { width: 10px; height: 10px; }"
        "QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 0px; border: none; }"
        "QSpinBox::up-arrow, QSpinBox::down-arrow, QDoubleSpinBox::up-arrow, QDoubleSpinBox::down-arrow { image: none; width: 0px; height: 0px; }"
    );
}

}
