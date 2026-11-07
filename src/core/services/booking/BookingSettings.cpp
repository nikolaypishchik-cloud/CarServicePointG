#include "core/services/booking/BookingSettings.h"

#include "infrastructure/db/sqlite/Db.h"

#include <QtGlobal>

namespace {
constexpr const char* kDefaultBookingBaseUrl = "https://carservicepointg.com.ua";
constexpr const char* kDefaultBookingToken = "";
}

BookingSyncSettings BookingSettings::load()
{
    BookingSyncSettings s;
    s.baseUrl = Database::getSetting(QStringLiteral("settings.booking.api.base_url"), QString::fromLatin1(kDefaultBookingBaseUrl)).toString().trimmed();
    s.token = Database::getSetting(QStringLiteral("settings.booking.api.token"), QString::fromLatin1(kDefaultBookingToken)).toString().trimmed();

    const QString envBaseUrl = qEnvironmentVariable("CSPG_BOOKING_API_BASE_URL").trimmed();
    if (!envBaseUrl.isEmpty()) s.baseUrl = envBaseUrl;
    const QString envToken = qEnvironmentVariable("CSPG_BOOKING_TOKEN").trimmed();
    if (!envToken.isEmpty()) s.token = envToken;

    s.enabled = Database::getSetting(QStringLiteral("settings.booking.sync.enabled"), true).toBool() && !s.baseUrl.isEmpty();
    s.pollIntervalMs = qBound(5000, Database::getSetting(QStringLiteral("settings.booking.sync.poll_ms"), 10000).toInt(), 120000);
    s.workStartHour = qBound(0, Database::getSetting(QStringLiteral("settings.booking.work_start_hour"), 9).toInt(), 23);
    s.workEndHour = qBound(s.workStartHour + 1, Database::getSetting(QStringLiteral("settings.booking.work_end_hour"), 18).toInt(), 24);
    s.slotMinutes = qBound(15, Database::getSetting(QStringLiteral("settings.booking.slot_minutes"), 30).toInt(), 240);
    s.capacityPerSlot = qBound(1, Database::getSetting(QStringLiteral("settings.booking.capacity_per_slot"), 1).toInt(), 20);
    s.longTermBlocksDay = Database::getSetting(QStringLiteral("settings.booking.long_term_blocks_day"), true).toBool();
    s.workDaysCsv = Database::getSetting(QStringLiteral("settings.booking.work_days"), QStringLiteral("1,2,3,4,5,6")).toString().trimmed();
    if (s.workDaysCsv.isEmpty()) s.workDaysCsv = QStringLiteral("1,2,3,4,5,6");
    return s;
}


bool BookingSettings::save(const BookingSyncSettings& settings)
{
    const QString baseUrl = settings.baseUrl.trimmed();
    const QString token = settings.token.trimmed();
    const int pollMs = qBound(5000, settings.pollIntervalMs, 120000);
    const int workStart = qBound(0, settings.workStartHour, 23);
    const int workEnd = qBound(workStart + 1, settings.workEndHour, 24);
    const int slotMinutes = qBound(15, settings.slotMinutes, 240);
    const int capacity = qBound(1, settings.capacityPerSlot, 20);
    QString workDays = settings.workDaysCsv.trimmed();
    if (workDays.isEmpty()) {
        workDays = QStringLiteral("1,2,3,4,5,6");
    }

    Database::setSetting(QStringLiteral("settings.booking.sync.enabled"), settings.enabled);
    Database::setSetting(QStringLiteral("settings.booking.api.base_url"), baseUrl);
    Database::setSetting(QStringLiteral("settings.booking.api.token"), token);
    Database::setSetting(QStringLiteral("settings.booking.sync.poll_ms"), pollMs);
    Database::setSetting(QStringLiteral("settings.booking.work_start_hour"), workStart);
    Database::setSetting(QStringLiteral("settings.booking.work_end_hour"), workEnd);
    Database::setSetting(QStringLiteral("settings.booking.slot_minutes"), slotMinutes);
    Database::setSetting(QStringLiteral("settings.booking.capacity_per_slot"), capacity);
    Database::setSetting(QStringLiteral("settings.booking.long_term_blocks_day"), settings.longTermBlocksDay);
    Database::setSetting(QStringLiteral("settings.booking.work_days"), workDays);
    return true;
}
