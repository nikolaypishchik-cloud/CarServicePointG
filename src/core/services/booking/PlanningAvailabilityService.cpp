#include "core/services/booking/PlanningAvailabilityService.h"

#include "core/services/booking/BookingSettings.h"
#include "core/services/planning/PlanningRepository.h"
#include "infrastructure/db/sqlite/Db.h"

#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QStringList>

namespace {

QSet<int> parseWorkDays(const QString& csv)
{
    QSet<int> out;
    const QStringList parts = csv.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        bool ok = false;
        const int day = part.trimmed().toInt(&ok);
        if (ok && day >= 1 && day <= 7) out.insert(day);
    }
    if (out.isEmpty()) {
        for (int d = 1; d <= 6; ++d) out.insert(d);
    }
    return out;
}

bool containsLongTermRepairMarker(const QString& text)
{
    const QString s = text.toLower();
    return s.contains(QString::fromUtf8("довгострок"))
        || s.contains(QString::fromUtf8("довготривал"))
        || s.contains(QString::fromUtf8("тривалий ремонт"))
        || s.contains(QString::fromUtf8("довгий ремонт"))
        || s.contains(QStringLiteral("long-term"))
        || s.contains(QStringLiteral("long term"));
}

QString comparablePlanningEntry(QString text)
{
    text.remove(QString::fromUtf8("✕"));
    text.remove(QString::fromUtf8("❌"));
    text.remove(QString::fromUtf8("✓"));
    text.remove(QString::fromUtf8("✅"));
    text.replace(QRegularExpression(QStringLiteral(R"(\s+)")), QStringLiteral(" "));
    text = text.trimmed().toLower();
    while (text.endsWith(QLatin1Char('.')) || text.endsWith(QLatin1Char(';')) || text.endsWith(QLatin1Char(','))) text.chop(1);
    return text.trimmed();
}

QString alnumUpper(QString text)
{
    text.remove(QRegularExpression(QStringLiteral(R"([^\p{L}0-9]+)")));
    return text.toUpper();
}

bool entryLooksLikeSameOnlineBooking(const QString& entry, const BookingRecord& booking)
{
    const QString plate = alnumUpper(booking.carNumber);
    if (plate.isEmpty()) return false;
    const QString entryKey = alnumUpper(entry);
    if (!entryKey.contains(plate)) return false;
    if (booking.timeFrom.isValid() && !entry.contains(booking.timeFrom.toString(QStringLiteral("HH:mm")))) return false;
    return entry.contains(QStringLiteral("[Online]"), Qt::CaseInsensitive)
        || entry.contains(QStringLiteral("Online"), Qt::CaseInsensitive);
}

bool sameBookingAlreadyInPlanning(const QMap<int, QString>& dayMap, const BookingRecord& booking)
{
    const QString compact = comparablePlanningEntry(bookingPlanningText(booking));
    const QString marker = booking.bookingUid.trimmed().isEmpty()
        ? QString()
        : QStringLiteral("booking:%1").arg(booking.bookingUid.trimmed()).toLower();

    for (auto it = dayMap.cbegin(); it != dayMap.cend(); ++it) {
        const QString value = it.value();
        if (!marker.isEmpty() && value.toLower().contains(marker)) return true;
        const QStringList entries = value.split(QStringLiteral(" || "), Qt::SkipEmptyParts);
        for (const QString& entry : entries) {
            if (!compact.isEmpty() && comparablePlanningEntry(entry) == compact) return true;
            if (entryLooksLikeSameOnlineBooking(entry, booking)) return true;
        }
    }
    return false;
}

int planningSlotEntryCount(const QString& value)
{
    const QString s = value.trimmed();
    if (s.isEmpty()) return 0;
    return qMax(1, s.split(QStringLiteral(" || "), Qt::SkipEmptyParts).size());
}

QString appendPlanningSlotEntry(const QString& existing, const QString& entry)
{
    const QString left = existing.trimmed();
    const QString right = entry.trimmed();
    if (left.isEmpty()) return right;
    if (right.isEmpty()) return left;
    return left + QStringLiteral(" || ") + right;
}

