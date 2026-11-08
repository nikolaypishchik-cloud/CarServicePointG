#include "core/util/DateFormats.h"
#include "ui/common/dialogs/OrderDialog.h"
#include "core/services/AccessControlService.h"
#include "ui/common/dialogs/OrderDialogFlowController.h"
#include "ui/common/dialogs/OrderDialogSubmitSupport.h"
#include "ui/theme/ThemeManager.h"
#include "core/services/settings/PersonnelSettingsService.h"
#include "core/services/settings/BrandingSettingsService.h"
#include "core/services/settings/GeneralSettingsService.h"
#include "core/services/OrderNumberService.h"
#include "core/services/OrderIdentityResolver.h"
#include "ui/tabs/orders/BayOccupancyPage.h"

#include "ui/tabs/orders/WorkPartsDialog.h"
#include <QPointer>
#include <QJsonObject>
#include "core/backup/CrashRecoveryManager.h"
#include "core/backup/BackupCommitPublisher.h"
#include "core/backup/BackupPerformanceGuard.h"
#include "app/AppContext.h"
#include "core/diagnostics/RuntimeLoggingControl.h"

#include "core/services/AppRuntimeService.h"
#include "core/services/OrderWorkspaceService.h"
#include "core/services/orders/OrderDialogReadQueryService.h"
#include "core/services/orders/OrderFinancialService.h"
#include "core/services/orders/OrderSnapshotService.h"
#include "core/services/orders/OrderDialogCoordinator.h"
#include "core/services/orders/OrderDialogPdfService.h"
#include "core/services/orders/OrderDialogStateSignature.h"
#include "core/services/orders/OrderDialogValidationService.h"
#include "core/services/orders/OrderDialogPrintQueryService.h"
#include "core/util/PlateUtils.h"
#include "ui/common/AdaptiveUi.h"
#include "core/util/VehicleNameUtils.h"
#include "core/util/FormatUtils.h"
#include "core/sync/SyncPayloadUtils.h"
#include "integrations/opendatabot/OpenDataBotClient.h"

#include <QStringList>
#include <QRegularExpression>
#include <QSqlRecord>
#include <QComboBox>
#include <QDir>
#include <QLineEdit>
#include <optional>
#include <QMessageBox>
#include <functional>
#include <QFontMetricsF>
#include <QScopedValueRollback>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>

namespace {
class ScopedOrderSaveBackupDisableForAccept final
{
public:
    ScopedOrderSaveBackupDisableForAccept()
    {
        if (!qApp) return;
        m_prev = qApp->property("csDisableOrderSaveBackup");
        qApp->setProperty("csDisableOrderSaveBackup", true);
    }

