#include "infrastructure/db/sqlite/LegacyCanonicalImportNormalization.h"

#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

namespace {
bool tableExists(QSqlDatabase db, const QString& tableName)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type='table' AND name=? LIMIT 1"));
    q.addBindValue(tableName);
    return q.exec() && q.next();
}

QSet<QString> tableColumns(QSqlDatabase db, const QString& tableName)
{
    QSet<QString> cols;
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(tableName))) {
        return cols;
    }
    while (q.next()) {
        cols.insert(q.value(1).toString().trimmed());
    }
    return cols;
}

bool execSql(QSqlDatabase db, const QString& sql, const QString& label, QString* errorText)
{
    QSqlQuery q(db);
    if (q.exec(sql)) {
        return true;
    }
    if (errorText) {
        *errorText = QStringLiteral("%1: %2").arg(label, q.lastError().text());
    }
    return false;
}
}

bool LegacyCanonicalImportNormalization::finalizeCanonicalImportedRelations(QSqlDatabase database, QString* errorText)
{
    if (!database.isValid()) {
        if (errorText) *errorText = QStringLiteral("Invalid database connection for canonical import normalization");
        return false;
    }
    if (!database.isOpen() && !database.open()) {
        if (errorText) *errorText = database.lastError().text();
        return false;
    }

    const bool hasOrders = tableExists(database, QStringLiteral("work_orders"));
    const bool hasServices = tableExists(database, QStringLiteral("work_order_services"));
    const bool hasParts = tableExists(database, QStringLiteral("work_order_parts"));
    const bool hasServiceMasters = tableExists(database, QStringLiteral("service_masters"));
    const bool hasMasters = tableExists(database, QStringLiteral("masters"));

    const QSet<QString> orderCols = hasOrders ? tableColumns(database, QStringLiteral("work_orders")) : QSet<QString>();
    const QSet<QString> serviceCols = hasServices ? tableColumns(database, QStringLiteral("work_order_services")) : QSet<QString>();
    const QSet<QString> partCols = hasParts ? tableColumns(database, QStringLiteral("work_order_parts")) : QSet<QString>();
    const QSet<QString> smCols = hasServiceMasters ? tableColumns(database, QStringLiteral("service_masters")) : QSet<QString>();
    const QSet<QString> masterCols = hasMasters ? tableColumns(database, QStringLiteral("masters")) : QSet<QString>();

    if (hasMasters) {
        if (masterCols.contains(QStringLiteral("role")) && !execSql(database,
                QStringLiteral("UPDATE masters SET role='master' WHERE TRIM(COALESCE(role,''))=''") ,
                QStringLiteral("normalize masters.role"), errorText)) return false;
        if (masterCols.contains(QStringLiteral("active")) && !execSql(database,
                QStringLiteral("UPDATE masters SET active=1 WHERE active IS NULL"),
                QStringLiteral("normalize masters.active"), errorText)) return false;
        if (masterCols.contains(QStringLiteral("admin_percent")) && !execSql(database,
                QStringLiteral("UPDATE masters SET admin_percent=0 WHERE admin_percent IS NULL"),
                QStringLiteral("normalize masters.admin_percent"), errorText)) return false;
    }

    if (hasServices) {
        if (serviceCols.contains(QStringLiteral("order_uid")) && hasOrders && orderCols.contains(QStringLiteral("order_uid")) && !execSql(database,
                QStringLiteral("UPDATE work_order_services SET order_uid=(SELECT COALESCE(wo.order_uid,'') FROM work_orders wo WHERE wo.number=work_order_services.order_number) WHERE EXISTS (SELECT 1 FROM work_orders wo WHERE wo.number=work_order_services.order_number) AND COALESCE(order_uid,'')<>COALESCE((SELECT COALESCE(wo.order_uid,'') FROM work_orders wo WHERE wo.number=work_order_services.order_number),'')") ,
                QStringLiteral("normalize services.order_uid"), errorText)) return false;
        if (serviceCols.contains(QStringLiteral("note")) && !execSql(database,
                QStringLiteral("UPDATE work_order_services SET note='' WHERE note IS NULL"),
                QStringLiteral("normalize services.note"), errorText)) return false;
        if (serviceCols.contains(QStringLiteral("paid")) && !execSql(database,
                QStringLiteral("UPDATE work_order_services SET paid=1 WHERE paid IS NULL"),
                QStringLiteral("normalize services.paid"), errorText)) return false;
        if (serviceCols.contains(QStringLiteral("amount")) && !execSql(database,
                QStringLiteral("UPDATE work_order_services SET amount = CASE WHEN amount <= 0 THEN ROUND(COALESCE(qty,1) * COALESCE(price,0), 2) ELSE amount END"),
                QStringLiteral("normalize services.amount"), errorText)) return false;
        if (serviceCols.contains(QStringLiteral("parent_id"))) {
            if (!execSql(database,
                    QStringLiteral("UPDATE work_order_services SET parent_id=NULL WHERE COALESCE(parent_id,0)<=0"),
                    QStringLiteral("normalize services.parent_id root"), errorText)) return false;
            if (!execSql(database,
                    QStringLiteral("UPDATE work_order_services SET parent_id=NULL WHERE COALESCE(parent_id,0)<>0 AND NOT EXISTS (SELECT 1 FROM work_order_services p WHERE p.id=work_order_services.parent_id)"),
                    QStringLiteral("normalize services.parent_id missing"), errorText)) return false;
            if (!execSql(database,
                    QStringLiteral("UPDATE work_order_services SET parent_id=NULL WHERE COALESCE(parent_id,0)<>0 AND EXISTS (SELECT 1 FROM work_order_services p WHERE p.id=work_order_services.parent_id AND COALESCE(p.order_number,0)<>COALESCE(work_order_services.order_number,0))"),
                    QStringLiteral("normalize services.parent_id cross-order"), errorText)) return false;
        }
    }

    if (hasParts) {
        if (partCols.contains(QStringLiteral("order_uid")) && hasOrders && orderCols.contains(QStringLiteral("order_uid")) && !execSql(database,
                QStringLiteral("UPDATE work_order_parts SET order_uid=(SELECT COALESCE(wo.order_uid,'') FROM work_orders wo WHERE wo.number=work_order_parts.order_number) WHERE EXISTS (SELECT 1 FROM work_orders wo WHERE wo.number=work_order_parts.order_number) AND COALESCE(order_uid,'')<>COALESCE((SELECT COALESCE(wo.order_uid,'') FROM work_orders wo WHERE wo.number=work_order_parts.order_number),'')") ,
                QStringLiteral("normalize parts.order_uid"), errorText)) return false;
        if (partCols.contains(QStringLiteral("note")) && !execSql(database,
                QStringLiteral("UPDATE work_order_parts SET note='' WHERE note IS NULL"),
                QStringLiteral("normalize parts.note"), errorText)) return false;
        if (partCols.contains(QStringLiteral("ordered_by_admin")) && !execSql(database,
                QStringLiteral("UPDATE work_order_parts SET ordered_by_admin=1 WHERE ordered_by_admin IS NULL"),
                QStringLiteral("normalize parts.ordered_by_admin"), errorText)) return false;
        if (partCols.contains(QStringLiteral("paid")) && !execSql(database,
                QStringLiteral("UPDATE work_order_parts SET paid=0 WHERE paid IS NULL"),
                QStringLiteral("normalize parts.paid"), errorText)) return false;
        if (partCols.contains(QStringLiteral("purchase_price")) && !execSql(database,
                QStringLiteral("UPDATE work_order_parts SET purchase_price = CASE WHEN purchase_price <= 0 THEN price ELSE purchase_price END WHERE purchase_price <= 0"),
                QStringLiteral("normalize parts.purchase_price"), errorText)) return false;
        if (partCols.contains(QStringLiteral("purchase_amount")) && !execSql(database,
                QStringLiteral("UPDATE work_order_parts SET purchase_amount = CASE WHEN purchase_amount <= 0 THEN amount ELSE purchase_amount END WHERE purchase_amount <= 0"),
                QStringLiteral("normalize parts.purchase_amount"), errorText)) return false;
        if (partCols.contains(QStringLiteral("amount")) && !execSql(database,
                QStringLiteral("UPDATE work_order_parts SET amount = CASE WHEN amount <= 0 THEN ROUND(COALESCE(qty,1) * COALESCE(price,0), 2) ELSE amount END"),
                QStringLiteral("normalize parts.amount"), errorText)) return false;
        if (partCols.contains(QStringLiteral("service_id")) && hasServices && !execSql(database,
                QStringLiteral("UPDATE work_order_parts SET service_id=0 WHERE COALESCE(service_id,0)<>0 AND NOT EXISTS (SELECT 1 FROM work_order_services s WHERE s.id=work_order_parts.service_id AND COALESCE(s.order_number,0)=COALESCE(work_order_parts.order_number,0))"),
                QStringLiteral("normalize parts.service_id"), errorText)) return false;
    }

    if (hasServiceMasters) {
        if (!execSql(database,
                QStringLiteral("DELETE FROM service_masters WHERE NOT EXISTS (SELECT 1 FROM work_order_services s WHERE s.id=service_masters.service_id)"),
                QStringLiteral("normalize service_masters.service_id"), errorText)) return false;
        if (!execSql(database,
                QStringLiteral("DELETE FROM service_masters WHERE TRIM(COALESCE(master_id,''))='' OR NOT EXISTS (SELECT 1 FROM masters m WHERE m.id=service_masters.master_id)"),
                QStringLiteral("normalize service_masters.master_id"), errorText)) return false;
        if (smCols.contains(QStringLiteral("share_percent")) && !execSql(database,
                QStringLiteral("UPDATE service_masters SET share_percent=100 WHERE COALESCE(share_percent,0)<=0"),
                QStringLiteral("normalize service_masters.share_percent"), errorText)) return false;
        if (smCols.contains(QStringLiteral("role")) && !execSql(database,
                QStringLiteral("UPDATE service_masters SET role='master' WHERE TRIM(COALESCE(role,''))=''"),
                QStringLiteral("normalize service_masters.role"), errorText)) return false;
        if (smCols.contains(QStringLiteral("master_name_closed")) && !execSql(database,
                QStringLiteral("UPDATE service_masters SET master_name_closed=COALESCE((SELECT m.name FROM masters m WHERE m.id=service_masters.master_id),'') WHERE TRIM(COALESCE(master_name_closed,''))=''"),
                QStringLiteral("normalize service_masters.master_name_closed"), errorText)) return false;
        if (hasOrders && hasMasters && !execSql(database,
                QStringLiteral(
                    "INSERT OR IGNORE INTO service_masters(service_id, master_id, share_percent, role, master_name_closed) "
                    "SELECT s.id, wo.master_id, 100.0, 'master', COALESCE(wo.master_name_closed, m.name, '') "
                    "FROM work_order_services s "
                    "JOIN work_orders wo ON wo.number=s.order_number "
                    "LEFT JOIN masters m ON m.id=wo.master_id "
                    "WHERE TRIM(COALESCE(wo.master_id,''))<>'' "
                    "AND EXISTS (SELECT 1 FROM masters mx WHERE mx.id=wo.master_id) "
                    "AND NOT EXISTS (SELECT 1 FROM service_masters sm WHERE sm.service_id=s.id)"),
                QStringLiteral("backfill missing service_masters"), errorText)) return false;
    }

    return true;
}
