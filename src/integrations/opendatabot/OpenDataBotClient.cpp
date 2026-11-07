#include "integrations/opendatabot/OpenDataBotClient.h"
#include <functional>

#include "infrastructure/db/sqlite/Db.h"
#include "core/util/PlateUtils.h"
#include "core/util/VehicleNameUtils.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace {

static QString getSettingString(const QString& key, const QString& def = QString())
{
    return Database::getSetting(key, def).toString().trimmed();
}

static bool getSettingBool(const QString& key, bool def = false)
{
    return Database::getSetting(key, def).toBool();
}

static QJsonValue unwrapRoot(const QJsonValue& value)
{
    if (!value.isObject()) return value;
    const QJsonObject obj = value.toObject();
    if (obj.contains(QStringLiteral("data"))) return unwrapRoot(obj.value(QStringLiteral("data")));
    if (obj.contains(QStringLiteral("result"))) return unwrapRoot(obj.value(QStringLiteral("result")));
    if (obj.contains(QStringLiteral("response"))) return unwrapRoot(obj.value(QStringLiteral("response")));
    if (obj.contains(QStringLiteral("transport"))) return unwrapRoot(obj.value(QStringLiteral("transport")));
    return value;
}

static bool keyMatches(const QString& key, const QStringList& aliases)
{
    const QString k = key.trimmed().toLower();
    for (const QString& alias : aliases) {
        const QString a = alias.trimmed().toLower();
        if (k == a || k.contains(a)) return true;
    }
    return false;
}

static QString firstStringForAliases(const QJsonValue& value, const QStringList& aliases)
{
    const QJsonValue root = unwrapRoot(value);
    if (root.isObject()) {
        const QJsonObject obj = root.toObject();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            if (keyMatches(it.key(), aliases)) {
                if (it.value().isString()) {
                    const QString s = it.value().toString().trimmed();
                    if (!s.isEmpty()) return s;
                }
                if (it.value().isDouble()) {
                    return QString::number(it.value().toDouble());
                }
            }
        }
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            const QString nested = firstStringForAliases(it.value(), aliases);
            if (!nested.isEmpty()) return nested;
        }
    } else if (root.isArray()) {
        const QJsonArray arr = root.toArray();
        for (const auto& item : arr) {
            const QString nested = firstStringForAliases(item, aliases);
            if (!nested.isEmpty()) return nested;
        }
    }
    return QString();
}

static int firstIntForAliases(const QJsonValue& value, const QStringList& aliases)
{
    const QString s = firstStringForAliases(value, aliases);
    bool ok = false;
    const int v = s.toInt(&ok);
    return ok ? v : 0;
}

static QString normalizeFuelType(const QString& raw)
{
    const QString t = raw.trimmed().toLower();
    if (t.isEmpty()) return QString();
    if (t.contains(QStringLiteral("diesel")) || t.contains(QString::fromUtf8("диз"))) return QString::fromUtf8("Дизель");
    if (t.contains(QStringLiteral("hybrid")) || t.contains(QString::fromUtf8("гібр"))) return QString::fromUtf8("Гібрид");
    if (t.contains(QStringLiteral("electric")) || t.contains(QStringLiteral("ev")) || t.contains(QString::fromUtf8("елект"))) return QString::fromUtf8("Електро");
    if (t.contains(QStringLiteral("petrol")) || t.contains(QStringLiteral("gasoline")) || t.contains(QStringLiteral("benz")) || t.contains(QString::fromUtf8("бенз")) || t.contains(QString::fromUtf8("газ"))) {
        return QString::fromUtf8("Бензин (газ)");
    }
    return raw.trimmed();
}

static QString normalizeEngineVolumeDisplay(const QString& raw)
{
    const QString trimmed = raw.trimmed();
    if (trimmed.isEmpty()) return QString();

    bool ok = false;
    const int cubicCm = trimmed.toInt(&ok);
    if (ok && cubicCm > 0) {
        const double liters = static_cast<double>(cubicCm) / 1000.0;
        QString out = QString::number(liters, 'f', 1);
        while (out.endsWith('0')) out.chop(1);
        if (out.endsWith('.')) out.chop(1);
        return out;
    }

    return trimmed;
}