QStringList splitPlanningSlotEntries(const QString& value)
{
    return value.split(QStringLiteral(" || "), Qt::SkipEmptyParts);
}

QString preserveReminderDecisionMarker(const QString& oldEntry, const QString& newEntry)
{
    const QString trimmed = oldEntry.trimmed();
    if (trimmed.startsWith(QString::fromUtf8("✅")) || trimmed.startsWith(QString::fromUtf8("✓"))) {
        return QString::fromUtf8("✅ %1").arg(newEntry.trimmed());
    }
    if (trimmed.startsWith(QString::fromUtf8("❌")) || trimmed.startsWith(QString::fromUtf8("✕"))) {
        return QString::fromUtf8("❌ %1").arg(newEntry.trimmed());
    }
    return newEntry.trimmed();
}

int bookingHour(const BookingRecord& booking)
{
    return booking.timeFrom.isValid() ? booking.timeFrom.hour() : -1;
}

QString slotText(int hour, int minute)
{
    return QStringLiteral("%1:%2")
        .arg(hour, 2, 10, QChar('0'))
        .arg(minute, 2, 10, QChar('0'));
}

} // namespace

bool PlanningAvailabilityService::canAcceptOnlineBooking(const BookingRecord& booking, QString* reason) const
{
    const BookingSyncSettings settings = BookingSettings::load();
    if (!booking.date.isValid()) {
        if (reason) *reason = QString::fromUtf8("Некоректна дата онлайн-запису");
        return false;
    }
    if (!booking.timeFrom.isValid()) {
        if (reason) *reason = QString::fromUtf8("Некоректний час онлайн-запису");
        return false;
    }
    if (!bookingStatusBlocksPlanning(booking.status)) {
        if (reason) *reason = QString::fromUtf8("Статус запису не блокує планування");
        return false;
    }

    const QSet<int> workDays = parseWorkDays(settings.workDaysCsv);
    if (!workDays.contains(booking.date.dayOfWeek())) {
        if (reason) *reason = QString::fromUtf8("Цей день не є робочим для онлайн-запису");
        return false;
    }

    const int hour = bookingHour(booking);
    if (hour < settings.workStartHour || hour >= settings.workEndHour) {
        if (reason) *reason = QString::fromUtf8("Обраний час поза робочими годинами");
        return false;
    }

    PlanningRepository repo;
    const QMap<int, QString> dayMap = repo.getDayMap(booking.date);
    if (sameBookingAlreadyInPlanning(dayMap, booking)) {
        return true;
    }

    if (settings.longTermBlocksDay) {
        for (auto it = dayMap.cbegin(); it != dayMap.cend(); ++it) {
            if (containsLongTermRepairMarker(it.value())) {
                if (reason) *reason = QString::fromUtf8("День заблокований довгостроковим ремонтом");
                return false;
            }
        }
    }

    const QString existing = dayMap.value(hour).trimmed();
    if (planningSlotEntryCount(existing) >= settings.capacityPerSlot) {
        if (reason) *reason = QString::fromUtf8("У цей час у Плануванні вже немає вільного поста/майстра");
        return false;
    }

    return true;
}

bool PlanningAvailabilityService::addOnlineBookingToPlanning(const BookingRecord& booking, QString* reason) const
{
    if (!canAcceptOnlineBooking(booking, reason)) return false;
    const int hour = bookingHour(booking);
    if (hour < 0) {
        if (reason) *reason = QString::fromUtf8("Некоректний час онлайн-запису");
        return false;
    }

    QSqlDatabase db = Database::db();
    const bool outerTransaction = db.transaction();
    PlanningRepository repo;
    QMap<int, QString> dayMap = repo.getDayMap(booking.date);
    if (sameBookingAlreadyInPlanning(dayMap, booking)) {
        if (outerTransaction && !db.commit()) {
            if (reason) *reason = db.lastError().text();
            db.rollback();
            return false;
        }
        return true;
    }
    const BookingSyncSettings settings = BookingSettings::load();
    const QString existing = dayMap.value(hour).trimmed();
    if (!sameBookingAlreadyInPlanning(dayMap, booking) && planningSlotEntryCount(existing) >= settings.capacityPerSlot) {
        if (outerTransaction) db.rollback();
        if (reason) *reason = QString::fromUtf8("Слот уже зайнятий під час запису в Планування");
        return false;
    }

    dayMap.insert(hour, appendPlanningSlotEntry(existing, bookingPlanningText(booking)));
    repo.setDayMap(booking.date, dayMap);

    if (outerTransaction && !db.commit()) {
        if (reason) *reason = db.lastError().text();
        db.rollback();
        return false;
    }
    return true;
}


