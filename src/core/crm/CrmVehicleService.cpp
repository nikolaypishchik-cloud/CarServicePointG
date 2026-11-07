#include "core/crm/CrmVehicleService.h"

#include "core/cloudsync/EntityUidService.h"
#include "core/services/MutationExecutor.h"
#include "core/util/PlateUtils.h"
#include "infrastructure/db/sqlite/Db.h"

#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariantMap>
#include <QVariant>

namespace {

QSqlDatabase effectiveDb(QSqlDatabase db)
{
    return db.isValid() ? db : Database::db();
}

QString normalizedPlate(const QString& plate)
{
    return PlateUtils::normalizePlate(plate).trimmed().toUpper();
}

QString normalizedVin(const QString& vin)
{
    QString out = vin.trimmed().toUpper();
    out.remove(QRegularExpression(QStringLiteral("[^A-Z0-9]")));
    // Bind an explicit empty string: crm_vehicles.vin is NOT NULL and mobile orders may omit VIN.
    return out.isNull() ? QStringLiteral("") : out;
}

QString requiredText(const QVariant& value)
{
    const QString text = value.toString().trimmed();
    return text.isNull() ? QStringLiteral("") : text;
}

QString normalizedText(const QString& raw)
{
    QString value = raw.simplified().toLower();
    value.remove(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+")));
    return value;
}

bool hasMeaningfulVehicleIdentity(const CrmVehicleService::VehicleRecord& input)
{
    return !normalizedPlate(input.plate).isEmpty()
        || !normalizedVin(input.vin).isEmpty()
        || normalizedText(input.brand + QLatin1Char(' ') + input.model).size() >= 3;
}

void splitCarText(const QString& carText, QString* brand, QString* model)
{
    const QString trimmed = carText.simplified();
    const int firstSpace = trimmed.indexOf(QLatin1Char(' '));
    if (firstSpace > 0) {
        if (brand) *brand = trimmed.left(firstSpace).trimmed();
        if (model) *model = trimmed.mid(firstSpace + 1).trimmed();
    } else {
        if (brand) *brand = trimmed;
        if (model) model->clear();
    }
}

bool tableReady(QSqlDatabase db)
{
    QSqlQuery q(db);
    return q.exec(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type='table' AND name='crm_vehicles' LIMIT 1")) && q.next();
}

void fillVehicle(QSqlQuery& q, CrmVehicleService::VehicleRecord* out)
{
    if (!out) return;
    out->id = q.value(0).toInt();
    out->vehicleUid = q.value(1).toString();
    out->clientUid = q.value(2).toString();
    out->plate = q.value(3).toString();
    out->vin = q.value(4).toString();
    out->brand = q.value(5).toString();
    out->model = q.value(6).toString();
    out->year = q.value(7).toInt();
    out->engine = q.value(8).toString();
    out->fuelType = q.value(9).toString();
    out->mileage = q.value(10).toInt();
    out->color = q.value(11).toString();
    out->isPrimary = q.value(12).toInt() != 0;
}

bool anyPrimaryVehicleForClient(QSqlDatabase db, const QString& clientUid)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT 1 FROM crm_vehicles WHERE client_uid=? AND COALESCE(is_primary,0)=1 LIMIT 1"));
    q.addBindValue(clientUid.trimmed());
    return q.exec() && q.next();
}

} // namespace

namespace CrmVehicleService {

bool findByPlateOrVin(QSqlDatabase db, const QString& plate, const QString& vin, VehicleRecord* out, QString* errorText)
{
    db = effectiveDb(db);
    if (!tableReady(db)) return false;
    const QString plateNorm = normalizedPlate(plate);
    const QString vinNorm = normalizedVin(vin);
    if (plateNorm.isEmpty() && vinNorm.isEmpty()) return false;

    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT id, vehicle_uid, client_uid, plate, vin, brand, model, year, engine, fuel_type, mileage, color, COALESCE(is_primary,0) "
                             "FROM crm_vehicles "
                             "WHERE (TRIM(?)<>'' AND UPPER(REPLACE(REPLACE(plate,' ',''),'-',''))=?) "
                             "   OR (TRIM(?)<>'' AND UPPER(REPLACE(REPLACE(vin,' ',''),'-',''))=?) "
                             "ORDER BY updated_at DESC, id DESC LIMIT 1"));
    q.addBindValue(plateNorm);
    q.addBindValue(plateNorm);
    q.addBindValue(vinNorm);
    q.addBindValue(vinNorm);
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    if (!q.next()) return false;
    fillVehicle(q, out);
    return true;
}

