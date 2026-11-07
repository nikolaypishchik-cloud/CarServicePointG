#include "core/util/DateFormats.h"
#include "core/services/booking/BookingRepository.h"

#include "infrastructure/db/sqlite/Db.h"
#include "core/util/PlateUtils.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QVariant>

namespace {
QString textOrEmpty(QString value)
{
    value = value.trimmed();
    return value.isNull() ? QStringLiteral("") : value;
}

QString isoDate(const QDate& d) { return d.isValid() ? d.toString(Qt::ISODate) : QStringLiteral(""); }
QString isoTime(const QTime& t) { return t.isValid() ? t.toString(QStringLiteral("HH:mm:ss")) : QStringLiteral(""); }
QString isoDateTime(const QDateTime& dt)
{
    if (!dt.isValid()) return QStringLiteral("");
    return dt.toUTC().toString(Qt::ISODateWithMs);
}


QString queryValue(const QSqlQuery& query, const QString& field)
{
    const int index = query.record().indexOf(field);
    if (index < 0) return QString();
    return query.value(index).toString().trimmed();
}

QDate parseIsoDateValue(const QString& value)
{
    return DateFormats::parseDate(value.trimmed());
}

QTime parseIsoTimeValue(const QString& value)
{
    QString v = value.trimmed();
    if (v.size() >= 5) v = v.left(5);
    return QTime::fromString(v, QStringLiteral("HH:mm"));
}

QDateTime parseIsoDateTimeValue(const QString& value)
{
    const QDateTime dtMs = DateFormats::parseDateTime(value.trimmed());
    if (dtMs.isValid()) return dtMs;
    return DateFormats::parseDateTime(value.trimmed());
}

BookingRecord bookingRecordFromQuery(const QSqlQuery& query)
{
    BookingRecord record;
    record.id = queryValue(query, QStringLiteral("id")).toInt();
    record.bookingUid = queryValue(query, QStringLiteral("booking_uid"));
    record.clientName = queryValue(query, QStringLiteral("client_name"));
    record.clientPhone = queryValue(query, QStringLiteral("client_phone"));
    record.clientEmail = queryValue(query, QStringLiteral("client_email"));
    record.carMakeModel = queryValue(query, QStringLiteral("car_make_model"));
    record.carMake = queryValue(query, QStringLiteral("car_make"));
    record.carModel = queryValue(query, QStringLiteral("car_model"));
    record.carYear = queryValue(query, QStringLiteral("car_year"));
    record.fuelType = queryValue(query, QStringLiteral("fuel_type"));
    record.engineVolumeL = queryValue(query, QStringLiteral("engine_volume_l"));
    record.odometerKm = queryValue(query, QStringLiteral("odometer_km"));
    record.carNumber = queryValue(query, QStringLiteral("car_number"));
    record.serviceType = queryValue(query, QStringLiteral("service_type"));
    record.comment = queryValue(query, QStringLiteral("comment"));
    record.date = parseIsoDateValue(queryValue(query, QStringLiteral("date")));
    record.timeFrom = parseIsoTimeValue(queryValue(query, QStringLiteral("time_from")));
    record.timeTo = parseIsoTimeValue(queryValue(query, QStringLiteral("time_to")));
    record.status = queryValue(query, QStringLiteral("status"));
    record.source = queryValue(query, QStringLiteral("source"));
    record.createdAt = parseIsoDateTimeValue(queryValue(query, QStringLiteral("created_at")));
    record.updatedAt = parseIsoDateTimeValue(queryValue(query, QStringLiteral("updated_at")));
    return record;
}

bool ensureOptionalColumn(QSqlDatabase db, const QString& tableName, const QString& columnName, const QString& columnSql)
{
    QSqlQuery info(db);
    if (!info.exec(QStringLiteral("PRAGMA table_info(%1)").arg(tableName))) {
        return false;
    }
    while (info.next()) {
        if (info.value(1).toString().compare(columnName, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }

    QSqlQuery alter(db);
    return alter.exec(QStringLiteral("ALTER TABLE %1 ADD COLUMN %2").arg(tableName, columnSql));
}
}

bool BookingRepository::ensureSchema(QString* errorText) const
{
    QSqlDatabase db = Database::db();
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS online_bookings ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " booking_uid TEXT NOT NULL UNIQUE,"
            " client_name TEXT NOT NULL DEFAULT '',"
            " client_phone TEXT NOT NULL DEFAULT '',"
            " client_email TEXT NOT NULL DEFAULT '',"
            " car_make_model TEXT NOT NULL DEFAULT '',"
            " car_make TEXT NOT NULL DEFAULT '',"
            " car_model TEXT NOT NULL DEFAULT '',"
            " car_year TEXT NOT NULL DEFAULT '',"
            " fuel_type TEXT NOT NULL DEFAULT '',"
            " engine_volume_l TEXT NOT NULL DEFAULT '',"
            " odometer_km TEXT NOT NULL DEFAULT '',"
            " car_number TEXT NOT NULL DEFAULT '',"
            " service_type TEXT NOT NULL DEFAULT '',"
            " comment TEXT NOT NULL DEFAULT '',"
            " date TEXT NOT NULL DEFAULT '',"
            " time_from TEXT NOT NULL DEFAULT '',"
            " time_to TEXT NOT NULL DEFAULT '',"
            " status TEXT NOT NULL DEFAULT 'pending',"
            " source TEXT NOT NULL DEFAULT 'online',"
            " planning_day TEXT NOT NULL DEFAULT '',"
            " planning_hour INTEGER NOT NULL DEFAULT -1,"
            " planning_synced INTEGER NOT NULL DEFAULT 0,"
            " rejection_reason TEXT NOT NULL DEFAULT '',"
            " created_at TEXT NOT NULL DEFAULT (datetime('now')),"
            " updated_at TEXT NOT NULL DEFAULT (datetime('now'))"
            ")"))) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_online_bookings_updated_at ON online_bookings(updated_at)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_online_bookings_status ON online_bookings(status)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_online_bookings_day_hour ON online_bookings(planning_day, planning_hour)"));
    q.exec(QStringLiteral("UPDATE online_bookings SET rejection_reason='' WHERE rejection_reason IS NULL"));

    bool ok = true;
    ok = ensureOptionalColumn(db, QStringLiteral("online_bookings"), QStringLiteral("car_make"), QStringLiteral("car_make TEXT NOT NULL DEFAULT ''")) && ok;
    ok = ensureOptionalColumn(db, QStringLiteral("online_bookings"), QStringLiteral("car_model"), QStringLiteral("car_model TEXT NOT NULL DEFAULT ''")) && ok;
    ok = ensureOptionalColumn(db, QStringLiteral("online_bookings"), QStringLiteral("car_year"), QStringLiteral("car_year TEXT NOT NULL DEFAULT ''")) && ok;
    ok = ensureOptionalColumn(db, QStringLiteral("online_bookings"), QStringLiteral("fuel_type"), QStringLiteral("fuel_type TEXT NOT NULL DEFAULT ''")) && ok;
    ok = ensureOptionalColumn(db, QStringLiteral("online_bookings"), QStringLiteral("engine_volume_l"), QStringLiteral("engine_volume_l TEXT NOT NULL DEFAULT ''")) && ok;
    ok = ensureOptionalColumn(db, QStringLiteral("online_bookings"), QStringLiteral("odometer_km"), QStringLiteral("odometer_km TEXT NOT NULL DEFAULT ''")) && ok;
    if (!ok && errorText && errorText->isEmpty()) {
        *errorText = QStringLiteral("Failed to ensure online_bookings optional columns");
    }
    return ok;
}

