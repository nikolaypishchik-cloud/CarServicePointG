#include "infrastructure/db/sqlite/DbSchemaAuditSupport.h"

#include <QDateTime>
#include <QMap>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>

namespace {

QSet<QString> tableColumns(QSqlDatabase db, const QString& table)
{
    QSet<QString> cols;
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table))) return cols;
    while (q.next()) cols.insert(q.value(1).toString().trimmed());
    return cols;
}

bool hasObject(QSqlDatabase db, const QString& name, const QString& type)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type=? AND name=? LIMIT 1"));
    q.addBindValue(type);
    q.addBindValue(name);
    if (!q.exec()) return false;
    return q.next();
}

bool hasIndex(QSqlDatabase db, const QString& name)
{
    return hasObject(db, name, QStringLiteral("index"));
}

bool hasView(QSqlDatabase db, const QString& name)
{
    return hasObject(db, name, QStringLiteral("view"));
}

bool ensureKeyValueTable(QSqlDatabase db, const QString& tableName, QString* errorText)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS %1(key TEXT PRIMARY KEY, value TEXT NOT NULL DEFAULT '')").arg(tableName))) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    return true;
}

bool upsertMetaValue(QSqlDatabase db, const QString& tableName, const QString& key, const QString& value)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT INTO %1(key, value) VALUES(?, ?) ON CONFLICT(key) DO UPDATE SET value=excluded.value").arg(tableName));
    q.addBindValue(key);
    q.addBindValue(value);
    return q.exec();
}

QString missingList(const QStringList& values)
{
    return values.join(QStringLiteral(", "));
}

} // namespace

