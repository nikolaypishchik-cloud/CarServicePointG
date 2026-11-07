#include "core/crm/CrmClientService.h"

#include "core/cloudsync/EntityUidService.h"
#include "core/crm/CrmTimelineService.h"
#include "core/crm/CrmVehicleService.h"
#include "infrastructure/db/sqlite/Db.h"

#include <QRegularExpression>
#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariantMap>
#include <QVariant>

namespace {

QSqlDatabase effectiveDb(QSqlDatabase db)
{
    return db.isValid() ? db : Database::db();
}

QString normalizedPhone(const QString& raw)
{
    QString out;
    for (const QChar ch : raw) {
        if (ch.isDigit()) out.append(ch);
    }
    if (out.startsWith(QStringLiteral("380")) && out.size() >= 12) out = out.mid(2);
    if (out.startsWith(QStringLiteral("38")) && out.size() >= 11) out = out.mid(2);
    return out.trimmed();
}

QString nonNullText(const QString& value)
{
    return value.isNull() ? QStringLiteral("") : value;
}

QString normalizedName(const QString& raw)
{
    QString value = raw.simplified().toLower();
    value.remove(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+")));
    return value;
}

QString sanitizedOrderClientName(const QString& raw)
{
    const QString value = raw.simplified();
    if (value.compare(QString::fromUtf8("ПІБ клієнта"), Qt::CaseInsensitive) == 0) return QString();
    return value;
}

QString sanitizedOrderClientPhone(const QString& raw)
{
    const QString value = raw.simplified();
    if (value.compare(QString::fromUtf8("Напр: +380501234567"), Qt::CaseInsensitive) == 0) return QString();
    return value;
}

QString sanitizedOrderVehicleText(const QString& raw)
{
    const QString value = raw.simplified();
    if (value.compare(QString::fromUtf8("Марка/модель"), Qt::CaseInsensitive) == 0) return QString();
    if (value.compare(QString::fromUtf8("Автомобіль"), Qt::CaseInsensitive) == 0) return QString();
    return value;
}

QString sanitizedOrderPlate(const QString& raw)
{
    const QString value = raw.simplified();
    if (value.compare(QString::fromUtf8("Держномер"), Qt::CaseInsensitive) == 0) return QString();
    if (value.compare(QStringLiteral("AA0000AA"), Qt::CaseInsensitive) == 0) return QString();
    return value;
}

QString normalizedEmail(const QString& raw)
{
    return raw.trimmed().toLower();
}

bool hasMeaningfulClientData(const CrmClientService::ClientRecord& input)
{
    return !input.fullName.trimmed().isEmpty()
        || !input.firstName.trimmed().isEmpty()
        || !input.lastName.trimmed().isEmpty()
        || !normalizedPhone(input.phone).isEmpty()
        || !normalizedEmail(input.email).isEmpty();
}

QVariant normalizedDirectFieldValue(const QString& key, const QVariant& value)
{
    if (key == QStringLiteral("loyalty_level") || key == QStringLiteral("discount_percent")) {
        return key == QStringLiteral("discount_percent") ? QVariant(value.toDouble()) : QVariant(nonNullText(value.toString().trimmed()));
    }
    if (key == QStringLiteral("notes")) return nonNullText(value.toString());
    return nonNullText(value.toString().trimmed());
}

QString phoneExpr(const QString& column)
{
    return QStringLiteral("REPLACE(REPLACE(REPLACE(REPLACE(REPLACE(REPLACE(COALESCE(%1,''), '+', ''), ' ', ''), '-', ''), '(', ''), ')', ''), '.', '')").arg(column);
}

bool tableReady(QSqlDatabase db)
{
    QSqlQuery q(db);
    return q.exec(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type='table' AND name='crm_clients' LIMIT 1")) && q.next();
}

bool tableExists(QSqlDatabase db, const QString& tableName)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type='table' AND name=? LIMIT 1"));
    q.addBindValue(tableName);
    return q.exec() && q.next();
}

bool columnExists(QSqlDatabase db, const QString& tableName, const QString& columnName)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("PRAGMA table_info(%1)").arg(tableName));
    if (!q.exec()) return false;
    while (q.next()) {
        if (q.value(1).toString().compare(columnName, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

QString columnExpr(QSqlDatabase db, const QString& tableName, const QString& columnName, const QString& defaultExpr = QStringLiteral("''"))
{
    return columnExists(db, tableName, columnName)
        ? QStringLiteral("COALESCE(%1,%2)").arg(columnName, defaultExpr)
        : defaultExpr;
}

bool execSql(QSqlDatabase db, const QString& sql, QString* errorText)
{
    QSqlQuery q(db);
    if (q.exec(sql)) return true;
    if (errorText) *errorText = q.lastError().text();
    return false;
}

bool ensureColumn(QSqlDatabase db, const QString& tableName, const QString& columnName, const QString& definition, QString* errorText)
{
    if (!tableExists(db, tableName) || columnExists(db, tableName, columnName)) return true;
    return execSql(db, QStringLiteral("ALTER TABLE %1 ADD COLUMN %2").arg(tableName, definition), errorText);
}

int scalarCount(QSqlDatabase db, const QString& sql, const QList<QVariant>& values = {})
{
    QSqlQuery q(db);
    q.prepare(sql);
    for (const QVariant& value : values) q.addBindValue(value);
    if (!q.exec() || !q.next()) return 0;
    return q.value(0).toInt();
}

bool backfillMissingCrmUidColumn(QSqlDatabase db,
                                 const QString& tableName,
                                 const QString& uidColumn,
                                 const QString& uidPrefix,
                                 QString* errorText)
{
    if (!tableExists(db, tableName) || !columnExists(db, tableName, uidColumn)) return true;

    QSqlQuery select(db);
    select.prepare(QStringLiteral("SELECT id FROM %1 WHERE TRIM(COALESCE(%2,''))='' ORDER BY id ASC").arg(tableName, uidColumn));
    if (!select.exec()) {
        if (errorText) *errorText = select.lastError().text();
        return false;
    }

    int updated = 0;
    while (select.next()) {
        QSqlQuery update(db);
        update.prepare(QStringLiteral("UPDATE %1 SET %2=? WHERE id=? AND TRIM(COALESCE(%2,''))=''").arg(tableName, uidColumn));
        update.addBindValue(EntityUidService::generateUid(uidPrefix));
        update.addBindValue(select.value(0));
        if (!update.exec()) {
            if (errorText) *errorText = update.lastError().text();
            return false;
        }
        const int affected = update.numRowsAffected();
        if (affected > 0) updated += affected;
    }

    if (updated > 0) {
        qInfo().noquote() << QStringLiteral("CRM_UID_BACKFILL table=%1 column=%2 updated=%3")
                                  .arg(tableName, uidColumn)
                                  .arg(updated);
    }
    return true;
}

void splitName(const QString& fullName, QString* firstName, QString* lastName)
{
    const QStringList parts = fullName.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        if (firstName) firstName->clear();
        if (lastName) lastName->clear();
        return;
    }
    if (parts.size() == 1) {
        if (firstName) firstName->clear();
        if (lastName) *lastName = parts.constFirst();
        return;
    }
    if (firstName) *firstName = parts.value(0);
    if (lastName) *lastName = parts.mid(1).join(QLatin1Char(' '));
}

void fillClient(QSqlQuery& q, CrmClientService::ClientRecord* out)
{
    if (!out) return;
    out->id = q.value(0).toInt();
    out->clientUid = q.value(1).toString();
    out->fullName = q.value(2).toString();
    out->firstName = q.value(3).toString();
    out->lastName = q.value(4).toString();
    out->phone = q.value(5).toString();
    out->email = q.value(6).toString();
    out->status = q.value(7).toString();
}

bool orderLinkEventExists(QSqlDatabase db, const QString& clientUid, const QString& orderUid)
{
    if (clientUid.trimmed().isEmpty() || orderUid.trimmed().isEmpty() || !tableExists(db, QStringLiteral("crm_client_events"))) return true;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT 1 FROM crm_client_events WHERE client_uid=? AND order_uid=? AND event_type='order_linked' LIMIT 1"));
    q.addBindValue(clientUid.trimmed());
    q.addBindValue(orderUid.trimmed());
    return q.exec() && q.next();
}

bool findByNormalizedEmailLocal(QSqlDatabase db, const QString& email, CrmClientService::ClientRecord* out, QString* errorText)
{
    if (!tableReady(db)) return false;
    const QString emailNorm = normalizedEmail(email);
    if (emailNorm.isEmpty()) return false;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT id, client_uid, full_name, first_name, last_name, phone, email, status "
                             "FROM crm_clients WHERE LOWER(TRIM(COALESCE(email,'')))=? "
                             "ORDER BY updated_at DESC, id DESC LIMIT 1"));
    q.addBindValue(emailNorm);
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    if (!q.next()) return false;
    fillClient(q, out);
    return true;
}

bool updateOrderLinkColumns(QSqlDatabase db,
                            const QString& tableName,
                            int orderNumber,
                            const QString& clientUid,
                            const QString& vehicleUid,
                            QString* errorText,
                            const QString& expectedOrderUid = QString())
{
    if (!tableExists(db, tableName) || orderNumber <= 0) return true;
    if (!columnExists(db, tableName, QStringLiteral("client_uid")) && !columnExists(db, tableName, QStringLiteral("vehicle_uid"))) return true;

    QStringList sets;
    QList<QVariant> values;
    if (columnExists(db, tableName, QStringLiteral("client_uid"))) {
        sets << QStringLiteral("client_uid=?");
        values << nonNullText(clientUid.trimmed());
    }
    if (columnExists(db, tableName, QStringLiteral("vehicle_uid"))) {
        sets << QStringLiteral("vehicle_uid=?");
        values << nonNullText(vehicleUid.trimmed());
    }
    if (columnExists(db, tableName, QStringLiteral("crm_backfilled_at"))) {
        sets << QStringLiteral("crm_backfilled_at=datetime('now')");
    }
    if (sets.isEmpty()) return true;

    QSqlQuery upd(db);
    const bool checkUid = !expectedOrderUid.isEmpty();
    if (checkUid && !columnExists(db, tableName, QStringLiteral("order_uid"))) {
        if (errorText) *errorText = QStringLiteral("Cannot verify order UID before CRM link update"); return false;
    }
    const auto predicate = checkUid ? QStringLiteral("number=? AND order_uid=?") : QStringLiteral("number=?");
    upd.prepare(QStringLiteral("UPDATE %1 SET %2 WHERE %3").arg(tableName, sets.join(QLatin1Char(',')), predicate));
    for (const QVariant& value : values) upd.addBindValue(value);
    upd.addBindValue(orderNumber);
    if (checkUid) upd.addBindValue(expectedOrderUid);
    if (!upd.exec()) {
        if (errorText) *errorText = upd.lastError().text();
        return false;
    }
    return true;
}

bool processOrderRow(QSqlDatabase db,
                     const QString& sourceTable,
                     int orderNumber,
                     CrmClientService::BackfillResult* result,
                     QString* clientUid,
                     QString* vehicleUid,
                     QString* errorText)
{
    if (clientUid) clientUid->clear();
    if (vehicleUid) vehicleUid->clear();
    if (!tableExists(db, sourceTable) || orderNumber <= 0) return true;

    const QString colorExpr = columnExists(db, sourceTable, QStringLiteral("color"))
        ? (columnExists(db, sourceTable, QStringLiteral("car_color"))
               ? QStringLiteral("COALESCE(color, car_color, '')")
               : QStringLiteral("COALESCE(color, '')"))
        : columnExpr(db, sourceTable, QStringLiteral("car_color"));

    const QString engineExpr = QStringLiteral("COALESCE(NULLIF(TRIM(%1),''), %2, '')")
                                   .arg(columnExpr(db, sourceTable, QStringLiteral("engine_volume")),
                                        columnExpr(db, sourceTable, QStringLiteral("engine_type")));
    const QStringList fields = {
        QStringLiteral("number"),
        columnExpr(db, sourceTable, QStringLiteral("order_uid")),
        columnExpr(db, sourceTable, QStringLiteral("client")),
        columnExpr(db, sourceTable, QStringLiteral("client_phone")),
        columnExpr(db, sourceTable, QStringLiteral("plate")),
        columnExpr(db, sourceTable, QStringLiteral("vin")),
        columnExpr(db, sourceTable, QStringLiteral("car")),
        columnExpr(db, sourceTable, QStringLiteral("car_year"), QStringLiteral("0")),
        columnExpr(db, sourceTable, QStringLiteral("odometer"), QStringLiteral("0")),
        engineExpr,
        columnExpr(db, sourceTable, QStringLiteral("fuel_type")),
        colorExpr,
        columnExpr(db, sourceTable, QStringLiteral("client_uid")),
        columnExpr(db, sourceTable, QStringLiteral("vehicle_uid"))
    };
    QSqlQuery select(db);
    select.prepare(QStringLiteral("SELECT %1 FROM %2 WHERE number=? LIMIT 1").arg(fields.join(QLatin1Char(',')), sourceTable));
    select.addBindValue(orderNumber);
    if (!select.exec()) {
        if (errorText) *errorText = select.lastError().text();
        return false;
    }
    if (!select.next()) {
        if (result) ++result->skipped;
        return true;
    }

    const QString orderUid = select.value(1).toString().trimmed();
    const QString name = sanitizedOrderClientName(select.value(2).toString());
    const QString phone = sanitizedOrderClientPhone(select.value(3).toString());
    const QString plate = sanitizedOrderPlate(select.value(4).toString());
    const QString vin = select.value(5).toString().trimmed();
    const QString carText = sanitizedOrderVehicleText(select.value(6).toString());

    QString recordedClientUid = select.value(12).toString().trimmed();
    const QString recordedVehicleUid = select.value(13).toString().trimmed();
    if (!recordedVehicleUid.isEmpty()) {
        QSqlQuery owner(db);
        owner.prepare(QStringLiteral("SELECT client_uid FROM crm_vehicles WHERE vehicle_uid=?")); owner.addBindValue(recordedVehicleUid);
        if (!owner.exec()) {if(errorText)*errorText=owner.lastError().text();return false;}
        if (!owner.next()) {
            if(errorText)*errorText=owner.lastError().isValid()?owner.lastError().text():QStringLiteral("Recorded vehicle UID is missing from CRM"); return false;
        }
        const auto vehicleOwner=owner.value(0).toString().trimmed();
        if (vehicleOwner.isEmpty() || (!recordedClientUid.isEmpty() && vehicleOwner!=recordedClientUid)) {
            if(errorText)*errorText=QStringLiteral("Recorded vehicle UID belongs to another client"); return false;
        }
        recordedClientUid=vehicleOwner;
    }
    const int clientsBefore = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM crm_clients"));
    CrmClientService::ClientRecord clientExisting;
    bool hadClient = false;
    QString resolvedClientUid;
    if (!recordedClientUid.isEmpty()) {
        QString lookupError;
        if (!CrmClientService::findByUid(db,recordedClientUid,&clientExisting,&lookupError)) {
            if(errorText)*errorText=lookupError.isEmpty()?QStringLiteral("Recorded client UID is missing from CRM"):lookupError; return false;
        }
        resolvedClientUid=recordedClientUid; hadClient=true;
        CrmClientService::ClientRecord hints; hints.fullName=name; hints.phone=phone;
        if (!CrmClientService::updateMissingClientFields(db,resolvedClientUid,hints,errorText)) return false;
    } else {
        hadClient = (!phone.trimmed().isEmpty() && CrmClientService::findByNormalizedPhone(db, phone, &clientExisting, nullptr))
            || (phone.trimmed().isEmpty() && CrmClientService::findByNormalizedName(db, name, &clientExisting, nullptr));
        if (!CrmClientService::findOrCreateFromOrder(db,name,phone,&resolvedClientUid,errorText)) return false;
    }
    const int clientsAfter = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM crm_clients"));
    if (result && !resolvedClientUid.isEmpty()) {
        if (clientsAfter > clientsBefore) ++result->clientsCreated;
        else if (hadClient) ++result->clientsUpdated;
    }
    if (!resolvedClientUid.isEmpty() && clientsAfter > clientsBefore) {
        qInfo().noquote() << QStringLiteral("CRM_ORDER_BACKFILL_CLIENT_CREATED source=%1 order=%2 client_uid=%3")
                                  .arg(sourceTable)
                                  .arg(orderNumber)
                                  .arg(resolvedClientUid);
    }

    CrmVehicleService::VehicleRecord vehicleExisting;
    const bool hadVehicle = !recordedVehicleUid.isEmpty() || (!resolvedClientUid.isEmpty()
        && ((!plate.trimmed().isEmpty() || !vin.trimmed().isEmpty())
                ? CrmVehicleService::findByPlateOrVinForClient(db, resolvedClientUid, plate, vin, &vehicleExisting, nullptr)
                : CrmVehicleService::findByClientAndCarText(db, resolvedClientUid, carText, select.value(7).toInt(), &vehicleExisting, nullptr)));
    const int vehiclesBefore = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM crm_vehicles"));
    QString resolvedVehicleUid;
    if (!recordedVehicleUid.isEmpty()) {
        resolvedVehicleUid=recordedVehicleUid;
        CrmVehicleService::VehicleRecord hints; hints.clientUid=resolvedClientUid; hints.plate=plate; hints.vin=vin;
        const int space=carText.indexOf(QLatin1Char(' '));
        hints.brand=space>0?carText.left(space):carText; hints.model=space>0?carText.mid(space+1):QStringLiteral("");
        hints.year=select.value(7).toInt(); hints.mileage=select.value(8).toInt(); hints.engine=select.value(9).toString();
        hints.fuelType=select.value(10).toString(); hints.color=select.value(11).toString();
        if (!CrmVehicleService::updateMissingVehicleFields(db,resolvedVehicleUid,hints,errorText)) return false;
    } else if (!CrmVehicleService::findOrCreateFromOrder(db,resolvedClientUid,plate,vin,carText,
            select.value(7).toInt(),select.value(8).toInt(),select.value(9).toString(),select.value(10).toString(),
            select.value(11).toString(),&resolvedVehicleUid,errorText)) return false;
    const int vehiclesAfter = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM crm_vehicles"));
    if (result && !resolvedVehicleUid.isEmpty()) {
        if (vehiclesAfter > vehiclesBefore) ++result->vehiclesCreated;
        else if (hadVehicle) ++result->vehiclesUpdated;
    }

    if (resolvedClientUid.isEmpty() && resolvedVehicleUid.isEmpty()) {
        if (!updateOrderLinkColumns(db, sourceTable, orderNumber, resolvedClientUid, resolvedVehicleUid, errorText, orderUid)) {
            return false;
        }
        if (result) ++result->skipped;
        return true;
    }

    if (!updateOrderLinkColumns(db, sourceTable, orderNumber, resolvedClientUid, resolvedVehicleUid, errorText, orderUid)) {
        return false;
    }
    if (sourceTable != QStringLiteral("work_orders") && !orderUid.isEmpty() && tableExists(db, QStringLiteral("work_orders"))) {
        if (!updateOrderLinkColumns(db, QStringLiteral("work_orders"), orderNumber, resolvedClientUid, resolvedVehicleUid, errorText, orderUid)) {
            return false;
        }
    }
    if (result) ++result->ordersLinked;

    if (!resolvedClientUid.isEmpty() && !orderUid.isEmpty() && !orderLinkEventExists(db, resolvedClientUid, orderUid)) {
        if (!CrmTimelineService::addEventDirect(db,
                                                EntityUidService::generateUid(QStringLiteral("event")),
                                                resolvedClientUid,
                                                resolvedVehicleUid,
                                                orderUid,
                                                QStringLiteral("order_linked"),
                                                QString::fromUtf8("Наряд прив’язано до CRM"),
                                                QString::fromUtf8("Автоматичне створення CRM-картки з локального наряду."),
                                                QStringLiteral("system"),
                                                errorText)) {
            return false;
        }
        if (result) ++result->eventsCreated;
    }

    if (clientUid) *clientUid = resolvedClientUid;
    if (vehicleUid) *vehicleUid = resolvedVehicleUid;
    return true;
}

} // namespace

