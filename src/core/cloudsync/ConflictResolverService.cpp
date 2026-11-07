#include "core/cloudsync/ConflictResolverService.h"

#include "core/cloudsync/EntityUidService.h"
#include "core/cloudsync/WorkOrderSyncApplyService.h"
#include "core/cloudsync/CloudSyncOutboxService.h"
#include "core/util/DateFormats.h"
#include "infrastructure/db/sqlite/TransactionRunner.h"
#include "core/crm/CrmClientService.h"
#include "core/crm/CrmTaskService.h"
#include "core/crm/CrmTimelineService.h"
#include "core/crm/CrmVehicleService.h"
#include "core/crm/OnlineBookingService.h"
#include "core/services/MutationExecutor.h"
#include "infrastructure/db/sqlite/Db.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QVariant>
#include <QVariantMap>

namespace {

struct ConflictRow {
    QString conflictUid;
    QString entityType;
    QString entityUid;
    QJsonObject localPayload;
    QJsonObject remotePayload;
    QString conflictType;
};

enum class ResolverPolicy {
    LocalWins,
    RemoteWins,
    ManualRequired,
    TimestampLatestWins,
};

QString resolverPolicyCode(ResolverPolicy policy)
{
    switch (policy) {
    case ResolverPolicy::LocalWins:
        return QStringLiteral("local_wins");
    case ResolverPolicy::RemoteWins:
        return QStringLiteral("remote_wins");
    case ResolverPolicy::TimestampLatestWins:
        return QStringLiteral("timestamp_latest_wins");
    case ResolverPolicy::ManualRequired:
    default:
        return QStringLiteral("manual_required");
    }
}

ResolverPolicy passiveDefaultPolicy()
{
    // Stage 09 foundation: record/report conflicts by default. Automatic
    // destructive overwrite is intentionally not a default policy.
    return ResolverPolicy::ManualRequired;
}

struct EntityMap {
    QString entityType;
    QString table;
    QString uidColumn;
    QStringList changedTables;
    QStringList writableFields;
    QStringList identityFields;
    QStringList sensitiveFields;
    bool appendOnly = false;
    bool localPriorityOnMerge = false;
};

QSqlDatabase effectiveDb(QSqlDatabase db)
{
    return db.isValid() ? db : Database::db();
}

QString utcNow()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QString jsonCompact(const QJsonObject& obj)
{
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QJsonObject parseObject(const QString& json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    return doc.isObject() ? doc.object() : QJsonObject{};
}

QJsonObject selectObject(const QJsonObject& event)
{
    QJsonObject payload = event.value(QStringLiteral("payload")).toObject();
    if (payload.isEmpty()) payload = event.value(QStringLiteral("data")).toObject();
    if (payload.isEmpty()) payload = event.value(QStringLiteral("row")).toObject();
    return payload.isEmpty() ? event : payload;
}

QString textValue(const QJsonObject& obj, const QString& key)
{
    return obj.value(key).toString().trimmed();
}

QVariant jsonVariant(const QJsonValue& value)
{
    if (value.isBool()) return value.toBool();
    if (value.isDouble()) return value.toDouble();
    if (value.isString()) return value.toString();
    if (value.isNull() || value.isUndefined()) return QVariant(QString());
    if (value.isArray()) return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
}

QString normalizedJsonValue(const QJsonValue& value)
{
    if (value.isUndefined() || value.isNull()) return QString();
    if (value.isString()) return value.toString().trimmed();
    if (value.isBool()) return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (value.isDouble()) return QString::number(value.toDouble(), 'g', 15);
    if (value.isArray()) return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    if (value.isObject()) return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    return QString();
}

bool isEmptyJsonValue(const QJsonValue& value)
{
    return value.isUndefined()
        || value.isNull()
        || (value.isString() && value.toString().trimmed().isEmpty());
}

bool safeIdentifier(const QString& identifier)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    return re.match(identifier).hasMatch();
}

bool execSql(QSqlDatabase db, const QString& sql, QString* errorText)
{
    QSqlQuery q(db);
    if (!q.exec(sql)) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    return true;
}

bool tableExists(QSqlDatabase db, const QString& table)
{
    if (!safeIdentifier(table)) return false;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type IN ('table','view') AND name=? LIMIT 1"));
    q.addBindValue(table);
    return q.exec() && q.next();
}

QSet<QString> tableColumns(QSqlDatabase db, const QString& table)
{
    QSet<QString> cols;
    if (!safeIdentifier(table)) return cols;
    QSqlQuery q(db);
    q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table));
    while (q.next()) {
        const QString col = q.value(1).toString();
        if (safeIdentifier(col)) cols.insert(col);
    }
    return cols;
}

bool ensureColumn(QSqlDatabase db, const QString& table, const QString& column, const QString& ddl, QString* errorText)
{
    if (!safeIdentifier(table) || !safeIdentifier(column)) return false;
    if (tableColumns(db, table).contains(column)) return true;
    return execSql(db, QStringLiteral("ALTER TABLE %1 ADD COLUMN %2").arg(table, ddl), errorText);
}