bool PlanningAvailabilityService::refreshOnlineBookingPlanningText(const BookingRecord& booking, QString* reason) const
{
    if (!booking.date.isValid() || booking.bookingUid.trimmed().isEmpty()) {
        if (reason) *reason = QString::fromUtf8("Некоректний онлайн-запис для оновлення Планування");
        return false;
    }

    QSqlDatabase db = Database::db();
    const bool outerTransaction = db.transaction();
    PlanningRepository repo;
    QMap<int, QString> dayMap = repo.getDayMap(booking.date);
    const QString marker = QStringLiteral("booking:%1").arg(booking.bookingUid.trimmed()).toLower();
    const QString updatedEntry = bookingPlanningText(booking);
    const QString updatedComparable = comparablePlanningEntry(updatedEntry);

    bool changed = false;
    for (auto it = dayMap.begin(); it != dayMap.end(); ++it) {
        QStringList entries = splitPlanningSlotEntries(it.value());
        bool entryChanged = false;
        for (QString& entry : entries) {
            const bool sameMarked = entry.toLower().contains(marker);
            const bool sameCompact = !updatedComparable.isEmpty() && comparablePlanningEntry(entry) == updatedComparable;
            const bool sameOnlineBooking = entryLooksLikeSameOnlineBooking(entry, booking);
            if (!sameMarked && !sameCompact && !sameOnlineBooking) continue;
            const QString replacement = preserveReminderDecisionMarker(entry, updatedEntry);
            if (entry.trimmed() != replacement.trimmed()) {
                entry = replacement;
                entryChanged = true;
                changed = true;
            }
        }
        if (entryChanged) it.value() = entries.join(QStringLiteral(" || "));
    }

    if (changed) {
        repo.setDayMap(booking.date, dayMap);
    }

    if (outerTransaction && !db.commit()) {
        if (reason) *reason = db.lastError().text();
        db.rollback();
        return false;
    }
    return true;
}

QStringList PlanningAvailabilityService::freeSlotsForDate(const QDate& day) const
{
    // Do not name this variable "slots": Qt defines "slots" as a keyword macro
    // in builds without QT_NO_KEYWORDS, which MSVC expands before compilation.
    QStringList freeSlotTimes;
    if (!day.isValid()) return freeSlotTimes;
    const BookingSyncSettings settings = BookingSettings::load();
    const QSet<int> workDays = parseWorkDays(settings.workDaysCsv);
    if (!workDays.contains(day.dayOfWeek())) return freeSlotTimes;

    PlanningRepository repo;
    const QMap<int, QString> dayMap = repo.getDayMap(day);
    if (settings.longTermBlocksDay) {
        for (auto it = dayMap.cbegin(); it != dayMap.cend(); ++it) {
            if (containsLongTermRepairMarker(it.value())) return freeSlotTimes;
        }
    }

    const int step = qBound(15, settings.slotMinutes, 240);
    for (int minutesFromStart = settings.workStartHour * 60;
         minutesFromStart < settings.workEndHour * 60;
         minutesFromStart += step) {
        const int h = minutesFromStart / 60;
        const int m = minutesFromStart % 60;
        if (planningSlotEntryCount(dayMap.value(h)) < settings.capacityPerSlot) {
            freeSlotTimes << slotText(h, m);
        }
    }
    return freeSlotTimes;
}