namespace DbSchemaAuditSupport {

bool ensureContractMeta(QSqlDatabase db, QString* errorText)
{
    if (!ensureKeyValueTable(db, QStringLiteral("db_contract_meta"), errorText)) {
        return false;
    }

    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    const QMap<QString, QString> rows = {
        {QStringLiteral("contract_version"), QStringLiteral("stage7_v1")},
        {QStringLiteral("active_order_source_of_truth"), QStringLiteral("work_orders")},
        {QStringLiteral("archive_order_source_of_truth"), QStringLiteral("archive_shard.work_orders")},
        {QStringLiteral("routing_model"), QStringLiteral("router_order_locator derived from registry/workspace")},
        {QStringLiteral("archive_policy"), QStringLiteral("closed active orders may be pruned into archive shards; hydrated copies stay derived")},
        {QStringLiteral("last_contract_audit_utc"), now}
    };

    for (auto it = rows.begin(); it != rows.end(); ++it) {
        if (!upsertMetaValue(db, QStringLiteral("db_contract_meta"), it.key(), it.value())) {
            if (errorText) *errorText = QStringLiteral("Failed to persist db_contract_meta");
            return false;
        }
    }
    return true;
}

bool applySupplementalIndexes(QSqlDatabase db, QString* errorText)
{
    const QStringList statements = {
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_work_order_parts_service_id ON work_order_parts(service_id)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_work_order_parts_order_service ON work_order_parts(order_number, service_id)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_work_order_parts_delivery_date ON work_order_parts(delivery_date)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_work_order_parts_installed_at ON work_order_parts(installed_at)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_work_order_parts_ordered_at ON work_order_parts(ordered_at)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_work_order_parts_paid_ordered_at ON work_order_parts(paid, ordered_at)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_work_order_services_order_number_id ON work_order_services(order_number, id)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_work_order_services_order_uid_id ON work_order_services(order_uid, id)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_work_order_parts_order_uid_id ON work_order_parts(order_uid, id)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_service_masters_service_id ON service_masters(service_id)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_outsource_repairs_service_id ON outsource_repairs(service_id)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_outsource_repairs_sent_at ON outsource_repairs(sent_at)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_service_attachments_service_id ON work_order_service_attachments(service_id)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_part_attachments_part_id ON work_order_part_attachments(part_id)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_outsource_jobs_order_service ON outsource_jobs(order_number, service_id)")
    };

    QSqlQuery q(db);
    for (const QString& sql : statements) {
        if (!q.exec(sql)) {
            if (errorText) *errorText = q.lastError().text();
            return false;
        }
    }
    return true;
}

bool runActiveSchemaAudit(QSqlDatabase db, QString* errorText)
{
    if (!ensureContractMeta(db, errorText)) return false;
    if (!applySupplementalIndexes(db, errorText)) return false;

    const QMap<QString, QStringList> requiredColumns = {
        {QStringLiteral("work_orders"), {QStringLiteral("number"), QStringLiteral("order_uid"), QStringLiteral("order_number"), QStringLiteral("order_year"), QStringLiteral("opened_at"), QStringLiteral("closed_at"), QStringLiteral("status"), QStringLiteral("plate_norm"), QStringLiteral("master_id"), QStringLiteral("works_sum"), QStringLiteral("parts_sum")}},
        {QStringLiteral("work_order_services"), {QStringLiteral("id"), QStringLiteral("order_uid"), QStringLiteral("order_number"), QStringLiteral("title"), QStringLiteral("price"), QStringLiteral("amount")}},
        {QStringLiteral("service_masters"), {QStringLiteral("service_id"), QStringLiteral("master_id"), QStringLiteral("share_percent"), QStringLiteral("role"), QStringLiteral("master_name_closed")}},
        {QStringLiteral("work_order_parts"), {QStringLiteral("id"), QStringLiteral("order_uid"), QStringLiteral("order_number"), QStringLiteral("ordered_at"), QStringLiteral("delivery_date"), QStringLiteral("installed_at"), QStringLiteral("part_number"), QStringLiteral("name"), QStringLiteral("qty"), QStringLiteral("price"), QStringLiteral("amount"), QStringLiteral("service_id"), QStringLiteral("purchase_price"), QStringLiteral("purchase_amount"), QStringLiteral("markup_percent"), QStringLiteral("manual_markup_override"), QStringLiteral("markup_source"), QStringLiteral("pricing_updated_at"), QStringLiteral("pricing_calc_version")}},
        {QStringLiteral("outsource_jobs"), {QStringLiteral("order_uid"), QStringLiteral("order_number"), QStringLiteral("service_id"), QStringLiteral("company_name"), QStringLiteral("sent_date"), QStringLiteral("client_price")}},
        {QStringLiteral("planning_calendar"), {QStringLiteral("day"), QStringLiteral("note")}},
        {QStringLiteral("order_registry"), {QStringLiteral("order_number"), QStringLiteral("order_uid"), QStringLiteral("storage_kind"), QStringLiteral("storage_path"), QStringLiteral("status")}},
        {QStringLiteral("workspace_order_state"), {QStringLiteral("order_number"), QStringLiteral("origin"), QStringLiteral("source_db"), QStringLiteral("order_uid")}}
    };

    QStringList failures;
    for (auto it = requiredColumns.begin(); it != requiredColumns.end(); ++it) {
        const QSet<QString> cols = tableColumns(db, it.key());
        QStringList missing;
        for (const QString& col : it.value()) {
            if (!cols.contains(col)) missing << QStringLiteral("%1.%2").arg(it.key(), col);
        }
        failures.append(missing);
    }

    const QStringList requiredIndexes = {
        QStringLiteral("idx_work_orders_uid_unique"),
        QStringLiteral("idx_work_orders_order_year_number_unique"),
        QStringLiteral("idx_work_order_parts_service_id"),
        QStringLiteral("idx_work_order_parts_order_service"),
        QStringLiteral("idx_work_order_parts_ordered_at"),
        QStringLiteral("idx_work_order_services_order_uid_id"),
        QStringLiteral("idx_work_order_parts_order_uid_id"),
        QStringLiteral("idx_work_order_parts_paid_ordered_at"),
        QStringLiteral("idx_service_masters_service_id"),
        QStringLiteral("idx_outsource_repairs_sent_at"),
        QStringLiteral("idx_outsource_jobs_order_service")
    };
    const QStringList requiredViews = {
        QStringLiteral("router_order_locator"),
        QStringLiteral("router_open_order_locator"),
        QStringLiteral("router_work_orders"),
        QStringLiteral("router_work_order_services"),
        QStringLiteral("router_work_order_parts")
    };
    for (const QString& idx : requiredIndexes) {
        if (!hasIndex(db, idx)) failures << QStringLiteral("missing index %1").arg(idx);
    }
    for (const QString& viewName : requiredViews) {
        if (!hasView(db, viewName)) failures << QStringLiteral("missing view %1").arg(viewName);
    }

    if (!failures.isEmpty()) {
        if (errorText) *errorText = QStringLiteral("DB schema audit failed: %1").arg(missingList(failures));
        return false;
    }
    return true;
}

bool runRegistrySchemaAudit(QSqlDatabase db, QString* errorText)
{
    const QMap<QString, QStringList> requiredColumns = {
        {QStringLiteral("order_registry"), {QStringLiteral("order_number"), QStringLiteral("order_uid"), QStringLiteral("storage_kind"), QStringLiteral("storage_path"), QStringLiteral("source_db"), QStringLiteral("visible_order_number"), QStringLiteral("order_year"), QStringLiteral("master_id"), QStringLiteral("master_name_closed"), QStringLiteral("works_sum"), QStringLiteral("parts_sum")}},
        {QStringLiteral("part_registry"), {QStringLiteral("entry_key"), QStringLiteral("order_uid"), QStringLiteral("part_uid"), QStringLiteral("order_number"), QStringLiteral("storage_kind"), QStringLiteral("storage_path"), QStringLiteral("ordered_at"), QStringLiteral("delivery_date"), QStringLiteral("part_number"), QStringLiteral("name")}}
    };

    QStringList failures;
    for (auto it = requiredColumns.begin(); it != requiredColumns.end(); ++it) {
        const QSet<QString> cols = tableColumns(db, it.key());
        QStringList missing;
        for (const QString& col : it.value()) {
            if (!cols.contains(col)) missing << QStringLiteral("%1.%2").arg(it.key(), col);
        }
        failures.append(missing);
    }

    const QStringList requiredIndexes = {
        QStringLiteral("idx_registry_visible_year"),
        QStringLiteral("idx_part_registry_order"),
        QStringLiteral("idx_part_registry_order_uid"),
        QStringLiteral("idx_part_registry_storage_part_uid"),
        QStringLiteral("idx_part_registry_partno"),
        QStringLiteral("idx_part_registry_dates"),
        QStringLiteral("idx_registry_storage_number"),
        QStringLiteral("idx_part_registry_storage_order")
    };
    if (!(hasIndex(db, QStringLiteral("idx_registry_order_uid_unique"))
          || hasIndex(db, QStringLiteral("idx_registry_order_uid_unique_nonempty")))) {
        failures << QStringLiteral("missing index idx_registry_order_uid_unique");
    }
    for (const QString& idx : requiredIndexes) {
        if (!hasIndex(db, idx)) failures << QStringLiteral("missing index %1").arg(idx);
    }

    if (!failures.isEmpty()) {
        if (errorText) *errorText = QStringLiteral("Registry schema audit failed: %1").arg(missingList(failures));
        return false;
    }
    return true;
}

} // namespace DbSchemaAuditSupport