EntityMap entityMapForType(const QString& entityType)
{
    const QString type = entityType.trimmed();
    const QStringList clientFields = {QStringLiteral("full_name"), QStringLiteral("first_name"), QStringLiteral("last_name"), QStringLiteral("phone"), QStringLiteral("phone_alt"), QStringLiteral("email"), QStringLiteral("source"), QStringLiteral("status"), QStringLiteral("loyalty_level"), QStringLiteral("discount_percent"), QStringLiteral("notes"), QStringLiteral("created_at"), QStringLiteral("updated_at")};
    const QStringList vehicleFields = {QStringLiteral("client_uid"), QStringLiteral("plate"), QStringLiteral("vin"), QStringLiteral("brand"), QStringLiteral("model"), QStringLiteral("year"), QStringLiteral("engine"), QStringLiteral("fuel_type"), QStringLiteral("mileage"), QStringLiteral("color"), QStringLiteral("notes"), QStringLiteral("is_primary"), QStringLiteral("created_at"), QStringLiteral("updated_at")};
    const QStringList orderFields = {QStringLiteral("client_uid"), QStringLiteral("vehicle_uid"), QStringLiteral("number"), QStringLiteral("status"), QStringLiteral("total_amount"), QStringLiteral("created_at"), QStringLiteral("updated_at")};
    const QStringList serviceFields = {QStringLiteral("order_uid"), QStringLiteral("order_number"), QStringLiteral("title"), QStringLiteral("qty"), QStringLiteral("price"), QStringLiteral("amount"), QStringLiteral("note"), QStringLiteral("paid"), QStringLiteral("parent_id"), QStringLiteral("created_at"), QStringLiteral("updated_at")};
    const QStringList partFields = {QStringLiteral("order_uid"), QStringLiteral("order_number"), QStringLiteral("ordered_at"), QStringLiteral("delivery_date"), QStringLiteral("installed_at"), QStringLiteral("store"), QStringLiteral("part_number"), QStringLiteral("name"), QStringLiteral("manufacturer"), QStringLiteral("note"), QStringLiteral("qty"), QStringLiteral("price"), QStringLiteral("amount"), QStringLiteral("service_id"), QStringLiteral("ordered_by_admin"), QStringLiteral("paid"), QStringLiteral("purchase_price"), QStringLiteral("purchase_amount"), QStringLiteral("markup_percent"), QStringLiteral("manual_markup_override"), QStringLiteral("markup_source"), QStringLiteral("pricing_calc_version"), QStringLiteral("updated_at")};
    const QStringList appointmentFields = {QStringLiteral("organization_uid"), QStringLiteral("branch_uid"), QStringLiteral("client_uid"), QStringLiteral("vehicle_uid"), QStringLiteral("client_name"), QStringLiteral("phone"), QStringLiteral("plate"), QStringLiteral("car_text"), QStringLiteral("service_reason"), QStringLiteral("desired_start_at"), QStringLiteral("desired_end_at"), QStringLiteral("status"), QStringLiteral("source"), QStringLiteral("comment"), QStringLiteral("created_at"), QStringLiteral("updated_at")};
    const QStringList crmEventFields = {QStringLiteral("client_uid"), QStringLiteral("vehicle_uid"), QStringLiteral("order_uid"), QStringLiteral("event_type"), QStringLiteral("title"), QStringLiteral("body"), QStringLiteral("created_by"), QStringLiteral("created_at")};
    const QStringList crmTaskFields = {QStringLiteral("client_uid"), QStringLiteral("vehicle_uid"), QStringLiteral("order_uid"), QStringLiteral("title"), QStringLiteral("due_at"), QStringLiteral("status"), QStringLiteral("priority"), QStringLiteral("assigned_to"), QStringLiteral("created_at"), QStringLiteral("completed_at")};
    const QStringList fileFields = {QStringLiteral("entity_type"), QStringLiteral("entity_uid"), QStringLiteral("local_path"), QStringLiteral("server_url"), QStringLiteral("remote_url"), QStringLiteral("mime_type"), QStringLiteral("file_name"), QStringLiteral("filename"), QStringLiteral("sha256"), QStringLiteral("size_bytes"), QStringLiteral("size"), QStringLiteral("storage_provider"), QStringLiteral("metadata_json"), QStringLiteral("sync_status"), QStringLiteral("created_at")};

    if (type == QStringLiteral("crm_client")) {
        return {type, QStringLiteral("crm_clients"), QStringLiteral("client_uid"), {QStringLiteral("crm_clients")}, clientFields,
                {QStringLiteral("full_name"), QStringLiteral("first_name"), QStringLiteral("last_name"), QStringLiteral("phone"), QStringLiteral("phone_alt"), QStringLiteral("email")},
                {QStringLiteral("discount_percent"), QStringLiteral("loyalty_level"), QStringLiteral("status")}, false, false};
    }
    if (type == QStringLiteral("crm_vehicle")) {
        return {type, QStringLiteral("crm_vehicles"), QStringLiteral("vehicle_uid"), {QStringLiteral("crm_vehicles")}, vehicleFields,
                {QStringLiteral("plate"), QStringLiteral("vin"), QStringLiteral("brand"), QStringLiteral("model"), QStringLiteral("year")},
                {QStringLiteral("mileage"), QStringLiteral("color"), QStringLiteral("is_primary")}, false, false};
    }
    if (type == QStringLiteral("work_order")) return {type, QStringLiteral("work_orders"), QStringLiteral("order_uid"), {QStringLiteral("work_orders")}, orderFields, {}, {}, false, true};
    if (type == QStringLiteral("work_order_service")) return {type, QStringLiteral("work_order_services"), QStringLiteral("service_uid"), {QStringLiteral("work_order_services")}, serviceFields, {}, {}, false, true};
    if (type == QStringLiteral("work_order_part")) {
        return {type, QStringLiteral("work_order_parts"), QStringLiteral("part_uid"), {QStringLiteral("work_order_parts")}, partFields, {},
                {QStringLiteral("price"), QStringLiteral("qty"), QStringLiteral("quantity"), QStringLiteral("paid"), QStringLiteral("is_paid"), QStringLiteral("installed"), QStringLiteral("is_installed"), QStringLiteral("installed_at"), QStringLiteral("client_price"), QStringLiteral("supplier_price"), QStringLiteral("total"), QStringLiteral("sum"), QStringLiteral("amount"), QStringLiteral("purchase_price"), QStringLiteral("purchase_amount")}, false, true};
    }
    if (type == QStringLiteral("appointment")) return {type, QStringLiteral("online_booking_requests"), QStringLiteral("appointment_uid"), {QStringLiteral("online_booking_requests")}, appointmentFields, {}, {}, false, false};
    if (type == QStringLiteral("crm_event")) return {type, QStringLiteral("crm_client_events"), QStringLiteral("event_uid"), {QStringLiteral("crm_client_events")}, crmEventFields, {}, {}, true, false};
    if (type == QStringLiteral("crm_task")) return {type, QStringLiteral("crm_client_tasks"), QStringLiteral("task_uid"), {QStringLiteral("crm_client_tasks")}, crmTaskFields, {}, {}, false, false};
    if (type == QStringLiteral("file_attachment")) return {type, QStringLiteral("file_attachments"), QStringLiteral("file_uid"), {QStringLiteral("file_attachments")}, fileFields, {}, {}, false, false};
    return {};
}

