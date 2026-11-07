#include "infrastructure/db/sqlite/DbArchiveRouterCacheImportSupport.h"
#include "core/diagnostics/RuntimeLoggingControl.h"

#include "infrastructure/db/sqlite/DbFacadeSupport.h"
#include "core/services/legacyimport/LegacyOrderChildMapper.h"

#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>
#include <QSet>
#include <QStringList>
#include <QVariant>
#include <QDebug>

namespace {

const QString archiveSchemaName = QStringLiteral("legacy_router");

int scalarCount_local(QSqlDatabase database, const QString& sql)
{
    QSqlQuery q(database);
    if (!q.exec(sql) || !q.next()) return 0;
    return q.value(0).toInt();
}

bool seedArchiveMasters_local(QSqlDatabase database, const QSet<QString>& srcMasters, QString* errorText)
{
    if (srcMasters.isEmpty()) return true;
    QSqlQuery q(database);
    QStringList insertCols, selectExprs;
    auto add = [&](const QString& dst, const QString& expr) {
        insertCols << sqlQuoteIdent(dst);
        selectExprs << expr;
    };
    add(QStringLiteral("id"), firstAvailableExpr(srcMasters, {QStringLiteral("id")}, QStringLiteral("''")));
    add(QStringLiteral("name"), firstAvailableExpr(srcMasters, {QStringLiteral("name")}, QStringLiteral("''")));
    add(QStringLiteral("role"), firstAvailableExpr(srcMasters, {QStringLiteral("role")}, QStringLiteral("'master'")));
    add(QStringLiteral("admin_percent"), firstAvailableExpr(srcMasters, {QStringLiteral("admin_percent")}, QStringLiteral("0")));
    add(QStringLiteral("active"), firstAvailableExpr(srcMasters, {QStringLiteral("active")}, QStringLiteral("1")));
    const QString sql = QStringLiteral("INSERT OR IGNORE INTO masters(%1) SELECT %2 FROM legacy_router.masters")
                            .arg(insertCols.join(','), selectExprs.join(','));
    if (q.exec(sql)) return true;
    if (errorText) *errorText = QStringLiteral("Legacy masters import to workspace failed: %1").arg(q.lastError().text());
    return false;
}

bool seedArchiveOrders_local(QSqlDatabase database, const QSet<QString>& srcOrders, QString* errorText)
{
    if (srcOrders.isEmpty()) return true;
    QSqlQuery q(database);
    QStringList insertCols, selectExprs;
    auto add = [&](const QString& dst, const QString& expr) {
        insertCols << sqlQuoteIdent(dst);
        selectExprs << expr;
    };
    // Qualify the source key: unqualified number inside the subquery binds to wo.number.
    const QString numberExpr = srcOrders.contains(QStringLiteral("number"))
        ? QStringLiteral("source_order.number") : QStringLiteral("0");
    const QString colorExpr = firstAvailableExpr(srcOrders, {QStringLiteral("color"), QStringLiteral("car_color")}, QStringLiteral("''"));
    const QString fallbackUidExpr = QStringLiteral("('archive_' || CAST((%1) AS TEXT))").arg(numberExpr);
    // An explicit source identity is authoritative even when numeric keys collide.
    // Only old records without a UID use the compatible active-number fallback.
    const QString sourceUidExpr = srcOrders.contains(QStringLiteral("order_uid"))
        ? QStringLiteral("NULLIF(TRIM(COALESCE(source_order.order_uid,'')), '')")
        : QStringLiteral("NULL");
    const QString uidExpr = QStringLiteral("COALESCE(%1, (SELECT NULLIF(TRIM(COALESCE(wo.order_uid,'')), '') FROM work_orders wo WHERE wo.number=%2), %3)")
                                .arg(sourceUidExpr, numberExpr, fallbackUidExpr);
    const QString visibleOrderExpr = srcOrders.contains(QStringLiteral("order_number"))
        ? QStringLiteral("CASE WHEN COALESCE(order_number,0)>0 THEN order_number ELSE %1 END").arg(numberExpr)
        : numberExpr;
    const QString yearExpr = srcOrders.contains(QStringLiteral("order_year"))
        ? QStringLiteral("CASE WHEN COALESCE(order_year,0)>0 THEN order_year ELSE CASE WHEN substr(TRIM(COALESCE(opened_at,'')),5,1)='-' AND substr(TRIM(COALESCE(opened_at,'')),8,1)='-' THEN CAST(substr(TRIM(COALESCE(opened_at,'')),1,4) AS INTEGER) WHEN substr(TRIM(COALESCE(opened_at,'')),1,10) GLOB '[0-9][0-9].[0-9][0-9].[0-9][0-9][0-9][0-9]' THEN CAST(substr(TRIM(COALESCE(opened_at,'')),7,4) AS INTEGER) WHEN substr(TRIM(COALESCE(opened_at,'')),3,1)='.' AND substr(TRIM(COALESCE(opened_at,'')),6,1)='.' AND length(TRIM(COALESCE(opened_at,'')))>=8 THEN 2000 + CAST(substr(TRIM(COALESCE(opened_at,'')),7,2) AS INTEGER) ELSE CAST(strftime('%Y','now') AS INTEGER) END END")
        : QStringLiteral("CASE WHEN substr(TRIM(COALESCE(opened_at,'')),5,1)='-' AND substr(TRIM(COALESCE(opened_at,'')),8,1)='-' THEN CAST(substr(TRIM(COALESCE(opened_at,'')),1,4) AS INTEGER) WHEN substr(TRIM(COALESCE(opened_at,'')),1,10) GLOB '[0-9][0-9].[0-9][0-9].[0-9][0-9][0-9][0-9]' THEN CAST(substr(TRIM(COALESCE(opened_at,'')),7,4) AS INTEGER) WHEN substr(TRIM(COALESCE(opened_at,'')),3,1)='.' AND substr(TRIM(COALESCE(opened_at,'')),6,1)='.' AND length(TRIM(COALESCE(opened_at,'')))>=8 THEN 2000 + CAST(substr(TRIM(COALESCE(opened_at,'')),7,2) AS INTEGER) ELSE CAST(strftime('%Y','now') AS INTEGER) END");
    add(QStringLiteral("number"), numberExpr);
    add(QStringLiteral("order_uid"), uidExpr);
    add(QStringLiteral("order_number"), visibleOrderExpr);
    add(QStringLiteral("order_year"), yearExpr);
    add(QStringLiteral("opened_at"), firstAvailableExpr(srcOrders, {QStringLiteral("opened_at")}, QStringLiteral("datetime('now')")));
    add(QStringLiteral("closed_at"), firstAvailableExpr(srcOrders, {QStringLiteral("closed_at")}, QStringLiteral("NULL")));
    add(QStringLiteral("status"), firstAvailableExpr(srcOrders, {QStringLiteral("status")}, QStringLiteral("'open'")));
    add(QStringLiteral("plate"), firstAvailableExpr(srcOrders, {QStringLiteral("plate")}, QStringLiteral("''")));
    add(QStringLiteral("plate_norm"), firstAvailableExpr(srcOrders, {QStringLiteral("plate_norm"), QStringLiteral("plate")}, QStringLiteral("''")));
    add(QStringLiteral("vin"), firstAvailableExpr(srcOrders, {QStringLiteral("vin")}, QStringLiteral("''")));
    add(QStringLiteral("car_year"), firstAvailableExpr(srcOrders, {QStringLiteral("car_year")}, QStringLiteral("0")));
    add(QStringLiteral("odometer"), firstAvailableExpr(srcOrders, {QStringLiteral("odometer")}, QStringLiteral("0")));
    add(QStringLiteral("engine_type"), firstAvailableExpr(srcOrders, {QStringLiteral("engine_type")}, QStringLiteral("''")));
    add(QStringLiteral("engine_volume"), firstAvailableExpr(srcOrders, {QStringLiteral("engine_volume")}, QStringLiteral("''")));
    add(QStringLiteral("fuel_type"), firstAvailableExpr(srcOrders, {QStringLiteral("fuel_type")}, QStringLiteral("''")));
    add(QStringLiteral("car"), firstAvailableExpr(srcOrders, {QStringLiteral("car")}, QStringLiteral("''")));
    add(QStringLiteral("client"), firstAvailableExpr(srcOrders, {QStringLiteral("client"), QStringLiteral("owner_name")}, QStringLiteral("''")));
    add(QStringLiteral("client_phone"), firstAvailableExpr(srcOrders, {QStringLiteral("client_phone")}, QStringLiteral("''")));
    add(QStringLiteral("master_id"), firstAvailableExpr(srcOrders, {QStringLiteral("master_id")}, QStringLiteral("''")));
    add(QStringLiteral("works_sum"), firstAvailableExpr(srcOrders, {QStringLiteral("works_sum")}, QStringLiteral("0")));
    add(QStringLiteral("parts_sum"), firstAvailableExpr(srcOrders, {QStringLiteral("parts_sum")}, QStringLiteral("0")));
    add(QStringLiteral("discount_mode"), firstAvailableExpr(srcOrders, {QStringLiteral("discount_mode")}, QStringLiteral("'legacy'")));
    add(QStringLiteral("discount"), firstAvailableExpr(srcOrders, {QStringLiteral("discount")}, QStringLiteral("0")));
    add(QStringLiteral("master_name_closed"), firstAvailableExpr(srcOrders, {QStringLiteral("master_name_closed")}, QStringLiteral("''")));
    add(QStringLiteral("car_model"), firstAvailableExpr(srcOrders, {QStringLiteral("car_model")}, QStringLiteral("''")));
    add(QStringLiteral("car_brand"), firstAvailableExpr(srcOrders, {QStringLiteral("car_brand")}, QStringLiteral("''")));
    add(QStringLiteral("car_kind"), firstAvailableExpr(srcOrders, {QStringLiteral("car_kind")}, QStringLiteral("''")));
    add(QStringLiteral("color"), colorExpr);
    add(QStringLiteral("car_color"), firstAvailableExpr(srcOrders, {QStringLiteral("car_color"), QStringLiteral("color")}, colorExpr));
    add(QStringLiteral("owner_name"), firstAvailableExpr(srcOrders, {QStringLiteral("owner_name"), QStringLiteral("client")}, QStringLiteral("''")));
    const QString sql = QStringLiteral("INSERT OR REPLACE INTO router_cache_work_orders(%1) SELECT %2 FROM legacy_router.work_orders AS source_order WHERE %3 > 0")
                            .arg(insertCols.join(','), selectExprs.join(','), numberExpr);
    if (q.exec(sql)) return true;
    if (errorText) *errorText = QStringLiteral("Legacy orders import to router cache failed: %1").arg(q.lastError().text());
    return false;
}

bool seedArchiveServices_local(QSqlDatabase database, const QSet<QString>& srcServices, QString* errorText)
{
    if (srcServices.isEmpty()) return true;
    QSqlQuery q(database);
    QStringList insertCols, selectExprs;
    auto add = [&](const QString& dst, const QString& expr) {
        insertCols << sqlQuoteIdent(dst);
        selectExprs << expr;
    };
    const QString orderExpr = firstAvailableExpr(srcServices, {QStringLiteral("order_number")}, QStringLiteral("0"));
    const QString orderUidExpr = LegacyOrderChildMapper::routerParentOrderUidExpr(
        orderExpr == QStringLiteral("0") ? orderExpr : QStringLiteral("source_service.%1").arg(orderExpr));
    add(QStringLiteral("id"), firstAvailableExpr(srcServices, {QStringLiteral("id")}, QStringLiteral("NULL")));
    add(QStringLiteral("order_uid"), orderUidExpr);
    add(QStringLiteral("order_number"), orderExpr);
    add(QStringLiteral("title"), firstAvailableExpr(srcServices, {QStringLiteral("title"), QStringLiteral("name")}, QStringLiteral("''")));
    add(QStringLiteral("qty"), firstAvailableExpr(srcServices, {QStringLiteral("qty")}, QStringLiteral("1")));
    add(QStringLiteral("price"), firstAvailableExpr(srcServices, {QStringLiteral("price")}, QStringLiteral("0")));
    add(QStringLiteral("amount"), firstAvailableExpr(srcServices, {QStringLiteral("amount")}, QStringLiteral("(COALESCE(qty,1)*COALESCE(price,0))")));
    add(QStringLiteral("note"), firstAvailableExpr(srcServices, {QStringLiteral("note")}, QStringLiteral("''")));
    add(QStringLiteral("paid"), firstAvailableExpr(srcServices, {QStringLiteral("paid")}, QStringLiteral("1")));
    add(QStringLiteral("parent_id"), firstAvailableExpr(srcServices, {QStringLiteral("parent_id")}, QStringLiteral("0")));
    const QString sql = QStringLiteral("INSERT OR REPLACE INTO router_cache_work_order_services(%1) SELECT %2 FROM legacy_router.work_order_services AS source_service WHERE %3 IN (SELECT number FROM router_cache_work_orders)")
                            .arg(insertCols.join(','), selectExprs.join(','), orderExpr);
    if (q.exec(sql)) return true;
    if (errorText) *errorText = QStringLiteral("Legacy services import to router cache failed: %1").arg(q.lastError().text());
    return false;
}

bool seedArchiveServiceMasters_local(QSqlDatabase database, const QSet<QString>& srcServiceMasters, QString* errorText)
{
    if (srcServiceMasters.isEmpty()) return true;
    QSqlQuery q(database);
    QStringList insertCols, selectExprs;
    auto add = [&](const QString& dst, const QString& expr) {
        insertCols << sqlQuoteIdent(dst);
        selectExprs << expr;
    };
    add(QStringLiteral("service_id"), firstAvailableExpr(srcServiceMasters, {QStringLiteral("service_id")}, QStringLiteral("0")));
    add(QStringLiteral("master_id"), firstAvailableExpr(srcServiceMasters, {QStringLiteral("master_id")}, QStringLiteral("''")));
    add(QStringLiteral("share_percent"), firstAvailableExpr(srcServiceMasters, {QStringLiteral("share_percent")}, QStringLiteral("100")));
    add(QStringLiteral("role"), firstAvailableExpr(srcServiceMasters, {QStringLiteral("role")}, QStringLiteral("'master'")));
    add(QStringLiteral("master_name_closed"), firstAvailableExpr(srcServiceMasters, {QStringLiteral("master_name_closed")}, QStringLiteral("''")));
    const QString sql = QStringLiteral("INSERT OR REPLACE INTO router_cache_service_masters(%1) SELECT %2 FROM legacy_router.service_masters WHERE service_id IN (SELECT id FROM router_cache_work_order_services)")
                            .arg(insertCols.join(','), selectExprs.join(','));
    if (q.exec(sql)) return true;
    if (errorText) *errorText = QStringLiteral("Legacy service masters import to router cache failed: %1").arg(q.lastError().text());
    return false;
}

bool seedArchiveParts_local(QSqlDatabase database, const QSet<QString>& srcParts, QString* errorText)
{
    if (srcParts.isEmpty()) return true;
    QSqlQuery q(database);
    const QString sourceTable = archiveSchemaName + QStringLiteral(".work_order_parts AS source_part");
    QStringList insertCols, selectExprs;
    auto add = [&](const QString& dst, const QString& expr) {
        insertCols << sqlQuoteIdent(dst);
        selectExprs << expr;
    };
    const QString orderExpr = firstAvailableExpr(srcParts, {QStringLiteral("order_number"), QStringLiteral("work_order"), QStringLiteral("work_order_id"), QStringLiteral("work_order_number")}, QStringLiteral("0"));
    const QString orderUidExpr = LegacyOrderChildMapper::routerParentOrderUidExpr(
        orderExpr == QStringLiteral("0") ? orderExpr : QStringLiteral("source_part.%1").arg(orderExpr));
    const QString sourceOrderExpr = orderExpr == QStringLiteral("0") ? orderExpr : QStringLiteral("source_part.%1").arg(orderExpr);
    const QString serviceExpr = srcParts.contains(QStringLiteral("service_id")) ? QStringLiteral("source_part.service_id") : QStringLiteral("0");
    // Never attach a part to a service belonging to another order, or erase its key.
    if (!q.exec(QStringLiteral(
            "SELECT %1,%2 FROM %3 WHERE COALESCE(%2,0)<>0 AND NOT EXISTS "
            "(SELECT 1 FROM router_cache_work_order_services s WHERE s.id=%2 AND s.order_number=%1) LIMIT 1")
            .arg(sourceOrderExpr, serviceExpr, sourceTable))) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    if (q.next()) {
        if (errorText) *errorText = QStringLiteral("Archive part service conflict: order=%1 service=%2; source preserved, mapping required")
            .arg(q.value(0).toString(), q.value(1).toString());
        return false;
    }
    q.finish();
    const QString orderedExpr = firstAvailableExpr(srcParts, {QStringLiteral("ordered_at"), QStringLiteral("date"), QStringLiteral("created_at")}, QStringLiteral("''"));
    const QString deliveryExpr = firstAvailableExpr(srcParts, {QStringLiteral("delivery_date")}, QStringLiteral("''"));
    const QString qtyExpr = firstAvailableExpr(srcParts, {QStringLiteral("qty"), QStringLiteral("quantity")}, QStringLiteral("1"));
    const QString priceExpr = firstAvailableExpr(srcParts, {QStringLiteral("price"), QStringLiteral("client_price"), QStringLiteral("sale_price")}, QStringLiteral("0"));
    const QString amountExpr = firstAvailableExpr(srcParts, {QStringLiteral("amount"), QStringLiteral("sum"), QStringLiteral("total"), QStringLiteral("client_amount")}, QStringLiteral("(COALESCE(%1,1) * COALESCE(%2,0))").arg(qtyExpr, priceExpr));
    add(QStringLiteral("id"), firstAvailableExpr(srcParts, {QStringLiteral("id")}, QStringLiteral("NULL")));
    add(QStringLiteral("part_uid"), firstAvailableExpr(srcParts, {QStringLiteral("part_uid")}, QStringLiteral("''")));
    add(QStringLiteral("order_uid"), orderUidExpr);
    add(QStringLiteral("order_number"), orderExpr);
    add(QStringLiteral("ordered_at"), orderedExpr);
    add(QStringLiteral("delivery_date"), deliveryExpr);
    add(QStringLiteral("installed_at"), firstAvailableExpr(srcParts, {QStringLiteral("installed_at")}, QStringLiteral("''")));
    add(QStringLiteral("store"), firstAvailableExpr(srcParts, {QStringLiteral("store"), QStringLiteral("shop")}, QStringLiteral("''")));
    add(QStringLiteral("part_number"), firstAvailableExpr(srcParts, {QStringLiteral("part_number"), QStringLiteral("oem"), QStringLiteral("oem_number"), QStringLiteral("article"), QStringLiteral("article_number")}, QStringLiteral("''")));
    add(QStringLiteral("name"), firstAvailableExpr(srcParts, {QStringLiteral("name"), QStringLiteral("title"), QStringLiteral("part_name")}, QStringLiteral("''")));
    add(QStringLiteral("manufacturer"), firstAvailableExpr(srcParts, {QStringLiteral("manufacturer"), QStringLiteral("brand")}, QStringLiteral("''")));
    add(QStringLiteral("note"), firstAvailableExpr(srcParts, {QStringLiteral("note"), QStringLiteral("comment"), QStringLiteral("notes"), QStringLiteral("customer_note")}, QStringLiteral("''")));
    add(QStringLiteral("qty"), qtyExpr);
    add(QStringLiteral("price"), priceExpr);
    add(QStringLiteral("amount"), amountExpr);
    add(QStringLiteral("service_id"), firstAvailableExpr(srcParts, {QStringLiteral("service_id")}, QStringLiteral("0")));
    add(QStringLiteral("ordered_by_admin"), firstAvailableExpr(srcParts, {QStringLiteral("ordered_by_admin"), QStringLiteral("is_ordered")}, QStringLiteral("1")));
    add(QStringLiteral("paid"), firstAvailableExpr(srcParts, {QStringLiteral("paid"), QStringLiteral("is_paid")}, QStringLiteral("0")));
    add(QStringLiteral("purchase_price"), firstAvailableExpr(srcParts, {QStringLiteral("purchase_price"), QStringLiteral("cost_price"), QStringLiteral("price")}, priceExpr));
    add(QStringLiteral("purchase_amount"), firstAvailableExpr(srcParts, {QStringLiteral("purchase_amount"), QStringLiteral("cost_amount"), QStringLiteral("amount")}, amountExpr));
    add(QStringLiteral("markup_percent"), firstAvailableExpr(srcParts, {QStringLiteral("markup_percent"), QStringLiteral("markup"), QStringLiteral("margin_percent")}, QStringLiteral("0")));
    add(QStringLiteral("manual_markup_override"), firstAvailableExpr(srcParts, {QStringLiteral("manual_markup_override")}, QStringLiteral("0")));
    add(QStringLiteral("markup_source"), firstAvailableExpr(srcParts, {QStringLiteral("markup_source")}, QStringLiteral("'legacy'")));
    add(QStringLiteral("price_input_mode"), firstAvailableExpr(srcParts, {QStringLiteral("price_input_mode")}, QStringLiteral("'unknown'")));
    add(QStringLiteral("pricing_updated_at"), firstAvailableExpr(srcParts, {QStringLiteral("pricing_updated_at")}, QStringLiteral("''")));
    add(QStringLiteral("pricing_calc_version"), firstAvailableExpr(srcParts, {QStringLiteral("pricing_calc_version")}, QStringLiteral("1")));
    for (const QString& name : {QStringLiteral("status"), QStringLiteral("installed"), QStringLiteral("installed_by"), QStringLiteral("request_uid"), QStringLiteral("requested_at"), QStringLiteral("requested_by"), QStringLiteral("requested_by_uid"), QStringLiteral("ordered_by"), QStringLiteral("ordered_by_uid"), QStringLiteral("order_comment"), QStringLiteral("node_group"), QStringLiteral("component"), QStringLiteral("metadata_json"), QStringLiteral("created_at"), QStringLiteral("updated_at")}) {
        if (srcParts.contains(name)) add(name, sqlQuoteIdent(name));
    }
    const QString sql = QStringLiteral("INSERT OR REPLACE INTO router_cache_work_order_parts(%1) SELECT %2 FROM %4 WHERE %3 IN (SELECT number FROM router_cache_work_orders)")
                            .arg(insertCols.join(','), selectExprs.join(','), orderExpr, sourceTable);
    if (q.exec(sql)) return true;
    if (errorText) *errorText = QStringLiteral("Legacy parts import to router cache failed: %1").arg(q.lastError().text());
    return false;
}

bool seedArchiveOutsource_local(QSqlDatabase database, const QSet<QString>& srcOutsource, QString* errorText)
{
    if (srcOutsource.isEmpty()) return true;
    QSqlQuery q(database);
    const QString sourceTable = archiveSchemaName + QStringLiteral(".outsource_repairs AS source_outsource");
    QStringList insertCols, selectExprs;
    auto add = [&](const QString& dst, const QString& expr) {
        insertCols << sqlQuoteIdent(dst);
        selectExprs << expr;
    };
    const QString orderExpr = (srcOutsource.contains(QStringLiteral("order_number")) ? QStringLiteral("source_outsource.order_number") : QStringLiteral("0"));
    const QString orderUidExpr = LegacyOrderChildMapper::routerParentOrderUidExpr(orderExpr);
    add(QStringLiteral("id"), firstAvailableExpr(srcOutsource, {QStringLiteral("id")}, QStringLiteral("NULL")));
    add(QStringLiteral("order_uid"), orderUidExpr);
    add(QStringLiteral("order_number"), orderExpr);
    add(QStringLiteral("sent_at"), firstAvailableExpr(srcOutsource, {QStringLiteral("sent_at")}, QStringLiteral("''")));
    add(QStringLiteral("company"), firstAvailableExpr(srcOutsource, {QStringLiteral("company")}, QStringLiteral("''")));
    add(QStringLiteral("part_name"), firstAvailableExpr(srcOutsource, {QStringLiteral("part_name")}, QStringLiteral("''")));
    add(QStringLiteral("repair_cost"), firstAvailableExpr(srcOutsource, {QStringLiteral("repair_cost")}, QStringLiteral("0")));
    add(QStringLiteral("client_price"), firstAvailableExpr(srcOutsource, {QStringLiteral("client_price")}, QStringLiteral("0")));
    add(QStringLiteral("note"), firstAvailableExpr(srcOutsource, {QStringLiteral("note")}, QStringLiteral("''")));
    add(QStringLiteral("created_at"), firstAvailableExpr(srcOutsource, {QStringLiteral("created_at"), QStringLiteral("sent_at")}, QStringLiteral("''")));
    const QString rawServiceIdExpr = (srcOutsource.contains(QStringLiteral("service_id")) ? QStringLiteral("source_outsource.service_id") : QStringLiteral("0"));
    // A foreign service key cannot be silently discarded or linked to another order.
    const QString conflictSql = QStringLiteral(
        "SELECT %1,%2 FROM %3 "
        "WHERE COALESCE(%2,0)<>0 AND NOT EXISTS(SELECT 1 FROM router_cache_work_order_services s "
        "WHERE s.id=%2 AND s.order_number=%1) LIMIT 1").arg(orderExpr, rawServiceIdExpr, sourceTable);
    if (!q.exec(conflictSql)) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    if (q.next()) {
        if (errorText) *errorText = QStringLiteral("Archive outsource service conflict: order=%1 service=%2; source preserved, mapping required")
            .arg(q.value(0).toString(), q.value(1).toString());
        return false;
    }
    q.finish();
    const QString normalizedServiceIdExpr = QStringLiteral(
        "CASE WHEN COALESCE(%1,0)<>0 AND EXISTS(SELECT 1 FROM router_cache_work_order_services s WHERE s.id=%1 AND COALESCE(s.order_number,0)=COALESCE(%2,0)) THEN %1 ELSE 0 END")
            .arg(rawServiceIdExpr, orderExpr);
    add(QStringLiteral("service_id"), normalizedServiceIdExpr);
    const QString sql = QStringLiteral("INSERT OR REPLACE INTO router_cache_outsource_repairs(%1) SELECT %2 FROM %4 WHERE %3 IN (SELECT number FROM router_cache_work_orders)")
                            .arg(insertCols.join(','), selectExprs.join(','), orderExpr, sourceTable);
    if (q.exec(sql)) return true;
    if (errorText) *errorText = QStringLiteral("Legacy outsource import to router cache failed: %1").arg(q.lastError().text());
    return false;
}

bool normalizeRouterCacheOrderUids_local(QSqlDatabase database, QString* errorText)
{
    QSqlQuery q(database);
    if (!q.exec(QStringLiteral("UPDATE router_cache_work_order_services SET order_uid=COALESCE((SELECT NULLIF(TRIM(COALESCE(wo.order_uid,'')), '') FROM router_cache_work_orders wo WHERE wo.number=router_cache_work_order_services.order_number), CASE WHEN COALESCE(order_number,0)>0 THEN 'rewrite_' || order_number ELSE '' END) WHERE EXISTS (SELECT 1 FROM router_cache_work_orders wo WHERE wo.number=router_cache_work_order_services.order_number) AND COALESCE(order_uid,'')<>COALESCE((SELECT COALESCE(NULLIF(TRIM(COALESCE(wo.order_uid,'')), ''), CASE WHEN COALESCE(router_cache_work_order_services.order_number,0)>0 THEN 'rewrite_' || router_cache_work_order_services.order_number ELSE '' END) FROM router_cache_work_orders wo WHERE wo.number=router_cache_work_order_services.order_number),'')"))) {
        if (errorText) *errorText = QStringLiteral("Archive cache UID normalization failed: %1").arg(q.lastError().text());
        return false;
    }
    if (!q.exec(QStringLiteral("UPDATE router_cache_work_order_parts SET order_uid=COALESCE((SELECT NULLIF(TRIM(COALESCE(wo.order_uid,'')), '') FROM router_cache_work_orders wo WHERE wo.number=router_cache_work_order_parts.order_number), CASE WHEN COALESCE(order_number,0)>0 THEN 'rewrite_' || order_number ELSE '' END) WHERE EXISTS (SELECT 1 FROM router_cache_work_orders wo WHERE wo.number=router_cache_work_order_parts.order_number) AND COALESCE(order_uid,'')<>COALESCE((SELECT COALESCE(NULLIF(TRIM(COALESCE(wo.order_uid,'')), ''), CASE WHEN COALESCE(router_cache_work_order_parts.order_number,0)>0 THEN 'rewrite_' || router_cache_work_order_parts.order_number ELSE '' END) FROM router_cache_work_orders wo WHERE wo.number=router_cache_work_order_parts.order_number),'')"))) {
        if (errorText) *errorText = QStringLiteral("Archive cache UID normalization failed: %1").arg(q.lastError().text());
        return false;
    }
    if (!q.exec(QStringLiteral("UPDATE router_cache_outsource_repairs SET order_uid=COALESCE((SELECT NULLIF(TRIM(COALESCE(wo.order_uid,'')), '') FROM router_cache_work_orders wo WHERE wo.number=router_cache_outsource_repairs.order_number), CASE WHEN COALESCE(order_number,0)>0 THEN 'rewrite_' || order_number ELSE '' END) WHERE EXISTS (SELECT 1 FROM router_cache_work_orders wo WHERE wo.number=router_cache_outsource_repairs.order_number) AND COALESCE(order_uid,'')<>COALESCE((SELECT COALESCE(NULLIF(TRIM(COALESCE(wo.order_uid,'')), ''), CASE WHEN COALESCE(router_cache_outsource_repairs.order_number,0)>0 THEN 'rewrite_' || router_cache_outsource_repairs.order_number ELSE '' END) FROM router_cache_work_orders wo WHERE wo.number=router_cache_outsource_repairs.order_number),'')"))) {
        if (errorText) *errorText = QStringLiteral("Archive cache UID normalization failed: %1").arg(q.lastError().text());
        return false;
    }
    return true;
}

} // namespace