bool BookingRepository::upsertBooking(const BookingRecord& booking, bool planningSynced, const QString& rejectionReason, QString* errorText) const
{
    if (booking.bookingUid.trimmed().isEmpty()) {
        if (errorText) *errorText = QStringLiteral("booking_uid is empty");
        return false;
    }
    if (!ensureSchema(errorText)) return false;

    QSqlQuery q(Database::db());
    q.prepare(QStringLiteral(
        "INSERT INTO online_bookings(booking_uid, client_name, client_phone, client_email, car_make_model, car_make, car_model, car_year, fuel_type, engine_volume_l, odometer_km, car_number, service_type, comment, date, time_from, time_to, status, source, planning_synced, rejection_reason, created_at, updated_at) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(booking_uid) DO UPDATE SET "
        " client_name=excluded.client_name, client_phone=excluded.client_phone, client_email=excluded.client_email, car_make_model=excluded.car_make_model,"
        " car_make=excluded.car_make, car_model=excluded.car_model, car_year=excluded.car_year, fuel_type=excluded.fuel_type, engine_volume_l=excluded.engine_volume_l, odometer_km=excluded.odometer_km,"
        " car_number=excluded.car_number, service_type=excluded.service_type, comment=excluded.comment, date=excluded.date, time_from=excluded.time_from, time_to=excluded.time_to,"
        " status=excluded.status, source=excluded.source, planning_synced=max(online_bookings.planning_synced, excluded.planning_synced),"
        " rejection_reason=CASE WHEN excluded.rejection_reason<>'' THEN excluded.rejection_reason ELSE online_bookings.rejection_reason END,"
        " updated_at=CASE WHEN excluded.updated_at<>'' THEN excluded.updated_at ELSE datetime('now') END"));
    q.addBindValue(textOrEmpty(booking.bookingUid));
    q.addBindValue(textOrEmpty(booking.clientName));
    q.addBindValue(textOrEmpty(booking.clientPhone));
    q.addBindValue(textOrEmpty(booking.clientEmail));
    q.addBindValue(textOrEmpty(booking.carMakeModel));
    q.addBindValue(textOrEmpty(booking.carMake));
    q.addBindValue(textOrEmpty(booking.carModel));
    q.addBindValue(textOrEmpty(booking.carYear));
    q.addBindValue(textOrEmpty(booking.fuelType));
    q.addBindValue(textOrEmpty(booking.engineVolumeL));
    q.addBindValue(textOrEmpty(booking.odometerKm));
    q.addBindValue(textOrEmpty(booking.carNumber));
    q.addBindValue(textOrEmpty(booking.serviceType));
    q.addBindValue(textOrEmpty(booking.comment));
    q.addBindValue(isoDate(booking.date));
    q.addBindValue(isoTime(booking.timeFrom));
    q.addBindValue(isoTime(booking.timeTo));
    q.addBindValue(bookingStatusNormalized(booking.status));
    const QString source = textOrEmpty(booking.source);
    q.addBindValue(source.isEmpty() ? QStringLiteral("online") : source);
    q.addBindValue(planningSynced ? 1 : 0);
    q.addBindValue(textOrEmpty(rejectionReason));
    q.addBindValue(isoDateTime(booking.createdAt));
    q.addBindValue(isoDateTime(booking.updatedAt));
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    return true;
}