bool conflictByUid(QSqlDatabase db, const QString& conflictUid, ConflictRow* row, QString* errorText)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT conflict_uid,entity_type,entity_uid,local_payload_json,remote_payload_json,conflict_type FROM sync_conflicts WHERE conflict_uid=? LIMIT 1"));
    q.addBindValue(conflictUid.trimmed());
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    if (!q.next()) {
        if (errorText) *errorText = QString::fromUtf8("Конфлікт не знайдено: %1").arg(conflictUid);
        return false;
    }
    if (row) {
        row->conflictUid = q.value(0).toString();
        row->entityType = q.value(1).toString();
        row->entityUid = q.value(2).toString();
        row->localPayload = parseObject(q.value(3).toString());
        row->remotePayload = parseObject(q.value(4).toString());
        row->conflictType = q.value(5).toString();
    }
    return true;
}

QJsonObject rowByUid(QSqlDatabase db, const EntityMap& map, const QString& uid)
{
    QJsonObject out;
    if (!safeIdentifier(map.table) || !safeIdentifier(map.uidColumn) || uid.trimmed().isEmpty()) return out;
    if (!tableExists(db, map.table)) return out;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT * FROM %1 WHERE %2=? LIMIT 1").arg(map.table, map.uidColumn));
    q.addBindValue(uid.trimmed());
    if (!q.exec() || !q.next()) return out;
    const QSqlRecord rec = q.record();
    for (int i = 0; i < rec.count(); ++i) out.insert(rec.fieldName(i), QJsonValue::fromVariant(q.value(i)));
    return out;
}

QJsonObject mergePayloadForEntity(const EntityMap& map, const QJsonObject& local, const QJsonObject& remote, const QString& resolution)
{
    if (resolution == QStringLiteral("accept_remote")) return remote;

    QJsonObject merged = local.isEmpty() ? remote : local;
    for (auto it = remote.constBegin(); it != remote.constEnd(); ++it) {
        const QString key = it.key();
        if (key == QStringLiteral("id")) continue;
        if (key == map.uidColumn) {
            merged.insert(key, it.value());
            continue;
        }

        const bool isSensitive = map.sensitiveFields.contains(key);
        const bool isIdentity = map.identityFields.contains(key);
        const bool keepLocal = map.localPriorityOnMerge || isSensitive || isIdentity;
        const QJsonValue localValue = merged.value(key);

        if (isEmptyJsonValue(localValue)) {
            merged.insert(key, it.value());
            continue;
        }
        if (key.endsWith(QStringLiteral("_at"))) {
            const QString localText = normalizedJsonValue(localValue);
            const QString remoteText = normalizedJsonValue(it.value());
            if (remoteText > localText) merged.insert(key, it.value());
            continue;
        }
        if (!keepLocal) {
            merged.insert(key, it.value());
        }
    }
    return merged;
}