namespace CrmClientService {

bool findByPhone(QSqlDatabase db, const QString& phone, ClientRecord* out, QString* errorText)
{
    return findByNormalizedPhone(db, phone, out, errorText);
}

bool findByNormalizedPhone(QSqlDatabase db, const QString& phone, ClientRecord* out, QString* errorText)
{
    db = effectiveDb(db);
    if (!tableReady(db)) return false;
    const QString phoneNorm = normalizedPhone(phone);
    if (phoneNorm.isEmpty()) return false;
    const QString tail = phoneNorm.right(qMin(9, phoneNorm.size()));

    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT id, client_uid, full_name, first_name, last_name, phone, email, status "
                             "FROM crm_clients "
                             "WHERE %1 LIKE ? OR %2 LIKE ? "
                             "ORDER BY updated_at DESC, id DESC LIMIT 1")
                  .arg(phoneExpr(QStringLiteral("phone")), phoneExpr(QStringLiteral("phone_alt"))));
    q.addBindValue(QStringLiteral("%") + tail);
    q.addBindValue(QStringLiteral("%") + tail);
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    if (!q.next()) return false;
    fillClient(q, out);
    return true;
}

bool findByNormalizedName(QSqlDatabase db, const QString& fullName, ClientRecord* out, QString* errorText)
{
    db = effectiveDb(db);
    if (!tableReady(db)) return false;
    const QString nameNorm = normalizedName(fullName);
    if (nameNorm.size() < 4) return false;

    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT id, client_uid, full_name, first_name, last_name, phone, email, status "
                             "FROM crm_clients ORDER BY updated_at DESC, id DESC"));
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    while (q.next()) {
        if (normalizedName(q.value(2).toString()) == nameNorm) {
            fillClient(q, out);
            return true;
        }
    }
    return false;
}

