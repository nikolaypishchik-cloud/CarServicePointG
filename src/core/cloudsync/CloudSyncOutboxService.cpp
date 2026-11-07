#include "core/cloudsync/CloudSyncOutboxService.h"
#include "core/cloudsync/OrderPartIssueEvidence.h"
#include "core/services/PurchasePaymentService.h"
#include "core/cloudsync/SyncEventPolicy.h"
#include "core/util/DateFormats.h"

#include "core/cloudsync/CloudSyncSchemaService.h"
#include "core/cloudsync/DeviceIdentityService.h"
#include "core/cloudsync/CloudSyncSettings.h"
#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/db/sqlite/DbThreadConnectionSupport.h"
#include "core/cloudsync/CloudSyncCredentials.h"

#include <QDateTime>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QVariant>
#include <QUuid>

namespace {

QSqlDatabase effectiveDb(QSqlDatabase db)
{
    return db.isValid() ? db : Database::db();
}

bool captureEnabledForConnection(QSqlDatabase db,bool* enabled,QString* errorText)
{
    *enabled=false;
    if (DbThreadConnectionSupport::isRuntimeConnection(db.connectionName())) {
        *enabled=CloudSyncSettings::load().enabled
            || Database::getSetting(QStringLiteral("settings.cloudsync.capture_outbox_while_disabled"),false).toBool();
        return true;
    }
    // Caller-owned copies use only their own settings, never the global profile,
    // credential migration or the global settings cache.
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type='table' AND name='settings'"))) {if(errorText)*errorText=q.lastError().text();return false;}
    if (!q.next()) return !q.lastError().isValid();
    q.finish();
    if (!q.exec(QStringLiteral("SELECT key,value FROM settings WHERE key IN ('settings.cloudsync.enabled','cloudsync.enabled','settings.cloudsync.capture_outbox_while_disabled','settings.backend.offline','settings.cloudsync.api_token')"))) {if(errorText)*errorText=q.lastError().text();return false;}
    QMap<QString,QVariant> values;
    while(q.next()) values.insert(q.value(0).toString(),q.value(1));
    if(q.lastError().isValid()) {if(errorText)*errorText=q.lastError().text();return false;}
    const bool detailed=values.value(QStringLiteral("settings.cloudsync.enabled")).isValid()
        && !values.value(QStringLiteral("settings.cloudsync.enabled")).isNull();
    bool configured=values.value(detailed?QStringLiteral("settings.cloudsync.enabled"):QStringLiteral("cloudsync.enabled"),false).toBool();
    const QString token=values.value(QStringLiteral("settings.cloudsync.api_token")).toString().trimmed();
    if(token.startsWith(QStringLiteral("dpapi:v1:"))) {
        QString tokenError;
        if(!CloudSyncCredentials::unprotectToken(token,&tokenError).isEmpty()) configured=true;
    } else if(!token.isEmpty()) {
        QString tokenError;const auto protectedToken=CloudSyncCredentials::protectToken(token,&tokenError);
        QSqlQuery migrate(db);
        if(protectedToken.isEmpty() || !migrate.prepare(QStringLiteral("UPDATE settings SET value=? WHERE key='settings.cloudsync.api_token'"))) {
            if(errorText)*errorText=tokenError.isEmpty()?migrate.lastError().text():tokenError;return false;
        }
        migrate.addBindValue(protectedToken);
        if(!migrate.exec() || migrate.numRowsAffected()!=1) {if(errorText)*errorText=QStringLiteral("Caller-owned token migration did not persist");return false;}
        configured=true;
    }
    *enabled=(configured && !values.value(QStringLiteral("settings.backend.offline"),false).toBool())
        || values.value(QStringLiteral("settings.cloudsync.capture_outbox_while_disabled"),false).toBool();
    return true;
}

QString createdAtUtcIso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QString compactJson(const QJsonObject& obj)
{
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QString compactJson(const QJsonArray& arr)
{
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QString sha256Hex(const QString& value)
{
    return QString::fromLatin1(QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha256).toHex());
}

bool outboxTableExists(QSqlDatabase db)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type='table' AND name='sync_outbox' LIMIT 1"));
    return q.exec() && q.next();
}

bool containsTable(const QStringList& tables, const QString& wanted)
{
    for (const QString& table : tables) {
        if (table.trimmed().compare(wanted, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

bool safeIdentifier(const QString& identifier)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    return re.match(identifier).hasMatch();
}

bool tableExists(QSqlDatabase db, const QString& table)
{
    if (!safeIdentifier(table)) return false;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type='table' AND name=? LIMIT 1"));
    q.addBindValue(table);
    return q.exec() && q.next();
}

bool columnExists(QSqlDatabase db, const QString& table, const QString& column)
{
    if (!safeIdentifier(table) || !safeIdentifier(column)) return false;
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table))) return false;
    while (q.next()) {
        if (q.value(1).toString().compare(column, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

QJsonValue jsonValueFromVariant(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) return QString();
    const QJsonValue json = QJsonValue::fromVariant(value);
    if (!json.isUndefined()) return json;
    return value.toString();
}

QJsonObject currentRowObject(QSqlQuery& q)
{
    QJsonObject obj;
    const QSqlRecord rec = q.record();
    for (int i = 0; i < rec.count(); ++i) {
        const QString fieldName = rec.fieldName(i);
        if (fieldName.compare(QStringLiteral("id"), Qt::CaseInsensitive) == 0) continue;
        if (!safeIdentifier(fieldName)) continue;
        obj.insert(fieldName, jsonValueFromVariant(q.value(i)));
    }
    return DateFormats::protocolDates(obj);
}

QJsonObject selectOneByField(QSqlDatabase db, const QString& table, const QString& field, const QString& value)
{
    if (!safeIdentifier(table) || !safeIdentifier(field) || value.trimmed().isEmpty()) return QJsonObject{};
    if (!tableExists(db, table) || !columnExists(db, table, field)) return QJsonObject{};
    QSqlQuery q(db);
    const bool planningDay = table == QStringLiteral("planning_calendar") && field == QStringLiteral("day");
    q.prepare(QStringLiteral("SELECT * FROM %1 WHERE %2 IN (?,?) LIMIT 1").arg(table, field));
    q.addBindValue(value.trimmed());
    q.addBindValue(planningDay ? DateFormats::storage(value) : value.trimmed());
    if (!q.exec() || !q.next()) return QJsonObject{};
    return currentRowObject(q);
}

QJsonArray selectManyByField(QSqlDatabase db, const QString& table, const QString& field, const QString& value, int limit = 200)
{
    QJsonArray rows;
    if (!safeIdentifier(table) || !safeIdentifier(field) || value.trimmed().isEmpty()) return rows;
    if (!tableExists(db, table) || !columnExists(db, table, field)) return rows;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT * FROM %1 WHERE %2=? ORDER BY id LIMIT ?").arg(table, field));
    q.addBindValue(value.trimmed());
    q.addBindValue(limit < 0 ? -1 : qMax(1, limit));
    if (!q.exec()) return rows;
    while (q.next()) rows.append(currentRowObject(q));
    return rows;
}

struct SnapshotMap {
    QString table;
    QString uidColumn;
};

QString purchasePaymentSql(QSqlDatabase db)
{
    const bool balances = tableExists(db, QStringLiteral("purchase_payment_balances"));
    const bool terms = tableExists(db, QStringLiteral("purchase_payment_terms"));
    return QStringLiteral("SELECT p.id,p.part_uid,p.order_uid,p.qty,p.purchase_price,p.purchase_amount,p.paid,%1 AS paid_cents,%2 AS due_date "
        "FROM work_order_parts p %3 %4 WHERE TRIM(COALESCE(p.part_uid,''))<>'' AND TRIM(COALESCE(p.order_uid,''))<>'' ")
        .arg(balances ? QStringLiteral("b.paid_cents") : QStringLiteral("NULL"),
             terms ? QStringLiteral("t.due_date") : QStringLiteral("''"),
             balances ? QStringLiteral("LEFT JOIN purchase_payment_balances b ON b.payment_key='part:'||p.id||':'||p.order_uid") : QString(),
             terms ? QStringLiteral("LEFT JOIN purchase_payment_terms t ON t.payment_key='part:'||p.id||':'||p.order_uid") : QString());
}

QJsonObject purchasePaymentSnapshot(const QSqlQuery& q)
{
    const double amount = q.value(5).toDouble() > 0 ? q.value(5).toDouble() : q.value(3).toDouble() * q.value(4).toDouble();
    const qint64 total = qMax<qint64>(0, PurchasePaymentService::cents(amount));
    const qint64 paid = q.value(7).isNull() ? (q.value(6).toBool() ? total : 0) : qMax<qint64>(0,q.value(7).toLongLong());
    const QDate due = DateFormats::parseDate(q.value(8).toString());
    return {{"part_uid",q.value(1).toString()}, {"order_uid",q.value(2).toString()},
        {"purchase_payment",QJsonObject{{"version",1},{"total_cents",total},{"paid_cents",paid},
            {"due_date",due.isValid() ? due.toString(Qt::ISODate) : QStringLiteral("")}}}};
}

SnapshotMap snapshotMapForEntity(const QString& entityType)
{
    const QString type = entityType.trimmed();
    if (type == QStringLiteral("planning_day")) return {QStringLiteral("planning_calendar"), QStringLiteral("day")};
    if (type == QStringLiteral("work_order")) return {QStringLiteral("work_orders"), QStringLiteral("order_uid")};
    if (type == QStringLiteral("work_order_service")) return {QStringLiteral("work_order_services"), QStringLiteral("service_uid")};
    if (type == QStringLiteral("work_order_part")) return {QStringLiteral("work_order_parts"), QStringLiteral("part_uid")};
    if (type == QStringLiteral("crm_client")) return {QStringLiteral("crm_clients"), QStringLiteral("client_uid")};
    if (type == QStringLiteral("crm_vehicle")) return {QStringLiteral("crm_vehicles"), QStringLiteral("vehicle_uid")};
    if (type == QStringLiteral("crm_task")) return {QStringLiteral("crm_client_tasks"), QStringLiteral("task_uid")};
    if (type == QStringLiteral("crm_event")) return {QStringLiteral("crm_client_events"), QStringLiteral("event_uid")};
    if (type == QStringLiteral("appointment")) return {QStringLiteral("online_booking_requests"), QStringLiteral("appointment_uid")};
    if (type == QStringLiteral("file_attachment")) return {QStringLiteral("file_attachments"), QStringLiteral("file_uid")};
    if (type == QStringLiteral("vehicle_intake")) return {QStringLiteral("vehicle_intake"), QStringLiteral("intake_uid")};
    if (type == QStringLiteral("vehicle_intake_photo")) return {QStringLiteral("vehicle_intake_photos"), QStringLiteral("photo_uid")};
    return {};
}

QJsonObject buildBusinessSnapshot(QSqlDatabase db, const QString& entityType, const QString& entityUid, const MutationResult& result)
{
    if (result.sourceModule == QStringLiteral("PurchasePaymentSync")) {
        QSqlQuery payment(db);
        payment.prepare(purchasePaymentSql(db) + QStringLiteral(" AND p.part_uid=?"));
        payment.addBindValue(entityUid);
        if (!payment.exec() || !payment.next()) return {};
        auto row = selectOneByField(db, QStringLiteral("work_order_parts"), QStringLiteral("part_uid"), entityUid);
        if (row.isEmpty()) return {};
        const auto settlement = purchasePaymentSnapshot(payment);
        for (auto it=settlement.begin(); it!=settlement.end(); ++it) row.insert(it.key(),it.value());
        return OrderPartIssueEvidence::attach(db,QJsonArray{row}).at(0).toObject();
    }
    const SnapshotMap map = snapshotMapForEntity(entityType);
    if (map.table.isEmpty() || map.uidColumn.isEmpty()) return QJsonObject{};

    QJsonObject row = selectOneByField(db, map.table, map.uidColumn, entityUid);
    if (entityType == QStringLiteral("planning_day")) {
        row.insert(QStringLiteral("day"), entityUid);
        if (!row.contains(QStringLiteral("note"))) row.insert(QStringLiteral("note"), QString());
        return row;
    }
    if (row.isEmpty() && entityType != QStringLiteral("work_order")) {
        bool ok = false;
        const int id = result.entityId.trimmed().toInt(&ok);
        if (ok && id > 0) row = selectOneByField(db, map.table, QStringLiteral("id"), QString::number(id));
    }
    if (row.isEmpty()) return row;
    if (!entityUid.trimmed().isEmpty() && !row.contains(map.uidColumn)) row.insert(map.uidColumn, entityUid.trimmed());
    if (entityType == QStringLiteral("work_order_part"))
        row = OrderPartIssueEvidence::attach(db, QJsonArray{row}).at(0).toObject();
    return row;
}

QJsonObject buildRelatedSnapshot(QSqlDatabase db, const QString& entityType, const QString& entityUid)
{
    QJsonObject related;
    const QString uid = entityUid.trimmed();
    if (uid.isEmpty()) return related;

    if (entityType == QStringLiteral("work_order")) {
        const QJsonArray services = selectManyByField(db, QStringLiteral("work_order_services"), QStringLiteral("order_uid"), uid, -1);
        const QJsonArray parts = OrderPartIssueEvidence::attach(db,
            selectManyByField(db, QStringLiteral("work_order_parts"), QStringLiteral("order_uid"), uid, -1));
        const QJsonArray files = selectManyByField(db, QStringLiteral("file_attachments"), QStringLiteral("entity_uid"), uid, 100);
        related.insert(QStringLiteral("services"), services);
        related.insert(QStringLiteral("parts"), parts);
        // Mark completeness only after independent count checks. Never infer
        // removal from a failed query or a truncated legacy snapshot.
        bool complete=true;
        for (const auto& pair : {qMakePair(QString("work_order_services"),services.size()),qMakePair(QString("work_order_parts"),parts.size())}) {
            QSqlQuery count(db);count.prepare("SELECT COUNT(*) FROM "+pair.first+" WHERE order_uid=?");count.addBindValue(uid);
            complete=complete && count.exec() && count.next() && count.value(0).toLongLong()==pair.second;
        }
        if (complete) related.insert(QStringLiteral("complete_order_rows_v1"),true);
        if (!files.isEmpty()) related.insert(QStringLiteral("files"), files);
    } else if (entityType == QStringLiteral("crm_client")) {
        const QJsonArray vehicles = selectManyByField(db, QStringLiteral("crm_vehicles"), QStringLiteral("client_uid"), uid, 100);
        const QJsonArray tasks = selectManyByField(db, QStringLiteral("crm_client_tasks"), QStringLiteral("client_uid"), uid, 100);
        const QJsonArray events = selectManyByField(db, QStringLiteral("crm_client_events"), QStringLiteral("client_uid"), uid, 100);
        if (!vehicles.isEmpty()) related.insert(QStringLiteral("vehicles"), vehicles);
        if (!tasks.isEmpty()) related.insert(QStringLiteral("tasks"), tasks);
        if (!events.isEmpty()) related.insert(QStringLiteral("events"), events);
    } else if (entityType == QStringLiteral("vehicle_intake")) {
        const QJsonArray photos = selectManyByField(db, QStringLiteral("vehicle_intake_photos"), QStringLiteral("intake_uid"), uid, 100);
        if (!photos.isEmpty()) related.insert(QStringLiteral("photos"), photos);
    }
    return related;
}

QString uidByNumericId(QSqlDatabase db, const QString& table, const QString& uidColumn, const QString& entityId)
{
    bool ok = false;
    const int id = entityId.toInt(&ok);
    if (!ok || id <= 0) return QString();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT COALESCE(%1,'') FROM %2 WHERE id=? LIMIT 1").arg(uidColumn, table));
    q.addBindValue(id);
    if (q.exec() && q.next()) return q.value(0).toString().trimmed();
    return QString();
}

bool ensureOutboxSchemaReady(QSqlDatabase db, QString* errorText)
{
    static bool s_schemaReady = false;
    if (s_schemaReady && outboxTableExists(db)) return true;
    QString schemaError;
    if (!CloudSyncSchemaService::ensureSchema(db, &schemaError)) {
        if (errorText) *errorText = schemaError;
        return false;
    }
    s_schemaReady = true;
    return true;
}

bool ensureOutboxTableAvailable(QSqlDatabase db, QString* errorText)
{
    if (!ensureOutboxSchemaReady(db, errorText)) return false;
    if (outboxTableExists(db)) return true;
    if (errorText) *errorText = QString();
    return false;
}

bool pendingIdempotencyExists(QSqlDatabase db, const QString& idempotencyKey)
{
    if (idempotencyKey.trimmed().isEmpty()) return false;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT 1 FROM sync_outbox WHERE idempotency_key=? AND status IN ('pending','failed','sending') LIMIT 1"));
    q.addBindValue(idempotencyKey.trimmed());
    return q.exec() && q.next();
}

bool blankPlaceholderCrmEntity(const QString& entityType, const QJsonObject& payload)
{
    if (entityType == QStringLiteral("crm_client")) {
        const QString identity = payload.value(QStringLiteral("full_name")).toString().trimmed()
            + payload.value(QStringLiteral("first_name")).toString().trimmed()
            + payload.value(QStringLiteral("last_name")).toString().trimmed()
            + payload.value(QStringLiteral("phone")).toString().trimmed()
            + payload.value(QStringLiteral("phone_alt")).toString().trimmed()
            + payload.value(QStringLiteral("email")).toString().trimmed();
        return identity.isEmpty();
    }
    if (entityType == QStringLiteral("crm_vehicle")) {
        if (payload.value(QStringLiteral("client_uid")).toString().trimmed().isEmpty()) return true;
        const QString identity = payload.value(QStringLiteral("plate")).toString().trimmed()
            + payload.value(QStringLiteral("vin")).toString().trimmed()
            + payload.value(QStringLiteral("brand")).toString().trimmed()
            + payload.value(QStringLiteral("model")).toString().trimmed();
        return identity.isEmpty();
    }
    return false;
}

QString resolveWorkOrderUid(QSqlDatabase db, const QString& entityId)
{
    bool ok = false;
    const int number = entityId.trimmed().toInt(&ok);
    if (!ok || number <= 0) return QString();
    for (const QString& field : {QStringLiteral("number"), QStringLiteral("order_number")}) {
        if (!columnExists(db, QStringLiteral("work_orders"), field)) continue;
        QSqlQuery q(db);
        q.prepare(QStringLiteral("SELECT COALESCE(order_uid,'') FROM work_orders WHERE %1=? LIMIT 1").arg(field));
        q.addBindValue(number);
        if (q.exec() && q.next()) return q.value(0).toString().trimmed();
    }
    return QString();
}

QString resolveParentOrderUidFromChild(QSqlDatabase db, const QString& table, const QString& idOrUid)
{
    if (!safeIdentifier(table) || idOrUid.trimmed().isEmpty() || !tableExists(db, table)) return QString();
    const bool isPartTable = table == QStringLiteral("work_order_parts");
    const QString uidColumn = isPartTable ? QStringLiteral("part_uid") : QStringLiteral("service_uid");

    QSqlQuery byUid(db);
    byUid.prepare(QStringLiteral("SELECT COALESCE(order_uid,'') FROM %1 WHERE %2=? LIMIT 1").arg(table, uidColumn));
    byUid.addBindValue(idOrUid.trimmed());
    if (byUid.exec() && byUid.next()) {
        const QString uid = byUid.value(0).toString().trimmed();
        if (!uid.isEmpty()) return uid;
    }

    bool ok = false;
    const int id = idOrUid.trimmed().toInt(&ok);
    if (ok && id > 0) {
        QSqlQuery byId(db);
        byId.prepare(QStringLiteral("SELECT COALESCE(order_uid,''), COALESCE(order_number,0) FROM %1 WHERE id=? LIMIT 1").arg(table));
        byId.addBindValue(id);
        if (byId.exec() && byId.next()) {
            const QString direct = byId.value(0).toString().trimmed();
            if (!direct.isEmpty()) return direct;
            const int orderNumber = byId.value(1).toInt();
            if (orderNumber > 0) return resolveWorkOrderUid(db, QString::number(orderNumber));
        }
    }
    return QString();
}

bool shouldPublishAsWorkOrderEnvelope(const MutationResult& result)
{
    const QString raw = result.entityType.trimmed();
    const bool touchesOrder = containsTable(result.changedTables, QStringLiteral("work_orders"));
    const bool touchesChildren = containsTable(result.changedTables, QStringLiteral("work_order_parts"))
        || containsTable(result.changedTables, QStringLiteral("work_order_services"));
    if (!touchesOrder || !touchesChildren) return false;
    if (raw == QStringLiteral("work_order")) return true;
    return raw == QStringLiteral("part")
        || raw == QStringLiteral("part_batch")
        || raw == QStringLiteral("work_order_part")
        || raw == QStringLiteral("work_order_part_batch")
        || raw == QStringLiteral("service")
        || raw == QStringLiteral("service_batch")
        || raw == QStringLiteral("work_order_service")
        || raw == QStringLiteral("work_order_service_batch");
}

QString normalizedEntityType(const MutationResult& result);

QString resolveEntityUid(QSqlDatabase db, const MutationResult& result)
{
    const QString entityType = normalizedEntityType(result);
    const QString entityId = result.entityId.trimmed();
    const QString rawType = result.entityType.trimmed();
    // A child row ID is not an order number, even when the numbers coincide.
    if (entityType == QStringLiteral("work_order") && shouldPublishAsWorkOrderEnvelope(result)) {
        if (rawType == QStringLiteral("part") || rawType == QStringLiteral("work_order_part"))
            return resolveParentOrderUidFromChild(db, QStringLiteral("work_order_parts"), entityId);
        if ((rawType == QStringLiteral("service") || rawType == QStringLiteral("work_order_service"))
            && result.operation != QStringLiteral("insert_service"))
            return resolveParentOrderUidFromChild(db, QStringLiteral("work_order_services"), entityId);
    }
    const bool entityIdAlreadyMatchesType =
        (entityType == QStringLiteral("work_order") && entityId.startsWith(QStringLiteral("order-")))
        || (entityType == QStringLiteral("crm_client") && entityId.startsWith(QStringLiteral("client-")))
        || (entityType == QStringLiteral("crm_vehicle") && entityId.startsWith(QStringLiteral("vehicle-")))
        || (entityType == QStringLiteral("work_order_part") && entityId.startsWith(QStringLiteral("part-")))
        || (entityType == QStringLiteral("work_order_service") && entityId.startsWith(QStringLiteral("svc-")))
        || (entityType == QStringLiteral("crm_task") && entityId.startsWith(QStringLiteral("task-")))
        || (entityType == QStringLiteral("crm_event") && entityId.startsWith(QStringLiteral("event-")))
        || (entityType == QStringLiteral("appointment") && entityId.startsWith(QStringLiteral("appt-")))
        || (entityType == QStringLiteral("file_attachment") && entityId.startsWith(QStringLiteral("file-")))
        || (entityType == QStringLiteral("vehicle_intake") && entityId.startsWith(QStringLiteral("intake-")))
        || (entityType == QStringLiteral("vehicle_intake_photo") && entityId.startsWith(QStringLiteral("photo-")));
    if (entityIdAlreadyMatchesType) {
        return entityId;
    }

    if (entityType == QStringLiteral("work_order") && !entityId.isEmpty()
        && !selectOneByField(db, QStringLiteral("work_orders"), QStringLiteral("order_uid"), entityId).isEmpty()) {
        return entityId;
    }

    if (entityType == QStringLiteral("work_order") || containsTable(result.changedTables, QStringLiteral("work_orders"))) {
        const QString orderUid = resolveWorkOrderUid(db, entityId);
        if (!orderUid.isEmpty()) return orderUid;
        if (containsTable(result.changedTables, QStringLiteral("work_order_parts"))) {
            const QString childOrderUid = resolveParentOrderUidFromChild(db, QStringLiteral("work_order_parts"), entityId);
            if (!childOrderUid.isEmpty()) return childOrderUid;
        }
        if (containsTable(result.changedTables, QStringLiteral("work_order_services"))) {
            const QString childOrderUid = resolveParentOrderUidFromChild(db, QStringLiteral("work_order_services"), entityId);
            if (!childOrderUid.isEmpty()) return childOrderUid;
        }
    }

    if (containsTable(result.changedTables, QStringLiteral("work_order_parts"))) {
        const QString uid = uidByNumericId(db, QStringLiteral("work_order_parts"), QStringLiteral("part_uid"), entityId);
        if (!uid.isEmpty()) return uid;
    }

    if (containsTable(result.changedTables, QStringLiteral("work_order_services"))) {
        const QString uid = uidByNumericId(db, QStringLiteral("work_order_services"), QStringLiteral("service_uid"), entityId);
        if (!uid.isEmpty()) return uid;
    }

    if (containsTable(result.changedTables, QStringLiteral("crm_clients"))) {
        const QString uid = uidByNumericId(db, QStringLiteral("crm_clients"), QStringLiteral("client_uid"), entityId);
        if (!uid.isEmpty()) return uid;
    }

    if (containsTable(result.changedTables, QStringLiteral("crm_vehicles"))) {
        const QString uid = uidByNumericId(db, QStringLiteral("crm_vehicles"), QStringLiteral("vehicle_uid"), entityId);
        if (!uid.isEmpty()) return uid;
    }

    if (containsTable(result.changedTables, QStringLiteral("crm_client_tasks"))) {
        const QString uid = uidByNumericId(db, QStringLiteral("crm_client_tasks"), QStringLiteral("task_uid"), entityId);
        if (!uid.isEmpty()) return uid;
    }

    if (containsTable(result.changedTables, QStringLiteral("crm_client_events"))) {
        const QString uid = uidByNumericId(db, QStringLiteral("crm_client_events"), QStringLiteral("event_uid"), entityId);
        if (!uid.isEmpty()) return uid;
    }

    if (containsTable(result.changedTables, QStringLiteral("appointments"))) {
        const QString uid = uidByNumericId(db, QStringLiteral("appointments"), QStringLiteral("appointment_uid"), entityId);
        if (!uid.isEmpty()) return uid;
    }

    if (containsTable(result.changedTables, QStringLiteral("file_attachments"))) {
        const QString uid = uidByNumericId(db, QStringLiteral("file_attachments"), QStringLiteral("file_uid"), entityId);
        if (!uid.isEmpty()) return uid;
    }
    if (containsTable(result.changedTables, QStringLiteral("online_booking_requests"))) {
        const QString uid = uidByNumericId(db, QStringLiteral("online_booking_requests"), QStringLiteral("appointment_uid"), entityId);
        if (!uid.isEmpty()) return uid;
    }
    if (containsTable(result.changedTables, QStringLiteral("vehicle_intake"))) {
        const QString uid = uidByNumericId(db, QStringLiteral("vehicle_intake"), QStringLiteral("intake_uid"), entityId);
        if (!uid.isEmpty()) return uid;
    }
    if (containsTable(result.changedTables, QStringLiteral("vehicle_intake_photos"))) {
        const QString uid = uidByNumericId(db, QStringLiteral("vehicle_intake_photos"), QStringLiteral("photo_uid"), entityId);
        if (!uid.isEmpty()) return uid;
    }
    return entityId;
}

QString normalizedEntityType(const MutationResult& result)
{
    const QString raw = result.entityType.trimmed();
    if (shouldPublishAsWorkOrderEnvelope(result)) return QStringLiteral("work_order");
    if (raw == QStringLiteral("order") || raw == QStringLiteral("order_graph")) return QStringLiteral("work_order");
    if (raw == QStringLiteral("part")) return QStringLiteral("work_order_part");
    if (raw == QStringLiteral("service")) return QStringLiteral("work_order_service");
    if (raw == QStringLiteral("part_batch") || raw == QStringLiteral("work_order_part_batch")) return QStringLiteral("work_order");
    if (raw == QStringLiteral("service_batch") || raw == QStringLiteral("work_order_service_batch")) return QStringLiteral("work_order");
    if (!raw.isEmpty()) return raw;
    if (containsTable(result.changedTables, QStringLiteral("work_orders"))) return QStringLiteral("work_order");
    if (containsTable(result.changedTables, QStringLiteral("work_order_parts"))) return QStringLiteral("work_order_part");
    if (containsTable(result.changedTables, QStringLiteral("work_order_services"))) return QStringLiteral("work_order_service");
    if (containsTable(result.changedTables, QStringLiteral("crm_clients"))) return QStringLiteral("crm_client");
    if (containsTable(result.changedTables, QStringLiteral("crm_vehicles"))) return QStringLiteral("crm_vehicle");
    if (containsTable(result.changedTables, QStringLiteral("crm_client_tasks"))) return QStringLiteral("crm_task");
    if (containsTable(result.changedTables, QStringLiteral("crm_client_events"))) return QStringLiteral("crm_event");
    if (containsTable(result.changedTables, QStringLiteral("appointments"))) return QStringLiteral("appointment");
    if (containsTable(result.changedTables, QStringLiteral("file_attachments"))) return QStringLiteral("file_attachment");
    if (containsTable(result.changedTables, QStringLiteral("online_booking_requests"))) return QStringLiteral("appointment");
    if (containsTable(result.changedTables, QStringLiteral("vehicle_intake"))) return QStringLiteral("vehicle_intake");
    if (containsTable(result.changedTables, QStringLiteral("vehicle_intake_photos"))) return QStringLiteral("vehicle_intake_photo");
    return QStringLiteral("mutation");
}

} // namespace

namespace CloudSyncOutboxService {

bool retireLegacyMetadata(QSqlDatabase database, QString* errorText)
{
    QSqlDatabase db = effectiveDb(database);
    if (!db.transaction()) { if (errorText) *errorText = db.lastError().text(); return false; }
    struct Queue { QString table, key, payload, filter, update; };
    const QList<Queue> queues = {
        {"sync_outbox", "event_uid", "payload_json", "status IN ('pending','failed','sending')",
         "status='local_only',last_error='Retained local audit metadata; business snapshots are queued separately',updated_at=datetime('now')"},
        {"sync_inbox", "event_uid", "payload_json", "status IN ('pending','failed','applying','conflict')",
         "status='ignored_metadata',apply_error='Local audit envelope; no business payload',applied_at=datetime('now')"},
        {"sync_conflicts", "conflict_uid", "remote_payload_json", "status='pending' AND conflict_type='unsupported_entity_type'",
         "status='resolved',resolution='ignored_metadata',resolved_by='SyncEventPolicy',resolved_at=datetime('now')"}
    };
    for (const auto& queue : queues) {
        if (!tableExists(db, queue.table)) continue;
        QSqlQuery read(db);
        if (!read.exec(QStringLiteral("SELECT %1,entity_type,%2 FROM %3 WHERE %4")
                       .arg(queue.key, queue.payload, queue.table, queue.filter))) {
            if (errorText) *errorText = read.lastError().text(); db.rollback(); return false;
        }
        QStringList ids;
        while (read.next()) {
            const auto payload = QJsonDocument::fromJson(read.value(2).toString().toUtf8());
            if (payload.isObject() && SyncEventPolicy::isLocalMetadata(read.value(1).toString(), payload.object()))
                ids.append(read.value(0).toString());
        }
        read.finish();
        for (const auto& id : ids) {
            QSqlQuery update(db);
            update.prepare(QStringLiteral("UPDATE %1 SET %2 WHERE %3=?").arg(queue.table, queue.update, queue.key));
            update.addBindValue(id);
            if (!update.exec()) { if (errorText) *errorText=update.lastError().text(); db.rollback(); return false; }
        }
    }
    if (!db.commit()) { if (errorText) *errorText=db.lastError().text(); db.rollback(); return false; }
    return true;
}

bool appendMutation(QSqlDatabase database, const MutationResult& result, QString* errorText)
{
    if (!result.ok) return true;

    // Етап 1: cloud sync має бути повністю вимкнений за замовчуванням і не
    // повинен створювати додаткове навантаження на локальні наряди/PDF/імпорт.
    // Outbox починає фіксувати бізнес-події тільки після явного увімкнення
    // синхронізації або службового прапорця для міграційних/тестових прогонів.
    QSqlDatabase db=effectiveDb(database);
    if (!db.isValid() || !db.isOpen()) {if(errorText)*errorText=QStringLiteral("Mutation outbox source is not open");return false;}
    bool outboxCaptureEnabled=false;
    if (!captureEnabledForConnection(db,&outboxCaptureEnabled,errorText)) return false;
    if (!outboxCaptureEnabled) return true;

    if (result.operation.trimmed().startsWith(QStringLiteral("sync_acked"), Qt::CaseInsensitive)) return true;
    if (result.operation.trimmed().startsWith(QStringLiteral("remote_"), Qt::CaseInsensitive)) return true;
    if (result.sourceModule.trimmed().compare(QStringLiteral("RemoteMutationApplyService"), Qt::CaseInsensitive) == 0) return true;

    if (!db.isOpen() && !db.open()) {
        if (errorText) *errorText = db.lastError().text();
        return false;
    }

    if (!ensureOutboxTableAvailable(db, errorText)) return false;

    const QString operationId = result.operationId.trimmed().isEmpty()
                                    ? QUuid::createUuid().toString(QUuid::WithoutBraces)
                                    : result.operationId.trimmed();
    const QString deviceUid = DeviceIdentityService::deviceUid(db);
    const QString branchUid = DeviceIdentityService::branchUid(db);
    const QString entityType = normalizedEntityType(result);
    // Local maintenance stays in the audit log, not in the domain replication queue.
    if (snapshotMapForEntity(entityType).table.isEmpty()) return true;
    const QString entityUid = resolveEntityUid(db, result);
    const QString now = createdAtUtcIso();
    const QString operation = result.operation.trimmed().isEmpty() ? QStringLiteral("commit") : result.operation.trimmed();

    QJsonArray changedTables;
    QStringList compactTables;
    for (const QString& table : result.changedTables) {
        const QString t = table.trimmed();
        if (t.isEmpty()) continue;
        changedTables.append(t);
        compactTables.append(t);
    }

    // Етапи 5-7: outbox має передавати не тільки metadata mutation, а й
    // контрольований бізнес-знімок сутності. Інакше Business Server API може
    // прийняти event, але не має достатньо даних для матеріалізації CRM/order
    // state у PostgreSQL та Web CRM.
    QJsonObject businessSnapshot = buildBusinessSnapshot(db, entityType, entityUid, result);
    if (businessSnapshot.isEmpty() && !operation.contains(QStringLiteral("delete"), Qt::CaseInsensitive)) return true;
    if (entityType == QStringLiteral("planning_day") && !result.auditBeforeJson.isEmpty()) {
        const auto before = QJsonDocument::fromJson(result.auditBeforeJson.toUtf8()).object();
        businessSnapshot.insert(QStringLiteral("base_note"), before.value(QStringLiteral("note")).toString());
    }
    const QJsonObject relatedSnapshot = buildRelatedSnapshot(db, entityType, entityUid);
    if (blankPlaceholderCrmEntity(entityType, businessSnapshot)) return true;

    const QString idempotencySeed = QStringList{
        operationId,
        deviceUid,
        branchUid,
        entityType,
        entityUid,
        operation,
        compactTables.join(QLatin1Char(',')),
        compactJson(businessSnapshot),
        compactJson(relatedSnapshot)
    }.join(QLatin1Char('|'));
    const QString idempotencyKey = sha256Hex(idempotencySeed);
    const QString mutationUid = QStringLiteral("mutation-") + idempotencyKey.left(40);
    const QString eventUid = mutationUid;
    if (pendingIdempotencyExists(db, idempotencyKey)) return true;

    QJsonObject payload;
    payload.insert(QStringLiteral("schema_version"), 1);
    payload.insert(QStringLiteral("event_uid"), eventUid);
    payload.insert(QStringLiteral("mutation_uid"), mutationUid);
    payload.insert(QStringLiteral("idempotency_key"), idempotencyKey);
    payload.insert(QStringLiteral("operation_id"), operationId);
    payload.insert(QStringLiteral("device_uid"), deviceUid);
    payload.insert(QStringLiteral("branch_uid"), branchUid);
    payload.insert(QStringLiteral("entity_type"), entityType);
    payload.insert(QStringLiteral("entity_uid"), entityUid);
    payload.insert(QStringLiteral("operation"), operation);
    payload.insert(QStringLiteral("changed_tables"), changedTables);
    payload.insert(QStringLiteral("created_at"), now);
    payload.insert(QStringLiteral("updated_at"), now);
    payload.insert(QStringLiteral("source_module"), result.sourceModule);
    payload.insert(QStringLiteral("description"), result.description);
    payload.insert(QStringLiteral("payload"), businessSnapshot);
    const QString closureOrderUid=entityType==QStringLiteral("work_order")?entityUid:businessSnapshot.value("order_uid").toString();
    if(!closureOrderUid.isEmpty() && tableExists(db,QStringLiteral("order_closure_commands"))) {
        QSqlQuery closure(db);
        closure.prepare("SELECT request_uid FROM order_closure_commands WHERE order_uid=? AND state='applied' ORDER BY rowid DESC LIMIT 1");
        closure.addBindValue(closureOrderUid);
        if(closure.exec() && closure.next())payload.insert("closure_base_request_uid",closure.value(0).toString());
    }
    payload.insert(QStringLiteral("payload_complete"), !businessSnapshot.isEmpty());
    payload.insert(QStringLiteral("payload_source"), businessSnapshot.isEmpty()
        ? QStringLiteral("mutation_metadata_only")
        : QStringLiteral("sqlite_business_snapshot"));
    if (!relatedSnapshot.isEmpty()) payload.insert(QStringLiteral("related"), relatedSnapshot);

    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT OR IGNORE INTO sync_outbox("
                             "event_uid, mutation_uid, idempotency_key, operation_id, device_uid, branch_uid, entity_type, entity_uid, operation, changed_tables, payload_json, status, created_at, updated_at, next_retry_at) "
                             "VALUES(?,?,?,?,?,?,?,?,?,?,?, 'pending', datetime('now'), datetime('now'), '')"));
    q.addBindValue(eventUid);
    q.addBindValue(mutationUid);
    q.addBindValue(idempotencyKey);
    q.addBindValue(operationId);
    q.addBindValue(deviceUid);
    q.addBindValue(branchUid);
    q.addBindValue(entityType);
    q.addBindValue(entityUid);
    q.addBindValue(operation);
    q.addBindValue(compactTables.join(QLatin1Char(',')));
    q.addBindValue(compactJson(payload));
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    return true;
}

bool enqueueInitialWorkOrderSnapshots(QSqlDatabase database, QString* errorText)
{
    const auto settings = CloudSyncSettings::load();
    if (!settings.enabled || settings.serverUrl.trimmed().isEmpty()) return true;

    QSqlDatabase db = effectiveDb(database);
    if (!db.isOpen() && !db.open()) {
        if (errorText) *errorText = db.lastError().text();
        return false;
    }
    if (!ensureOutboxTableAvailable(db, errorText)) return false;

    // Some legacy close/delete paths bypass MutationExecutor. Reconcile their
    // durable state before pushing, without deriving deletion from mere absence.
    QSqlQuery changedOrders(db);
    if (!changedOrders.exec(QStringLiteral("SELECT order_uid FROM work_orders WHERE COALESCE(order_uid,'')<>''"))) return false;
    while (changedOrders.next()) {
        const QString uid = changedOrders.value(0).toString();
        const QJsonObject row = selectOneByField(db, QStringLiteral("work_orders"), QStringLiteral("order_uid"), uid);
        const QString signature = sha256Hex(compactJson(row));
        const QString key = QStringLiteral("settings.cloudsync.order_snapshot_closure_v2.") + uid;
        if (Database::getSetting(key, QString()).toString() == signature) continue;
        MutationResult snapshot;
        snapshot.ok = true;
        snapshot.sourceModule = QStringLiteral("CloudSyncReconciliation");
        snapshot.entityType = QStringLiteral("work_order");
        snapshot.entityId = uid;
        snapshot.operation = QStringLiteral("upsert");
        snapshot.changedTables = {QStringLiteral("work_orders")};
        if (!appendMutation(db, snapshot, errorText) || !Database::setSetting(key, signature)) return false;
    }
    QSqlQuery deletions(db);
    if (deletions.exec(QStringLiteral("SELECT id,order_uid FROM legacy_mutation_log WHERE entity_name='order' AND action='delete' AND order_uid<>''"))) {
        while (deletions.next()) {
            const QString uid = deletions.value(1).toString();
            if (!selectOneByField(db, QStringLiteral("work_orders"), QStringLiteral("order_uid"), uid).isEmpty()) continue;
            const QString key = QStringLiteral("settings.cloudsync.deletion_v1.") + deletions.value(0).toString();
            if (Database::getSetting(key, false).toBool()) continue;
            MutationResult tombstone;
            tombstone.ok = true;
            tombstone.sourceModule = QStringLiteral("CloudSyncReconciliation");
            tombstone.entityType = QStringLiteral("work_order");
            tombstone.entityId = uid;
            tombstone.operation = QStringLiteral("delete");
            if (!appendMutation(db, tombstone, errorText) || !Database::setSetting(key, true)) return false;
        }
    }

    const QString calendarKey = QStringLiteral("settings.cloudsync.initial_planning_snapshot_server_v1");
    if (Database::getSetting(calendarKey, QString()).toString() != settings.serverUrl.trimmed()) {
        QSqlQuery days(db);
        if (!days.exec(QStringLiteral("SELECT day FROM planning_calendar ORDER BY day"))) return false;
        while (days.next()) {
            MutationResult snapshot;
            snapshot.ok = true;
            snapshot.sourceModule = QStringLiteral("CloudSyncInitialSnapshot");
            snapshot.entityType = QStringLiteral("planning_day");
            snapshot.entityId = DateFormats::parseDate(days.value(0).toString()).toString(Qt::ISODate);
            snapshot.operation = QStringLiteral("initial_snapshot");
            snapshot.changedTables = {QStringLiteral("planning_calendar")};
            if (!appendMutation(db, snapshot, errorText)) return false;
        }
        if (!Database::setSetting(calendarKey, settings.serverUrl.trimmed())) return false;
    }
    const QString stateKey = QStringLiteral("settings.cloudsync.initial_work_order_snapshot_server");
    if (Database::getSetting(stateKey, QString()).toString().trimmed() == settings.serverUrl.trimmed()) {
        return true;
    }

    QSqlQuery orders(db);
    if (!orders.exec(QStringLiteral(
            "SELECT COALESCE(order_uid,'') FROM work_orders "
            "WHERE COALESCE(order_uid,'')<>'' ORDER BY number"))) {
        if (errorText) *errorText = orders.lastError().text();
        return false;
    }

    while (orders.next()) {
        MutationResult snapshot;
        snapshot.ok = true;
        snapshot.sourceModule = QStringLiteral("CloudSyncInitialSnapshot");
        snapshot.entityType = QStringLiteral("work_order");
        snapshot.entityId = orders.value(0).toString().trimmed();
        snapshot.operation = QStringLiteral("initial_snapshot");
        snapshot.description = QString::fromUtf8("Первинна публікація локального наряду на сервер");
        snapshot.changedTables = {
            QStringLiteral("work_orders"),
            QStringLiteral("work_order_services"),
            QStringLiteral("work_order_parts"),
        };
        QString appendError;
        if (!appendMutation(db, snapshot, &appendError)) {
            if (errorText) *errorText = appendError;
            return false;
        }
    }

    if (!Database::setSetting(stateKey, settings.serverUrl.trimmed())) {
        if (errorText) *errorText = QString::fromUtf8("Не вдалося позначити первинну публікацію нарядів.");
        return false;
    }
    return true;
}
bool enqueuePurchasePaymentSnapshots(QSqlDatabase db, QString* errorText)
{
    const auto settings = CloudSyncSettings::load();
    if (!settings.enabled && !Database::getSetting(QStringLiteral("settings.cloudsync.capture_outbox_while_disabled"), false).toBool()) return true;
    if (!tableExists(db, QStringLiteral("work_order_parts")) || !columnExists(db, QStringLiteral("work_order_parts"), QStringLiteral("purchase_amount"))) return true;
    if (!ensureOutboxTableAvailable(db, errorText)) return false;
    QSqlQuery rows(db);
    if (!rows.exec(purchasePaymentSql(db))) { if (errorText) *errorText=rows.lastError().text(); return false; }
    while (rows.next()) {
        const auto snapshot = purchasePaymentSnapshot(rows);
        const QString key = QStringLiteral("cloudsync.payment_snapshot.v2.") + sha256Hex(settings.serverUrl + QLatin1Char('|') + rows.value(1).toString());
        const QString hash = sha256Hex(compactJson(snapshot));
        QSqlQuery state(db);
        state.prepare(QStringLiteral("SELECT value FROM settings WHERE key=?")); state.addBindValue(key);
        if (!state.exec()) { if (errorText) *errorText=state.lastError().text(); return false; }
        if (state.next() && state.value(0).toString()==hash) continue;
        MutationResult mutation;
        mutation.ok=true; mutation.sourceModule=QStringLiteral("PurchasePaymentSync");
        mutation.entityType=QStringLiteral("work_order_part"); mutation.entityId=rows.value(0).toString();
        mutation.operation=QStringLiteral("payment_snapshot"); mutation.changedTables={QStringLiteral("work_order_parts")};
        if (!appendMutation(db,mutation,errorText)) return false;
        state.prepare(QStringLiteral("INSERT INTO settings(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value"));
        state.addBindValue(key); state.addBindValue(hash);
        if (!state.exec()) { if (errorText) *errorText=state.lastError().text(); return false; }
    }
    return true;
}

int pendingCount(QSqlDatabase database, QString* errorText)
{
    QSqlDatabase db = effectiveDb(database);
    if (!db.isOpen() && !db.open()) {
        if (errorText) *errorText = db.lastError().text();
        return -1;
    }
    if (!ensureOutboxTableAvailable(db, errorText)) return 0;
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM sync_outbox WHERE status IN ('pending','failed','sending')"))) {
        if (errorText) *errorText = q.lastError().text();
        return -1;
    }
    return q.next() ? q.value(0).toInt() : 0;
}

} // namespace CloudSyncOutboxService