bool upsertAllowedFieldsByUid(QSqlDatabase db, const EntityMap& map, const QString& uid, const QJsonObject& data, QString* errorText)
{
    if (!safeIdentifier(map.table) || !safeIdentifier(map.uidColumn)) {
        if (errorText) *errorText = QString::fromUtf8("Небезпечна назва таблиці/UID у conflict resolver.");
        return false;
    }
    if (uid.trimmed().isEmpty()) {
        if (errorText) *errorText = QString::fromUtf8("Конфлікт %1 без UID.").arg(map.entityType);
        return false;
    }
    if (!tableExists(db, map.table)) {
        if (errorText) *errorText = QString::fromUtf8("Локальна таблиця для conflict resolver відсутня: %1.").arg(map.table);
        return false;
    }
    const QSet<QString> columns = tableColumns(db, map.table);
    if (!columns.contains(map.uidColumn)) {
        if (errorText) *errorText = QString::fromUtf8("У таблиці %1 відсутня UID-колонка %2.").arg(map.table, map.uidColumn);
        return false;
    }

    QStringList insertColumns{map.uidColumn};
    QList<QVariant> values{uid.trimmed()};
    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        const QString key = it.key().trimmed();
        if (key == map.uidColumn || key == QStringLiteral("id")) continue;
        if (!safeIdentifier(key) || !columns.contains(key)) continue;
        if (!map.writableFields.isEmpty() && !map.writableFields.contains(key)) continue;
        insertColumns.append(key);
        values.append(jsonVariant(it.value()));
    }
    if (columns.contains(QStringLiteral("updated_at")) && !insertColumns.contains(QStringLiteral("updated_at"))) {
        insertColumns.append(QStringLiteral("updated_at"));
        values.append(utcNow());
    }

    if (map.appendOnly) {
        QSqlQuery exists(db);
        exists.prepare(QStringLiteral("SELECT 1 FROM %1 WHERE %2=? LIMIT 1").arg(map.table, map.uidColumn));
        exists.addBindValue(uid.trimmed());
        if (exists.exec() && exists.next()) return true;
    }

    QStringList updateAssignments;
    QList<QVariant> updateValues;
    for (int i = 0; i < insertColumns.size(); ++i) {
        if (insertColumns.at(i) == map.uidColumn) continue;
        updateAssignments.append(QStringLiteral("%1=?").arg(insertColumns.at(i)));
        updateValues.append(values.at(i));
    }
    if (!updateAssignments.isEmpty()) {
        QSqlQuery update(db);
        update.prepare(QStringLiteral("UPDATE %1 SET %2 WHERE %3=?").arg(map.table, updateAssignments.join(QLatin1Char(',')), map.uidColumn));
        for (const QVariant& value : updateValues) update.addBindValue(value);
        update.addBindValue(uid.trimmed());
        if (!update.exec()) {
            if (errorText) *errorText = update.lastError().text();
            return false;
        }
        if (update.numRowsAffected() > 0) return true;
    }

    QStringList placeholders;
    for (int i = 0; i < insertColumns.size(); ++i) placeholders.append(QStringLiteral("?"));
    QSqlQuery insert(db);
    insert.prepare(QStringLiteral("INSERT INTO %1(%2) VALUES(%3)")
        .arg(map.table, insertColumns.join(QLatin1Char(',')), placeholders.join(QLatin1Char(','))));
    for (const QVariant& value : values) insert.addBindValue(value);
    if (!insert.exec()) {
        if (errorText) *errorText = insert.lastError().text();
        return false;
    }
    return true;
}

bool appendSyncAudit(QSqlDatabase db,
                     const QString& action,
                     const QString& entityType,
                     const QString& entityUid,
                     const QString& conflictUid,
                     const QString& resolution,
                     const QString& result,
                     const QString& actor,
                     const QString& reason,
                     const QJsonObject& details)
{
    QString ignored;
    execSql(db,
            QStringLiteral("CREATE TABLE IF NOT EXISTS sync_audit_log ("
                           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                           "created_at TEXT NOT NULL DEFAULT (datetime('now')),"
                           "action TEXT NOT NULL DEFAULT '',"
                           "entity_type TEXT NOT NULL DEFAULT '',"
                           "entity_uid TEXT NOT NULL DEFAULT '',"
                           "conflict_uid TEXT NOT NULL DEFAULT '',"
                           "resolution TEXT NOT NULL DEFAULT '',"
                           "result TEXT NOT NULL DEFAULT '',"
                           "actor TEXT NOT NULL DEFAULT '',"
                           "reason TEXT NOT NULL DEFAULT '',"
                           "details_json TEXT NOT NULL DEFAULT '{}'"
                           ");"),
            &ignored);
    execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_sync_audit_log_created ON sync_audit_log(created_at);"), &ignored);
    execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_sync_audit_log_action ON sync_audit_log(action);"), &ignored);

    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT INTO sync_audit_log(created_at,action,entity_type,entity_uid,conflict_uid,resolution,result,actor,reason,details_json) "
                             "VALUES(?,?,?,?,?,?,?,?,?,?)"));
    q.addBindValue(utcNow());
    q.addBindValue(action.trimmed());
    q.addBindValue(entityType.trimmed());
    q.addBindValue(entityUid.trimmed());
    q.addBindValue(conflictUid.trimmed());
    q.addBindValue(resolution.trimmed());
    q.addBindValue(result.trimmed());
    q.addBindValue(actor.trimmed());
    q.addBindValue(reason.trimmed());
    q.addBindValue(jsonCompact(details));
    const bool ok = q.exec();

    if (tableExists(db, QStringLiteral("audit_log"))) {
        QSqlQuery aq(db);
        aq.prepare(QStringLiteral("INSERT INTO audit_log(created_at,username,action,entity_type,entity_id,result,reason,details_json,device_id) "
                                  "VALUES(?,?,?,?,?,?,?,?,?)"));
        aq.addBindValue(utcNow());
        aq.addBindValue(actor.trimmed());
        aq.addBindValue(action.trimmed());
        aq.addBindValue(entityType.trimmed());
        aq.addBindValue(entityUid.trimmed());
        aq.addBindValue(result.trimmed());
        aq.addBindValue(reason.trimmed());
        aq.addBindValue(jsonCompact(details));
        aq.addBindValue(QStringLiteral("cloudsync"));
        aq.exec();
    }
    return ok;
}