static OpenDataBotClient::VehicleLookupResult buildDemoResult(const QString& plate)
{
    OpenDataBotClient::VehicleLookupResult out;
    out.success = true;
    out.demo = true;
    out.plate = PlateUtils::normalizePlate(plate);
    out.vin = QStringLiteral("3VWC57BU7KM194708");
    out.carDisplay = QStringLiteral("VOLKSWAGEN JETTA");
    out.color = QString::fromUtf8("ЧОРНИЙ");
    out.engineVolume = QStringLiteral("1.4");
    out.fuelType = QString::fromUtf8("Бензин (газ)");
    out.year = 2019;
    return out;
}

static OpenDataBotClient::VehicleLookupResult parseVehicleResponse(const QByteArray& body)
{
    OpenDataBotClient::VehicleLookupResult out;

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError) {
        out.error = QString::fromUtf8("Сервер повернув невалідний JSON: %1").arg(err.errorString());
        return out;
    }

    const QJsonValue root = unwrapRoot(doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array()));

    const QString vin = firstStringForAliases(root, {
        QStringLiteral("vin"),
        QStringLiteral("vehicle_vin"),
        QStringLiteral("body_number"),
        QStringLiteral("bodynum")
    }).trimmed().toUpper();

    QString brand = firstStringForAliases(root, {
        QStringLiteral("brand"),
        QStringLiteral("make"),
        QStringLiteral("vendor"),
        QStringLiteral("mark"),
        QString::fromUtf8("марка")
    }).trimmed();

    QString model = firstStringForAliases(root, {
        QStringLiteral("model"),
        QStringLiteral("model_name"),
        QStringLiteral("commercial_name"),
        QString::fromUtf8("модель")
    }).trimmed();

    QString vehicle = firstStringForAliases(root, {
        QStringLiteral("vehicle"),
        QStringLiteral("car"),
        QStringLiteral("name")
    }).trimmed();

    if (!vehicle.isEmpty() && brand.isEmpty() && model.isEmpty()) {
        out.carDisplay = vehicle;
    }
    if (out.carDisplay.isEmpty()) {
        out.carDisplay = QStringLiteral("%1 %2").arg(brand, model).trimmed();
    }
    out.carDisplay = VehicleNameUtils::normalizeVehicleFreeText(out.carDisplay);

    out.vin = vin;
    out.color = firstStringForAliases(root, {
        QStringLiteral("color"),
        QStringLiteral("colour"),
        QStringLiteral("color_name"),
        QString::fromUtf8("колір")
    }).trimmed();
    out.engineVolume = normalizeEngineVolumeDisplay(firstStringForAliases(root, {
        QStringLiteral("engine_volume"),
        QStringLiteral("volume"),
        QStringLiteral("enginecapacity"),
        QStringLiteral("displacement"),
        QStringLiteral("capacity"),
        QStringLiteral("engine_capacity"),
        QString::fromUtf8("обєм"),
        QString::fromUtf8("об'єм")
    }));
    out.fuelType = normalizeFuelType(firstStringForAliases(root, {
        QStringLiteral("fuel"),
        QStringLiteral("fuel_type"),
        QStringLiteral("fuelname"),
        QString::fromUtf8("паливо")
    }));
    out.year = firstIntForAliases(root, {
        QStringLiteral("year"),
        QStringLiteral("makeYear"),
        QStringLiteral("make_year"),
        QStringLiteral("model_year"),
        QStringLiteral("manufacture_year"),
        QStringLiteral("production_year"),
        QString::fromUtf8("рік")
    });

    out.plate = firstStringForAliases(root, {
        QStringLiteral("number"),
        QStringLiteral("plate"),
        QStringLiteral("plate_number")
    }).trimmed().toUpper();

    out.success = !out.vin.isEmpty() || !out.carDisplay.isEmpty() || out.year > 0 || !out.color.isEmpty();
    if (!out.success) {
        out.error = QString::fromUtf8("OpenDataBot не повернув дані авто для цього номера.");
    }
    return out;
}

} // namespace