bool importArchiveSourceIntoRouterCache(QSqlDatabase database,
                                       const ArchiveRouterSourceSchema& sourceSchema,
                                       QString* errorText)
{
    QSqlQuery transaction(database);
    if (!transaction.exec(QStringLiteral("SAVEPOINT archive_cache_import"))) {
        if (errorText) *errorText = transaction.lastError().text();
        return false;
    }
    const bool imported = seedArchiveMasters_local(database, sourceSchema.srcMasters, errorText)
        && seedArchiveOrders_local(database, sourceSchema.srcOrders, errorText)
        && seedArchiveServices_local(database, sourceSchema.srcServices, errorText)
        && seedArchiveServiceMasters_local(database, sourceSchema.srcServiceMasters, errorText)
        && seedArchiveParts_local(database, sourceSchema.srcParts, errorText)
        && seedArchiveOutsource_local(database, sourceSchema.srcOutsource, errorText)
        && normalizeRouterCacheOrderUids_local(database, errorText);
    if (imported && transaction.exec(QStringLiteral("RELEASE SAVEPOINT archive_cache_import")))
        return true;
    if (imported && errorText) *errorText = transaction.lastError().text();
    if (!transaction.exec(QStringLiteral("ROLLBACK TO SAVEPOINT archive_cache_import"))) {
        if (errorText) *errorText += QStringLiteral("; rollback failed: ") + transaction.lastError().text();
        return false;
    }
    if (!transaction.exec(QStringLiteral("RELEASE SAVEPOINT archive_cache_import")) && errorText)
        *errorText += QStringLiteral("; savepoint release failed: ") + transaction.lastError().text();
    return false;
}