QVariantMap jsonObjectToVariantMap(const QJsonObject& data)
{
    QVariantMap map;
    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        map.insert(it.key(), jsonVariant(it.value()));
    }
    return map;
}

bool applyResolvedPayloadThroughDomainService(QSqlDatabase db,
                                              const EntityMap& map,
                                              const QString& uid,
                                              const QJsonObject& data,
                                              QString* errorText)
{
    QVariantMap fields = jsonObjectToVariantMap(data);
    if (map.entityType == QStringLiteral("crm_client")) {
        fields.insert(QStringLiteral("client_uid"), uid);
        return CrmClientService::upsertClientDirect(db, uid, fields, errorText);
    }
    if (map.entityType == QStringLiteral("crm_vehicle")) {
        fields.insert(QStringLiteral("vehicle_uid"), uid);
        return CrmVehicleService::upsertVehicleDirect(db, uid, fields, errorText);
    }
    if (map.entityType == QStringLiteral("crm_task")) {
        fields.insert(QStringLiteral("task_uid"), uid);
        return CrmTaskService::upsertTaskDirect(db, uid, fields, errorText);
    }
    if (map.entityType == QStringLiteral("crm_event")) {
        return CrmTimelineService::addEventDirect(db,
                                                 uid,
                                                 textValue(data, QStringLiteral("client_uid")),
                                                 textValue(data, QStringLiteral("vehicle_uid")),
                                                 textValue(data, QStringLiteral("order_uid")),
                                                 textValue(data, QStringLiteral("event_type")),
                                                 textValue(data, QStringLiteral("title")),
                                                 textValue(data, QStringLiteral("body")),
                                                 textValue(data, QStringLiteral("created_by")),
                                                 errorText);
    }
    if (map.entityType == QStringLiteral("appointment")) {
        fields.insert(QStringLiteral("appointment_uid"), uid);
        return OnlineBookingService::upsertRequest(db, fields, errorText);
    }
    if (map.entityType == QStringLiteral("work_order")) {
        fields.insert(QStringLiteral("order_uid"), uid);
        return WorkOrderSyncApplyService::upsertWorkOrderDirect(db, uid, fields, errorText);
    }
    if (map.entityType == QStringLiteral("work_order_service")) {
        fields.insert(QStringLiteral("service_uid"), uid);
        return WorkOrderSyncApplyService::upsertServiceDirect(db, uid, fields, errorText);
    }
    if (map.entityType == QStringLiteral("work_order_part")) {
        fields.insert(QStringLiteral("part_uid"), uid);
        return WorkOrderSyncApplyService::upsertPartDirect(db, uid, fields, errorText);
    }
    return upsertAllowedFieldsByUid(db, map, uid, data, errorText);
}

bool applyResolutionPayload(QSqlDatabase db, const ConflictRow& conflict, const QString& resolution, QString* errorText)
{
    const EntityMap map = entityMapForType(conflict.entityType);
    if (map.table.isEmpty()) {
        if (errorText) *errorText = QString::fromUtf8("Непідтриманий тип конфлікту: %1").arg(conflict.entityType);
        return false;
    }
    const QJsonObject remote = selectObject(conflict.remotePayload);
    const QJsonObject localFromConflict = conflict.localPayload;
    const QString uid = textValue(remote, map.uidColumn).isEmpty() ? conflict.entityUid.trimmed() : textValue(remote, map.uidColumn);
    const QJsonObject currentLocal = rowByUid(db, map, uid);
    const QJsonObject local = currentLocal.isEmpty() ? localFromConflict : currentLocal;

    if (resolution == QStringLiteral("keep_local")) {
        return true;
    }

    const QJsonObject data = mergePayloadForEntity(map, local, remote, resolution);
    if (map.appendOnly && !rowByUid(db, map, uid).isEmpty()) {
        return true;
    }

    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("ConflictResolverService");
    request.entityType = conflict.entityType;
    request.entityId = uid;
    request.operation = QStringLiteral("resolve_%1").arg(resolution);
    request.description = QString::fromUtf8("Застосування рішення конфлікту синхронізації через mutation pipeline");
    request.changedTables = map.changedTables;
    request.beginErrorFallback = QString::fromUtf8("Не вдалося відкрити транзакцію рішення конфлікту.");
    request.commitErrorFallback = QString::fromUtf8("Не вдалося завершити рішення конфлікту.");
    request.systemMaintenance = true;

    const auto result = MutationExecutor::execute(db, request, [&](QSqlDatabase& txDb, QString* txError) {
        return applyResolvedPayloadThroughDomainService(txDb, map, uid, data, txError);
    });
    if (!result.ok) {
        if (errorText) *errorText = result.errorText;
        return false;
    }
    return true;
}