bool findByUid(QSqlDatabase db, const QString& clientUid, ClientRecord* out, QString* errorText)
{
    db = effectiveDb(db);
    if (!tableReady(db) || clientUid.trimmed().isEmpty()) return false;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT id, client_uid, full_name, first_name, last_name, phone, email, status FROM crm_clients WHERE client_uid=? LIMIT 1"));
    q.addBindValue(clientUid.trimmed());
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    if (!q.next()) return false;
    fillClient(q, out);
    return true;
}

bool updateMissingClientFields(QSqlDatabase db, const QString& clientUid, const ClientRecord& input, QString* errorText)
{
    db = effectiveDb(db);
    if (clientUid.trimmed().isEmpty()) return true;
    QString first = input.firstName.trimmed();
    QString last = input.lastName.trimmed();
    if (first.isEmpty() && last.isEmpty()) splitName(input.fullName, &first, &last);

    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE crm_clients SET "
                             "full_name=CASE WHEN TRIM(COALESCE(full_name,''))='' THEN ? ELSE full_name END, "
                             "first_name=CASE WHEN TRIM(COALESCE(first_name,''))='' THEN ? ELSE first_name END, "
                             "last_name=CASE WHEN TRIM(COALESCE(last_name,''))='' THEN ? ELSE last_name END, "
                             "phone=CASE WHEN TRIM(COALESCE(phone,''))='' THEN ? ELSE phone END, "
                             "email=CASE WHEN TRIM(COALESCE(email,''))='' THEN ? ELSE email END, "
                             "updated_at=datetime('now') WHERE client_uid=?"));
    q.addBindValue(nonNullText(input.fullName.trimmed()));
    q.addBindValue(nonNullText(first));
    q.addBindValue(nonNullText(last));
    q.addBindValue(nonNullText(input.phone.trimmed()));
    q.addBindValue(nonNullText(normalizedEmail(input.email)));
    q.addBindValue(clientUid.trimmed());
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    return true;
}