bool findByPlateOrVinForClient(QSqlDatabase db, const QString& clientUid, const QString& plate, const QString& vin, VehicleRecord* out, QString* errorText)
{
    db = effectiveDb(db);
    if (!tableReady(db) || clientUid.trimmed().isEmpty()) return false;
    const QString plateNorm = normalizedPlate(plate);
    const QString vinNorm = normalizedVin(vin);
    if (plateNorm.isEmpty() && vinNorm.isEmpty()) return false;

    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT id, vehicle_uid, client_uid, plate, vin, brand, model, year, engine, fuel_type, mileage, color, COALESCE(is_primary,0) "
                             "FROM crm_vehicles "
                             "WHERE client_uid=? AND ("
                             "      (TRIM(?)<>'' AND UPPER(REPLACE(REPLACE(REPLACE(plate,' ',''),'-',''),'.',''))=?) "
                             "   OR (TRIM(?)<>'' AND UPPER(REPLACE(REPLACE(REPLACE(vin,' ',''),'-',''),'.',''))=?)) "
                             "ORDER BY updated_at DESC, id DESC LIMIT 1"));
    q.addBindValue(clientUid.trimmed());
    q.addBindValue(plateNorm);
    q.addBindValue(plateNorm);
    q.addBindValue(vinNorm);
    q.addBindValue(vinNorm);
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    if (!q.next()) return false;
    fillVehicle(q, out);
    return true;
}

bool findByClientAndCarText(QSqlDatabase db, const QString& clientUid, const QString& carText, int year, VehicleRecord* out, QString* errorText)
{
    db = effectiveDb(db);
    if (!tableReady(db) || clientUid.trimmed().isEmpty()) return false;
    const QString carNorm = normalizedText(carText);
    if (carNorm.size() < 3) return false;

    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT id, vehicle_uid, client_uid, plate, vin, brand, model, year, engine, fuel_type, mileage, color, COALESCE(is_primary,0) "
                             "FROM crm_vehicles WHERE client_uid=? ORDER BY COALESCE(is_primary,0) DESC, updated_at DESC, id DESC"));
    q.addBindValue(clientUid.trimmed());
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    while (q.next()) {
        const QString existingNorm = normalizedText(q.value(5).toString() + QLatin1Char(' ') + q.value(6).toString());
        const bool sameCar = !existingNorm.isEmpty() && existingNorm == carNorm;
        const bool yearOk = year <= 0 || q.value(7).toInt() <= 0 || q.value(7).toInt() == year;
        if (sameCar && yearOk) {
            fillVehicle(q, out);
            return true;
        }
    }
    return false;
}