bool routerCacheContentReady(QSqlDatabase database, QString* errorText)
{
    QSqlQuery q(database);
    if (!q.exec("SELECT ready FROM router_cache_content_state WHERE singleton=1") || !q.next()) {
        if (errorText) *errorText = q.lastError().isValid() ? q.lastError().text() : QStringLiteral("Router cache content state is missing");
        return false;
    }
    if (q.value(0).toInt()!=1) {
        if (errorText) *errorText = QStringLiteral("Router cache content requires reconstruction from authoritative sources");
        return false;
    }
    return true;
}

static bool backfillRouterCacheBody_local(QSqlDatabase database,
                                           QString* errorText)
{
    if (!tableExistsInDb(database, QStringLiteral("work_orders"))
            || !tableExistsInDb(database, QStringLiteral("work_order_services"))
            || !tableExistsInDb(database, QStringLiteral("work_order_parts"))) {
        return routerCacheContentReady(database, errorText);
    }

    if (!routerCacheContentReady(database)) {
        // A workspace-only reconstruction must never discard archive-only identities.
        QSqlQuery coverage(database);
        const QStringList missingSources{
            QStringLiteral("SELECT 1 FROM router_cache_work_orders c WHERE NOT EXISTS(SELECT 1 FROM work_orders a WHERE a.order_uid=c.order_uid AND a.number=c.number AND a.order_year=c.order_year AND a.order_number=c.order_number) LIMIT 1"),
            QStringLiteral("SELECT 1 FROM router_cache_work_order_parts c WHERE NOT EXISTS(SELECT 1 FROM work_order_parts a WHERE a.part_uid=c.part_uid AND a.order_uid=c.order_uid AND a.order_number=c.order_number) LIMIT 1"),
            QStringLiteral("SELECT 1 FROM router_cache_work_order_services c WHERE NOT EXISTS(SELECT 1 FROM work_order_services a WHERE a.id=c.id AND a.order_uid=c.order_uid AND a.order_number=c.order_number) LIMIT 1"),
            QStringLiteral("SELECT 1 FROM router_cache_outsource_repairs c WHERE NOT EXISTS(SELECT 1 FROM outsource_repairs a WHERE a.id=c.id AND a.order_uid=c.order_uid AND a.order_number=c.order_number) LIMIT 1"),
            QStringLiteral("SELECT 1 FROM router_cache_service_masters c WHERE NOT EXISTS(SELECT 1 FROM service_masters a WHERE a.service_id=c.service_id AND a.master_id=c.master_id) LIMIT 1")};
        for (const auto& sql : missingSources) {
            if (!coverage.exec(sql)) { if(errorText)*errorText=coverage.lastError().text();return false; }
            if (coverage.next()) {
                if(errorText)*errorText=QStringLiteral("Router cache reconstruction requires an archive source; workspace coverage is incomplete");
                return false;
            }
            if (coverage.lastError().isValid()) { if(errorText)*errorText=coverage.lastError().text();return false; }
            coverage.finish();
        }
        return rebuildRouterCacheFromActiveWorkspaceData(database,errorText);
    }

    const int activeOrders = scalarCount_local(database, QStringLiteral("SELECT COUNT(*) FROM work_orders"));
    const int activeServices = scalarCount_local(database, QStringLiteral("SELECT COUNT(*) FROM work_order_services"));
    const int activeParts = scalarCount_local(database, QStringLiteral("SELECT COUNT(*) FROM work_order_parts"));
    const int cacheOrders = scalarCount_local(database, QStringLiteral("SELECT COUNT(*) FROM router_cache_work_orders"));
    const int cacheServices = scalarCount_local(database, QStringLiteral("SELECT COUNT(*) FROM router_cache_work_order_services"));
    const int cacheParts = scalarCount_local(database, QStringLiteral("SELECT COUNT(*) FROM router_cache_work_order_parts"));

    if (cacheOrders >= activeOrders && cacheServices >= activeServices && cacheParts >= activeParts) {
        return true;
    }

    if (RuntimeLoggingControl::deepDebugEnabled()) qInfo() << "Router cache fallback backfill from active workspace triggered"
            << "active" << activeOrders << activeServices << activeParts
            << "cache" << cacheOrders << cacheServices << cacheParts;

    return rebuildRouterCacheFromActiveWorkspaceData(database, errorText);
}