bool upsertClientDirect(QSqlDatabase db,
                        const QString& clientUid,
                        const QVariantMap& fields,
                        QString* errorText)
{
    db = effectiveDb(db);
    const QString uid = clientUid.trimmed().isEmpty()
        ? EntityUidService::generateUid(QStringLiteral("client"))
        : clientUid.trimmed();
    if (uid.isEmpty()) {
        if (errorText) *errorText = QString::fromUtf8("CRM client sync-подія без client_uid.");
        return false;
    }
    if (!tableReady(db)) {
        if (errorText) *errorText = QString::fromUtf8("Таблиця crm_clients відсутня.");
        return false;
    }

    static const QStringList allowed = {
        QStringLiteral("full_name"), QStringLiteral("first_name"), QStringLiteral("last_name"),
        QStringLiteral("phone"), QStringLiteral("phone_alt"), QStringLiteral("email"),
        QStringLiteral("source"), QStringLiteral("status"), QStringLiteral("loyalty_level"),
        QStringLiteral("discount_percent"), QStringLiteral("notes"), QStringLiteral("created_at"),
        QStringLiteral("updated_at")
    };

    QStringList assignments;
    QList<QVariant> values;
    for (const QString& key : allowed) {
        if (!fields.contains(key)) continue;
        assignments.append(QStringLiteral("%1=?").arg(key));
        values.append(normalizedDirectFieldValue(key, fields.value(key)));
    }
    if (!assignments.contains(QStringLiteral("updated_at=?"))) {
        assignments.append(QStringLiteral("updated_at=datetime('now')"));
    }

    if (!assignments.isEmpty()) {
        QSqlQuery update(db);
        update.prepare(QStringLiteral("UPDATE crm_clients SET %1 WHERE client_uid=?").arg(assignments.join(QLatin1Char(','))));
        for (const QVariant& value : values) update.addBindValue(value);
        update.addBindValue(uid);
        if (!update.exec()) {
            if (errorText) *errorText = update.lastError().text();
            return false;
        }
        if (update.numRowsAffected() > 0) return true;
    }

    QString fullName = fields.value(QStringLiteral("full_name")).toString().trimmed();
    QString firstName = fields.value(QStringLiteral("first_name")).toString().trimmed();
    QString lastName = fields.value(QStringLiteral("last_name")).toString().trimmed();
    if (fullName.isEmpty()) fullName = (firstName + QLatin1Char(' ') + lastName).simplified();
    if (firstName.isEmpty() && lastName.isEmpty()) splitName(fullName, &firstName, &lastName);
    if (fullName.isEmpty() && firstName.isEmpty() && lastName.isEmpty()
        && normalizedPhone(fields.value(QStringLiteral("phone")).toString()).isEmpty()
        && normalizedEmail(fields.value(QStringLiteral("email")).toString()).isEmpty()) {
        if (errorText) *errorText = QString::fromUtf8("Порожня CRM-картка клієнта пропущена.");
        return true;
    }

    QSqlQuery insert(db);
    insert.prepare(QStringLiteral("INSERT INTO crm_clients(client_uid, full_name, first_name, last_name, phone, phone_alt, email, source, status, loyalty_level, discount_percent, notes, created_at, updated_at) "
                                   "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,COALESCE(NULLIF(?,''),datetime('now')),COALESCE(NULLIF(?,''),datetime('now')))"));
    insert.addBindValue(uid);
    insert.addBindValue(nonNullText(fullName));
    insert.addBindValue(nonNullText(firstName));
    insert.addBindValue(nonNullText(lastName));
    insert.addBindValue(nonNullText(fields.value(QStringLiteral("phone")).toString().trimmed()));
    insert.addBindValue(nonNullText(fields.value(QStringLiteral("phone_alt")).toString().trimmed()));
    insert.addBindValue(nonNullText(normalizedEmail(fields.value(QStringLiteral("email")).toString())));
    insert.addBindValue(nonNullText(fields.value(QStringLiteral("source")).toString().trimmed()));
    insert.addBindValue(fields.value(QStringLiteral("status"), QStringLiteral("active")).toString().trimmed().isEmpty() ? QStringLiteral("active") : fields.value(QStringLiteral("status")).toString().trimmed());
    insert.addBindValue(nonNullText(fields.value(QStringLiteral("loyalty_level")).toString().trimmed()));
    insert.addBindValue(fields.value(QStringLiteral("discount_percent")).toDouble());
    insert.addBindValue(fields.value(QStringLiteral("notes")).toString());
    insert.addBindValue(nonNullText(fields.value(QStringLiteral("created_at")).toString().trimmed()));
    insert.addBindValue(nonNullText(fields.value(QStringLiteral("updated_at")).toString().trimmed()));
    if (!insert.exec()) {
        if (errorText) *errorText = insert.lastError().text();
        return false;
    }
    return true;
}