bool updateMissingVehicleFields(QSqlDatabase db, const QString& vehicleUid, const VehicleRecord& input, QString* errorText)
{
    db = effectiveDb(db);
    if (vehicleUid.trimmed().isEmpty()) return true;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE crm_vehicles SET "
                             "client_uid=CASE WHEN TRIM(COALESCE(client_uid,''))='' THEN ? ELSE client_uid END, "
                             "plate=CASE WHEN TRIM(COALESCE(plate,''))='' THEN ? ELSE plate END, "
                             "vin=CASE WHEN TRIM(COALESCE(vin,''))='' THEN ? ELSE vin END, "
                             "brand=CASE WHEN TRIM(COALESCE(brand,''))='' THEN ? ELSE brand END, "
                             "model=CASE WHEN TRIM(COALESCE(model,''))='' THEN ? ELSE model END, "
                             "year=CASE WHEN COALESCE(year,0)<=0 THEN ? ELSE year END, "
                             "engine=CASE WHEN TRIM(COALESCE(engine,''))='' THEN ? ELSE engine END, "
                             "fuel_type=CASE WHEN TRIM(COALESCE(fuel_type,''))='' THEN ? ELSE fuel_type END, "
                             "mileage=CASE WHEN ?>COALESCE(mileage,0) THEN ? WHEN COALESCE(mileage,0)<=0 THEN ? ELSE mileage END, "
                             "color=CASE WHEN TRIM(COALESCE(color,''))='' THEN ? ELSE color END, "
                             "updated_at=datetime('now') WHERE vehicle_uid=?"));
    q.addBindValue(requiredText(input.clientUid));
    q.addBindValue(requiredText(input.plate));
    q.addBindValue(normalizedVin(input.vin));
    q.addBindValue(requiredText(input.brand));
    q.addBindValue(requiredText(input.model));
    q.addBindValue(input.year);
    q.addBindValue(requiredText(input.engine));
    q.addBindValue(requiredText(input.fuelType));
    q.addBindValue(input.mileage);
    q.addBindValue(input.mileage);
    q.addBindValue(input.mileage);
    q.addBindValue(requiredText(input.color));
    q.addBindValue(vehicleUid.trimmed());
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    return true;
}