bool BookingRepository::isPlanningSynced(const QString& bookingUid) const
{
    if (bookingUid.trimmed().isEmpty()) return false;
    QString ignored;
    if (!ensureSchema(&ignored)) return false;
    QSqlQuery q(Database::db());
    q.prepare(QStringLiteral("SELECT planning_synced FROM online_bookings WHERE booking_uid=? LIMIT 1"));
    q.addBindValue(bookingUid.trimmed());
    if (!q.exec() || !q.next()) return false;
    return q.value(0).toInt() != 0;
}

bool BookingRepository::markPlanningSynced(const QString& bookingUid, const QDate& day, int hour, QString* errorText) const
{
    if (bookingUid.trimmed().isEmpty()) return false;
    if (!ensureSchema(errorText)) return false;
    QSqlQuery q(Database::db());
    q.prepare(QStringLiteral("UPDATE online_bookings SET planning_synced=1, planning_day=?, planning_hour=?, updated_at=datetime('now') WHERE booking_uid=?"));
    q.addBindValue(isoDate(day));
    q.addBindValue(hour);
    q.addBindValue(bookingUid.trimmed());
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    return true;
}

bool BookingRepository::updateLocalStatus(const QString& bookingUid, const QString& status, const QString& reason, QString* errorText) const
{
    if (bookingUid.trimmed().isEmpty()) return false;
    if (!ensureSchema(errorText)) return false;
    QSqlQuery q(Database::db());
    q.prepare(QStringLiteral("UPDATE online_bookings SET status=?, rejection_reason=CASE WHEN ?<>'' THEN ? ELSE rejection_reason END, updated_at=datetime('now') WHERE booking_uid=?"));
    const QString r = reason.trimmed();
    q.addBindValue(bookingStatusNormalized(status));
    q.addBindValue(r);
    q.addBindValue(r);
    q.addBindValue(bookingUid.trimmed());
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    return true;
}


std::optional<BookingRecord> BookingRepository::findByDateTimePlate(const QDate& date, const QTime& timeFrom, const QString& plateDisplay, QString* errorText) const
{
    if (!date.isValid() || !timeFrom.isValid()) return std::nullopt;
    if (!ensureSchema(errorText)) return std::nullopt;

    QSqlQuery q(Database::db());
    q.prepare(QStringLiteral(
        "SELECT id, booking_uid, client_name, client_phone, client_email, car_make_model, car_make, car_model, car_year, fuel_type, engine_volume_l, odometer_km, car_number, service_type, comment, date, time_from, time_to, status, source, created_at, updated_at "
        "FROM online_bookings "
        "WHERE date IN (?,?) AND substr(time_from,1,5)=? "
        "ORDER BY updated_at DESC LIMIT 20"));
    q.addBindValue(isoDate(date));
    q.addBindValue(date.toString("dd.MM.yy"));
    q.addBindValue(timeFrom.toString(QStringLiteral("HH:mm")));
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return std::nullopt;
    }

    const QString requestedPlateNorm = PlateUtils::normalizePlate(plateDisplay).trimmed().toUpper();
    std::optional<BookingRecord> firstCandidate;
    while (q.next()) {
        BookingRecord record = bookingRecordFromQuery(q);
        if (!firstCandidate.has_value()) firstCandidate = record;

        const QString rowPlateNorm = PlateUtils::normalizePlate(record.carNumber).trimmed().toUpper();
        if (requestedPlateNorm.isEmpty() || rowPlateNorm.isEmpty() || rowPlateNorm == requestedPlateNorm) {
            return record;
        }
    }
    return firstCandidate;
}

QDateTime BookingRepository::lastSyncTime() const
{
    const QString raw = Database::getSetting(QStringLiteral("settings.booking.sync.last_sync_at"), QString()).toString().trimmed();
    const QDateTime dt = DateFormats::parseDateTime(raw);
    return dt.isValid() ? dt : QDateTime();
}

void BookingRepository::setLastSyncTime(const QDateTime& value) const
{
    if (!value.isValid()) return;
    Database::setSetting(QStringLiteral("settings.booking.sync.last_sync_at"), value.toUTC().toString(Qt::ISODateWithMs));
}
