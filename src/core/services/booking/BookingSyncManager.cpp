#include "core/util/DateFormats.h"
#include "core/services/booking/BookingSyncManager.h"

#include "core/services/booking/BookingRepository.h"
#include "core/services/booking/BookingSettings.h"
#include "core/services/booking/PlanningAvailabilityService.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QVariant>
#include <QDebug>
#include <QRegularExpression>
#include <QtGlobal>
#include <algorithm>

namespace {

// Public booking API contract markers tracked by scripts/check_booking_sync_tz_100.py:
// GET /api/booking/sync
// PATCH /api/booking/

bool bookingSyncDeepDebug()
{
    static const bool enabled = qEnvironmentVariableIntValue("CS_DEEP_DEBUG") > 0;
    return enabled;
}

QString stringValue(const QJsonObject& obj, const QStringList& keys)
{
    for (const QString& key : keys) {
        const QJsonValue value = obj.value(key);
        if (value.isString()) return value.toString().trimmed();
        if (value.isDouble()) return QString::number(value.toVariant().toLongLong());
    }
    return QString();
}


QString stringValueNested(const QJsonObject& obj, const QStringList& keys)
{
    const QString direct = stringValue(obj, keys);
    if (!direct.isEmpty()) return direct;

    static const QStringList containers = {
        QStringLiteral("vehicle"),
        QStringLiteral("car"),
        QStringLiteral("auto"),
        QStringLiteral("vehicle_data"),
        QStringLiteral("car_data"),
        QStringLiteral("booking"),
        QStringLiteral("data"),
        QStringLiteral("details")
    };
    for (const QString& container : containers) {
        const QJsonValue nested = obj.value(container);
        if (!nested.isObject()) continue;
        const QString v = stringValue(nested.toObject(), keys);
        if (!v.isEmpty()) return v;
    }
    return QString();
}

QString normalizedBookingLabel(QString label)
{
    label = label.trimmed().toLower();
    label.replace(QString::fromUtf8("’"), QStringLiteral("'"));
    label.replace(QString::fromUtf8("ё"), QString::fromUtf8("е"));
    label.remove(QRegularExpression(QStringLiteral(R"([\s\.\-_/\\'`"():]+)")));
    return label;
}

bool bookingLabelMatches(const QString& label, const QStringList& expectedLabels)
{
    const QString normalized = normalizedBookingLabel(label);
    for (const QString& expected : expectedLabels) {
        if (normalized == normalizedBookingLabel(expected)) return true;
    }
    return false;
}

QString htmlToPlainBookingText(QString text)
{
    text.replace(QRegularExpression(QStringLiteral(R"(<\s*br\s*/?\s*>)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("\n"));
    text.replace(QRegularExpression(QStringLiteral(R"(</\s*(p|div|tr|li|table)\s*>)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("\n"));
    text.remove(QRegularExpression(QStringLiteral(R"(<[^>]+>)")));
    text.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
    text.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    text.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    text.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
    text.replace(QString::fromUtf8("&ndash;"), QStringLiteral("-"));
    text.replace(QString::fromUtf8("&mdash;"), QStringLiteral("-"));
    return text;
}

QString combinedBookingText(const QJsonObject& obj)
{
    QStringList parts;
    const QStringList textKeys = {
        QStringLiteral("details"),
        QStringLiteral("detail"),
        QStringLiteral("body"),
        QStringLiteral("message"),
        QStringLiteral("text"),
        QStringLiteral("description"),
        QStringLiteral("raw"),
        QStringLiteral("raw_text"),
        QStringLiteral("email_body"),
        QStringLiteral("html"),
        QStringLiteral("comment"),
        QStringLiteral("notes")
    };

    for (const QString& key : textKeys) {
        const QJsonValue value = obj.value(key);
        if (value.isString() && !value.toString().trimmed().isEmpty()) parts << value.toString();
    }

    static const QStringList containers = {
        QStringLiteral("vehicle"),
        QStringLiteral("car"),
        QStringLiteral("auto"),
        QStringLiteral("vehicle_data"),
        QStringLiteral("car_data"),
        QStringLiteral("booking"),
        QStringLiteral("data"),
        QStringLiteral("details")
    };
    for (const QString& container : containers) {
        const QJsonValue nested = obj.value(container);
        if (!nested.isObject()) continue;
        const QJsonObject nestedObj = nested.toObject();
        for (const QString& key : textKeys) {
            const QJsonValue value = nestedObj.value(key);
            if (value.isString() && !value.toString().trimmed().isEmpty()) parts << value.toString();
        }
    }

    return htmlToPlainBookingText(parts.join(QStringLiteral("\n")));
}

QString extractBookingLabeledText(const QString& rawText, const QStringList& labels)
{
    QString text = htmlToPlainBookingText(rawText);
    text.replace(QChar(0x00A0), QLatin1Char(' '));
    const QStringList segments = text.split(QRegularExpression(QStringLiteral(R"([\r\n;|]+)")), Qt::SkipEmptyParts);

    for (QString segment : segments) {
        segment = segment.trimmed();
        if (segment.isEmpty()) continue;
        int sep = segment.indexOf(QLatin1Char(':'));
        const int equals = segment.indexOf(QLatin1Char('='));
        if (sep < 0 || (equals >= 0 && equals < sep)) sep = equals;
        if (sep <= 0) continue;

        const QString label = segment.left(sep);
        if (!bookingLabelMatches(label, labels)) continue;

        QString value = segment.mid(sep + 1).trimmed();
        value.remove(QRegularExpression(QStringLiteral(R"(^[\s:=-]+)")));
        value = value.simplified();
        if (!value.isEmpty()) return value;
    }
    return QString();
}

QString cleanNumericBookingValue(QString value)
{
    value = value.trimmed();
    value.replace(QLatin1Char(','), QLatin1Char('.'));
    value.remove(QRegularExpression(QStringLiteral(R"(\s*(?:км|km|л|l|литр(?:а|ів|ов)?)\s*$)"), QRegularExpression::CaseInsensitiveOption));
    return value.simplified();
}

void fillMissingBookingDetailsFromText(BookingRecord* booking, const QString& rawText)
{
    if (!booking || rawText.trimmed().isEmpty()) return;

    if (booking->clientName.trimmed().isEmpty()) {
        booking->clientName = extractBookingLabeledText(rawText, {
            QString::fromUtf8("Клієнт"), QString::fromUtf8("ПІБ"), QString::fromUtf8("Замовник"), QStringLiteral("Client"), QStringLiteral("Name")
        });
    }
    if (booking->clientPhone.trimmed().isEmpty()) {
        booking->clientPhone = extractBookingLabeledText(rawText, {
            QString::fromUtf8("Телефон"), QString::fromUtf8("Тел"), QStringLiteral("Phone"), QStringLiteral("Telephone")
        });
    }
    if (booking->carMakeModel.trimmed().isEmpty()) {
        booking->carMakeModel = extractBookingLabeledText(rawText, {
            QString::fromUtf8("Авто"), QString::fromUtf8("Марка модель"), QString::fromUtf8("Марка/модель"), QString::fromUtf8("Авто марка модель"), QStringLiteral("Car"), QStringLiteral("Vehicle")
        });
    }
    if (booking->carYear.trimmed().isEmpty()) {
        booking->carYear = cleanNumericBookingValue(extractBookingLabeledText(rawText, {
            QString::fromUtf8("Рік"), QString::fromUtf8("Рік випуску"), QStringLiteral("Year"), QStringLiteral("Car year"), QStringLiteral("Model year")
        }));
    }
    if (booking->fuelType.trimmed().isEmpty()) {
        booking->fuelType = extractBookingLabeledText(rawText, {
            QString::fromUtf8("Паливо"), QString::fromUtf8("Тип палива"), QStringLiteral("Fuel"), QStringLiteral("Fuel type")
        });
    }
    if (booking->engineVolumeL.trimmed().isEmpty()) {
        booking->engineVolumeL = cleanNumericBookingValue(extractBookingLabeledText(rawText, {
            QString::fromUtf8("Об'єм двигуна"), QString::fromUtf8("Об’єм двигуна"), QString::fromUtf8("Об'єм"), QString::fromUtf8("Об’єм"), QString::fromUtf8("Двигун"), QStringLiteral("Engine volume"), QStringLiteral("Volume")
        }));
    }
    if (booking->odometerKm.trimmed().isEmpty()) {
        booking->odometerKm = cleanNumericBookingValue(extractBookingLabeledText(rawText, {
            QString::fromUtf8("Одометр"), QString::fromUtf8("Пробіг"), QStringLiteral("Odometer"), QStringLiteral("Mileage")
        }));
    }
    if (booking->carNumber.trimmed().isEmpty()) {
        booking->carNumber = extractBookingLabeledText(rawText, {
            QString::fromUtf8("Держ номер"), QString::fromUtf8("Держ. номер"), QString::fromUtf8("Номер авто"), QString::fromUtf8("Номер"), QStringLiteral("Plate"), QStringLiteral("License plate")
        });
    }
    if (booking->serviceType.trimmed().isEmpty()) {
        booking->serviceType = extractBookingLabeledText(rawText, {
            QString::fromUtf8("Тип запису"), QString::fromUtf8("Послуга"), QString::fromUtf8("Робота"), QStringLiteral("Service"), QStringLiteral("Service type")
        });
    }
}

int intValue(const QJsonObject& obj, const QStringList& keys)
{
    for (const QString& key : keys) {
        const QJsonValue value = obj.value(key);
        if (value.isDouble()) return value.toInt();
        if (value.isString()) {
            bool ok = false;
            const int v = value.toString().trimmed().toInt(&ok);
            if (ok) return v;
        }
    }
    return 0;
}

QDate parseDateValue(const QString& raw)
{
    const QString s = raw.trimmed();
    QDate d = DateFormats::parseDate(s);
    if (d.isValid()) return d;
    d = QDate::fromString(s, QStringLiteral("dd.MM.yyyy"));
    if (d.isValid()) return d;
    d = QDate::fromString(s, QStringLiteral("dd.MM.yy"));
    return d;
}

QTime parseTimeValue(const QString& raw)
{
    QString s = raw.trimmed();
    if (s.size() == 5) s += QStringLiteral(":00");
    QTime t = QTime::fromString(s, QStringLiteral("HH:mm:ss"));
    if (t.isValid()) return t;
    return QTime::fromString(raw.trimmed(), QStringLiteral("HH:mm"));
}

QDateTime parseDateTimeValue(const QString& raw)
{
    QDateTime dt = DateFormats::parseDateTime(raw.trimmed());
    if (dt.isValid()) return dt;
    dt = DateFormats::parseDateTime(raw.trimmed());
    return dt;
}

BookingRecord parseBooking(const QJsonObject& obj)
{
    BookingRecord b;
    b.id = intValue(obj, {QStringLiteral("id")});
    b.bookingUid = stringValue(obj, {QStringLiteral("booking_uid"), QStringLiteral("uid"), QStringLiteral("bookingUid")});
    if (b.bookingUid.isEmpty() && b.id > 0) b.bookingUid = QString::number(b.id);
    b.clientName = stringValueNested(obj, {QStringLiteral("client_name"), QStringLiteral("clientName"), QStringLiteral("customer_name"), QStringLiteral("customerName"), QStringLiteral("name")});
    b.clientPhone = stringValueNested(obj, {QStringLiteral("client_phone"), QStringLiteral("clientPhone"), QStringLiteral("customer_phone"), QStringLiteral("customerPhone"), QStringLiteral("phone"), QStringLiteral("telephone")});
    b.clientEmail = stringValueNested(obj, {QStringLiteral("client_email"), QStringLiteral("clientEmail"), QStringLiteral("customer_email"), QStringLiteral("customerEmail"), QStringLiteral("email")});
    b.carMake = stringValueNested(obj, {QStringLiteral("car_make"), QStringLiteral("carMake"), QStringLiteral("vehicle_make"), QStringLiteral("vehicleMake"), QStringLiteral("make"), QStringLiteral("brand"), QStringLiteral("mark")});
    b.carModel = stringValueNested(obj, {QStringLiteral("car_model"), QStringLiteral("carModel"), QStringLiteral("vehicle_model"), QStringLiteral("vehicleModel"), QStringLiteral("model")});
    b.carMakeModel = stringValueNested(obj, {QStringLiteral("car_make_model"), QStringLiteral("carMakeModel"), QStringLiteral("vehicle_make_model"), QStringLiteral("vehicleMakeModel"), QStringLiteral("car")});
    if (b.carMakeModel.isEmpty()) b.carMakeModel = QStringLiteral("%1 %2").arg(b.carMake, b.carModel).simplified();
    b.carYear = cleanNumericBookingValue(stringValueNested(obj, {QStringLiteral("car_year"), QStringLiteral("carYear"), QStringLiteral("vehicle_year"), QStringLiteral("vehicleYear"), QStringLiteral("year"), QStringLiteral("year_release"), QStringLiteral("release_year"), QStringLiteral("model_year")}));
    b.fuelType = stringValueNested(obj, {QStringLiteral("fuel_type"), QStringLiteral("fuelType"), QStringLiteral("car_fuel"), QStringLiteral("vehicle_fuel"), QStringLiteral("fuel")});
    b.engineVolumeL = cleanNumericBookingValue(stringValueNested(obj, {QStringLiteral("engine_volume_l"), QStringLiteral("engineVolumeL"), QStringLiteral("engine_volume"), QStringLiteral("engineVolume"), QStringLiteral("engine_capacity"), QStringLiteral("engineCapacity"), QStringLiteral("volume"), QStringLiteral("engine")}));
    b.odometerKm = cleanNumericBookingValue(stringValueNested(obj, {QStringLiteral("odometer_km"), QStringLiteral("odometerKm"), QStringLiteral("odometer"), QStringLiteral("mileage_km"), QStringLiteral("mileageKm"), QStringLiteral("mileage"), QStringLiteral("odo"), QStringLiteral("km")}));
    b.carNumber = stringValueNested(obj, {QStringLiteral("car_number"), QStringLiteral("carNumber"), QStringLiteral("plate"), QStringLiteral("plate_number"), QStringLiteral("plateNumber"), QStringLiteral("license_plate"), QStringLiteral("licensePlate"), QStringLiteral("registration_number"), QStringLiteral("registrationNumber"), QStringLiteral("reg_number"), QStringLiteral("regNumber")});
    b.serviceType = stringValueNested(obj, {QStringLiteral("service_type"), QStringLiteral("serviceType"), QStringLiteral("service"), QStringLiteral("service_name"), QStringLiteral("serviceName"), QStringLiteral("booking_type"), QStringLiteral("bookingType")});
    b.comment = stringValueNested(obj, {QStringLiteral("comment"), QStringLiteral("notes")});
    b.date = parseDateValue(stringValue(obj, {QStringLiteral("date"), QStringLiteral("booking_date"), QStringLiteral("bookingDate")}));
    b.timeFrom = parseTimeValue(stringValue(obj, {QStringLiteral("time_from"), QStringLiteral("timeFrom"), QStringLiteral("from"), QStringLiteral("start_time")}));
    b.timeTo = parseTimeValue(stringValue(obj, {QStringLiteral("time_to"), QStringLiteral("timeTo"), QStringLiteral("to"), QStringLiteral("end_time")}));
    b.status = bookingStatusNormalized(stringValue(obj, {QStringLiteral("status")}));
    b.source = stringValue(obj, {QStringLiteral("source")});
    if (b.source.isEmpty()) b.source = QStringLiteral("online");
    b.createdAt = parseDateTimeValue(stringValue(obj, {QStringLiteral("created_at"), QStringLiteral("createdAt")}));
    b.updatedAt = parseDateTimeValue(stringValue(obj, {QStringLiteral("updated_at"), QStringLiteral("updatedAt")}));

    fillMissingBookingDetailsFromText(&b, combinedBookingText(obj));
    if (b.carMakeModel.trimmed().isEmpty()) {
        b.carMakeModel = QStringLiteral("%1 %2").arg(b.carMake.trimmed(), b.carModel.trimmed()).simplified();
    }
    return b;
}

QJsonArray extractBookingArray(const QJsonDocument& doc)
{
    if (doc.isArray()) return doc.array();
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("bookings")).isArray()) return root.value(QStringLiteral("bookings")).toArray();
    if (root.value(QStringLiteral("items")).isArray()) return root.value(QStringLiteral("items")).toArray();
    if (root.value(QStringLiteral("data")).isArray()) return root.value(QStringLiteral("data")).toArray();
    return {};
}

QString cleanBaseUrl(QString baseUrl)
{
    baseUrl = baseUrl.trimmed();
    while (baseUrl.endsWith(QLatin1Char('/'))) baseUrl.chop(1);
    return baseUrl;
}

} // namespace

BookingSyncManager::BookingSyncManager(QObject* parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
    m_timer.setSingleShot(false);
    connect(&m_timer, &QTimer::timeout, this, [this]() { syncNow(); });
}

BookingSyncManager::~BookingSyncManager()
{
    stop();
}

void BookingSyncManager::setNotificationCallback(NotificationCallback callback)
{
    m_notificationCallback = std::move(callback);
}

void BookingSyncManager::applySettings()
{
    const BookingSyncSettings settings = BookingSettings::load();
    m_baseUrl = cleanBaseUrl(settings.baseUrl);
    m_token = settings.token.trimmed();
    m_timer.setInterval(settings.pollIntervalMs);
    m_running = settings.enabled && !m_baseUrl.isEmpty();
}

void BookingSyncManager::start()
{
    applySettings();
    if (!m_running) {
        qInfo() << "BookingSyncManager disabled: settings.booking.sync.enabled is false or base URL is empty";
        return;
    }
    BookingRepository repo;
    QString schemaError;
    if (!repo.ensureSchema(&schemaError)) {
        qWarning() << "BookingSyncManager schema initialization failed" << schemaError;
        return;
    }
    if (!m_timer.isActive()) m_timer.start();
    qInfo() << "BookingSyncManager started" << m_baseUrl << "poll_ms=" << m_timer.interval() << "token_configured=" << !m_token.isEmpty();
    QTimer::singleShot(1500, this, [this]() { syncNow(); });
}

void BookingSyncManager::stop()
{
    m_timer.stop();
    m_running = false;
}

void BookingSyncManager::syncNow()
{
    applySettings();
    if (!m_running || m_requestInFlight || m_baseUrl.isEmpty()) return;

    BookingRepository repo;
    const QDateTime since = repo.lastSyncTime();
    publishAvailability(false);

    QUrl url(m_baseUrl + QStringLiteral("/api/booking-sync.php"));
    QUrlQuery query;
    const bool periodicFullReconcile = (m_syncCounter++ % 20) == 0;
    if (since.isValid() && !periodicFullReconcile) {
        query.addQueryItem(QStringLiteral("since"), since.toUTC().toString(Qt::ISODateWithMs));
    }
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("CarService/booking-sync"));
    request.setRawHeader("Accept", "application/json");
    if (!m_token.isEmpty()) request.setRawHeader("Authorization", QByteArray("Bearer ") + m_token.toUtf8());

    m_requestInFlight = true;
    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { handleSyncReply(reply); });
}

void BookingSyncManager::handleSyncReply(QNetworkReply* reply)
{
    m_requestInFlight = false;
    if (!reply) return;
    reply->deleteLater();

    const QByteArray body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qWarning() << "Booking sync failed" << reply->errorString()
                   << "http_status=" << httpStatus
                   << "body=" << QString::fromUtf8(body.left(500));
        return;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || (!doc.isArray() && !doc.isObject())) {
        qWarning() << "Booking sync JSON parse failed" << parseError.errorString();
        return;
    }

    const QJsonObject root = doc.isObject() ? doc.object() : QJsonObject();
    QDateTime newest = parseDateTimeValue(stringValue(root, {QStringLiteral("server_time")}));
    if (!newest.isValid()) newest = QDateTime::currentDateTimeUtc();

    int processed = 0;
    int accepted = 0;
    const QJsonArray arr = extractBookingArray(doc);
    for (const QJsonValue& value : arr) {
        if (!value.isObject()) continue;
        const BookingRecord booking = parseBooking(value.toObject());
        ++processed;
        handleBookingFromServer(booking);
        if (bookingStatusBlocksPlanning(booking.status)) ++accepted;
        if (booking.updatedAt.isValid() && booking.updatedAt > newest) newest = booking.updatedAt;
    }

    qInfo() << "Booking sync completed" << "processed=" << processed << "active=" << accepted;
    BookingRepository repo;
    repo.setLastSyncTime(newest);
    publishAvailability(true);
}

void BookingSyncManager::handleBookingFromServer(const BookingRecord& booking)
{
    if (booking.bookingUid.trimmed().isEmpty()) return;
    BookingRepository repo;
    QString dbError;
    if (!repo.upsertBooking(booking, false, QString(), &dbError)) {
        qWarning() << "Booking upsert failed" << booking.bookingUid << dbError;
        return;
    }

    if (bookingSyncDeepDebug()) {
        qInfo() << "Online booking received" << booking.bookingUid << booking.date.toString(Qt::ISODate) << booking.timeFrom.toString(QStringLiteral("HH:mm")) << booking.clientName;
    }

    if (!bookingStatusBlocksPlanning(booking.status)) return;

    PlanningAvailabilityService availability;
    if (repo.isPlanningSynced(booking.bookingUid)) {
        QString refreshReason;
        if (!availability.refreshOnlineBookingPlanningText(booking, &refreshReason)) {
            qWarning() << "Online booking planning refresh skipped" << booking.bookingUid << refreshReason;
        }
        return;
    }

    QString reason;
    if (!availability.canAcceptOnlineBooking(booking, &reason)) {
        repo.updateLocalStatus(booking.bookingUid, QStringLiteral("cancelled"), reason, nullptr);
        sendStatusPatch(booking, QStringLiteral("cancelled"), reason.isEmpty() ? QString::fromUtf8("Немає вільного місця у Плануванні") : reason);
        qInfo() << "Online booking rejected by local planning availability" << booking.bookingUid << reason;
        return;
    }

    if (!availability.addOnlineBookingToPlanning(booking, &reason)) {
        repo.updateLocalStatus(booking.bookingUid, QStringLiteral("cancelled"), reason, nullptr);
        sendStatusPatch(booking, QStringLiteral("cancelled"), reason.isEmpty() ? QString::fromUtf8("Не вдалося додати запис у Планування") : reason);
        qWarning() << "Online booking failed to insert into planning" << booking.bookingUid << reason;
        return;
    }

    repo.markPlanningSynced(booking.bookingUid, booking.date, booking.timeFrom.hour(), nullptr);
    qInfo() << "Online booking inserted into planning" << booking.bookingUid;
    if (m_notificationCallback) {
        m_notificationCallback(booking);
    }
}


void BookingSyncManager::publishAvailability(bool force)
{
    applySettings();
    if (!m_running || m_baseUrl.isEmpty() || m_availabilityRequestInFlight) return;

    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (!force && m_lastAvailabilityPublish.isValid() && m_lastAvailabilityPublish.secsTo(now) < 60) {
        return;
    }

    const BookingSyncSettings settings = BookingSettings::load();
    PlanningAvailabilityService availability;
    QJsonObject days;
    QJsonObject meta;
    meta.insert(QStringLiteral("work_start_hour"), settings.workStartHour);
    meta.insert(QStringLiteral("work_end_hour"), settings.workEndHour);
    meta.insert(QStringLiteral("slot_minutes"), settings.slotMinutes);
    meta.insert(QStringLiteral("capacity_per_slot"), settings.capacityPerSlot);
    meta.insert(QStringLiteral("work_days"), settings.workDaysCsv);

    const QDate start = QDate::currentDate();
    for (int offset = 0; offset < 45; ++offset) {
        const QDate day = start.addDays(offset);
        QJsonArray arr;
        const QStringList free = availability.freeSlotsForDate(day);
        for (const QString& timeText : free) arr.append(timeText);
        days.insert(day.toString(Qt::ISODate), arr);
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("generated_at"), now.toString(Qt::ISODateWithMs));
    payload.insert(QStringLiteral("source"), QStringLiteral("CarService"));
    payload.insert(QStringLiteral("meta"), meta);
    payload.insert(QStringLiteral("days"), days);

    QNetworkRequest request(QUrl(m_baseUrl + QStringLiteral("/api/booking-availability.php")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("CarService/booking-availability"));
    request.setRawHeader("Accept", "application/json");
    if (!m_token.isEmpty()) request.setRawHeader("Authorization", QByteArray("Bearer ") + m_token.toUtf8());

    m_availabilityRequestInFlight = true;
    QNetworkReply* reply = m_network->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, now]() {
        m_availabilityRequestInFlight = false;
        const QByteArray body = reply->readAll();
        if (reply->error() == QNetworkReply::NoError) {
            m_lastAvailabilityPublish = now;
            qInfo() << "Booking availability published" << now.toString(Qt::ISODateWithMs);
        } else {
            const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            qWarning() << "Booking availability publish failed" << reply->errorString()
                       << "http_status=" << httpStatus
                       << "body=" << QString::fromUtf8(body.left(500));
        }
        reply->deleteLater();
    });
}

void BookingSyncManager::updateBookingStatus(const BookingRecord& booking, const QString& status, const QString& reason)
{
    BookingRepository repo;
    repo.updateLocalStatus(booking.bookingUid, status, reason, nullptr);
    sendStatusPatch(booking, status, reason);
}

void BookingSyncManager::sendStatusPatch(const BookingRecord& booking, const QString& status, const QString& reason)
{
    applySettings();
    if (m_baseUrl.isEmpty() || booking.bookingUid.trimmed().isEmpty()) return;
    QUrl url(m_baseUrl + QStringLiteral("/api/booking-status.php"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    if (!m_token.isEmpty()) request.setRawHeader("Authorization", QByteArray("Bearer ") + m_token.toUtf8());

    QJsonObject payload;
    payload.insert(QStringLiteral("booking_uid"), booking.bookingUid.trimmed());
    if (booking.id > 0) payload.insert(QStringLiteral("id"), booking.id);
    payload.insert(QStringLiteral("status"), bookingStatusNormalized(status));
    if (!reason.trimmed().isEmpty()) payload.insert(QStringLiteral("reason"), reason.trimmed());
    const QByteArray data = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QNetworkReply* reply = m_network->post(request, data);
    connect(reply, &QNetworkReply::finished, this, [reply]() {
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "Booking status update failed" << reply->errorString();
        }
        reply->deleteLater();
    });
}