bool upsertVehicleDirect(QSqlDatabase db,
                         const QString& vehicleUid,
                         const QVariantMap& fields,
                         QString* errorText)
{
    db = effectiveDb(db);
    const QString uid = vehicleUid.trimmed().isEmpty()
        ? EntityUidService::generateUid(QStringLiteral("vehicle"))
        : vehicleUid.trimmed();
    if (uid.isEmpty()) {
        if (errorText) *errorText = QString::fromUtf8("CRM vehicle sync-подія без vehicle_uid.");
        return false;
    }
    if (!tableReady(db)) {
        if (errorText) *errorText = QString::fromUtf8("Таблиця crm_vehicles відсутня.");
        return false;
    }

    static const QStringList allowed = {
        QStringLiteral("client_uid"), QStringLiteral("plate"), QStringLiteral("vin"), QStringLiteral("brand"),
        QStringLiteral("model"), QStringLiteral("year"), QStringLiteral("engine"), QStringLiteral("fuel_type"),
        QStringLiteral("mileage"), QStringLiteral("color"), QStringLiteral("notes"), QStringLiteral("is_primary"), QStringLiteral("created_at"),
        QStringLiteral("updated_at")
    };
    QStringList assignments;
    QList<QVariant> values;
    for (const QString& key : allowed) {
        if (!fields.contains(key)) continue;
        assignments.append(QStringLiteral("%1=?").arg(key));
        QVariant value = fields.value(key);
        if (key == QStringLiteral("vin")) value = normalizedVin(value.toString());
        values.append(value);
    }
    if (!assignments.contains(QStringLiteral("updated_at=?"))) assignments.append(QStringLiteral("updated_at=datetime('now')"));
    if (!assignments.isEmpty()) {
        QSqlQuery update(db);
        update.prepare(QStringLiteral("UPDATE crm_vehicles SET %1 WHERE vehicle_uid=?").arg(assignments.join(QLatin1Char(','))));
        for (const QVariant& value : values) update.addBindValue(value);
        update.addBindValue(uid);
        if (!update.exec()) {
            if (errorText) *errorText = update.lastError().text();
            return false;
        }
        if (update.numRowsAffected() > 0) {
            if (fields.value(QStringLiteral("is_primary")).toInt() != 0) {
                QSqlQuery clearOthers(db);
                clearOthers.prepare(QStringLiteral("UPDATE crm_vehicles SET is_primary=0, updated_at=datetime('now') WHERE client_uid=(SELECT client_uid FROM crm_vehicles WHERE vehicle_uid=? LIMIT 1) AND vehicle_uid<>?"));
                clearOthers.addBindValue(uid);
                clearOthers.addBindValue(uid);
                if (!clearOthers.exec()) {
                    if (errorText) *errorText = clearOthers.lastError().text();
                    return false;
                }
            }
            return true;
        }
    }

    QSqlQuery insert(db);
    insert.prepare(QStringLiteral("INSERT INTO crm_vehicles(vehicle_uid, client_uid, plate, vin, brand, model, year, engine, fuel_type, mileage, color, notes, is_primary, created_at, updated_at) "
                                   "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,COALESCE(NULLIF(?,''),datetime('now')),COALESCE(NULLIF(?,''),datetime('now')))"));
    insert.addBindValue(uid);
    insert.addBindValue(requiredText(fields.value(QStringLiteral("client_uid"))));
    insert.addBindValue(requiredText(fields.value(QStringLiteral("plate"))));
    insert.addBindValue(normalizedVin(fields.value(QStringLiteral("vin")).toString()));
    insert.addBindValue(requiredText(fields.value(QStringLiteral("brand"))));
    insert.addBindValue(requiredText(fields.value(QStringLiteral("model"))));
    insert.addBindValue(fields.value(QStringLiteral("year")).toInt());
    insert.addBindValue(requiredText(fields.value(QStringLiteral("engine"))));
    insert.addBindValue(requiredText(fields.value(QStringLiteral("fuel_type"))));
    insert.addBindValue(fields.value(QStringLiteral("mileage")).toInt());
    insert.addBindValue(requiredText(fields.value(QStringLiteral("color"))));
    insert.addBindValue(requiredText(fields.value(QStringLiteral("notes"))));
    insert.addBindValue(fields.value(QStringLiteral("is_primary")).toInt());
    insert.addBindValue(fields.value(QStringLiteral("created_at")).toString().trimmed());
    insert.addBindValue(fields.value(QStringLiteral("updated_at")).toString().trimmed());
    if (!insert.exec()) {
        if (errorText) *errorText = insert.lastError().text();
        return false;
    }
    if (fields.value(QStringLiteral("is_primary")).toInt() != 0) {
        QSqlQuery clearOthers(db);
        clearOthers.prepare(QStringLiteral("UPDATE crm_vehicles SET is_primary=0, updated_at=datetime('now') WHERE client_uid=? AND vehicle_uid<>?"));
        clearOthers.addBindValue(requiredText(fields.value(QStringLiteral("client_uid"))));
        clearOthers.addBindValue(uid);
        if (!clearOthers.exec()) {
            if (errorText) *errorText = clearOthers.lastError().text();
            return false;
        }
    }
    return true;
}

