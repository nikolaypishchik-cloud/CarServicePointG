#include "infrastructure/db/sqlite/DbCanonicalRewritePrimaryImportSupport.h"

#include "infrastructure/db/sqlite/DbFacadeSupport.h"
#include "core/services/legacyimport/LegacyOrderChildMapper.h"

#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>

namespace {

bool execLabeledSql(QSqlDatabase database, const QString& sql, const QString& label, QString* errorText)
{
    QSqlQuery q(database);
    if (q.exec(sql)) return true;
    if (errorText) *errorText = QStringLiteral("%1: %2").arg(label, q.lastError().text());
    return false;
}

}

bool importCanonicalRewritePrimaryTables(QSqlDatabase database,
                                         const CanonicalRewriteSourceSchema& legacySchema,
                                         QString* errorText)
{
    const QSet<QString>& srcMasters = legacySchema.masters;
    const QSet<QString>& srcOrders = legacySchema.orders;
    const QSet<QString>& srcServices = legacySchema.services;
    const QSet<QString>& srcParts = legacySchema.parts;

    const QSet<QString> dstMasters = tableColumnsForDb(database, QStringLiteral("masters"));
    const QSet<QString> dstOrders = tableColumnsForDb(database, QStringLiteral("work_orders"));
    const QSet<QString> dstServices = tableColumnsForDb(database, QStringLiteral("work_order_services"));
    const QSet<QString> dstParts = tableColumnsForDb(database, QStringLiteral("work_order_parts"));

    auto add = [](const QSet<QString>& dstCols, QStringList& insertCols, QStringList& selectExprs, const QString& dst, const QString& expr) {
        if (dstCols.contains(dst)) {
            insertCols << sqlQuoteIdent(dst);
            selectExprs << expr;
        }
    };
    // Preserve same-name fields which do not require conversion.
    const auto carryFields = [&](const QSet<QString>& source, const QSet<QString>& target,
                                 QStringList& columns, QStringList& expressions) {
        QStringList ordered=source.values();
        ordered.sort();
        for (const auto& field : ordered) {
            const auto quoted=sqlQuoteIdent(field);
            if (target.contains(field) && !columns.contains(quoted)) {
                columns<<quoted;
                expressions<<quoted;
            }
        }
    };
    const auto safeText = [](const QSet<QString>& srcCols, const QStringList& names, const QString& fallback = QStringLiteral("''")) {
        return nonNullTextSql(firstAvailableExpr(srcCols, names, fallback));
    };
    const auto safeInt = [](const QSet<QString>& srcCols, const QStringList& names, const QString& fallback = QStringLiteral("0")) {
        return nonNullIntSql(firstAvailableExpr(srcCols, names, fallback));
    };
    const auto safeReal = [](const QSet<QString>& srcCols, const QStringList& names, const QString& fallback = QStringLiteral("0")) {
        return nonNullRealSql(firstAvailableExpr(srcCols, names, fallback));
    };
    const auto safeBoolInt = [&](const QSet<QString>& srcCols, const QStringList& names, int fallbackValue) {
        return QStringLiteral("CASE WHEN COALESCE(%1,%2)<>0 THEN 1 ELSE 0 END")
                .arg(firstAvailableExpr(srcCols, names, QString::number(fallbackValue)), QString::number(fallbackValue));
    };

    if (!srcMasters.isEmpty()) {
        QStringList insertCols, selectExprs;
        add(dstMasters, insertCols, selectExprs, QStringLiteral("id"), firstAvailableExpr(srcMasters, {QStringLiteral("id")}, QStringLiteral("''")));
        add(dstMasters, insertCols, selectExprs, QStringLiteral("name"), safeText(srcMasters, {QStringLiteral("name")}));
        add(dstMasters, insertCols, selectExprs, QStringLiteral("role"), QStringLiteral("CASE WHEN TRIM(COALESCE(%1,''))='' THEN 'master' ELSE TRIM(COALESCE(%1,'')) END").arg(firstAvailableExpr(srcMasters, {QStringLiteral("role")}, QStringLiteral("'master'"))));
        add(dstMasters, insertCols, selectExprs, QStringLiteral("admin_percent"), safeReal(srcMasters, {QStringLiteral("admin_percent")}));
        add(dstMasters, insertCols, selectExprs, QStringLiteral("active"), safeBoolInt(srcMasters, {QStringLiteral("active")}, 1));
        carryFields(srcMasters,dstMasters,insertCols,selectExprs);
        if (!insertCols.isEmpty() && !execLabeledSql(database,
                QStringLiteral("INSERT INTO masters(%1) SELECT %2 FROM legacy_rewrite.masters").arg(insertCols.join(','), selectExprs.join(',')),
                QStringLiteral("import masters"), errorText)) {
            return false;
        }
    }

    {
        QStringList insertCols, selectExprs;
        const QString numberExpr = firstAvailableExpr(srcOrders, {QStringLiteral("number")}, QStringLiteral("0"));
        const QString colorExpr = firstAvailableExpr(srcOrders, {QStringLiteral("color"), QStringLiteral("car_color")}, QStringLiteral("''"));
        const QString fallbackUidExpr = QStringLiteral("('rewrite_' || CAST((%1) AS TEXT))").arg(numberExpr);
        const QString uidExpr = srcOrders.contains(QStringLiteral("order_uid"))
            ? QStringLiteral("COALESCE(NULLIF(order_uid,''), %1)").arg(fallbackUidExpr)
            : fallbackUidExpr;
        const QString visibleOrderExpr = srcOrders.contains(QStringLiteral("order_number"))
            ? QStringLiteral("CASE WHEN COALESCE(order_number,0)>0 THEN order_number ELSE %1 END").arg(numberExpr)
            : numberExpr;
        const QString openedDateExpr = QStringLiteral("TRIM(COALESCE(%1,''))")
            .arg(firstAvailableExpr(srcOrders, {QStringLiteral("opened_at")}, QStringLiteral("''")));
        const QString inferredYearExpr = QStringLiteral(
            "CASE WHEN substr(%1,5,1)='-' AND substr(%1,8,1)='-' THEN CAST(substr(%1,1,4) AS INTEGER) "
            "WHEN substr(%1,3,1)='.' AND substr(%1,6,1)='.' AND substr(%1,7,4) GLOB '[0-9][0-9][0-9][0-9]' "
            "THEN CAST(substr(%1,7,4) AS INTEGER) "
            "WHEN substr(%1,3,1)='.' AND substr(%1,6,1)='.' AND length(%1)>=8 "
            "THEN 2000 + CAST(substr(%1,7,2) AS INTEGER) ELSE CAST(strftime('%Y','now') AS INTEGER) END")
            .arg(openedDateExpr);
        const QString yearExpr = srcOrders.contains(QStringLiteral("order_year"))
            ? QStringLiteral("CASE WHEN COALESCE(order_year,0)>0 THEN order_year ELSE %1 END").arg(inferredYearExpr)
            : inferredYearExpr;
        add(dstOrders, insertCols, selectExprs, QStringLiteral("number"), numberExpr);
        add(dstOrders, insertCols, selectExprs, QStringLiteral("order_uid"), uidExpr);
        add(dstOrders, insertCols, selectExprs, QStringLiteral("order_number"), visibleOrderExpr);
        add(dstOrders, insertCols, selectExprs, QStringLiteral("order_year"), yearExpr);
        add(dstOrders, insertCols, selectExprs, QStringLiteral("opened_at"), safeText(srcOrders, {QStringLiteral("opened_at")}, QStringLiteral("datetime('now')")));
        add(dstOrders, insertCols, selectExprs, QStringLiteral("closed_at"), firstAvailableExpr(srcOrders, {QStringLiteral("closed_at")}, QStringLiteral("NULL")));
        add(dstOrders, insertCols, selectExprs, QStringLiteral("status"), safeText(srcOrders, {QStringLiteral("status")}, QStringLiteral("'open'")));
        add(dstOrders, insertCols, selectExprs, QStringLiteral("plate"), safeText(srcOrders, {QStringLiteral("plate")}));
        add(dstOrders, insertCols, selectExprs, QStringLiteral("plate_norm"), safeText(srcOrders, {QStringLiteral("plate_norm"), QStringLiteral("plate")}));
        add(dstOrders, insertCols, selectExprs, QStringLiteral("vin"), safeText(srcOrders, {QStringLiteral("vin")}));
        add(dstOrders, insertCols, selectExprs, QStringLiteral("car_year"), safeInt(srcOrders, {QStringLiteral("car_year")}));
        add(dstOrders, insertCols, selectExprs, QStringLiteral("odometer"), safeInt(srcOrders, {QStringLiteral("odometer")}));
        add(dstOrders, insertCols, selectExprs, QStringLiteral("engine_type"), safeText(srcOrders, {QStringLiteral("engine_type")}));
        add(dstOrders, insertCols, selectExprs, QStringLiteral("engine_volume"), safeText(srcOrders, {QStringLiteral("engine_volume")}));
        add(dstOrders, insertCols, selectExprs, QStringLiteral("fuel_type"), safeText(srcOrders, {QStringLiteral("fuel_type")}));
        add(dstOrders, insertCols, selectExprs, QStringLiteral("car"), safeText(srcOrders, {QStringLiteral("car")}));
        add(dstOrders, insertCols, selectExprs, QStringLiteral("client"), safeText(srcOrders, {QStringLiteral("client"), QStringLiteral("owner_name")}));
        add(dstOrders, insertCols, selectExprs, QStringLiteral("client_phone"), safeText(srcOrders, {QStringLiteral("client_phone")}));
        add(dstOrders, insertCols, selectExprs, QStringLiteral("master_id"), safeText(srcOrders, {QStringLiteral("master_id")}, QStringLiteral("(SELECT id FROM masters ORDER BY id LIMIT 1)")));
        add(dstOrders, insertCols, selectExprs, QStringLiteral("works_sum"), safeReal(srcOrders, {QStringLiteral("works_sum")}));
        add(dstOrders, insertCols, selectExprs, QStringLiteral("parts_sum"), safeReal(srcOrders, {QStringLiteral("parts_sum")}));
        add(dstOrders, insertCols, selectExprs, QStringLiteral("discount"), safeReal(srcOrders, {QStringLiteral("discount")}));
        add(dstOrders, insertCols, selectExprs, QStringLiteral("discount_mode"), safeText(srcOrders, {QStringLiteral("discount_mode")}, QStringLiteral("'legacy'")));
        add(dstOrders, insertCols, selectExprs, QStringLiteral("master_name_closed"), safeText(srcOrders, {QStringLiteral("master_name_closed")}));
        add(dstOrders, insertCols, selectExprs, QStringLiteral("car_model"), safeText(srcOrders, {QStringLiteral("car_model")}));
        add(dstOrders, insertCols, selectExprs, QStringLiteral("car_brand"), safeText(srcOrders, {QStringLiteral("car_brand")}));
        add(dstOrders, insertCols, selectExprs, QStringLiteral("car_kind"), safeText(srcOrders, {QStringLiteral("car_kind")}));
        add(dstOrders, insertCols, selectExprs, QStringLiteral("color"), nonNullTextSql(colorExpr));
        add(dstOrders, insertCols, selectExprs, QStringLiteral("car_color"), safeText(srcOrders, {QStringLiteral("car_color"), QStringLiteral("color")}, colorExpr));
        add(dstOrders, insertCols, selectExprs, QStringLiteral("owner_name"), safeText(srcOrders, {QStringLiteral("owner_name"), QStringLiteral("client")}));
        carryFields(srcOrders,dstOrders,insertCols,selectExprs);
        if (!execLabeledSql(database,
                QStringLiteral("INSERT INTO work_orders(%1) SELECT %2 FROM legacy_rewrite.work_orders WHERE %3 > 0").arg(insertCols.join(','), selectExprs.join(','), numberExpr),
                QStringLiteral("import work_orders"), errorText)) {
            return false;
        }
    }

    if (!srcServices.isEmpty()) {
        QStringList insertCols, selectExprs;
        const QString serviceOrderColumn = firstAvailableExpr(srcServices,
            {QStringLiteral("order_number"), QStringLiteral("work_order"),
             QStringLiteral("work_order_id"), QStringLiteral("work_order_number")}, QStringLiteral("0"));
        const QString serviceOrderExpr = serviceOrderColumn == QStringLiteral("0")
            ? serviceOrderColumn : QStringLiteral("source_service.%1").arg(serviceOrderColumn);
        const QString serviceOrderUidExpr = QStringLiteral("COALESCE(NULLIF(TRIM(COALESCE(%1,'')),''), CASE WHEN COALESCE(%2,0)>0 THEN 'rewrite_' || %2 ELSE '' END)")
            .arg(LegacyOrderChildMapper::activeParentOrderUidExpr(serviceOrderExpr), serviceOrderExpr);
        add(dstServices, insertCols, selectExprs, QStringLiteral("id"), firstAvailableExpr(srcServices, {QStringLiteral("id")}, QStringLiteral("rowid")));
        add(dstServices, insertCols, selectExprs, QStringLiteral("order_uid"), serviceOrderUidExpr);
        add(dstServices, insertCols, selectExprs, QStringLiteral("order_number"), serviceOrderExpr);
        add(dstServices, insertCols, selectExprs, QStringLiteral("title"), safeText(srcServices, {QStringLiteral("title"), QStringLiteral("name")}));
        add(dstServices, insertCols, selectExprs, QStringLiteral("qty"), safeReal(srcServices, {QStringLiteral("qty")}, QStringLiteral("1")));
        add(dstServices, insertCols, selectExprs, QStringLiteral("price"), safeReal(srcServices, {QStringLiteral("price")}));
        add(dstServices, insertCols, selectExprs, QStringLiteral("amount"), safeReal(srcServices, {QStringLiteral("amount")}, QStringLiteral("COALESCE(qty,1)*COALESCE(price,0)")));
        add(dstServices, insertCols, selectExprs, QStringLiteral("note"), safeText(srcServices, {QStringLiteral("note")}));
        add(dstServices, insertCols, selectExprs, QStringLiteral("paid"), safeBoolInt(srcServices, {QStringLiteral("paid")}, 1));
        const QString parentIdExpr = firstAvailableExpr(srcServices, {QStringLiteral("parent_id")}, QStringLiteral("NULL"));
        add(dstServices, insertCols, selectExprs, QStringLiteral("parent_id"), QStringLiteral("CASE WHEN COALESCE(%1,0)<=0 THEN NULL ELSE %1 END").arg(parentIdExpr));
        carryFields(srcServices,dstServices,insertCols,selectExprs);
        if (!insertCols.isEmpty() && !execLabeledSql(database,
                QStringLiteral("INSERT INTO work_order_services(%1) SELECT %2 FROM legacy_rewrite.work_order_services AS source_service WHERE %3 IN (SELECT number FROM work_orders)").arg(insertCols.join(','), selectExprs.join(','), serviceOrderExpr),
                QStringLiteral("import work_order_services"), errorText)) {
            return false;
        }
    }

    if (!srcParts.isEmpty()) {
        QStringList insertCols, selectExprs;
        const QString orderColumn = firstAvailableExpr(srcParts, {QStringLiteral("order_number"), QStringLiteral("work_order"), QStringLiteral("work_order_id"), QStringLiteral("work_order_number")}, QStringLiteral("0"));
        const QString orderExpr = orderColumn == QStringLiteral("0")
            ? orderColumn : QStringLiteral("source_part.%1").arg(orderColumn);
        const QString orderUidExpr = QStringLiteral("COALESCE(NULLIF(TRIM(COALESCE(%1,'')),''), CASE WHEN COALESCE(%2,0)>0 THEN 'rewrite_' || %2 ELSE '' END)")
            .arg(LegacyOrderChildMapper::activeParentOrderUidExpr(orderExpr), orderExpr);
        const QString orderedExpr = firstAvailableExpr(srcParts, {QStringLiteral("ordered_at"), QStringLiteral("date"), QStringLiteral("created_at")}, QStringLiteral("''"));
        const QString qtyExpr = firstAvailableExpr(srcParts, {QStringLiteral("qty"), QStringLiteral("quantity")}, QStringLiteral("1"));
        const QString priceExpr = firstAvailableExpr(srcParts, {QStringLiteral("price"), QStringLiteral("client_price"), QStringLiteral("sale_price"), QStringLiteral("purchase_price")}, QStringLiteral("0"));
        const QString amountExpr = firstAvailableExpr(srcParts, {QStringLiteral("amount"), QStringLiteral("sum"), QStringLiteral("total"), QStringLiteral("client_amount"), QStringLiteral("purchase_amount")}, QStringLiteral("(COALESCE(%1,1) * COALESCE(%2,0))").arg(qtyExpr, priceExpr));
        const QString deliveryExpr = firstAvailableExpr(srcParts, {QStringLiteral("delivery_date"), QStringLiteral("received_at"), QStringLiteral("arrival_date")}, QStringLiteral("''"));
        add(dstParts, insertCols, selectExprs, QStringLiteral("id"), firstAvailableExpr(srcParts, {QStringLiteral("id")}, QStringLiteral("rowid")));
        add(dstParts, insertCols, selectExprs, QStringLiteral("order_uid"), orderUidExpr);
        add(dstParts, insertCols, selectExprs, QStringLiteral("order_number"), orderExpr);
        add(dstParts, insertCols, selectExprs, QStringLiteral("ordered_at"), nonNullTextSql(orderedExpr));
        add(dstParts, insertCols, selectExprs, QStringLiteral("delivery_date"), nonNullTextSql(deliveryExpr));
        add(dstParts, insertCols, selectExprs, QStringLiteral("store"), safeText(srcParts, {QStringLiteral("store"), QStringLiteral("shop")}));
        add(dstParts, insertCols, selectExprs, QStringLiteral("part_number"), firstAvailableExpr(srcParts, {QStringLiteral("part_number"), QStringLiteral("oem"), QStringLiteral("oem_number"), QStringLiteral("article"), QStringLiteral("article_number")}, QStringLiteral("''")));
        add(dstParts, insertCols, selectExprs, QStringLiteral("name"), safeText(srcParts, {QStringLiteral("name"), QStringLiteral("title"), QStringLiteral("part_name")}));
        add(dstParts, insertCols, selectExprs, QStringLiteral("note"), safeText(srcParts, {QStringLiteral("note"), QStringLiteral("comment"), QStringLiteral("notes"), QStringLiteral("customer_note")}));
        add(dstParts, insertCols, selectExprs, QStringLiteral("qty"), nonNullRealSql(qtyExpr, QStringLiteral("1")));
        add(dstParts, insertCols, selectExprs, QStringLiteral("price"), nonNullRealSql(priceExpr));
        add(dstParts, insertCols, selectExprs, QStringLiteral("amount"), nonNullRealSql(amountExpr));
        add(dstParts, insertCols, selectExprs, QStringLiteral("service_id"), safeInt(srcParts, {QStringLiteral("service_id")}));
        add(dstParts, insertCols, selectExprs, QStringLiteral("ordered_by_admin"), safeBoolInt(srcParts, {QStringLiteral("ordered_by_admin"), QStringLiteral("is_ordered")}, 1));
        add(dstParts, insertCols, selectExprs, QStringLiteral("paid"), safeBoolInt(srcParts, {QStringLiteral("paid"), QStringLiteral("is_paid")}, 0));
        add(dstParts, insertCols, selectExprs, QStringLiteral("purchase_price"), safeReal(srcParts, {QStringLiteral("purchase_price"), QStringLiteral("cost_price"), QStringLiteral("price")}, priceExpr));
        add(dstParts, insertCols, selectExprs, QStringLiteral("purchase_amount"), safeReal(srcParts, {QStringLiteral("purchase_amount"), QStringLiteral("cost_amount"), QStringLiteral("amount")}, amountExpr));
        add(dstParts, insertCols, selectExprs, QStringLiteral("markup_percent"), safeReal(srcParts, {QStringLiteral("markup_percent"), QStringLiteral("markup"), QStringLiteral("margin_percent")}));
        add(dstParts, insertCols, selectExprs, QStringLiteral("manual_markup_override"), safeBoolInt(srcParts, {QStringLiteral("manual_markup_override"), QStringLiteral("manual_markup"), QStringLiteral("is_manual_markup")}, 0));
        add(dstParts, insertCols, selectExprs, QStringLiteral("markup_source"), QStringLiteral("CASE WHEN COALESCE(%1,0)<>0 THEN 'manual' ELSE COALESCE(NULLIF(TRIM(COALESCE(%2,'')),''),'legacy') END")
            .arg(firstAvailableExpr(srcParts, {QStringLiteral("manual_markup_override"), QStringLiteral("manual_markup"), QStringLiteral("is_manual_markup")}, QStringLiteral("0")),
                 firstAvailableExpr(srcParts, {QStringLiteral("markup_source")}, QStringLiteral("'legacy'"))));
        add(dstParts, insertCols, selectExprs, QStringLiteral("price_input_mode"), QStringLiteral("COALESCE(NULLIF(TRIM(COALESCE(%1,'')),''),'unknown')")
            .arg(firstAvailableExpr(srcParts, {QStringLiteral("price_input_mode")}, QStringLiteral("'unknown'"))));
        add(dstParts, insertCols, selectExprs, QStringLiteral("pricing_updated_at"), QStringLiteral("COALESCE(%1,'')")
            .arg(firstAvailableExpr(srcParts, {QStringLiteral("pricing_updated_at")}, QStringLiteral("''"))));
        add(dstParts, insertCols, selectExprs, QStringLiteral("pricing_calc_version"), QStringLiteral("CASE WHEN COALESCE(%1,0)>0 THEN COALESCE(%1,0) ELSE 1 END")
            .arg(firstAvailableExpr(srcParts, {QStringLiteral("pricing_calc_version")}, QStringLiteral("1"))));
        carryFields(srcParts,dstParts,insertCols,selectExprs);
        if (!insertCols.isEmpty() && !execLabeledSql(database,
                QStringLiteral("INSERT INTO work_order_parts(%1) SELECT %2 FROM legacy_rewrite.work_order_parts AS source_part WHERE %3 IN (SELECT number FROM work_orders)").arg(insertCols.join(','), selectExprs.join(','), orderExpr),
                QStringLiteral("import work_order_parts"), errorText)) {
            return false;
        }
    }

    return true;
}