bool resolvePlanningConflict(QSqlDatabase db,const ConflictRow& conflict,const QString& resolution,
                             const QString& actor,QString* errorText)
{
    if(resolution==QStringLiteral("merge")) {
        if(errorText) *errorText=QString::fromUtf8("Для календаря виберіть локальну або серверну версію після порівняння записів.");
        return false;
    }
    const auto result=TransactionRunner::run(db,QString::fromUtf8("Не вдалося почати узгодження календаря."),
        QString::fromUtf8("Не вдалося зберегти узгодження календаря."),[&](QString* error) {
        QSqlQuery query(db);
        query.prepare(QStringLiteral("SELECT day,note FROM planning_calendar WHERE day IN (?,?)"));
        query.addBindValue(DateFormats::storage(conflict.entityUid)); query.addBindValue(conflict.entityUid);
        if(!query.exec() || !query.next()) { if(error) *error=QString::fromUtf8("Локальний запис календаря не знайдено."); return false; }
        const QString day=query.value(0).toString(), localNote=query.value(1).toString();
        if(query.next()) { if(error) *error=QString::fromUtf8("Знайдено два записи календаря на одну дату. Потрібна перевірка дублікатів."); return false; }
        if(localNote!=conflict.localPayload.value(QStringLiteral("note")).toString()) {
            if(error) *error=QString::fromUtf8("Календар змінився після виявлення конфлікту. Повторіть синхронізацію та порівняння."); return false;
        }
        const auto remote=selectObject(conflict.remotePayload);
        if(!remote.contains(QStringLiteral("note")) || !remote.value(QStringLiteral("note")).isString()) {
            if(error) *error=QString::fromUtf8("Серверна версія календаря не містить записів."); return false;
        }
        const QString remoteNote=remote.value(QStringLiteral("note")).toString();
        // Retain rejected events for audit, but never resend the obsolete baseline.
        query.prepare(QStringLiteral("UPDATE sync_outbox SET status='superseded',last_error='Resolved calendar conflict',updated_at=datetime('now') WHERE entity_type='planning_day' AND entity_uid=? AND status IN ('pending','failed','sending')"));
        query.addBindValue(conflict.entityUid);
        if(!query.exec()) { if(error) *error=query.lastError().text(); return false; }
        if(resolution==QStringLiteral("accept_remote")) {
            query.prepare(QStringLiteral("UPDATE planning_calendar SET note=? WHERE day=?"));
            query.addBindValue(remoteNote); query.addBindValue(day);
            if(!query.exec()) { if(error) *error=query.lastError().text(); return false; }
        } else {
            MutationResult mutation; mutation.ok=true; mutation.entityType=QStringLiteral("planning_day");
            mutation.entityId=conflict.entityUid; mutation.operation=QStringLiteral("update");
            mutation.sourceModule=QStringLiteral("ConflictResolverService"); mutation.changedTables={QStringLiteral("planning_calendar")};
            mutation.auditBeforeJson=jsonCompact(QJsonObject{{QStringLiteral("note"),remoteNote}});
            if(!CloudSyncOutboxService::appendMutation(db,mutation,error)) return false;
            query.prepare(QStringLiteral("SELECT 1 FROM sync_outbox WHERE entity_type='planning_day' AND entity_uid=? AND status='pending' LIMIT 1"));
            query.addBindValue(conflict.entityUid);
            if(!query.exec() || !query.next()) { if(error) *error=QString::fromUtf8("Не створено чергу передачі обраної версії календаря."); return false; }
        }
        query.prepare(QStringLiteral("UPDATE sync_inbox SET status='applied',apply_error='' WHERE entity_type='planning_day' AND entity_uid=? AND status IN ('failed','conflict')"));
        query.addBindValue(conflict.entityUid);
        if(!query.exec()) { if(error) *error=query.lastError().text(); return false; }
        query.prepare(QStringLiteral("UPDATE sync_conflicts SET resolution=?,status='resolved',resolved_by=?,resolved_at=datetime('now') WHERE conflict_uid=? AND resolution='pending'"));
        query.addBindValue(resolution); query.addBindValue(actor); query.addBindValue(conflict.conflictUid);
        if(!query.exec() || query.numRowsAffected()!=1) { if(error) *error=QString::fromUtf8("Конфлікт уже змінено. Оновіть список."); return false; }
        return appendSyncAudit(db,QStringLiteral("sync_conflict_resolved"),conflict.entityType,conflict.entityUid,
            conflict.conflictUid,resolution,QStringLiteral("ok"),actor,conflict.conflictType,
            QJsonObject{{QStringLiteral("local_payload"),conflict.localPayload},{QStringLiteral("remote_payload"),conflict.remotePayload}});
    });
    if(!result.ok && errorText) *errorText=result.errorText;
    return result.ok;
}

} // namespace