bool createVehicle(QSqlDatabase db, const VehicleRecord& input, QString* vehicleUid, QString* errorText)
{
    db = effectiveDb(db);
    if (input.clientUid.trimmed().isEmpty() || !hasMeaningfulVehicleIdentity(input)) {
        if (vehicleUid) vehicleUid->clear();
        return true;
    }
    VehicleRecord existing;
    if ((!input.plate.trimmed().isEmpty() || !input.vin.trimmed().isEmpty())
        && findByPlateOrVinForClient(db, input.clientUid, input.plate, input.vin, &existing, nullptr)) {
        updateMissingVehicleFields(db, existing.vehicleUid, input, nullptr);
        if (vehicleUid) *vehicleUid = existing.vehicleUid;
        return true;
    }
    if (input.plate.trimmed().isEmpty() && input.vin.trimmed().isEmpty()
        && findByClientAndCarText(db, input.clientUid, input.brand + QLatin1Char(' ') + input.model, input.year, &existing, nullptr)) {
        updateMissingVehicleFields(db, existing.vehicleUid, input, nullptr);
        if (vehicleUid) *vehicleUid = existing.vehicleUid;
        return true;
    }

    const QString uid = input.vehicleUid.trimmed().isEmpty() ? EntityUidService::generateUid(QStringLiteral("vehicle")) : input.vehicleUid.trimmed();
    QSqlQuery q(db);
    const bool makePrimary = input.isPrimary || !anyPrimaryVehicleForClient(db, input.clientUid);
    q.prepare(QStringLiteral("INSERT INTO crm_vehicles(vehicle_uid, client_uid, plate, vin, brand, model, year, engine, fuel_type, mileage, color, is_primary, created_at, updated_at) "
                             "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,datetime('now'),datetime('now'))"));
    q.addBindValue(uid);
    q.addBindValue(requiredText(input.clientUid));
    q.addBindValue(requiredText(input.plate));
    q.addBindValue(normalizedVin(input.vin));
    q.addBindValue(requiredText(input.brand));
    q.addBindValue(requiredText(input.model));
    q.addBindValue(input.year);
    q.addBindValue(requiredText(input.engine));
    q.addBindValue(requiredText(input.fuelType));
    q.addBindValue(input.mileage);
    q.addBindValue(requiredText(input.color));
    q.addBindValue(makePrimary ? 1 : 0);
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    if (makePrimary) {
        QSqlQuery clearOthers(db);
        clearOthers.prepare(QStringLiteral("UPDATE crm_vehicles SET is_primary=0, updated_at=datetime('now') WHERE client_uid=? AND vehicle_uid<>?"));
        clearOthers.addBindValue(input.clientUid.trimmed());
        clearOthers.addBindValue(uid);
        if (!clearOthers.exec()) {
            if (errorText) *errorText = clearOthers.lastError().text();
            return false;
        }
    }
    if (vehicleUid) *vehicleUid = uid;
    return true;
}

bool setPrimaryStatus(QSqlDatabase db, const QString& clientUid, const QString& vehicleUid, bool primary, QString* errorText)
{
    db = effectiveDb(db);
    const QString client = clientUid.trimmed();
    const QString vehicle = vehicleUid.trimmed();
    if (client.isEmpty() || vehicle.isEmpty()) {
        if (errorText) *errorText = QString::fromUtf8("CRM vehicle primary: порожній client_uid або vehicle_uid.");
        return false;
    }
    if (!tableReady(db)) {
        if (errorText) *errorText = QString::fromUtf8("Таблиця crm_vehicles відсутня.");
        return false;
    }

    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("CrmVehicleService");
    request.entityType = QStringLiteral("crm_vehicle");
    request.entityId = vehicle;
    request.operation = primary ? QStringLiteral("set_primary_vehicle") : QStringLiteral("unset_primary_vehicle");
    request.description = QString::fromUtf8("Зміна основного авто клієнта CRM");
    request.changedTables = {QStringLiteral("crm_vehicles")};

    int oldPrimary = 0;
    int newPrimary = primary ? 1 : 0;
    const auto result = MutationExecutor::execute(db, request, [&](QSqlDatabase& txDb, QString* txError) {
        QSqlQuery current(txDb);
        current.prepare(QStringLiteral("SELECT COALESCE(is_primary,0) FROM crm_vehicles WHERE client_uid=? AND vehicle_uid=? LIMIT 1"));
        current.addBindValue(client);
        current.addBindValue(vehicle);
        if (!current.exec() || !current.next()) {
            if (txError) *txError = current.lastError().text().trimmed().isEmpty()
                ? QString::fromUtf8("Авто клієнта не знайдено.")
                : current.lastError().text();
            return false;
        }
        oldPrimary = current.value(0).toInt();

        if (primary) {
            QSqlQuery clear(txDb);
            clear.prepare(QStringLiteral("UPDATE crm_vehicles SET is_primary=0, updated_at=datetime('now') WHERE client_uid=? AND COALESCE(is_primary,0)<>0"));
            clear.addBindValue(client);
            if (!clear.exec()) {
                if (txError) *txError = clear.lastError().text();
                return false;
            }
        } else {
            QSqlQuery count(txDb);
            count.prepare(QStringLiteral("SELECT COUNT(*) FROM crm_vehicles WHERE client_uid=?"));
            count.addBindValue(client);
            if (!count.exec() || !count.next()) {
                if (txError) *txError = count.lastError().text();
                return false;
            }
            if (count.value(0).toInt() <= 1) {
                if (txError) *txError = QString::fromUtf8("Єдине авто клієнта має залишатися основним.");
                return false;
            }
        }

        QSqlQuery update(txDb);
        update.prepare(QStringLiteral("UPDATE crm_vehicles SET is_primary=?, updated_at=datetime('now') WHERE client_uid=? AND vehicle_uid=?"));
        update.addBindValue(newPrimary);
        update.addBindValue(client);
        update.addBindValue(vehicle);
        if (!update.exec()) {
            if (txError) *txError = update.lastError().text();
            return false;
        }
        return true;
    });

    qInfo() << "CRM vehicle primary status changed"
            << "client_uid" << client
            << "vehicle_uid" << vehicle
            << "old_primary" << oldPrimary
            << "new_primary" << newPrimary
            << "result" << result.ok;
    if (!result.ok && errorText) *errorText = result.errorText;
    return result.ok;
}