bool OpenDataBotClient::isConfigured()
{
    if (!getSettingBool(QStringLiteral("settings.int.odb.api_enabled"),
                        getSettingBool(QStringLiteral("settings.int.odb.demo_mode"), false)
                        || !getSettingString(QStringLiteral("settings.int.odb.api_key")).isEmpty())) {
        return false;
    }
    return !getSettingString(QStringLiteral("settings.int.odb.base_url"), QStringLiteral("https://opendatabot.com/api/v4")).isEmpty()
        && !getSettingString(QStringLiteral("settings.int.odb.api_key")).isEmpty();
}

QString OpenDataBotClient::configuredBaseUrl()
{
    return getSettingString(QStringLiteral("settings.int.odb.base_url"), QStringLiteral("https://opendatabot.com/api/v4"));
}

QString OpenDataBotClient::buildPlateLookupUrl(const QString& plate)
{
    const QString baseUrl = configuredBaseUrl();
    QString endpoint = getSettingString(QStringLiteral("settings.int.odb.plate_endpoint"), QStringLiteral("transport"));
    const QString plateParam = getSettingString(QStringLiteral("settings.int.odb.plate_param"), QStringLiteral("number"));
    const QString keyParam = getSettingString(QStringLiteral("settings.int.odb.api_key_param"), QStringLiteral("apiKey"));
    const QString apiKey = getSettingString(QStringLiteral("settings.int.odb.api_key"));

    QUrl url(baseUrl);
    QString path = url.path();
    if (!path.endsWith('/')) path += '/';
    endpoint.remove(QRegularExpression(QStringLiteral("^/+")));
    path += endpoint;
    url.setPath(path);

    QUrlQuery query(url);
    query.addQueryItem(plateParam, PlateUtils::normalizePlate(plate));
    if (!apiKey.isEmpty() && !keyParam.isEmpty()) {
        query.addQueryItem(keyParam, apiKey);
    }
    url.setQuery(query);
    return url.toString();
}

void OpenDataBotClient::lookupByPlate(QObject* context,
                                      const QString& plate,
                                      std::function<void(const VehicleLookupResult&)> callback)
{
    VehicleLookupResult early;
    if (!context) {
        early.error = QString::fromUtf8("Відсутній context для виконання API-запиту.");
        callback(early);
        return;
    }

    if (!getSettingBool(QStringLiteral("settings.int.odb.api_enabled"),
                        getSettingBool(QStringLiteral("settings.int.odb.demo_mode"), false)
                        || !getSettingString(QStringLiteral("settings.int.odb.api_key")).isEmpty())) {
        early.error = QString::fromUtf8("API-запити OpenDataBot вимкнено. Застосунок працює офлайн.");
        callback(early);
        return;
    }

    if (!isConfigured()) {
        early.error = QString::fromUtf8("OpenDataBot не налаштований. Вкажіть Base URL та API key та увімкніть API-запити.");
        callback(early);
        return;
    }

    const QUrl url(buildPlateLookupUrl(plate));
    if (!url.isValid()) {
        early.error = QString::fromUtf8("Невалідний URL OpenDataBot.");
        callback(early);
        return;
    }

    auto* manager = new QNetworkAccessManager(context);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");

    const QString headerName = getSettingString(QStringLiteral("settings.int.odb.header_name"));
    const QString apiKey = getSettingString(QStringLiteral("settings.int.odb.api_key"));
    if (!headerName.isEmpty() && !apiKey.isEmpty()) {
        request.setRawHeader(headerName.toUtf8(), apiKey.toUtf8());
    }

    QNetworkReply* reply = manager->get(request);
    QObject::connect(reply, &QNetworkReply::finished, context, [reply, manager, callback]() {
        VehicleLookupResult out;
        const QByteArray body = reply->readAll();

        if (reply->error() != QNetworkReply::NoError) {
            out.error = QString::fromUtf8("Помилка запиту OpenDataBot: %1").arg(reply->errorString());
        } else {
            out = parseVehicleResponse(body);
        }

        reply->deleteLater();
        manager->deleteLater();
        callback(out);
    });
}