    ~ScopedOrderSaveBackupDisableForAccept()
    {
        if (!qApp) return;
        qApp->setProperty("csDisableOrderSaveBackup", m_prev);
    }

private:
    QVariant m_prev;
};

static QString toPlateDisplayLetters(QString s) {
    for (QChar& ch : s) {
        switch (ch.unicode()) {
            case 'A': ch = QChar(u'А'); break;
            case 'B': ch = QChar(u'В'); break;
            case 'C': ch = QChar(u'С'); break;
            case 'E': ch = QChar(u'Е'); break;
            case 'H': ch = QChar(u'Н'); break;
            case 'I': ch = QChar(u'І'); break;
            case 'K': ch = QChar(u'К'); break;
            case 'M': ch = QChar(u'М'); break;
            case 'O': ch = QChar(u'О'); break;
            case 'P': ch = QChar(u'Р'); break;
            case 'T': ch = QChar(u'Т'); break;
            case 'X': ch = QChar(u'Х'); break;
            default: break;
        }
    }
    return s;
}

static QString capitalizeSentence(QString s) {
    s = s.trimmed();
    if (s.isEmpty()) return s;
    // Uppercase first letter only (rest unchanged)
    const QChar first = s.at(0).toUpper();
    s[0] = first;
    return s;
}

static QString titleCaseWords(QString s) {
    s = s.trimmed();
    if (s.isEmpty()) return s;
    const auto parts = s.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    QStringList out;
    out.reserve(parts.size());
    for (QString p : parts) {
        if (p.contains('.')) {
            out << p.toUpper();
        } else if (!p.isEmpty()) {
            p = p.toLower();
            p[0] = p.at(0).toUpper();
            out << p;
        }
    }
    return out.join(' ');
}

static QString nonNullDialogText(const QString& value) {
    return value.isNull() ? QStringLiteral("") : value;
}

static bool ensureOrderDialogDbReady(QWidget* parent, QString* errorText = nullptr)
{
    QString dbError;
    if (!AppRuntimeService::ensureConnectionAlive(&dbError)) {
        if (errorText) *errorText = dbError;
        if (parent) {
            QMessageBox::critical(parent, QString::fromUtf8("Помилка"),
                                  QString::fromUtf8("Не вдалося підготувати БД для відкриття наряду.\n%1").arg(dbError));
        }
        return false;
    }
    // Avoid full schema/routing rebuilds in the interactive order-detail hot
    // path. Startup already completed the required schema preparation.
    return true;
}

static QString formatPlateProgressive(const QString& input)
{
    const QString norm = PlateUtils::normalizePlate(input);
    if (norm.isEmpty()) return QString();

    QString a1, nums, a2;
    for (const QChar& ch : norm) {
        if (ch.isLetter()) {
            if (a1.size() < 2 && nums.isEmpty() && a2.isEmpty()) a1 += ch;
            else if (!a1.isEmpty() && nums.size() >= 1 && a2.size() < 2) a2 += ch;
        } else if (ch.isDigit()) {
            if (a2.isEmpty() && nums.size() < 4) nums += ch;
        }
    }

    a1 = toPlateDisplayLetters(a1);
    a2 = toPlateDisplayLetters(a2);

    QString out;
    if (!a1.isEmpty()) out += a1;
    if (!nums.isEmpty()) {
        if (!out.isEmpty()) out += ' ';
        out += nums;
    }
    if (!a2.isEmpty()) {
        if (!out.isEmpty()) out += ' ';
        out += a2;
    }
    return out;
}

static QString safeTrimmed(const QVariant& v)
{
    return v.toString().trimmed();
}

static QString fieldValue(const QSqlRecord& rec, const QString& name)
{
    const int idx = rec.indexOf(name);
    return idx >= 0 ? safeTrimmed(rec.value(idx)) : QString();
}

static QString firstNonEmptyField(const QSqlRecord& rec, const std::initializer_list<const char*>& names)
{
    for (const char* name : names) {
        const QString value = fieldValue(rec, QString::fromLatin1(name));
        if (!value.isEmpty()) return value;
    }
    return QString();
}

static QString normalizeLegacyClientName(const QSqlRecord& rec)
{
    QString client = firstNonEmptyField(rec, {"client", "owner_name"});
    if (client == QStringLiteral("-") || client == QStringLiteral("—")) client.clear();
    return client;
}

static QString normalizeLegacyCarDisplay(const QSqlRecord& rec)
{
    QString brand = firstNonEmptyField(rec, {"car_brand"});
    QString model = firstNonEmptyField(rec, {"car_model"});
    QString display = firstNonEmptyField(rec, {"car"});

    if (display == QStringLiteral("-") || display == QStringLiteral("—")) display.clear();
    if (brand == QStringLiteral("-") || brand == QStringLiteral("—")) brand.clear();
    if (model == QStringLiteral("-") || model == QStringLiteral("—")) model.clear();

    if (!brand.isEmpty() || !model.isEmpty()) {
        QString joined = (brand + QStringLiteral(" ") + model).trimmed();
        joined.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
        joined = VehicleNameUtils::normalizeVehicleFreeText(joined);
        if (!joined.isEmpty()) return titleCaseWords(joined);
    }

    display.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return titleCaseWords(VehicleNameUtils::normalizeVehicleFreeText(display));
}

static QString normalizeLegacyPlateDisplay(const QSqlRecord& rec)
{
    const QString raw = firstNonEmptyField(rec, {"plate_norm", "plate"});
    const QString normalized = PlateUtils::normalizePlate(raw);
    const QString display = PlateUtils::formatPlateDisplay(normalized);
    return display.isEmpty() ? formatPlateProgressive(raw) : display;
}

static QString normalizeLegacyMasterId(const QSqlRecord& rec)
{
    return firstNonEmptyField(rec, {"master_id"});
}

static QString normalizeLegacyMasterName(const QSqlRecord& rec)
{
    return firstNonEmptyField(rec, {"master_name_closed"});
}

static QString normalizeSnapshotClientName(const OrderSnapshotService::OrderSnapshot& snapshot)
{
    QString client = snapshot.client.trimmed();
    if (client.isEmpty()) client = snapshot.ownerName.trimmed();
    if (client == QStringLiteral("-") || client == QStringLiteral("—")) client.clear();
    return client;
}

static QString normalizeSnapshotCarDisplay(const OrderSnapshotService::OrderSnapshot& snapshot)
{
    QString brand = snapshot.carBrand.trimmed();
    QString model = snapshot.carModel.trimmed();
    QString display = snapshot.car.trimmed();

    if (display == QStringLiteral("-") || display == QStringLiteral("—")) display.clear();
    if (brand == QStringLiteral("-") || brand == QStringLiteral("—")) brand.clear();
    if (model == QStringLiteral("-") || model == QStringLiteral("—")) model.clear();

    if (!brand.isEmpty() || !model.isEmpty()) {
        QString joined = (brand + QStringLiteral(" ") + model).trimmed();
        joined.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
        joined = VehicleNameUtils::normalizeVehicleFreeText(joined);
        if (!joined.isEmpty()) return titleCaseWords(joined);
    }

    display.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return titleCaseWords(VehicleNameUtils::normalizeVehicleFreeText(display));
}

static QString normalizeSnapshotPlateDisplay(const OrderSnapshotService::OrderSnapshot& snapshot)
{
    const QString raw = !snapshot.plateNorm.trimmed().isEmpty() ? snapshot.plateNorm : snapshot.plate;
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

static QString dialogMetallicStyle()
{
    const bool lightUi = ThemeManager::currentTheme() == QStringLiteral("light");
    if (lightUi) {
        return QString::fromUtf8(
            "QDialog#orderDialog {"
            " background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "   stop:0 #566170,"
            "   stop:0.06 #5d6978,"
            "   stop:0.12 #9fadb9,"
            "   stop:0.18 #8794a1,"
            "   stop:0.34 #778391,"
            "   stop:0.68 #8e9aa7,"
            "   stop:1 #a6b1bd);"
            "}"
            "QDialog#orderDialog, QWidget#orderFormTab, QWidget#orderFormSurface, QWidget#orderFormShell, QWidget#orderOpenedWrap, QWidget#orderFuelWrap, QWidget#orderEngineWrap, QWidget#qt_tabwidget_stackedwidget { border:none; border-radius:0px; }"
            "QWidget#orderFormTab, QWidget#orderFormSurface, QWidget#orderFormShell, QWidget#orderOpenedWrap, QWidget#orderFuelWrap, QWidget#orderEngineWrap, QWidget#qt_tabwidget_stackedwidget { background:transparent; }"
            "QTabWidget::pane { background:transparent; border:none; top:0px; margin:0px; padding:0px; }"
            "QTabWidget::tab-bar { alignment:left; left:0px; }"
            "QTabWidget::left-corner, QTabWidget::right-corner { background:transparent; border:none; width:0px; }"
            "QTabBar { background: transparent; }"
            "QTabBar::tab { color:#0e1115; font-weight:700; padding:10px 18px; margin:0px 2px 0px 0px;"
            "  background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(255,255,255,0.42), stop:0.18 rgba(255,255,255,0.30), stop:1 rgba(255,255,255,0.12));"
            "  border:1px solid rgba(54,64,76,0.14); border-bottom:none; border-top-left-radius:12px; border-top-right-radius:12px; }"
            "QTabBar::tab:selected { color:#0a0d11; background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(255,255,255,0.56), stop:0.16 rgba(255,255,255,0.40), stop:1 rgba(255,255,255,0.18)); }"
            "QTabBar::tab:!selected { margin-top:2px; }"
        );
    }
    return QString::fromUtf8(
        "QDialog#orderDialog {"
        " background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "   stop:0 #46505d,"
        "   stop:0.06 #4d5866,"
        "   stop:0.12 #7c8895,"
        "   stop:0.18 #66717e,"
        "   stop:0.34 #58626f,"
        "   stop:0.68 #707b88,"
        "   stop:1 #8994a0);"
        "}"
        "QDialog#orderDialog, QWidget#orderFormTab, QWidget#orderFormSurface, QWidget#orderFormShell, QWidget#orderOpenedWrap, QWidget#orderFuelWrap, QWidget#orderEngineWrap, QWidget#qt_tabwidget_stackedwidget { border:none; border-radius:0px; }"
        "QWidget#orderFormTab, QWidget#orderFormSurface, QWidget#orderFormShell, QWidget#orderOpenedWrap, QWidget#orderFuelWrap, QWidget#orderEngineWrap, QWidget#qt_tabwidget_stackedwidget { background:transparent; }"
        "QTabWidget::pane { background:transparent; border:none; top:0px; margin:0px; padding:0px; }"
        "QTabWidget::tab-bar { alignment:left; left:0px; }"
        "QTabWidget::left-corner, QTabWidget::right-corner { background:transparent; border:none; width:0px; }"
        "QTabBar { background: transparent; }"
        "QTabBar::tab { color:#f5f7fa; font-weight:700; padding:10px 18px; margin:0px 2px 0px 0px;"
        "  background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(255,255,255,0.22), stop:0.16 rgba(255,255,255,0.15), stop:1 rgba(255,255,255,0.05));"
        "  border:1px solid rgba(255,255,255,0.10); border-bottom:none; border-top-left-radius:12px; border-top-right-radius:12px; }"
        "QTabBar::tab:selected { color:#ffffff; background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(255,255,255,0.34), stop:0.14 rgba(255,255,255,0.24), stop:1 rgba(255,255,255,0.09)); }"
        "QTabBar::tab:!selected { margin-top:2px; }"
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

} // namespace

OrderDialog::OrderDialog(QWidget* parent, int orderNumber)
    : QDialog(parent), m_orderNumber(orderNumber), m_isNewOrder(orderNumber <= 0)
{
    if (orderNumber > 0 && !AccessControlService::canAccessOrder( orderNumber)) {
        setEnabled(false);
        QTimer::singleShot(0, this, &QDialog::reject);
        return;
    }
    setModal(true);
    setWindowFlag(Qt::Window, true);
    setWindowFlag(Qt::WindowMinimizeButtonHint, true);
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    setWindowFlag(Qt::WindowCloseButtonHint, true);

    setObjectName(QStringLiteral("orderDialog"));
    if (m_isNewOrder) {
        resize(1180, 706);
        setMinimumSize(980, 690);
    } else {
        resize(1480, 860);
        setMinimumSize(1220, 720);
    }
    setStyleSheet(dialogMetallicStyle() + (m_isNewOrder ? QStringLiteral(
        "QDialog#orderDialog { background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #273642,stop:1 #0e1922); }"
        "QTabWidget::pane {border:0;background:transparent;}") : QString()));
    buildUi();
    BackupPerformanceGuard::hold(AppContext::instance().backupManager(), m_isNewOrder ? 2500 : 4000);
    if (auto* crash = AppContext::instance().crashRecoveryManager()) { crash->registerParticipant(this); }
    if (windowTitle().isEmpty()) {
        setWindowTitle(m_isNewOrder ? QString::fromUtf8("Новий наряд") : QString::fromUtf8("Редагування наряду"));
    }
    Q_UNUSED(parent);
    loadMasters();
    if (m_isNewOrder && m_master && !AccessControlService::currentActorIsMaster()) {
        m_master->setCurrentIndex(-1);
    }

    if (m_orderNumber > 0) {
        loadOrder(m_orderNumber);
        m_num->setEnabled(false);
    } else if (m_num) {
        const int suggestedNumber = qMax(0, OrderNumberService::nextVisibleOrderNumber());
        m_num->setValue(suggestedNumber);
        m_num->setReadOnly(true);
        m_num->setButtonSymbols(QAbstractSpinBox::NoButtons);
        m_num->setToolTip(QString::fromUtf8("Номер наряду підтверджується під час збереження."));
    }

    captureSavedState();

    if (m_isNewOrder && SyncPayloadUtils::syncModuleEnabled()) {
        const auto syncPayload = SyncPayloadUtils::loadLatestPendingPayload();
        if (syncPayload.isValid() && (syncPayload.targetsNewOrder() || syncPayload.targetOrderNumber() <= 0)) {
            this->setProperty("sync.pending_source_file", syncPayload.sourceFile);
            this->setProperty("sync.pending_parts_count", syncPayload.parts.size());
            this->setProperty("sync.pending_has_payload", true);
            this->setProperty("sync.pending_notes", syncPayload.notes);
            this->setProperty("sync.pending_attachments_count", syncPayload.attachments.size());
        }
    }

    QPointer<OrderDialog> self(this);
    QTimer::singleShot(0, this, [self]() {
        if (!self) return;
        if (self->m_isNewOrder) {
            AdaptiveUi::fitTopLevelWindow(self,
                                          QSize(1180, 706),
                                          QSize(900, 620));
        } else {
            AdaptiveUi::fillTopLevelWindow(self, QSize(1080, 680));
        }
    });
}

void OrderDialog::accept()
{
    if(!m_acceptInProgress && currentStatusValue(m_status)=="closed") {
        QString oldStatus;
        if(!OrderDialogReadQueryService::loadCurrentStatus(m_orderNumber, &oldStatus)) {
            QMessageBox::warning(this,QString::fromUtf8("Закриття наряду"),QString::fromUtf8("Спочатку збережіть і синхронізуйте наряд."));return;
        }
        if(oldStatus!="closed") {
            const int oldIndex=statusComboIndexByValue(m_status,oldStatus);
            if(oldIndex>=0)m_status->setCurrentIndex(oldIndex);
            if(hasUnsavedChanges()) {
                QMessageBox::warning(this,QString::fromUtf8("Незбережені зміни"),QString::fromUtf8("Збережіть зміни наряду перед закриттям. Оплату ще не зараховано."));return;
            }
            QScopedValueRollback<bool> guard(m_acceptInProgress,true);
            const auto uid=OrderDialogReadQueryService::canonicalOrderUidForInternalNumber(m_orderNumber);
            if(!BayOccupancyPage::closeOrders(this,{uid}))return;
            const int closedIndex=statusComboIndexByValue(m_status,"closed");
            if(closedIndex>=0)m_status->setCurrentIndex(closedIndex);
            captureSavedState();QDialog::accept();return;
        }
    }
    if (m_acceptInProgress) {
        qWarning() << "OrderDialog accept reentry blocked for order" << m_orderNumber;
        return;
    }

    if (property("csAcceptQueued").toBool()) {
        qWarning() << "OrderDialog accept already queued for order" << m_orderNumber;
        return;
    }

    auto queueDeferredAccept = [this](int delayMs, const char* reason) {
        setProperty("csAcceptQueued", true);
        qWarning() << "OrderDialog accept deferred order" << m_orderNumber << "reason" << reason << "delay_ms" << delayMs;
        if (m_btnOk) m_btnOk->setEnabled(false);
        QTimer::singleShot(delayMs, this, [this]() {
            setProperty("csAcceptQueued", false);
            if (m_btnOk) m_btnOk->setEnabled(true);
            if (!m_acceptInProgress) accept();
        });
    };

    if (QWidget* popup = QApplication::activePopupWidget()) {
        const QString popupName = popup->metaObject() ? QString::fromLatin1(popup->metaObject()->className()) : QString();
        qWarning().noquote() << QStringLiteral("OrderDialog accept blocked: activePopup=") << popupName
                             << QStringLiteral(" order=") << m_orderNumber;
        popup->close();
        queueDeferredAccept(75, "active_popup");
        return;
    }

    auto resetInlineEditorFlagsForTable = [this](QTableWidget* table, const char* reason, bool forceEvenIfVisible) {
        if (!table || !table->property("inlineEditorActive").toBool()) return false;
        bool hasVisibleViewportEditor = false;
        if (table->viewport()) {
            const auto widgets = table->viewport()->findChildren<QWidget*>(QString(), Qt::FindChildrenRecursively);
            for (QWidget* widget : widgets) {
                if (!widget) continue;
                if (!(qobject_cast<QLineEdit*>(widget) || qobject_cast<QComboBox*>(widget))) continue;
                if (!widget->isVisible()) continue;
                hasVisibleViewportEditor = true;
                widget->clearFocus();
            }
        }
        if (hasVisibleViewportEditor && !forceEvenIfVisible) {
            return false;
        }
        qWarning() << "OrderDialog inline editor state reset order" << m_orderNumber
                   << "table" << (table->objectName().isEmpty() ? QStringLiteral("<unnamed>") : table->objectName())
                   << "reason" << reason
                   << "force" << forceEvenIfVisible;
        table->setProperty("inlineEditorActive", false);
        table->setProperty("inlineEditingRow", -1);
        table->setProperty("inlineEditingCol", -1);
        table->viewport()->update();
        return true;
    };

    auto flushInlineEditorValueToModelLocal = [](QTableWidget* table) {
        if (!table || !table->property("inlineEditorActive").toBool()) return;
        const int row = table->property("inlineEditingRow").toInt();
        const int col = table->property("inlineEditingCol").toInt();
        if (row < 0 || col < 0 || row >= table->rowCount() || col >= table->columnCount()) return;
        QTableWidgetItem* item = table->item(row, col);
        if (!item) return;
        QWidget* editor = nullptr;
        if (table->viewport()) {
            const auto widgets = table->viewport()->findChildren<QWidget*>(QString(), Qt::FindChildrenRecursively);
            for (QWidget* widget : widgets) {
                if (!widget) continue;
                if (!(qobject_cast<QLineEdit*>(widget) || qobject_cast<QComboBox*>(widget))) continue;
                if (!widget->isVisible()) continue;
                editor = widget;
                break;
            }
        }
        if (!editor) return;
        QString value;
        if (auto* combo = qobject_cast<QComboBox*>(editor)) value = combo->currentText();
        else if (auto* edit = qobject_cast<QLineEdit*>(editor)) value = edit->text();
        else return;
        value = value.trimmed();
        if (item->text() == value) return;
        item->setText(value);
        if (QAbstractItemModel* model = table->model()) {
            const QModelIndex index = model->index(row, col);
            model->setData(index, value, Qt::EditRole);
            model->setData(index, value, Qt::DisplayRole);
        }
    };

    auto normalizeInlineEditorState = [this, &resetInlineEditorFlagsForTable, &flushInlineEditorValueToModelLocal](bool forceReset) {
        bool inlineEditorActive = false;
        if (m_workPartsPage) {
            const auto tables = m_workPartsPage->findChildren<QTableWidget*>();
            for (QTableWidget* table : tables) {
                if (!table) continue;
                flushInlineEditorValueToModelLocal(table);
                table->clearFocus();
                if (table->property("inlineEditorActive").toBool()) {
                    resetInlineEditorFlagsForTable(table, forceReset ? "forced_accept_retry_limit" : "stale_after_flush", forceReset);
                }
                if (table->property("inlineEditorActive").toBool()) {
                    inlineEditorActive = true;
                }
            }
        }
        if (QWidget* fw = focusWidget()) fw->clearFocus();
        return inlineEditorActive;
    };

    bool inlineEditorActive = normalizeInlineEditorState(false);
    if (inlineEditorActive) {
        const int retryCount = property("csAcceptInlineRetryCount").toInt() + 1;
        setProperty("csAcceptInlineRetryCount", retryCount);
        if (retryCount >= 8) {
            inlineEditorActive = normalizeInlineEditorState(true);
            if (!inlineEditorActive) {
                setProperty("csAcceptInlineRetryCount", 0);
            }
        }
        if (inlineEditorActive) {
            queueDeferredAccept(75, "inline_editor_active");
            return;
        }
    }
    setProperty("csAcceptInlineRetryCount", 0);

    QScopedValueRollback<bool> acceptGuard(m_acceptInProgress, true);

    ScopedOrderSaveBackupDisableForAccept backupDisableGuard;

    QElapsedTimer timer;
    timer.start();

    if (RuntimeLoggingControl::deepDebugEnabled()) qInfo() << "OrderDialog accept begin order" << m_orderNumber;

    const int visibleNumber = m_num ? m_num->value() : 0;

    if (m_workPartsPage) {
        bool importBusy = m_workPartsPage->property("csImportBusy").toBool()
                       || m_workPartsPage->property("csImportApplyingRows").toBool();
        const auto workPartsTables = m_workPartsPage->findChildren<QTableWidget*>();
        for (QTableWidget* table : workPartsTables) {
            if (!table) continue;
            if (table->property("csImportBusy").toBool() || table->property("csImportApplyingRows").toBool()) {
                importBusy = true;
                break;
            }
        }
        if (importBusy) {
            QMessageBox::warning(this,
                                 QString::fromUtf8("Імпорт накладної"),
                                 QString::fromUtf8("Дочекайтеся повного завершення імпорту накладної перед збереженням наряду."));
            return;
        }
        QString pendingError;
        if (!m_workPartsPage->commitPendingEdits(&pendingError, true, true)) {
            QMessageBox::critical(this, QString::fromUtf8("Помилка БД"), pendingError);
            return;
        }
    }

    const auto totals = m_orderNumber>0 ? OrderDialogReadQueryService::financialTotals(m_orderNumber)
        : OrderFinancialService::TotalsResult{OrderFinancialService::TotalsStatus::Ok,0,0,{}};
    if (!totals.ok()) {
        QMessageBox::warning(this,QStringLiteral("Фінансові дані недоступні"),totals.error);
        return;
    }
    const double worksSum = totals.worksCents / 100.0;
    const double partsSum = totals.partsCents / 100.0;
    const double discount = m_discount ? FormatUtils::roundMoneyValue(m_discount->value()) : 0.0;

    const QString requestedStatus = currentStatusValue(m_status);

    OrderDialogCoordinator::SubmissionRequest request;
    request.input = buildWorkspaceInput(requestedStatus, worksSum, partsSum, discount);
    request.requestedStatus = requestedStatus;
    request.worksSum = worksSum;
    request.partsSum = partsSum;

    const auto validation = OrderDialogCoordinator::validateSubmissionInput(request);
    if (!validation.ok
        && OrderDialogFlowController::showValidationErrorAndFocus(
            this,
            validation,
            m_tabs,
            m_plate,
            m_vin,
            m_odometer,
            m_openedDate,
            QString::fromUtf8("Оберіть майстра"))) {
        return;
    }

    if (m_status) {
        const int resolvedStatusIndex = statusComboIndexByValue(m_status, validation.effectiveStatus);
        if (resolvedStatusIndex >= 0) m_status->setCurrentIndex(resolvedStatusIndex);
    }
    request.input.status = validation.effectiveStatus;
    request.input.number = m_orderNumber;
    request.requestedVisibleNumber = visibleNumber;
    request.pendingSyncFile = property("sync.pending_source_file").toString();
    request.importSyncParts = AppRuntimeService::loadSyncAutoPartsEnabled();

    // The shared authority commits status + physical release together. Local
    // persistence below is a replica; a failed local save is recovered by sync.
    if(m_orderNumber>0 && (request.input.status=="closed" || request.input.status=="cancelled")) {
        QString oldStatus, statusError;
        if(!OrderDialogReadQueryService::loadCurrentStatus(m_orderNumber, &oldStatus, &statusError)) {
            QMessageBox::warning(this, QString::fromUtf8("Статус наряду"), statusError);
            return;
        }
        if(oldStatus!=request.input.status) {
            const auto uid=OrderDialogReadQueryService::canonicalOrderUidForInternalNumber(m_orderNumber);
            if(!BayOccupancyPage::closeOrders(this,{uid},request.input.status))return;
        }
    }
    const auto submitResult = OrderDialogCoordinator::submitEditableOrder(request);
    if (!submitResult.ok) {
        QMessageBox::critical(this,
                              QString::fromUtf8("Помилка БД"),
                              QString::fromUtf8("Наряд створено, але не вдалося підготувати його для редагування робіт та запчастин.\n%1").arg(submitResult.error));
        return;
    }

    OrderDialogSubmitSupport::handleSubmitSyncFeedback(this,
                                                      this,
                                                      request.pendingSyncFile,
                                                      submitResult.syncWarning);

    OrderDialogSubmitSupport::applySubmitSuccessState(this,
                                                      m_num,
                                                      m_orderNumber,
                                                      m_draftCreated,
                                                      submitResult.orderNumber,
                                                      submitResult.displayNumber,
                                                      visibleNumber);

    refreshTotals();

    if (m_isNewOrder) {
        QString stockError;
        if (!issuePendingSpare(&stockError)) {
            m_draftCreated=true;
            QMessageBox::warning(this,QString::fromUtf8("Не вдалося видати запчастину"),stockError);
            return;
        }
        refreshTotals();
        if (m_newOrderPost && m_newOrderPost->currentData().toInt() > 0) {
            const auto uid = OrderDialogReadQueryService::canonicalOrderUidForInternalNumber(m_orderNumber);
            if (!uid.isEmpty()) BayOccupancyPage::open(this, uid, m_newOrderPost->currentData().toInt());
        }
        OrderDialogSubmitSupport::scheduleOpenCreatedOrderEditor(parentWidget(), m_orderNumber);
    }

    captureSavedState();
    if (RuntimeLoggingControl::deepDebugEnabled()) qInfo() << "OrderDialog accept elapsed_ms" << timer.elapsed() << "order" << m_orderNumber;
    QDialog::accept();
}