bool createClient(QSqlDatabase db, const ClientRecord& input, QString* clientUid, QString* errorText)
{
    db = effectiveDb(db);
    if (!hasMeaningfulClientData(input)) {
        if (clientUid) clientUid->clear();
        return true;
    }
    ClientRecord existing;
    if (!normalizedPhone(input.phone).isEmpty() && findByNormalizedPhone(db, input.phone, &existing, nullptr)) {
        updateMissingClientFields(db, existing.clientUid, input, nullptr);
        if (clientUid) *clientUid = existing.clientUid;
        return true;
    }
    if (!normalizedEmail(input.email).isEmpty() && findByNormalizedEmailLocal(db, input.email, &existing, nullptr)) {
        updateMissingClientFields(db, existing.clientUid, input, nullptr);
        if (clientUid) *clientUid = existing.clientUid;
        return true;
    }
    if (input.phone.trimmed().isEmpty() && findByNormalizedName(db, input.fullName, &existing, nullptr)) {
        updateMissingClientFields(db, existing.clientUid, input, nullptr);
        if (clientUid) *clientUid = existing.clientUid;
        return true;
    }

    const QString uid = input.clientUid.trimmed().isEmpty() ? EntityUidService::generateUid(QStringLiteral("client")) : input.clientUid.trimmed();
    QString first = input.firstName.trimmed();
    QString last = input.lastName.trimmed();
    if (first.isEmpty() && last.isEmpty()) splitName(input.fullName, &first, &last);

    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT INTO crm_clients(client_uid, full_name, first_name, last_name, phone, email, source, status, created_at, updated_at) "
                             "VALUES(?,?,?,?,?,?, 'desktop_order', ?,datetime('now'),datetime('now'))"));
    q.addBindValue(uid);
    q.addBindValue(nonNullText(input.fullName.trimmed()));
    q.addBindValue(nonNullText(first));
    q.addBindValue(nonNullText(last));
    q.addBindValue(nonNullText(input.phone.trimmed()));
    q.addBindValue(nonNullText(normalizedEmail(input.email)));
    q.addBindValue(input.status.trimmed().isEmpty() ? QStringLiteral("active") : input.status.trimmed());
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    if (clientUid) *clientUid = uid;
    return true;
}

