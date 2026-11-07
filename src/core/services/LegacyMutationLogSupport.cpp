#include "core/services/LegacyMutationLogSupport.h"
#include "infrastructure/db/sqlite/MutationJournalSchema.h"
#include "core/util/DateFormats.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>

const QString& mutationJournalTableName()
{
    static const QString name = QStringLiteral("legacy_mutation_log");
    return name;
}

namespace {

QString jsonForSingleRow(QSqlQuery& q)
{
    if (!q.next()) return QStringLiteral("{}");

    QJsonObject obj;
    const QSqlRecord rec = q.record();
    for (int i = 0; i < rec.count(); ++i) {
        const QString key = rec.fieldName(i);
        const QVariant value = q.value(i);
        obj.insert(key, QJsonValue::fromVariant(value));
    }
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

bool execEnsure(QSqlDatabase database, const QString& sql, QString* errorText)
{
    QSqlQuery q(database);
    if (!q.exec(sql)) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    return true;
}

QString nonNullText(QString value)
{
    if (value.isNull()) return QStringLiteral("");
    return value.trimmed();
}

}

namespace LegacyMutationLogSupport {

bool ensureSchema(QSqlDatabase database, QString* errorText)
{
    if (!database.isValid()) {
        if (errorText) *errorText = QStringLiteral("Mutation log DB is invalid");
        return false;
    }
    if (!database.isOpen() && !database.open()) {
        if (errorText) *errorText = database.lastError().text();
        return false;
    }
    return execEnsure(database,
                      QStringLiteral(
                          "CREATE TABLE IF NOT EXISTS legacy_mutation_log ("
                          " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                          " created_at TEXT NOT NULL DEFAULT (datetime('now')),")
                          + QStringLiteral(
                          " entity_name TEXT NOT NULL,"
                          " action TEXT NOT NULL,"
                          " order_number INTEGER NOT NULL DEFAULT 0,"
                          " order_uid TEXT NOT NULL DEFAULT '',"
                          " record_key TEXT NOT NULL DEFAULT '',"
                          " before_json TEXT NOT NULL DEFAULT '',"
                          " after_json TEXT NOT NULL DEFAULT ''"
                          ");"),
                      errorText)
        && execEnsure(database,
                      QStringLiteral("CREATE INDEX IF NOT EXISTS idx_legacy_mutation_log_order ON legacy_mutation_log(order_number, created_at)"),
                      errorText)
        && execEnsure(database,
                      QStringLiteral("CREATE INDEX IF NOT EXISTS idx_legacy_mutation_log_entity ON legacy_mutation_log(entity_name, created_at)"),
                      errorText);
}

QString orderUidForNumber(QSqlDatabase database, int orderNumber)
{
    if (orderNumber <= 0) return QString();
    QSqlQuery q(database);
    q.prepare(QStringLiteral("SELECT COALESCE(order_uid,'') FROM work_orders WHERE number=? LIMIT 1"));
    q.addBindValue(orderNumber);
    return (q.exec() && q.next()) ? q.value(0).toString().trimmed() : QString();
}

QString snapshotOrder(QSqlDatabase database, int orderNumber)
{
    if (orderNumber <= 0) return QStringLiteral("{}");
    QSqlQuery q(database);
    q.prepare(QStringLiteral(
        "SELECT number, COALESCE(order_uid,'') AS order_uid, COALESCE(order_number,0) AS order_number, COALESCE(order_year,0) AS order_year, "
        "COALESCE(opened_at,'') AS opened_at, COALESCE(closed_at,'') AS closed_at, COALESCE(status,'') AS status, "
        "COALESCE(plate,'') AS plate, COALESCE(vin,'') AS vin, COALESCE(car,'') AS car, COALESCE(client,'') AS client, "
        "COALESCE(master_id,'') AS master_id, COALESCE(works_sum,0) AS works_sum, COALESCE(parts_sum,0) AS parts_sum, COALESCE(discount,0) AS discount, COALESCE(discount_mode,'legacy') AS discount_mode "
        "FROM work_orders WHERE number=? LIMIT 1"));
    q.addBindValue(orderNumber);
    if (!q.exec()) return QStringLiteral("{}");
    return jsonForSingleRow(q);
}

QString snapshotService(QSqlDatabase database, int serviceId)
{
    if (serviceId <= 0) return QStringLiteral("{}");
    QSqlQuery q(database);
    q.prepare(QStringLiteral(
        "SELECT s.id, COALESCE(s.order_uid,'') AS order_uid, COALESCE(s.order_number,0) AS order_number, "
        "COALESCE(s.title,'') AS title, COALESCE(s.qty,0) AS qty, COALESCE(s.price,0) AS price, COALESCE(s.amount,0) AS amount, "
        "COALESCE(s.note,'') AS note, COALESCE(s.paid,0) AS paid, COALESCE(s.parent_id,0) AS parent_id, "
        "COALESCE((SELECT GROUP_CONCAT(master_id, ',') FROM service_masters sm WHERE sm.service_id=s.id),'') AS master_ids "
        "FROM work_order_services s WHERE s.id=? LIMIT 1"));
    q.addBindValue(serviceId);
    if (!q.exec()) return QStringLiteral("{}");
    return jsonForSingleRow(q);
}

QString snapshotPart(QSqlDatabase database, int partId)
{
    if (partId <= 0) return QStringLiteral("{}");
    QSqlQuery q(database);
    q.prepare(QStringLiteral(
        "SELECT id, COALESCE(order_uid,'') AS order_uid, COALESCE(order_number,0) AS order_number, "
        "COALESCE(ordered_at,'') AS ordered_at, COALESCE(delivery_date,'') AS delivery_date, COALESCE(store,'') AS store, "
        "COALESCE(part_number,'') AS part_number, COALESCE(name,'') AS name, COALESCE(manufacturer,'') AS manufacturer, COALESCE(note,'') AS note, "
        "COALESCE(qty,0) AS qty, COALESCE(price,0) AS price, COALESCE(amount,0) AS amount, COALESCE(service_id,0) AS service_id, "
        "COALESCE(purchase_price,0) AS purchase_price, COALESCE(purchase_amount,0) AS purchase_amount, COALESCE(markup_percent,0) AS markup_percent "
        "FROM work_order_parts WHERE id=? LIMIT 1"));
    q.addBindValue(partId);
    if (!q.exec()) return QStringLiteral("{}");
    return jsonForSingleRow(q);
}

QString snapshotMaster(QSqlDatabase database, const QString& masterId)
{
    if (masterId.trimmed().isEmpty()) return QStringLiteral("{}");
    QSqlQuery q(database);
    q.prepare(QStringLiteral(
        "SELECT COALESCE(id,'') AS id, COALESCE(name,'') AS name, COALESCE(role,'') AS role, COALESCE(admin_percent,0) AS admin_percent, "
        "COALESCE(active,1) AS active, COALESCE(active_from,'') AS active_from, COALESCE(active_to,'') AS active_to "
        "FROM masters WHERE id=? LIMIT 1"));
    q.addBindValue(masterId.trimmed());
    if (!q.exec()) return QStringLiteral("{}");
    return jsonForSingleRow(q);
}

QString snapshotPlanningDay(QSqlDatabase database, const QString& dayIso)
{
    if (dayIso.trimmed().isEmpty()) return QStringLiteral("{}");
    QSqlQuery q(database);
    q.prepare(QStringLiteral("SELECT COALESCE(day,'') AS day, COALESCE(note,'') AS note FROM planning_calendar WHERE day IN (?,?) LIMIT 1"));
    q.addBindValue(dayIso.trimmed());
    q.addBindValue(DateFormats::storage(dayIso));
    if (!q.exec()) return QStringLiteral("{}");
    return jsonForSingleRow(q);
}

bool logMutation(QSqlDatabase database,
                 const QString& entityName,
                 const QString& action,
                 int orderNumber,
                 const QString& orderUid,
                 const QString& recordKey,
                 const QString& beforeJson,
                 const QString& afterJson,
                 QString* errorText)
{
    if (!ensureSchema(database, errorText)) return false;

    QSqlQuery q(database);
    q.prepare(QStringLiteral(
        "INSERT INTO %1(entity_name, action, order_number, order_uid, record_key, before_json, after_json) "
        "VALUES(?,?,?,?,?,?,?)").arg(mutationJournalTableName()));
    const QString safeEntityName = nonNullText(entityName);
    const QString safeAction = nonNullText(action);
    const QString safeOrderUid = nonNullText(orderUid);
    const QString safeRecordKey = nonNullText(recordKey);
    const QString safeBeforeJson = nonNullText(beforeJson);
    const QString safeAfterJson = nonNullText(afterJson);
    q.addBindValue(QVariant(safeEntityName));
    q.addBindValue(QVariant(safeAction));
    q.addBindValue(orderNumber);
    q.addBindValue(QVariant(safeOrderUid.isEmpty() ? QStringLiteral("") : safeOrderUid));
    q.addBindValue(QVariant(safeRecordKey));
    q.addBindValue(QVariant(safeBeforeJson));
    q.addBindValue(QVariant(safeAfterJson));
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    return true;
}

} // namespace LegacyMutationLogSupport