bool backfillRouterCacheFromActiveIfNeeded(QSqlDatabase database, QString* errorText)
{
    // Keep source coverage reads and the resulting rebuild in the same SQLite snapshot.
    QSqlQuery boundary(database);
    if (!boundary.exec(QStringLiteral("SAVEPOINT archive_cache_backfill"))) {
        if(errorText)*errorText=boundary.lastError().text();
        return false;
    }
    const bool rebuilt=backfillRouterCacheBody_local(database,errorText);
    if(rebuilt && boundary.exec(QStringLiteral("RELEASE SAVEPOINT archive_cache_backfill")))return true;
    if(rebuilt && errorText)*errorText=boundary.lastError().text();
    if(!boundary.exec(QStringLiteral("ROLLBACK TO SAVEPOINT archive_cache_backfill"))) {
        if(errorText)*errorText+=QStringLiteral("; backfill rollback failed: ")+boundary.lastError().text();
        return false;
    }
    if(!boundary.exec(QStringLiteral("RELEASE SAVEPOINT archive_cache_backfill")) && errorText)
        *errorText+=QStringLiteral("; backfill savepoint release failed: ")+boundary.lastError().text();
    return false;
}

bool validateRouterCacheReconstructionSources(QSqlDatabase database, bool archiveAttached, QString* errorText)
{
    auto rejectRows = [&](const QString& sql, const QString& problem) {
        QSqlQuery q(database);
        if (!q.exec(sql)) { if(errorText)*errorText=q.lastError().text(); return false; }
        if (q.next()) { if(errorText)*errorText=problem; return false; }
        if (q.lastError().isValid()) { if(errorText)*errorText=q.lastError().text(); return false; }
        return true;
    };
    const QStringList schemas=archiveAttached?QStringList{QStringLiteral("main"),archiveSchemaName}:QStringList{"main"};
    for(const auto& schema:schemas) {
        const auto orders=sqlQuoteIdent(schema)+".work_orders";
        if(!rejectRows("SELECT 1 FROM "+orders+" WHERE order_uid IS NULL OR TRIM(order_uid)='' OR order_uid<>TRIM(order_uid) OR order_year<=0 OR order_number<=0 LIMIT 1","Cache source has incomplete canonical order identity")
           || !rejectRows("SELECT 1 FROM "+orders+" GROUP BY order_uid HAVING COUNT(*)<>1 LIMIT 1","Cache source has duplicate order UID")
           || !rejectRows("SELECT 1 FROM "+orders+" GROUP BY order_year,order_number HAVING COUNT(*)<>1 LIMIT 1","Cache source has duplicate annual identity")) return false;
        for(const auto& table:QStringList{"work_order_parts","work_order_services","outsource_repairs"}) {
            if(!tableExistsInDb(database,table,schema)) continue;
            const auto source=sqlQuoteIdent(schema)+"."+sqlQuoteIdent(table);
            if(!rejectRows("SELECT 1 FROM "+source+" c WHERE NOT EXISTS(SELECT 1 FROM "+orders+" o WHERE o.order_uid=c.order_uid AND o.number=c.order_number) LIMIT 1","Cache source has conflicting child ownership"))return false;
            if(table=="work_order_parts" && (!rejectRows("SELECT 1 FROM "+source+" WHERE part_uid IS NULL OR TRIM(part_uid)='' OR part_uid<>TRIM(part_uid) LIMIT 1","Cache source has unknown part UID")
                || !rejectRows("SELECT 1 FROM "+source+" GROUP BY part_uid HAVING COUNT(*)<>1 LIMIT 1","Cache source has duplicate part UID")))return false;
        }
    }
    const QList<QPair<QString,QString>> families{
        {"work_orders","a.order_uid=c.order_uid AND a.number=c.number AND a.order_year=c.order_year AND a.order_number=c.order_number"},
        {"work_order_parts","a.part_uid=c.part_uid AND a.order_uid=c.order_uid AND a.order_number=c.order_number"},
        {"work_order_services","a.id=c.id AND a.order_uid=c.order_uid AND a.order_number=c.order_number"},
        {"outsource_repairs","a.id=c.id AND a.order_uid=c.order_uid AND a.order_number=c.order_number"},
        {"service_masters","a.service_id=c.service_id AND a.master_id=c.master_id"}};
    for(const auto& family:families) {
        QStringList sources;
        for(const auto& schema:schemas) if(tableExistsInDb(database,family.first,schema))
            sources<<"EXISTS(SELECT 1 FROM "+sqlQuoteIdent(schema)+"."+sqlQuoteIdent(family.first)+" a WHERE "+family.second+")";
        if(sources.isEmpty())sources<<"0";
        if(!rejectRows("SELECT 1 FROM "+sqlQuoteIdent("router_cache_"+family.first)+" c WHERE NOT ("+sources.join(" OR ")+") LIMIT 1","Router cache contains identities absent from authoritative sources"))return false;
    }
    return true;
}