bool findOrCreateFromOrder(QSqlDatabase db,
                           const QString& fullName,
                           const QString& phone,
                           QString* clientUid,
                           QString* errorText)
{
    db = effectiveDb(db);
    ClientRecord input;
    input.fullName = sanitizedOrderClientName(fullName);
    input.phone = sanitizedOrderClientPhone(phone);
    if (input.fullName.isEmpty() && input.phone.isEmpty()) {
        if (clientUid) clientUid->clear();
        return true;
    }

    ClientRecord existing;
    if (!input.phone.isEmpty() && findByNormalizedPhone(db, input.phone, &existing, errorText)) {
        updateMissingClientFields(db, existing.clientUid, input, nullptr);
        if (clientUid) *clientUid = existing.clientUid;
        return true;
    }
    if (input.phone.isEmpty() && findByNormalizedName(db, input.fullName, &existing, nullptr)) {
        updateMissingClientFields(db, existing.clientUid, input, nullptr);
        if (clientUid) *clientUid = existing.clientUid;
        return true;
    }
    return createClient(db, input, clientUid, errorText);
}

bool ensureLocalSchema(QSqlDatabase db, QString* errorText)
{
    db = effectiveDb(db);
    if (!tableExists(db, QStringLiteral("work_orders"))) {
        if (errorText) *errorText = QString::fromUtf8("Таблиця work_orders відсутня.");
        return false;
    }

    if (!execSql(db,
                 QStringLiteral("CREATE TABLE IF NOT EXISTS crm_clients ("
                                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                "client_uid TEXT NOT NULL UNIQUE,"
                                "full_name TEXT NOT NULL DEFAULT '',"
                                "first_name TEXT NOT NULL DEFAULT '',"
                                "last_name TEXT NOT NULL DEFAULT '',"
                                "phone TEXT NOT NULL DEFAULT '',"
                                "phone_alt TEXT NOT NULL DEFAULT '',"
                                "email TEXT NOT NULL DEFAULT '',"
                                "source TEXT NOT NULL DEFAULT '',"
                                "status TEXT NOT NULL DEFAULT 'active',"
                                "loyalty_level TEXT NOT NULL DEFAULT '',"
                                "discount_percent REAL NOT NULL DEFAULT 0,"
                                "notes TEXT NOT NULL DEFAULT '',"
                                "created_at TEXT NOT NULL DEFAULT (datetime('now')),"
                                "updated_at TEXT NOT NULL DEFAULT (datetime('now'))"
                                ");"),
                 errorText)) return false;
    if (!execSql(db,
                 QStringLiteral("CREATE TABLE IF NOT EXISTS crm_vehicles ("
                                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                "vehicle_uid TEXT NOT NULL UNIQUE,"
                                "client_uid TEXT NOT NULL DEFAULT '',"
                                "plate TEXT NOT NULL DEFAULT '',"
                                "vin TEXT NOT NULL DEFAULT '',"
                                "brand TEXT NOT NULL DEFAULT '',"
                                "model TEXT NOT NULL DEFAULT '',"
                                "year INTEGER NOT NULL DEFAULT 0,"
                                "engine TEXT NOT NULL DEFAULT '',"
                                "fuel_type TEXT NOT NULL DEFAULT '',"
                                "mileage INTEGER NOT NULL DEFAULT 0,"
                                "color TEXT NOT NULL DEFAULT '',"
                                "notes TEXT NOT NULL DEFAULT '',"
                                "is_primary INTEGER NOT NULL DEFAULT 0,"
                                "created_at TEXT NOT NULL DEFAULT (datetime('now')),"
                                "updated_at TEXT NOT NULL DEFAULT (datetime('now'))"
                                ");"),
                 errorText)) return false;
    if (!execSql(db,
                 QStringLiteral("CREATE TABLE IF NOT EXISTS crm_client_events ("
                                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                "event_uid TEXT NOT NULL UNIQUE,"
                                "client_uid TEXT NOT NULL DEFAULT '',"
                                "vehicle_uid TEXT NOT NULL DEFAULT '',"
                                "order_uid TEXT NOT NULL DEFAULT '',"
                                "event_type TEXT NOT NULL DEFAULT '',"
                                "title TEXT NOT NULL DEFAULT '',"
                                "body TEXT NOT NULL DEFAULT '',"
                                "created_by TEXT NOT NULL DEFAULT '',"
                                "created_at TEXT NOT NULL DEFAULT (datetime('now'))"
                                ");"),
                 errorText)) return false;
    if (!execSql(db,
                 QStringLiteral("CREATE TABLE IF NOT EXISTS crm_client_tasks ("
                                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                "task_uid TEXT NOT NULL UNIQUE,"
                                "client_uid TEXT NOT NULL DEFAULT '',"
                                "vehicle_uid TEXT NOT NULL DEFAULT '',"
                                "order_uid TEXT NOT NULL DEFAULT '',"
                                "title TEXT NOT NULL DEFAULT '',"
                                "due_at TEXT NOT NULL DEFAULT '',"
                                "status TEXT NOT NULL DEFAULT 'open',"
                                "priority TEXT NOT NULL DEFAULT 'normal',"
                                "assigned_to TEXT NOT NULL DEFAULT '',"
                                "created_at TEXT NOT NULL DEFAULT (datetime('now')),"
                                "completed_at TEXT NOT NULL DEFAULT ''"
                                ");"),
                 errorText)) return false;

    if (!ensureColumn(db, QStringLiteral("crm_clients"), QStringLiteral("client_uid"), QStringLiteral("client_uid TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_clients"), QStringLiteral("full_name"), QStringLiteral("full_name TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_clients"), QStringLiteral("first_name"), QStringLiteral("first_name TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_clients"), QStringLiteral("last_name"), QStringLiteral("last_name TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_clients"), QStringLiteral("phone"), QStringLiteral("phone TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_clients"), QStringLiteral("phone_alt"), QStringLiteral("phone_alt TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_clients"), QStringLiteral("email"), QStringLiteral("email TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_clients"), QStringLiteral("source"), QStringLiteral("source TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_clients"), QStringLiteral("status"), QStringLiteral("status TEXT NOT NULL DEFAULT 'active'"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_clients"), QStringLiteral("loyalty_level"), QStringLiteral("loyalty_level TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_clients"), QStringLiteral("discount_percent"), QStringLiteral("discount_percent REAL NOT NULL DEFAULT 0"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_clients"), QStringLiteral("notes"), QStringLiteral("notes TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_clients"), QStringLiteral("created_at"), QStringLiteral("created_at TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_clients"), QStringLiteral("updated_at"), QStringLiteral("updated_at TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_vehicles"), QStringLiteral("vehicle_uid"), QStringLiteral("vehicle_uid TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_vehicles"), QStringLiteral("client_uid"), QStringLiteral("client_uid TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_vehicles"), QStringLiteral("plate"), QStringLiteral("plate TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_vehicles"), QStringLiteral("vin"), QStringLiteral("vin TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_vehicles"), QStringLiteral("brand"), QStringLiteral("brand TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_vehicles"), QStringLiteral("model"), QStringLiteral("model TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_vehicles"), QStringLiteral("year"), QStringLiteral("year INTEGER NOT NULL DEFAULT 0"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_vehicles"), QStringLiteral("engine"), QStringLiteral("engine TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_vehicles"), QStringLiteral("fuel_type"), QStringLiteral("fuel_type TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_vehicles"), QStringLiteral("mileage"), QStringLiteral("mileage INTEGER NOT NULL DEFAULT 0"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_vehicles"), QStringLiteral("color"), QStringLiteral("color TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_vehicles"), QStringLiteral("notes"), QStringLiteral("notes TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_vehicles"), QStringLiteral("is_primary"), QStringLiteral("is_primary INTEGER NOT NULL DEFAULT 0"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_vehicles"), QStringLiteral("created_at"), QStringLiteral("created_at TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_vehicles"), QStringLiteral("updated_at"), QStringLiteral("updated_at TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!backfillMissingCrmUidColumn(db, QStringLiteral("crm_clients"), QStringLiteral("client_uid"), QStringLiteral("client"), errorText)) return false;
    if (!backfillMissingCrmUidColumn(db, QStringLiteral("crm_vehicles"), QStringLiteral("vehicle_uid"), QStringLiteral("vehicle"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("work_orders"), QStringLiteral("client_uid"), QStringLiteral("client_uid TEXT"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("work_orders"), QStringLiteral("vehicle_uid"), QStringLiteral("vehicle_uid TEXT"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("work_orders"), QStringLiteral("crm_backfilled_at"), QStringLiteral("crm_backfilled_at TEXT"), errorText)) return false;
    if (tableExists(db, QStringLiteral("router_cache_work_orders"))) {
        if (!ensureColumn(db, QStringLiteral("router_cache_work_orders"), QStringLiteral("client_uid"), QStringLiteral("client_uid TEXT"), errorText)) return false;
        if (!ensureColumn(db, QStringLiteral("router_cache_work_orders"), QStringLiteral("vehicle_uid"), QStringLiteral("vehicle_uid TEXT"), errorText)) return false;
        if (!ensureColumn(db, QStringLiteral("router_cache_work_orders"), QStringLiteral("crm_backfilled_at"), QStringLiteral("crm_backfilled_at TEXT"), errorText)) return false;
    }
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_work_orders_client_uid ON work_orders(client_uid);"), errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_work_orders_vehicle_uid ON work_orders(vehicle_uid);"), errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_crm_clients_phone ON crm_clients(phone);"), errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_crm_clients_phone_alt ON crm_clients(phone_alt);"), errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_crm_clients_status_updated ON crm_clients(status, updated_at);"), errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_crm_vehicles_client_uid ON crm_vehicles(client_uid);"), errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_crm_vehicles_client_primary ON crm_vehicles(client_uid, is_primary);"), errorText)) return false;
    if (!execSql(db,
                 QStringLiteral("UPDATE crm_vehicles SET is_primary=1 "
                                "WHERE id IN ("
                                "  SELECT v.id FROM crm_vehicles v "
                                "  WHERE TRIM(COALESCE(v.client_uid,''))<>'' "
                                "    AND NOT EXISTS (SELECT 1 FROM crm_vehicles p WHERE p.client_uid=v.client_uid AND COALESCE(p.is_primary,0)=1) "
                                "    AND v.id=(SELECT vv.id FROM crm_vehicles vv WHERE vv.client_uid=v.client_uid ORDER BY vv.updated_at DESC, vv.id DESC LIMIT 1)"
                                ")"),
                 errorText)) return false;
    if (!execSql(db,
                 QStringLiteral("UPDATE crm_vehicles SET is_primary=0 "
                                "WHERE COALESCE(is_primary,0)=1 "
                                "  AND TRIM(COALESCE(client_uid,''))<>'' "
                                "  AND id NOT IN ("
                                "    SELECT keep.id FROM crm_vehicles keep "
                                "    WHERE COALESCE(keep.is_primary,0)=1 "
                                "      AND keep.id=("
                                "        SELECT kk.id FROM crm_vehicles kk "
                                "        WHERE kk.client_uid=keep.client_uid AND COALESCE(kk.is_primary,0)=1 "
                                "        ORDER BY kk.updated_at DESC, kk.id DESC LIMIT 1"
                                "      )"
                                "  )"),
                 errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_crm_vehicles_plate ON crm_vehicles(plate);"), errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_crm_vehicles_client_plate ON crm_vehicles(client_uid, plate);"), errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_crm_vehicles_client_vin ON crm_vehicles(client_uid, vin);"), errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_crm_events_client_created ON crm_client_events(client_uid, created_at);"), errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_crm_tasks_client_status ON crm_client_tasks(client_uid, status);"), errorText)) return false;
    if (tableExists(db, QStringLiteral("router_cache_work_orders"))) {
        if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_router_cache_work_orders_client_uid ON router_cache_work_orders(client_uid);"), errorText)) return false;
        if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_router_cache_work_orders_vehicle_uid ON router_cache_work_orders(vehicle_uid);"), errorText)) return false;
    }
    return true;
}

bool ensureFromWorkOrder(QSqlDatabase db, int orderNumber, QString* clientUid, QString* vehicleUid, QString* errorText)
{
    db = effectiveDb(db);
    if (clientUid) clientUid->clear();
    if (vehicleUid) vehicleUid->clear();
    if (orderNumber <= 0) return true;
    if (!ensureLocalSchema(db, errorText)) return false;
    return processOrderRow(db, QStringLiteral("work_orders"), orderNumber, nullptr, clientUid, vehicleUid, errorText);
}

bool backfillWorkOrders(QSqlDatabase db, QString* errorText)
{
    return backfillExistingData(db, nullptr, errorText);
}

bool backfillExistingData(QSqlDatabase db, BackfillResult* result, QString* errorText)
{
    db = effectiveDb(db);
    if (result) *result = BackfillResult{};
    if (!ensureLocalSchema(db, errorText)) return false;

    const bool startedTx = !db.transaction() ? false : true;
    auto rollbackOnError = [&]() {
        if (startedTx) db.rollback();
    };

    const QStringList sources = tableExists(db, QStringLiteral("router_cache_work_orders"))
        ? QStringList{QStringLiteral("work_orders"), QStringLiteral("router_cache_work_orders")}
        : QStringList{QStringLiteral("work_orders")};

    for (const QString& source : sources) {
        if (!tableExists(db, source)) continue;
        QSqlQuery select(db);
        const bool hasBackfillMarker = columnExists(db, source, QStringLiteral("crm_backfilled_at"));
        QString where = QStringLiteral("COALESCE(number,0)>0");
        if (hasBackfillMarker) {
            where += QStringLiteral(" AND TRIM(COALESCE(crm_backfilled_at,''))=''");
        } else {
            const bool hasClientUid = columnExists(db, source, QStringLiteral("client_uid"));
            const bool hasVehicleUid = columnExists(db, source, QStringLiteral("vehicle_uid"));
            if (hasClientUid || hasVehicleUid) {
                QStringList pending;
                if (hasClientUid) pending << QStringLiteral("TRIM(COALESCE(client_uid,''))=''");
                if (hasVehicleUid) pending << QStringLiteral("TRIM(COALESCE(vehicle_uid,''))=''");
                where += QStringLiteral(" AND (%1)").arg(pending.join(QStringLiteral(" OR ")));
            }
        }
        select.prepare(QStringLiteral("SELECT number FROM %1 WHERE %2 ORDER BY number ASC").arg(source, where));
        if (!select.exec()) {
            if (errorText) *errorText = select.lastError().text();
            rollbackOnError();
            return false;
        }
        while (select.next()) {
            if (!processOrderRow(db, source, select.value(0).toInt(), result, nullptr, nullptr, errorText)) {
                rollbackOnError();
                return false;
            }
        }
    }

    if (startedTx && !db.commit()) {
        if (errorText) *errorText = db.lastError().text();
        return false;
    }
    if (result) {
        qInfo().noquote() << QStringLiteral("CRM_EXISTING_DATA_BACKFILL clients_created=%1 clients_updated=%2 vehicles_created=%3 vehicles_updated=%4 orders_linked=%5 events_created=%6 skipped=%7")
                                 .arg(result->clientsCreated)
                                 .arg(result->clientsUpdated)
                                 .arg(result->vehiclesCreated)
                                 .arg(result->vehiclesUpdated)
                                 .arg(result->ordersLinked)
                                 .arg(result->eventsCreated)
                                 .arg(result->skipped);
    }
    return true;
}

} // namespace CrmClientService