namespace ConflictResolverService {

bool ensureSchema(QSqlDatabase db, QString* errorText)
{
    db = effectiveDb(db);
    if (!db.isOpen() && !db.open()) {
        if (errorText) *errorText = db.lastError().text();
        return false;
    }
    if (!execSql(db,
                 QStringLiteral("CREATE TABLE IF NOT EXISTS sync_conflicts ("
                                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                "conflict_uid TEXT NOT NULL UNIQUE,"
                                "entity_type TEXT NOT NULL DEFAULT '',"
                                "entity_uid TEXT NOT NULL DEFAULT '',"
                                "local_payload_json TEXT NOT NULL DEFAULT '{}',"
                                "remote_payload_json TEXT NOT NULL DEFAULT '{}',"
                                "conflict_type TEXT NOT NULL DEFAULT '',"
                                "resolution TEXT NOT NULL DEFAULT 'pending',"
                                "resolved_by TEXT NOT NULL DEFAULT '',"
                                "created_at TEXT NOT NULL DEFAULT (datetime('now')),"
                                "resolved_at TEXT NOT NULL DEFAULT ''"
                                ");"),
                 errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("sync_conflicts"), QStringLiteral("deferred_at"), QStringLiteral("deferred_at TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("sync_conflicts"), QStringLiteral("defer_reason"), QStringLiteral("defer_reason TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("sync_conflicts"), QStringLiteral("base_version"), QStringLiteral("base_version TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("sync_conflicts"), QStringLiteral("base_hash"), QStringLiteral("base_hash TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("sync_conflicts"), QStringLiteral("detected_at"), QStringLiteral("detected_at TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("sync_conflicts"), QStringLiteral("status"), QStringLiteral("status TEXT NOT NULL DEFAULT 'pending'"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("sync_conflicts"), QStringLiteral("resolver_policy"), QStringLiteral("resolver_policy TEXT NOT NULL DEFAULT 'manual_required'"), errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_sync_conflicts_resolution_created ON sync_conflicts(resolution, created_at);"), errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_sync_conflicts_entity ON sync_conflicts(entity_type, entity_uid);"), errorText)) return false;
    if (!execSql(db,
                 QStringLiteral("CREATE TABLE IF NOT EXISTS sync_audit_log ("
                                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                "created_at TEXT NOT NULL DEFAULT (datetime('now')),"
                                "action TEXT NOT NULL DEFAULT '',"
                                "entity_type TEXT NOT NULL DEFAULT '',"
                                "entity_uid TEXT NOT NULL DEFAULT '',"
                                "conflict_uid TEXT NOT NULL DEFAULT '',"
                                "resolution TEXT NOT NULL DEFAULT '',"
                                "result TEXT NOT NULL DEFAULT '',"
                                "actor TEXT NOT NULL DEFAULT '',"
                                "reason TEXT NOT NULL DEFAULT '',"
                                "details_json TEXT NOT NULL DEFAULT '{}'"
                                ");"),
                 errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_sync_audit_log_created ON sync_audit_log(created_at);"), errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_sync_audit_log_action ON sync_audit_log(action);"), errorText)) return false;
    return true;
}

bool recordConflict(QSqlDatabase db,
                    const QString& entityType,
                    const QString& entityUid,
                    const QJsonObject& localPayload,
                    const QJsonObject& remotePayload,
                    const QString& conflictType,
                    QString* conflictUid,
                    QString* errorText)
{
    db = effectiveDb(db);
    if (!ensureSchema(db, errorText)) return false;
    const QString normalizedType = conflictType.trimmed().isEmpty() ? QStringLiteral("version_mismatch") : conflictType.trimmed();

    QSqlQuery existing(db);
    existing.prepare(QStringLiteral("SELECT conflict_uid FROM sync_conflicts WHERE resolution='pending' AND entity_type=? AND entity_uid=? AND conflict_type=? LIMIT 1"));
    existing.addBindValue(entityType.trimmed());
    existing.addBindValue(entityUid.trimmed());
    existing.addBindValue(normalizedType);
    if (existing.exec() && existing.next()) {
        const QString uid = existing.value(0).toString();
        QSqlQuery update(db);
        update.prepare(QStringLiteral("UPDATE sync_conflicts SET local_payload_json=?, remote_payload_json=?, created_at=datetime('now'), detected_at=?, status='pending', resolver_policy=? WHERE conflict_uid=?"));
        update.addBindValue(jsonCompact(localPayload));
        update.addBindValue(jsonCompact(remotePayload));
        update.addBindValue(utcNow());
        update.addBindValue(resolverPolicyCode(passiveDefaultPolicy()));
        update.addBindValue(uid);
        if (!update.exec()) {
            if (errorText) *errorText = update.lastError().text();
            return false;
        }
        appendSyncAudit(db, QStringLiteral("sync_conflict_refreshed"), entityType, entityUid, uid, QStringLiteral("pending"), QStringLiteral("ok"), QString(), normalizedType, remotePayload);
        if (conflictUid) *conflictUid = uid;
        return true;
    }

    const QString uid = EntityUidService::generateUid(QStringLiteral("conflict"));
    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT INTO sync_conflicts(conflict_uid, entity_type, entity_uid, local_payload_json, remote_payload_json, conflict_type, resolution, created_at, detected_at, status, resolver_policy) "
                             "VALUES(?,?,?,?,?,?,'pending',datetime('now'),?,'pending',?)"));
    q.addBindValue(uid);
    q.addBindValue(entityType.trimmed());
    q.addBindValue(entityUid.trimmed());
    q.addBindValue(jsonCompact(localPayload));
    q.addBindValue(jsonCompact(remotePayload));
    q.addBindValue(normalizedType);
    q.addBindValue(utcNow());
    q.addBindValue(resolverPolicyCode(passiveDefaultPolicy()));
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    appendSyncAudit(db, QStringLiteral("sync_conflict_created"), entityType, entityUid, uid, QStringLiteral("pending"), QStringLiteral("ok"), QString(), normalizedType, remotePayload);
    if (conflictUid) *conflictUid = uid;
    return true;
}

int pendingCount(QSqlDatabase db, QString* errorText)
{
    db = effectiveDb(db);
    if (!ensureSchema(db, errorText)) return -1;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM sync_conflicts WHERE resolution='pending'"));
    if (!q.exec() || !q.next()) {
        if (errorText) *errorText = q.lastError().text();
        return -1;
    }
    return q.value(0).toInt();
}

bool resolveConflict(QSqlDatabase db,
                     const QString& conflictUid,
                     const QString& resolution,
                     const QString& resolvedBy,
                     QString* errorText)
{
    db = effectiveDb(db);
    if (!ensureSchema(db, errorText)) return false;
    const QString normalizedRaw = resolution.trimmed().isEmpty() ? QStringLiteral("deferred") : resolution.trimmed();
    const QString normalized = (normalizedRaw == QStringLiteral("defer") || normalizedRaw == QStringLiteral("postpone"))
        ? QStringLiteral("deferred")
        : normalizedRaw;

    ConflictRow row;
    if (!conflictByUid(db, conflictUid, &row, errorText)) return false;

    if (normalized == QStringLiteral("deferred")) {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("UPDATE sync_conflicts SET resolution='pending', status='pending', resolved_by=?, resolved_at='', deferred_at=?, defer_reason=? WHERE conflict_uid=?"));
        q.addBindValue(resolvedBy.trimmed());
        q.addBindValue(utcNow());
        q.addBindValue(QString::fromUtf8("Користувач відклав рішення конфлікту."));
        q.addBindValue(conflictUid.trimmed());
        if (!q.exec()) {
            if (errorText) *errorText = q.lastError().text();
            return false;
        }
        QJsonObject details;
        details.insert(QStringLiteral("conflict_type"), row.conflictType);
        appendSyncAudit(db, QStringLiteral("sync_conflict_deferred"), row.entityType, row.entityUid, conflictUid, normalized, QStringLiteral("ok"), resolvedBy, row.conflictType, details);
        return q.numRowsAffected() > 0;
    }

    if (normalized != QStringLiteral("keep_local")
        && normalized != QStringLiteral("accept_remote")
        && normalized != QStringLiteral("merge")) {
        const QString denial = QString::fromUtf8("Непідтримане рішення конфлікту: %1").arg(normalized);
        if (errorText) *errorText = denial;
        appendSyncAudit(db, QStringLiteral("sync_conflict_resolve_denied"), row.entityType, row.entityUid, conflictUid, normalized, QStringLiteral("failed"), resolvedBy, denial, row.remotePayload);
        return false;
    }

    if(row.entityType==QStringLiteral("planning_day"))
        return resolvePlanningConflict(db,row,normalized,resolvedBy,errorText);

    if (!applyResolutionPayload(db, row, normalized, errorText)) {
        appendSyncAudit(db, QStringLiteral("sync_conflict_resolve_failed"), row.entityType, row.entityUid, conflictUid, normalized, QStringLiteral("failed"), resolvedBy, errorText ? *errorText : QString(), row.remotePayload);
        return false;
    }

    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE sync_conflicts SET resolution=?, status='resolved', resolved_by=?, resolved_at=datetime('now') WHERE conflict_uid=?"));
    q.addBindValue(normalized);
    q.addBindValue(resolvedBy.trimmed());
    q.addBindValue(conflictUid.trimmed());
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        appendSyncAudit(db, QStringLiteral("sync_conflict_resolve_failed"), row.entityType, row.entityUid, conflictUid, normalized, QStringLiteral("failed"), resolvedBy, q.lastError().text(), row.remotePayload);
        return false;
    }
    QJsonObject details;
    details.insert(QStringLiteral("conflict_type"), row.conflictType);
    details.insert(QStringLiteral("local_payload"), row.localPayload);
    details.insert(QStringLiteral("remote_payload"), row.remotePayload);
    appendSyncAudit(db, QStringLiteral("sync_conflict_resolved"), row.entityType, row.entityUid, conflictUid, normalized, QStringLiteral("ok"), resolvedBy, row.conflictType, details);
    return q.numRowsAffected() > 0;
}

} // namespace ConflictResolverService