static bool writeActiveWorkspaceRouterCache(QSqlDatabase database,
                                           QString* errorText, bool preserveArchive)
{
    if (!tableExistsInDb(database, QStringLiteral("work_orders"))
            || !tableExistsInDb(database, QStringLiteral("work_order_services"))
            || !tableExistsInDb(database, QStringLiteral("work_order_parts"))) {
        return true;
    }

    QSqlQuery transaction(database);
    if (!transaction.exec(QStringLiteral("SAVEPOINT archive_cache_rebuild"))) {
        if (errorText) *errorText = transaction.lastError().text();
        return false;
    }
    const bool rebuilt = [&]() -> bool {
        QSqlQuery q(database);
        if(preserveArchive) {
            QStringList conflicts{
                "SELECT 1 FROM router_cache_work_orders c JOIN work_orders a ON c.number=a.number OR c.order_uid=a.order_uid OR (c.order_year=a.order_year AND c.order_number=a.order_number) WHERE c.number<>a.number OR c.order_uid<>a.order_uid OR c.order_year<>a.order_year OR c.order_number<>a.order_number LIMIT 1"};
            for(const auto& table:QStringList{"work_order_services","work_order_parts","outsource_repairs"})
                if(tableExistsInDb(database,table))conflicts<<"SELECT 1 FROM "+sqlQuoteIdent("router_cache_"+table)+" c JOIN "+sqlQuoteIdent(table)+" a ON c.id=a.id WHERE c.order_uid<>a.order_uid OR c.order_number<>a.order_number LIMIT 1";
            conflicts<<"SELECT 1 FROM router_cache_work_order_parts c JOIN work_order_parts a ON c.part_uid=a.part_uid AND c.part_uid<>'' WHERE c.order_uid<>a.order_uid OR c.order_number<>a.order_number LIMIT 1";
            for(const auto& sql:conflicts) {
                if(!q.exec(sql)){if(errorText)*errorText=q.lastError().text();return false;}
                if(q.next()){if(errorText)*errorText=QStringLiteral("Archive/workspace cache identity collision; explicit reconciliation required");return false;}
                if(q.lastError().isValid()){if(errorText)*errorText=q.lastError().text();return false;}
                q.finish();
            }
        }
        const QString scopedOrders=preserveArchive?QStringLiteral(" WHERE order_uid IN (SELECT order_uid FROM main.work_orders)"):QString();
        const QString scopedMasters=preserveArchive?QStringLiteral(" WHERE service_id IN (SELECT id FROM router_cache_work_order_services WHERE order_uid IN (SELECT order_uid FROM main.work_orders))"):QString();

        if (!q.exec(QStringLiteral("DELETE FROM router_cache_service_masters") + scopedMasters)) {
            if (errorText) *errorText = q.lastError().text();
            return false;
        }
        if (!q.exec(QStringLiteral("DELETE FROM router_cache_outsource_repairs") + scopedOrders)) {
            if (errorText) *errorText = q.lastError().text();
            return false;
        }
        if (!q.exec(QStringLiteral("DELETE FROM router_cache_work_order_parts") + scopedOrders)) {
            if (errorText) *errorText = q.lastError().text();
            return false;
        }
        if (!q.exec(QStringLiteral("DELETE FROM router_cache_work_order_services") + scopedOrders)) {
            if (errorText) *errorText = q.lastError().text();
            return false;
        }
        if (!q.exec(QStringLiteral("DELETE FROM router_cache_work_orders") + scopedOrders)) {
            if (errorText) *errorText = q.lastError().text();
            return false;
        }
    
        const QString activeOrderUidExpr = QStringLiteral("COALESCE(NULLIF(TRIM(COALESCE(order_uid,'')),''), CASE WHEN COALESCE(number,0)>0 THEN 'rewrite_' || number ELSE '' END)");
        if (!q.exec(
                QStringLiteral(
                "INSERT OR REPLACE INTO router_cache_work_orders(number, order_uid, order_number, order_year, opened_at, closed_at, status, plate, plate_norm, vin, car_year, odometer, engine_type, engine_volume, fuel_type, car, client, client_phone, master_id, works_sum, parts_sum, discount, master_name_closed, car_model, car_brand, car_kind, color, car_color, owner_name, discount_mode) "
                "SELECT number, %1, CASE WHEN COALESCE(order_number,0)>0 THEN order_number ELSE number END, "
                "CASE WHEN COALESCE(order_year,0)>0 THEN order_year ELSE CASE WHEN substr(TRIM(COALESCE(opened_at,'')),5,1)='-' AND substr(TRIM(COALESCE(opened_at,'')),8,1)='-' THEN CAST(substr(TRIM(COALESCE(opened_at,'')),1,4) AS INTEGER) WHEN substr(TRIM(COALESCE(opened_at,'')),1,10) GLOB '[0-9][0-9].[0-9][0-9].[0-9][0-9][0-9][0-9]' THEN CAST(substr(TRIM(COALESCE(opened_at,'')),7,4) AS INTEGER) WHEN substr(TRIM(COALESCE(opened_at,'')),3,1)='.' AND substr(TRIM(COALESCE(opened_at,'')),6,1)='.' AND length(TRIM(COALESCE(opened_at,'')))>=8 THEN 2000 + CAST(substr(TRIM(COALESCE(opened_at,'')),7,2) AS INTEGER) ELSE CAST(strftime('%Y','now') AS INTEGER) END END, "
                "COALESCE(opened_at,''), closed_at, COALESCE(status,''), COALESCE(plate,''), COALESCE(plate_norm, plate, ''), COALESCE(vin,''), COALESCE(car_year,0), COALESCE(odometer,0), COALESCE(engine_type,''), COALESCE(engine_volume,''), COALESCE(fuel_type,''), COALESCE(car,''), COALESCE(client,''), COALESCE(client_phone,''), COALESCE(master_id,''), COALESCE(works_sum,0), COALESCE(parts_sum,0), COALESCE(discount,0), COALESCE(master_name_closed,''), COALESCE(car_model,''), COALESCE(car_brand,''), COALESCE(car_kind,''), COALESCE(color,''), COALESCE(car_color,''), COALESCE(owner_name,''), COALESCE(discount_mode,'legacy') "
                "FROM work_orders WHERE COALESCE(number,0)>0").arg(activeOrderUidExpr))) {
            if (errorText) *errorText = QStringLiteral("Active orders router-cache seed failed: %1").arg(q.lastError().text());
            return false;
        }
    
        if (!q.exec(QStringLiteral(
                "INSERT OR REPLACE INTO router_cache_work_order_services(id, order_uid, order_number, title, qty, price, amount, note, paid, parent_id) "
                "SELECT id, COALESCE(NULLIF(TRIM(COALESCE(order_uid,'')),''), (SELECT NULLIF(TRIM(COALESCE(o.order_uid,'')), '') FROM router_cache_work_orders o WHERE o.number=work_order_services.order_number), CASE WHEN COALESCE(order_number,0)>0 THEN 'rewrite_' || order_number ELSE '' END), "
                "COALESCE(order_number,0), COALESCE(title,''), COALESCE(qty,1), COALESCE(price,0), COALESCE(amount,0), COALESCE(note,''), COALESCE(paid,1), COALESCE(parent_id,0) "
                "FROM work_order_services WHERE COALESCE(order_number,0) IN (SELECT number FROM router_cache_work_orders)"))) {
            if (errorText) *errorText = QStringLiteral("Active services router-cache seed failed: %1").arg(q.lastError().text());
            return false;
        }
    
        if (!q.exec(QStringLiteral(
                "INSERT OR REPLACE INTO router_cache_work_order_parts(id, part_uid, order_uid, order_number, ordered_at, delivery_date, installed_at, store, part_number, name, manufacturer, note, qty, price, amount, service_id, ordered_by_admin, paid, purchase_price, purchase_amount, markup_percent, manual_markup_override, markup_source, price_input_mode, pricing_updated_at, pricing_calc_version, status, installed, installed_by, request_uid, requested_at, requested_by, requested_by_uid, ordered_by, ordered_by_uid, order_comment, node_group, component, metadata_json, created_at, updated_at) "
                "SELECT id, COALESCE(part_uid,''), COALESCE(NULLIF(TRIM(COALESCE(order_uid,'')),''), (SELECT NULLIF(TRIM(COALESCE(o.order_uid,'')), '') FROM router_cache_work_orders o WHERE o.number=work_order_parts.order_number), CASE WHEN COALESCE(order_number,0)>0 THEN 'rewrite_' || order_number ELSE '' END), "
                "COALESCE(order_number,0), COALESCE(ordered_at,''), COALESCE(delivery_date,''), COALESCE(installed_at,''), COALESCE(store,''), part_number, COALESCE(name,''), COALESCE(manufacturer,''), COALESCE(note,''), COALESCE(qty,1), COALESCE(price,0), COALESCE(amount,0), COALESCE(service_id,0), COALESCE(ordered_by_admin,1), COALESCE(paid,0), COALESCE(purchase_price,0), COALESCE(purchase_amount,0), COALESCE(markup_percent,0), COALESCE(manual_markup_override,0), markup_source, price_input_mode, COALESCE(pricing_updated_at,''), COALESCE(pricing_calc_version,1), status, installed, installed_by, request_uid, requested_at, requested_by, requested_by_uid, ordered_by, ordered_by_uid, order_comment, node_group, component, metadata_json, created_at, updated_at "
                "FROM work_order_parts WHERE COALESCE(order_number,0) IN (SELECT number FROM router_cache_work_orders)"))) {
            if (errorText) *errorText = QStringLiteral("Active parts router-cache seed failed: %1").arg(q.lastError().text());
            return false;
        }
    
        if (tableExistsInDb(database, QStringLiteral("service_masters"))) {
            if (!q.exec(QStringLiteral(
                    "INSERT OR REPLACE INTO router_cache_service_masters(service_id, master_id, share_percent, role, master_name_closed) "
                    "SELECT COALESCE(service_id,0), COALESCE(master_id,''), COALESCE(share_percent,100), COALESCE(NULLIF(TRIM(role),''),'master'), COALESCE(master_name_closed,'') "
                    "FROM service_masters WHERE COALESCE(service_id,0) IN (SELECT id FROM router_cache_work_order_services)"))) {
                if (errorText) *errorText = QStringLiteral("Active service_masters router-cache seed failed: %1").arg(q.lastError().text());
                return false;
            }
        }
    
        if (tableExistsInDb(database, QStringLiteral("outsource_repairs"))) {
            if (!q.exec(QStringLiteral(
                    "INSERT OR REPLACE INTO router_cache_outsource_repairs(id, order_uid, order_number, sent_at, company, part_name, repair_cost, client_price, note, created_at, service_id) "
                    "SELECT id, COALESCE(NULLIF(TRIM(COALESCE(order_uid,'')),''), (SELECT NULLIF(TRIM(COALESCE(o.order_uid,'')), '') FROM router_cache_work_orders o WHERE o.number=outsource_repairs.order_number), CASE WHEN COALESCE(order_number,0)>0 THEN 'rewrite_' || order_number ELSE '' END), "
                    "COALESCE(order_number,0), COALESCE(sent_at,''), COALESCE(company,''), COALESCE(part_name,''), COALESCE(repair_cost,0), COALESCE(client_price,0), COALESCE(note,''), COALESCE(created_at,''), "
                    "CASE WHEN COALESCE(service_id,0)<>0 AND EXISTS(SELECT 1 FROM router_cache_work_order_services s WHERE s.id=outsource_repairs.service_id AND COALESCE(s.order_number,0)=COALESCE(outsource_repairs.order_number,0)) THEN service_id ELSE 0 END "
                    "FROM outsource_repairs WHERE COALESCE(order_number,0) IN (SELECT number FROM router_cache_work_orders)"))) {
                if (errorText) *errorText = QStringLiteral("Active outsource router-cache seed failed: %1").arg(q.lastError().text());
                return false;
            }
        }
    
        if (!q.exec("UPDATE router_cache_content_state SET ready=1 WHERE singleton=1") || q.numRowsAffected()!=1) {
            if(errorText)*errorText=q.lastError().isValid()?q.lastError().text():QStringLiteral("Router cache content state was not recorded");
            return false;
        }
        return true;
    }();
    if (rebuilt && transaction.exec(QStringLiteral("RELEASE SAVEPOINT archive_cache_rebuild")))
        return true;
    if (rebuilt && errorText) *errorText = transaction.lastError().text();
    if (!transaction.exec(QStringLiteral("ROLLBACK TO SAVEPOINT archive_cache_rebuild"))) {
        if (errorText) *errorText += QStringLiteral("; rollback failed: ") + transaction.lastError().text();
        return false;
    }
    if (!transaction.exec(QStringLiteral("RELEASE SAVEPOINT archive_cache_rebuild")) && errorText)
        *errorText += QStringLiteral("; savepoint release failed: ") + transaction.lastError().text();
    return false;
}

bool rebuildRouterCacheFromActiveWorkspaceData(QSqlDatabase database, QString* errorText)
{
    return writeActiveWorkspaceRouterCache(database,errorText,false);
}

bool mergeActiveWorkspaceIntoRouterCache(QSqlDatabase database, QString* errorText)
{
    return writeActiveWorkspaceRouterCache(database,errorText,true);
}