bool findOrCreateFromOrder(QSqlDatabase db,
                           const QString& clientUid,
                           const QString& plate,
                           const QString& vin,
                           const QString& carText,
                           int year,
                           int mileage,
                           const QString& engine,
                           const QString& fuelType,
                           const QString& color,
                           QString* vehicleUid,
                           QString* errorText)
{
    db = effectiveDb(db);
    if (clientUid.trimmed().isEmpty()) {
        if (vehicleUid) vehicleUid->clear();
        return true;
    }

    QString brand;
    QString model;
    splitCarText(carText, &brand, &model);

    VehicleRecord rec;
    rec.clientUid = clientUid.trimmed();
    rec.plate = plate.trimmed();
    rec.vin = normalizedVin(vin);
    rec.brand = brand;
    rec.model = model;
    rec.year = year;
    rec.mileage = mileage;
    rec.engine = engine.trimmed();
    rec.fuelType = fuelType.trimmed();
    rec.color = color.trimmed();

    VehicleRecord existing;
    if (!hasMeaningfulVehicleIdentity(rec)) {
        if (vehicleUid) vehicleUid->clear();
        return true;
    }
    if (findByPlateOrVinForClient(db, clientUid, plate, vin, &existing, errorText)) {
        updateMissingVehicleFields(db, existing.vehicleUid, rec, nullptr);
        if (vehicleUid) *vehicleUid = existing.vehicleUid;
        return true;
    }
    if (plate.trimmed().isEmpty() && vin.trimmed().isEmpty()
        && findByClientAndCarText(db, clientUid, carText, year, &existing, nullptr)) {
        updateMissingVehicleFields(db, existing.vehicleUid, rec, nullptr);
        if (vehicleUid) *vehicleUid = existing.vehicleUid;
        return true;
    }
    return createVehicle(db, rec, vehicleUid, errorText);
}

PrimaryStatusResult primaryStatuses(const QString& clientUid, QSqlDatabase db)
{
    PrimaryStatusResult result;
    db = effectiveDb(db);
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT vehicle_uid, COALESCE(is_primary,0) FROM crm_vehicles WHERE client_uid=?"));
    query.addBindValue(clientUid);
    if (!query.exec()) {
        result.error = query.lastError().text();
        return result;
    }
    while (query.next())
        result.vehicles.insert(query.value(0).toString().trimmed(), query.value(1).toInt() != 0);
    if (query.lastError().isValid()) {
        result.vehicles.clear();
        result.error = query.lastError().text();
    }
    return result;
}

} // namespace CrmVehicleService
