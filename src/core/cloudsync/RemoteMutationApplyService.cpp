#include "core/cloudsync/RemoteMutationApplyService.h"
#include "core/services/orders/OrderClosureJournal.h"
#include "core/cloudsync/SyncEventPolicy.h"
#include "core/util/DateFormats.h"

#include "core/cloudsync/CloudSyncInboxService.h"
#include "core/cloudsync/ConflictResolverService.h"
#include "core/cloudsync/DeviceIdentityService.h"
#include "core/cloudsync/WorkOrderSyncApplyService.h"
#include "core/crm/CrmClientService.h"
#include "core/crm/CrmTaskService.h"
#include "core/crm/CrmTimelineService.h"
#include "core/crm/CrmVehicleService.h"
#include "core/crm/OnlineBookingService.h"
#include "core/services/MutationExecutor.h"
#include "infrastructure/db/sqlite/Db.h"
#include "runtime/RuntimeJobDispatcher.h"

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
#include <QThread>

namespace {

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

QJsonObject parseObject(const QString& json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    return doc.isObject() ? doc.object() : QJsonObject{};
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

QString compactObjectJson(const QJsonObject& object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QJsonObject mobileOrderState(const QJsonObject& data)
{
    static const QStringList stateFields = {
        QStringLiteral("workflow"), QStringLiteral("diagnostics"), QStringLiteral("recommendations"),
        QStringLiteral("diagnostic_uid"), QStringLiteral("diagnostic_kind"),
        QStringLiteral("created_part_request_uids"), QStringLiteral("created_service_uids"),
        QStringLiteral("customer_signature_file_uid"), QStringLiteral("customer_signature_at")
    };
    QJsonObject state;
    for (const QString& field : stateFields) {
        if (data.contains(field)) state.insert(field, data.value(field));
    }
    return state;
}

QJsonObject selectObject(const QJsonObject& event)
{
    QJsonObject payload = event.value(QStringLiteral("payload")).toObject();
    if (payload.isEmpty()) payload = event.value(QStringLiteral("data")).toObject();
    if (payload.isEmpty()) payload = event.value(QStringLiteral("row")).toObject();
    return payload.isEmpty() ? event : payload;
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

QJsonObject localRow(QSqlDatabase db, const QString& table, const QString& uidColumn, const QString& uid)
{
    QJsonObject obj;
    if (!safeIdentifier(table) || !safeIdentifier(uidColumn) || uid.trimmed().isEmpty()) return obj;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT * FROM %1 WHERE %2 IN (?,?) LIMIT 1").arg(table, uidColumn));
    q.addBindValue(uid.trimmed());
    q.addBindValue(table == QStringLiteral("planning_calendar") ? DateFormats::storage(uid) : uid.trimmed());
    if (!q.exec() || !q.next()) return obj;
    const QSqlRecord rec = q.record();
    for (int i = 0; i < rec.count(); ++i) obj.insert(rec.fieldName(i), q.value(i).toString());
    return DateFormats::protocolDates(obj);
}

bool rowHasNewerLocalVersion(const QJsonObject& local, const QJsonObject& remote)
{
    const QString localUpdated = local.value(QStringLiteral("updated_at")).toString();
    const QString remoteUpdated = remote.value(QStringLiteral("updated_at")).toString();
    if (localUpdated.isEmpty() || remoteUpdated.isEmpty()) return false;
    return QString::compare(localUpdated, remoteUpdated, Qt::CaseInsensitive) > 0;
}

QString normalizedJsonValue(const QJsonValue& value);

bool hasComparableUpdatedAt(const QJsonObject& local, const QJsonObject& remote)
{
    return !local.value(QStringLiteral("updated_at")).toString().trimmed().isEmpty()
        && !remote.value(QStringLiteral("updated_at")).toString().trimmed().isEmpty();
}

bool isMutableRemoteOperation(const QString& operation)
{
    const QString op = operation.trimmed().toLower();
    return op.isEmpty()
        || op.contains(QStringLiteral("update"))
        || op.contains(QStringLiteral("upsert"))
        || op.contains(QStringLiteral("create"))
        || op.contains(QStringLiteral("patch"));
}

bool hasAnyNonEmptyDifference(const QJsonObject& local, const QJsonObject& remote)
{
    static const QSet<QString> ignored = {
        QStringLiteral("id"), QStringLiteral("created_at"), QStringLiteral("updated_at"),
        QStringLiteral("received_at"), QStringLiteral("applied_at"), QStringLiteral("acked_at")
    };
    for (auto it = remote.constBegin(); it != remote.constEnd(); ++it) {
        const QString key = it.key();
        if (ignored.contains(key) || key.endsWith(QStringLiteral("_json_hash"))) continue;
        const QString ls = normalizedJsonValue(local.value(key));
        const QString rs = normalizedJsonValue(it.value());
        if (!ls.isEmpty() && !rs.isEmpty() && ls != rs) return true;
    }
    return false;
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

QStringList differingNonEmptyFields(const QJsonObject& local, const QJsonObject& remote, const QStringList& fields)
{
    QStringList out;
    for (const QString& field : fields) {
        const QString ls = normalizedJsonValue(local.value(field));
        const QString rs = normalizedJsonValue(remote.value(field));
        if (!ls.isEmpty() && !rs.isEmpty() && ls != rs) out.append(field);
    }
    return out;
}

bool hasCriticalDifference(const QJsonObject& local, const QJsonObject& remote, const QStringList& fields)
{
    return !differingNonEmptyFields(local, remote, fields).isEmpty();
}

QJsonObject mergeEmptyLocalFields(const QJsonObject& local, const QJsonObject& remote)
{
    QJsonObject merged = local;
    for (auto it = remote.constBegin(); it != remote.constEnd(); ++it) {
        const QString key = it.key();
        if (key == QStringLiteral("id")) continue;
        const QJsonValue localValue = merged.value(key);
        const bool localEmpty = localValue.isUndefined()
            || localValue.isNull()
            || (localValue.isString() && localValue.toString().trimmed().isEmpty());
        if (localEmpty || key.endsWith(QStringLiteral("_at"))) merged.insert(key, it.value());
    }
    return merged;
}

QString partConflictType(const QJsonObject& local, const QJsonObject& remote)
{
    const QStringList monitored = {
        QStringLiteral("price"), QStringLiteral("qty"), QStringLiteral("quantity"),
        QStringLiteral("paid"), QStringLiteral("is_paid"), QStringLiteral("installed"),
        QStringLiteral("is_installed"), QStringLiteral("installed_at"), QStringLiteral("client_price"),
        QStringLiteral("supplier_price"), QStringLiteral("total"), QStringLiteral("sum")
    };
    const QStringList diff = differingNonEmptyFields(local, remote, monitored);
    return diff.isEmpty()
        ? QStringLiteral("version_mismatch")
        : QStringLiteral("part_sensitive_fields_changed:%1").arg(diff.join(QLatin1Char('|')));
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
    const QStringList intakeFields = {QStringLiteral("order_uid"), QStringLiteral("client_uid"), QStringLiteral("vehicle_uid"), QStringLiteral("mileage"), QStringLiteral("fuel_level"), QStringLiteral("exterior_state"), QStringLiteral("interior_state"), QStringLiteral("checklist_json"), QStringLiteral("customer_signature_path"), QStringLiteral("pdf_path"), QStringLiteral("sync_status"), QStringLiteral("created_at"), QStringLiteral("updated_at")};
    const QStringList intakePhotoFields = {QStringLiteral("intake_uid"), QStringLiteral("order_uid"), QStringLiteral("local_path"), QStringLiteral("server_url"), QStringLiteral("photo_type"), QStringLiteral("created_at")};
    const QStringList approvalFields = {QStringLiteral("order_uid"), QStringLiteral("client_uid"), QStringLiteral("vehicle_uid"), QStringLiteral("estimate_json"), QStringLiteral("status"), QStringLiteral("decision_comment"), QStringLiteral("approved_at"), QStringLiteral("declined_at"), QStringLiteral("created_at"), QStringLiteral("updated_at")};
    const QStringList supplierQuoteFields = {QStringLiteral("supplier_code"), QStringLiteral("part_number"), QStringLiteral("brand"), QStringLiteral("name"), QStringLiteral("price"), QStringLiteral("currency"), QStringLiteral("available_qty"), QStringLiteral("status"), QStringLiteral("raw_payload_json"), QStringLiteral("created_at"), QStringLiteral("updated_at")};
    const QStringList communicationFields = {QStringLiteral("client_uid"), QStringLiteral("order_uid"), QStringLiteral("channel"), QStringLiteral("recipient"), QStringLiteral("subject"), QStringLiteral("body"), QStringLiteral("status"), QStringLiteral("provider"), QStringLiteral("last_error"), QStringLiteral("created_at"), QStringLiteral("sent_at")};
    const QStringList dashboardFields = {QStringLiteral("period_start"), QStringLiteral("period_end"), QStringLiteral("metrics_json"), QStringLiteral("created_at")};
    const QStringList auditFields = {QStringLiteral("actor"), QStringLiteral("actor_user_uid"), QStringLiteral("actor_login"), QStringLiteral("actor_role"), QStringLiteral("action"), QStringLiteral("entity_type"), QStringLiteral("entity_uid"), QStringLiteral("before_json"), QStringLiteral("after_json"), QStringLiteral("details_json"), QStringLiteral("source"), QStringLiteral("created_at")};
    const QStringList backupFields = {QStringLiteral("device_uid"), QStringLiteral("branch_uid"), QStringLiteral("scope"), QStringLiteral("status"), QStringLiteral("db_path"), QStringLiteral("backup_path"), QStringLiteral("size"), QStringLiteral("sha256"), QStringLiteral("storage_path"), QStringLiteral("checksum"), QStringLiteral("last_error"), QStringLiteral("created_at"), QStringLiteral("completed_at")};

    if (type == QStringLiteral("crm_client")) return {type, QStringLiteral("crm_clients"), QStringLiteral("client_uid"), {QStringLiteral("crm_clients")}, clientFields, {QStringLiteral("full_name"), QStringLiteral("first_name"), QStringLiteral("last_name"), QStringLiteral("phone"), QStringLiteral("phone_alt"), QStringLiteral("email")}, {QStringLiteral("discount_percent"), QStringLiteral("loyalty_level"), QStringLiteral("status")}, false, false};
    if (type == QStringLiteral("crm_vehicle")) return {type, QStringLiteral("crm_vehicles"), QStringLiteral("vehicle_uid"), {QStringLiteral("crm_vehicles")}, vehicleFields, {QStringLiteral("plate"), QStringLiteral("vin"), QStringLiteral("brand"), QStringLiteral("model"), QStringLiteral("year")}, {QStringLiteral("mileage"), QStringLiteral("color"), QStringLiteral("is_primary")}, false, false};
    if (type == QStringLiteral("work_order")) return {type, QStringLiteral("work_orders"), QStringLiteral("order_uid"), {QStringLiteral("work_orders")}, orderFields, {}, {}, false, true};
    if (type == QStringLiteral("work_order_service")) return {type, QStringLiteral("work_order_services"), QStringLiteral("service_uid"), {QStringLiteral("work_order_services")}, serviceFields, {}, {}, false, true};
    if (type == QStringLiteral("work_order_part")) return {type, QStringLiteral("work_order_parts"), QStringLiteral("part_uid"), {QStringLiteral("work_order_parts")}, partFields, {}, {QStringLiteral("price"), QStringLiteral("qty"), QStringLiteral("quantity"), QStringLiteral("paid"), QStringLiteral("is_paid"), QStringLiteral("installed"), QStringLiteral("is_installed"), QStringLiteral("installed_at"), QStringLiteral("client_price"), QStringLiteral("supplier_price"), QStringLiteral("total"), QStringLiteral("sum"), QStringLiteral("amount"), QStringLiteral("purchase_price"), QStringLiteral("purchase_amount")}, false, true};
    if (type == QStringLiteral("planning_day")) return {type, QStringLiteral("planning_calendar"), QStringLiteral("day"), {QStringLiteral("planning_calendar")}, {QStringLiteral("note")}, {}, {}, false, false};
    if (type == QStringLiteral("appointment")) return {type, QStringLiteral("online_booking_requests"), QStringLiteral("appointment_uid"), {QStringLiteral("online_booking_requests")}, appointmentFields, {}, {}, false, false};
    if (type == QStringLiteral("crm_event")) return {type, QStringLiteral("crm_client_events"), QStringLiteral("event_uid"), {QStringLiteral("crm_client_events")}, crmEventFields, {}, {}, true, false};
    if (type == QStringLiteral("crm_task")) return {type, QStringLiteral("crm_client_tasks"), QStringLiteral("task_uid"), {QStringLiteral("crm_client_tasks")}, crmTaskFields, {}, {}, false, false};
    if (type == QStringLiteral("file_attachment")) return {type, QStringLiteral("file_attachments"), QStringLiteral("file_uid"), {QStringLiteral("file_attachments")}, fileFields, {}, {}, false, false};
    if (type == QStringLiteral("vehicle_intake")) return {type, QStringLiteral("vehicle_intake"), QStringLiteral("intake_uid"), {QStringLiteral("vehicle_intake")}, intakeFields, {}, {}, false, false};
    if (type == QStringLiteral("vehicle_intake_photo")) return {type, QStringLiteral("vehicle_intake_photos"), QStringLiteral("photo_uid"), {QStringLiteral("vehicle_intake_photos")}, intakePhotoFields, {}, {}, false, false};
    if (type == QStringLiteral("estimate_approval")) return {type, QStringLiteral("estimate_approvals"), QStringLiteral("approval_uid"), {QStringLiteral("estimate_approvals")}, approvalFields, {}, {QStringLiteral("status"), QStringLiteral("estimate_json")}, false, false};
    if (type == QStringLiteral("supplier_quote")) return {type, QStringLiteral("supplier_price_quotes"), QStringLiteral("quote_uid"), {QStringLiteral("supplier_price_quotes")}, supplierQuoteFields, {}, {}, false, false};
    if (type == QStringLiteral("communication_message")) return {type, QStringLiteral("communication_messages"), QStringLiteral("message_uid"), {QStringLiteral("communication_messages")}, communicationFields, {}, {}, false, false};
    if (type == QStringLiteral("owner_dashboard_snapshot")) return {type, QStringLiteral("owner_dashboard_snapshots"), QStringLiteral("snapshot_uid"), {QStringLiteral("owner_dashboard_snapshots")}, dashboardFields, {}, {}, false, false};
    if (type == QStringLiteral("crm_audit_event")) return {type, QStringLiteral("crm_audit_events"), QStringLiteral("audit_uid"), {QStringLiteral("crm_audit_events")}, auditFields, {}, {}, true, false};
    if (type == QStringLiteral("server_backup_job")) return {type, QStringLiteral("server_backup_jobs"), QStringLiteral("backup_uid"), {QStringLiteral("server_backup_jobs")}, backupFields, {}, {}, false, false};
    return {};
}

QString remoteUidForEvent(const CloudSyncInboxService::InboxEvent& ev, const QJsonObject& data, const EntityMap& map)
{
    const QString fromPayload = textValue(data, map.uidColumn);
    if (!fromPayload.isEmpty()) return fromPayload;
    return ev.entityUid.trimmed();
}

bool upsertAllowedFieldsByUid(QSqlDatabase db, const EntityMap& map, const QString& uid, const QJsonObject& data, QString* errorText)
{
    if (!safeIdentifier(map.table) || !safeIdentifier(map.uidColumn)) {
        if (errorText) *errorText = QString::fromUtf8("Небезпечна назва таблиці/UID у sync apply.");
        return false;
    }
    if (uid.trimmed().isEmpty()) {
        if (errorText) *errorText = QString::fromUtf8("Серверна подія %1 без UID.").arg(map.entityType);
        return false;
    }
    if (!tableExists(db, map.table)) {
        if (errorText) *errorText = QString::fromUtf8("Локальна таблиця для sync відсутня: %1.").arg(map.table);
        return false;
    }
    const QSet<QString> columns = tableColumns(db, map.table);
    if (!columns.contains(map.uidColumn)) {
        if (errorText) *errorText = QString::fromUtf8("У таблиці %1 відсутня UID-колонка %2.").arg(map.table, map.uidColumn);
        return false;
    }

    const bool planningDay = map.table == QStringLiteral("planning_calendar");
    QString storageUid = uid.trimmed();
    if (planningDay) {
        const auto existing = localRow(db, map.table, map.uidColumn, uid);
        storageUid = existing.isEmpty() ? DateFormats::storage(uid) : existing.value(map.uidColumn).toString();
    }
    QStringList insertColumns{map.uidColumn};
    QList<QVariant> values{storageUid};
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
        values.append(textValue(data, QStringLiteral("updated_at")).isEmpty()
            ? QVariant(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs))
            : QVariant(textValue(data, QStringLiteral("updated_at"))));
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
        update.addBindValue(storageUid);
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



QVariantMap jsonObjectToVariantMap(const QJsonObject& data)
{
    QVariantMap map;
    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        map.insert(it.key(), jsonVariant(it.value()));
    }
    return map;
}

bool applyRemoteCrmClient(QSqlDatabase db, const QString& uid, const QJsonObject& data, QString* errorText)
{
    QVariantMap fields = jsonObjectToVariantMap(data);
    fields.insert(QStringLiteral("client_uid"), uid);
    return CrmClientService::upsertClientDirect(db, uid, fields, errorText);
}

bool applyRemoteCrmVehicle(QSqlDatabase db, const QString& uid, const QJsonObject& data, QString* errorText)
{
    QVariantMap fields = jsonObjectToVariantMap(data);
    fields.insert(QStringLiteral("vehicle_uid"), uid);
    return CrmVehicleService::upsertVehicleDirect(db, uid, fields, errorText);
}

bool applyRemoteCrmTask(QSqlDatabase db, const QString& uid, const QJsonObject& data, QString* errorText)
{
    QVariantMap fields = jsonObjectToVariantMap(data);
    fields.insert(QStringLiteral("task_uid"), uid);
    return CrmTaskService::upsertTaskDirect(db, uid, fields, errorText);
}

bool applyRemoteCrmEvent(QSqlDatabase db, const QString& uid, const QJsonObject& data, QString* errorText)
{
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

bool applyRemoteAppointment(QSqlDatabase db, const QString& uid, const QJsonObject& data, QString* errorText)
{
    QVariantMap fields = jsonObjectToVariantMap(data);
    fields.insert(QStringLiteral("appointment_uid"), uid);
    return OnlineBookingService::upsertRequest(db, fields, errorText);
}

bool applyRemoteWorkOrder(QSqlDatabase db, const QString& uid, const QJsonObject& data, QString* errorText)
{
    QVariantMap fields = jsonObjectToVariantMap(data);
    fields.insert(QStringLiteral("order_uid"), uid);
    const QJsonObject state = mobileOrderState(data);
    if (!state.isEmpty()) fields.insert(QStringLiteral("mobile_state_json"), compactObjectJson(state));
    return WorkOrderSyncApplyService::upsertWorkOrderDirect(db, uid, fields, errorText);
}

bool applyRemoteWorkOrderService(QSqlDatabase db, const QString& uid, const QJsonObject& data, QString* errorText)
{
    QVariantMap fields = jsonObjectToVariantMap(data);
    fields.insert(QStringLiteral("service_uid"), uid);
    fields.insert(QStringLiteral("metadata_json"), compactObjectJson(data));
    return WorkOrderSyncApplyService::upsertServiceDirect(db, uid, fields, errorText);
}

bool applyRemoteWorkOrderPart(QSqlDatabase db, const QString& uid, const QJsonObject& data, QString* errorText)
{
    QVariantMap fields = jsonObjectToVariantMap(data);
    fields.insert(QStringLiteral("part_uid"), uid);
    fields.insert(QStringLiteral("metadata_json"), compactObjectJson(data));
    return WorkOrderSyncApplyService::upsertPartDirect(db, uid, fields, errorText);
}

bool applyRemoteMappedEntity(QSqlDatabase db, const EntityMap& map, const QString& uid, const QJsonObject& data, QString* errorText)
{
    // Last-resort domain boundary for entities that do not yet have a richer
    // desktop service in this codebase. It is still constrained by EntityMap:
    // explicit table + UID column + writable fields + conflict policy.
    return upsertAllowedFieldsByUid(db, map, uid, data, errorText);
}

bool applyRemoteDomainMutation(QSqlDatabase db,
                               const EntityMap& map,
                               const QString& uid,
                               const QJsonObject& data,
                               const QString& operation,
                               QString* errorText)
{
    // Domain boundary for server events: every supported entity has an explicit
    // table, UID column, writable-field allowlist and conflict policy. The SQL
    // helper below is intentionally limited by EntityMap::writableFields, so a
    // server payload cannot blindly write arbitrary local columns.
    const QString op = operation.trimmed().toLower();
    if (op.contains(QStringLiteral("delete"))) {
        if (errorText) *errorText = QString::fromUtf8("Видалення серверних сутностей застосовується тільки через conflict resolver/manual review.");
        return false;
    }

    if (map.entityType == QStringLiteral("crm_event")) {
        // CRM timeline is append-only and goes through CrmTimelineService, not arbitrary SQL.
        if (!localRow(db, map.table, map.uidColumn, uid).isEmpty()) return true;
        return applyRemoteCrmEvent(db, uid, data, errorText);
    }

    if (map.entityType == QStringLiteral("crm_task")) {
        // Tasks use last-write-wins after conflict detection in applyKnownEntity().
        return applyRemoteCrmTask(db, uid, data, errorText);
    }

    if (map.entityType == QStringLiteral("crm_client")) {
        // CRM master data uses the CRM service boundary and field allowlist.
        return applyRemoteCrmClient(db, uid, data, errorText);
    }

    if (map.entityType == QStringLiteral("crm_vehicle")) {
        return applyRemoteCrmVehicle(db, uid, data, errorText);
    }

    if (map.entityType == QStringLiteral("appointment")) {
        // Online booking rows are handled through OnlineBookingService, so CRM links
        // and booking dedupe stay in one domain path.
        return applyRemoteAppointment(db, uid, data, errorText);
    }

    if (map.entityType == QStringLiteral("work_order")) {
        return applyRemoteWorkOrder(db, uid, data, errorText);
    }

    if (map.entityType == QStringLiteral("work_order_service")) {
        return applyRemoteWorkOrderService(db, uid, data, errorText);
    }

    if (map.entityType == QStringLiteral("work_order_part")) {
        return applyRemoteWorkOrderPart(db, uid, data, errorText);
    }

    if (map.entityType == QStringLiteral("file_attachment")
        || map.entityType == QStringLiteral("vehicle_intake")
        || map.entityType == QStringLiteral("vehicle_intake_photo")
        || map.entityType == QStringLiteral("estimate_approval")
        || map.entityType == QStringLiteral("supplier_quote")
        || map.entityType == QStringLiteral("communication_message")
        || map.entityType == QStringLiteral("owner_dashboard_snapshot")
        || map.entityType == QStringLiteral("crm_audit_event")
        || map.entityType == QStringLiteral("server_backup_job")
        || map.entityType == QStringLiteral("planning_day")) {
        return applyRemoteMappedEntity(db, map, uid, data, errorText);
    }

    if (errorText) *errorText = QString::fromUtf8("Непідтриманий доменний apply типу: %1").arg(map.entityType);
    return false;
}


bool applyRelatedRowsForRelation(QSqlDatabase db,
                                 const QString& parentEntityType,
                                 const QString& parentUid,
                                 const QString& relationName,
                                 const QString& childEntityType,
                                 const QString& parentField,
                                 const QJsonObject& related,
                                 QString* errorText)
{
    const QJsonArray rows = related.value(relationName).toArray();
    if (rows.isEmpty()) return true;

    const EntityMap childMap = entityMapForType(childEntityType);
    if (childMap.table.isEmpty() || childMap.uidColumn.isEmpty()) {
        if (errorText) *errorText = QString::fromUtf8("Непідтриманий related sync тип: %1").arg(childEntityType);
        return false;
    }

    for (const QJsonValue& value : rows) {
        if (!value.isObject()) continue;
        QJsonObject childData = value.toObject();
        if (!parentUid.trimmed().isEmpty() && childData.value(parentField).toString().trimmed().isEmpty()) {
            childData.insert(parentField, parentUid.trimmed());
        }
        if (parentEntityType == QStringLiteral("work_order") && childEntityType == QStringLiteral("file_attachment")) {
            if (childData.value(QStringLiteral("entity_type")).toString().trimmed().isEmpty()) {
                childData.insert(QStringLiteral("entity_type"), QStringLiteral("work_order"));
            }
            if (childData.value(QStringLiteral("entity_uid")).toString().trimmed().isEmpty()) {
                childData.insert(QStringLiteral("entity_uid"), parentUid.trimmed());
            }
        }

        const QString childUid = textValue(childData, childMap.uidColumn);
        if (childUid.isEmpty()) {
            if (errorText) *errorText = QString::fromUtf8("Related sync рядок %1 без UID-колонки %2.").arg(childEntityType, childMap.uidColumn);
            return false;
        }

        const QJsonObject local = localRow(db, childMap.table, childMap.uidColumn, childUid);
        if (!local.isEmpty() && childEntityType == QStringLiteral("work_order_part")) {
            const QStringList monitored = {
                QStringLiteral("price"), QStringLiteral("qty"), QStringLiteral("quantity"),
                QStringLiteral("paid"), QStringLiteral("is_paid"), QStringLiteral("installed"),
                QStringLiteral("is_installed"), QStringLiteral("installed_at"), QStringLiteral("amount"),
                QStringLiteral("purchase_price"), QStringLiteral("purchase_amount")
            };
            if (hasCriticalDifference(local, childData, monitored)) {
                QString conflictUid;
                if (!ConflictResolverService::recordConflict(db, childEntityType, childUid, local, childData,
                        QStringLiteral("related_part_sensitive_fields_changed"), &conflictUid, errorText)) {
                    return false;
                }
                continue;
            }
        }

        if (!applyRemoteDomainMutation(db, childMap, childUid, childData, QStringLiteral("upsert"), errorText)) {
            return false;
        }
    }
    return true;
}

bool applyRelatedPayload(QSqlDatabase db, const QString& parentEntityType, const QString& parentUid, const QJsonObject& remotePayload, QString* errorText)
{
    QJsonObject related = remotePayload.value(QStringLiteral("related")).toObject();
    if (related.isEmpty()) {
        const QJsonObject payload = remotePayload.value(QStringLiteral("payload")).toObject();
        related = payload.value(QStringLiteral("related")).toObject();
    }
    if (related.isEmpty()) return true;

    if (parentEntityType == QStringLiteral("work_order")) {
        return applyRelatedRowsForRelation(db, parentEntityType, parentUid, QStringLiteral("services"), QStringLiteral("work_order_service"), QStringLiteral("order_uid"), related, errorText)
            && applyRelatedRowsForRelation(db, parentEntityType, parentUid, QStringLiteral("parts"), QStringLiteral("work_order_part"), QStringLiteral("order_uid"), related, errorText)
            && applyRelatedRowsForRelation(db, parentEntityType, parentUid, QStringLiteral("files"), QStringLiteral("file_attachment"), QStringLiteral("entity_uid"), related, errorText);
    }

    if (parentEntityType == QStringLiteral("crm_client")) {
        return applyRelatedRowsForRelation(db, parentEntityType, parentUid, QStringLiteral("vehicles"), QStringLiteral("crm_vehicle"), QStringLiteral("client_uid"), related, errorText)
            && applyRelatedRowsForRelation(db, parentEntityType, parentUid, QStringLiteral("tasks"), QStringLiteral("crm_task"), QStringLiteral("client_uid"), related, errorText)
            && applyRelatedRowsForRelation(db, parentEntityType, parentUid, QStringLiteral("events"), QStringLiteral("crm_event"), QStringLiteral("client_uid"), related, errorText);
    }

    return true;
}

bool applyKnownEntity(QSqlDatabase db, const CloudSyncInboxService::InboxEvent& ev, const QJsonObject& remotePayload, QString* errorText)
{
    if(ev.entityType==QStringLiteral("order_closure")) {
        if(ev.sourceDeviceUid!=QStringLiteral("order-closure-authority")) {
            if(errorText)*errorText=QStringLiteral("Invalid closure authority");return false;
        }
        return OrderClosureJournal::apply(db,selectObject(remotePayload),errorText);
    }
    const EntityMap map = entityMapForType(ev.entityType);
    if (map.table.isEmpty()) {
        QString conflictUid;
        ConflictResolverService::recordConflict(db, ev.entityType, ev.entityUid, QJsonObject{}, remotePayload, QStringLiteral("unsupported_entity_type"), &conflictUid, errorText);
        if (errorText) *errorText = QStringLiteral("conflict:unsupported_entity_type");
        return false;
    }

    QJsonObject data = selectObject(remotePayload);
    const QString uid = remoteUidForEvent(ev, data, map);
    const QJsonObject local = localRow(db, map.table, map.uidColumn, uid);
    const QString op = ev.operation.trimmed().toLower();

    if (ev.entityType == QStringLiteral("work_order") && local.isEmpty()
        && !op.contains(QStringLiteral("delete")) && data.value(QStringLiteral("number")).toVariant().toInt() > 0) {
        QSqlQuery existing(db);
        existing.prepare(QStringLiteral("SELECT order_uid FROM work_orders WHERE number=? LIMIT 1"));
        existing.addBindValue(data.value(QStringLiteral("number")).toVariant().toInt());
        if (!existing.exec()) { if (errorText) *errorText = existing.lastError().text(); return false; }
        if (existing.next() && existing.value(0).toString() != uid) {
            const auto other = localRow(db, map.table, map.uidColumn, existing.value(0).toString());
            QString conflictUid;
            if (!ConflictResolverService::recordConflict(db, ev.entityType, uid, other, remotePayload,
                    QStringLiteral("order_number_uid_collision"), &conflictUid, errorText)) return false;
            if (errorText) *errorText = QStringLiteral("conflict:order_number_uid_collision");
            return false;
        }
    }

    if (ev.entityType == QStringLiteral("planning_day") && !local.isEmpty()) {
        QSqlQuery pending(db);
        pending.prepare(QStringLiteral("SELECT status,last_error FROM sync_outbox WHERE entity_type='planning_day' AND entity_uid=? AND status IN ('pending','failed','sending') ORDER BY id"));
        pending.addBindValue(uid);
        if (!pending.exec()) { if(errorText) *errorText=pending.lastError().text(); return false; }
        bool waiting=false, rejected=false;
        while(pending.next()) {
            waiting=true;
            rejected |= pending.value(0).toString()==QStringLiteral("failed") && pending.value(1).toString().contains(QStringLiteral("409"));
        }
        if (waiting && local.value(QStringLiteral("note")) != data.value(QStringLiteral("note"))) {
            if(rejected) {
                QString conflictUid;
                if(!ConflictResolverService::recordConflict(db,ev.entityType,uid,local,remotePayload,
                        QStringLiteral("planning_local_push_rejected"),&conflictUid,errorText)) return false;
                if(errorText) *errorText=QStringLiteral("conflict:planning_local_push_rejected");
            } else if (errorText) *errorText = QString::fromUtf8("Очікується передавання локальних змін календаря.");
            return false;
        }
    }

    if (op.contains(QStringLiteral("delete"))) {
        if (local.isEmpty()) return true;
        QString conflictUid;
        const QString type = local.isEmpty() ? QStringLiteral("remote_deleted_missing_local") : QStringLiteral("remote_deleted_local_changed");
        ConflictResolverService::recordConflict(db, ev.entityType, uid, local, remotePayload, type, &conflictUid, errorText);
        CloudSyncInboxService::markStatus(db, ev.eventUid, QStringLiteral("conflict"), type, errorText);
        if (errorText) *errorText = QStringLiteral("conflict:") + type;
        return false;
    }

    if (!local.isEmpty()) {
        // Повніший conflict gate для етапу 9: не покладаємося тільки на updated_at.
        // Якщо timestamp відсутній або однаковий, але серверна подія змінює вже заповнені локальні поля,
        // застосовуємо політики ТЗ замість silent overwrite.
        const bool localNewer = rowHasNewerLocalVersion(local, data);
        const bool ambiguousTimeline = !hasComparableUpdatedAt(local, data)
            || local.value(QStringLiteral("updated_at")).toString().trimmed()
                == data.value(QStringLiteral("updated_at")).toString().trimmed();
        const bool remoteMutatesExisting = isMutableRemoteOperation(ev.operation);
        const bool anyBusinessDiff = hasAnyNonEmptyDifference(local, data);
        // Server-validated mobile lifecycle events are authoritative. They are
        // not competing desktop edits, so a stale local status must not turn a
        // completed service, diagnosis, or installed part into a conflict.
        const bool mobileLifecycleEvent = ev.sourceDeviceUid.trimmed().compare(
            QStringLiteral("mobile-panel"), Qt::CaseInsensitive) == 0;
        const bool needsPolicyReview = !mobileLifecycleEvent
            && (localNewer || (ambiguousTimeline && remoteMutatesExisting && anyBusinessDiff));

        // CRM notes/timeline are append-only: duplicate event_uid is considered already applied.
        if (ev.entityType == QStringLiteral("crm_event")) {
            return true;
        }

        // CRM tasks: last-write-wins. If local is newer, keep it; otherwise remote update may proceed.
        if (ev.entityType == QStringLiteral("crm_task") && localNewer) {
            return true;
        }

        QString conflictType;
        if (ev.entityType == QStringLiteral("work_order") && needsPolicyReview) {
            conflictType = QStringLiteral("same_field_changed_local_order_priority");
        } else if (ev.entityType == QStringLiteral("work_order_service") && needsPolicyReview) {
            conflictType = QStringLiteral("same_field_changed_service_local_priority");
        } else if (ev.entityType == QStringLiteral("work_order_part") && !mobileLifecycleEvent) {
            const QStringList monitored = {
                QStringLiteral("price"), QStringLiteral("qty"), QStringLiteral("quantity"),
                QStringLiteral("paid"), QStringLiteral("is_paid"), QStringLiteral("installed"),
                QStringLiteral("is_installed"), QStringLiteral("installed_at"), QStringLiteral("client_price"),
                QStringLiteral("supplier_price"), QStringLiteral("total"), QStringLiteral("sum")
            };
            if (hasCriticalDifference(local, data, monitored) || needsPolicyReview) {
                conflictType = partConflictType(local, data);
            }
        } else if (ev.entityType == QStringLiteral("crm_client")) {
            const QStringList identityFields = {
                QStringLiteral("full_name"), QStringLiteral("first_name"), QStringLiteral("last_name"),
                QStringLiteral("phone"), QStringLiteral("phone_alt"), QStringLiteral("email"),
                QStringLiteral("discount_percent"), QStringLiteral("loyalty_level"), QStringLiteral("status")
            };
            if (hasCriticalDifference(local, data, identityFields)) {
                conflictType = QStringLiteral("same_field_changed_client_identity");
            } else if (needsPolicyReview) {
                data = mergeEmptyLocalFields(local, data);
            }
        } else if (ev.entityType == QStringLiteral("crm_vehicle")) {
            const QStringList identityFields = {
                QStringLiteral("plate"), QStringLiteral("vin"), QStringLiteral("brand"),
                QStringLiteral("model"), QStringLiteral("year")
            };
            if (hasCriticalDifference(local, data, identityFields)) {
                conflictType = QStringLiteral("same_field_changed_vehicle_identity");
            } else if (needsPolicyReview) {
                data = mergeEmptyLocalFields(local, data);
            }
        }

        if (!conflictType.isEmpty()) {
            QString conflictUid;
            if (!ConflictResolverService::recordConflict(db, ev.entityType, uid, local, remotePayload, conflictType, &conflictUid, errorText)) return false;
            CloudSyncInboxService::markStatus(db, ev.eventUid, QStringLiteral("conflict"), conflictType, errorText);
            if (errorText) *errorText = QStringLiteral("conflict:") + conflictType;
            return false;
        }
    }

    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("RemoteMutationApplyService");
    request.entityType = ev.entityType;
    request.entityId = uid;
    request.operation = ev.operation.isEmpty() ? QStringLiteral("remote_upsert") : QStringLiteral("remote_%1").arg(ev.operation);
    request.description = QString::fromUtf8("Застосування серверної sync-події");
    request.changedTables = map.changedTables;
    request.beginErrorFallback = QString::fromUtf8("Не вдалося відкрити транзакцію застосування серверної події.");
    request.commitErrorFallback = QString::fromUtf8("Не вдалося завершити застосування серверної події.");
    request.systemMaintenance = true;

    const auto result = MutationExecutor::execute(db, request, [&](QSqlDatabase& txDb, QString* txError) {
        if (!applyRemoteDomainMutation(txDb, map, uid, data, ev.operation, txError)) {
            return false;
        }
        if (!applyRelatedPayload(txDb, ev.entityType, uid, remotePayload, txError)) return false;
        // Persist the receipt with the domain change, not after its commit.
        return CloudSyncInboxService::markStatus(txDb, ev.eventUid, QStringLiteral("applied"), QString(), txError);
    });
    if (!result.ok) {
        if (errorText) *errorText = result.errorText;
        return false;
    }
    return true;
}

} // namespace

namespace RemoteMutationApplyService {

ApplyStats applyPendingEvents(QSqlDatabase db, int limit, QString* errorText)
{
    ApplyStats stats;
    const auto cancelled = [&] {
        if (!QThread::currentThread()->isInterruptionRequested()
            && !RuntimeJob::RuntimeJobDispatcher::instance().isShutdownRequested()) return false;
        if (errorText) *errorText = QString::fromUtf8("Обробку змін зупинено. Незастосовані події залишено в черзі.");
        return true;
    };
    if (cancelled()) return stats;
    db = effectiveDb(db);
    QString localError;
    if (!CloudSyncInboxService::recoverInterruptedApplying(db, 900, &localError)) {
        if (errorText) *errorText = localError;
        return stats;
    }
    const auto events = CloudSyncInboxService::pendingEvents(db, limit, &localError);
    if (!localError.isEmpty()) {
        if (errorText) *errorText = localError;
        return stats;
    }

    const QString ownDeviceUid = DeviceIdentityService::deviceUid(db);
    for (const auto& ev : events) {
        // Cancellation is checked between events, never inside an atomic mutation.
        if (cancelled()) break;
        if (SyncEventPolicy::isLocalMetadata(ev.entityType, parseObject(ev.payloadJson))) {
            if (CloudSyncInboxService::markStatus(db, ev.eventUid, QStringLiteral("ignored_metadata"),
                    QStringLiteral("Local audit envelope; no business payload"), &localError)) ++stats.applied;
            else { ++stats.failed; if (errorText) *errorText = localError; }
            continue;
        }
        if (!ownDeviceUid.isEmpty() && ev.sourceDeviceUid == ownDeviceUid) {
            CloudSyncInboxService::markStatus(db, ev.eventUid, QStringLiteral("ignored_self_event"));
            ++stats.ignoredSelf;
            continue;
        }

        if (!CloudSyncInboxService::markStatus(db, ev.eventUid, QStringLiteral("applying"), QString(), &localError)) {
            if (errorText) *errorText = localError;
            ++stats.failed;
            continue;
        }

        QString applyError;
        const QJsonObject payload = parseObject(ev.payloadJson);
        const int conflictsBefore = ConflictResolverService::pendingCount(db);
        if (applyKnownEntity(db, ev, payload, &applyError)) {
            if (CloudSyncInboxService::markStatus(db, ev.eventUid, QStringLiteral("applied"), QString(), &applyError)) {
                ++stats.applied;
            } else {
                ++stats.failed;
                if (errorText) *errorText = applyError;
            }
        } else {
            const int conflictsAfter = ConflictResolverService::pendingCount(db);
            if (conflictsAfter > conflictsBefore || applyError.startsWith(QStringLiteral("conflict:"))) {
                CloudSyncInboxService::markStatus(db, ev.eventUid, QStringLiteral("conflict"), applyError);
                ++stats.conflicts;
            } else {
                CloudSyncInboxService::markStatus(db, ev.eventUid, QStringLiteral("failed"), applyError);
                ++stats.failed;
                if (errorText && errorText->size() < 1200) {
                    if (!errorText->isEmpty()) *errorText += QLatin1Char('\n');
                    *errorText += QStringLiteral("%1 [%2]: %3")
                        .arg(ev.entityType, ev.entityUid, applyError.left(400));
                }
            }
        }
    }
    return stats;
}

} // namespace RemoteMutationApplyService
