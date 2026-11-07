#include "infrastructure/db/sqlite/DbCanonicalRewriteAncillaryImportSupport.h"

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

bool importCanonicalRewriteAncillaryTables(QSqlDatabase database,
                                           const CanonicalRewriteSourceSchema& legacySchema,
                                           QString* errorText)
{
    const QSet<QString>& srcServiceMasters = legacySchema.serviceMasters;
    const QSet<QString>& srcSettings = legacySchema.settings;
    const QSet<QString>& srcExpenses = legacySchema.expenses;
    const QSet<QString>& srcPlanning = legacySchema.planning;
    const QSet<QString>& srcCatalog = legacySchema.catalog;
    const QSet<QString>& srcOutsource = legacySchema.outsource;

    const QSet<QString> dstServiceMasters = tableColumnsForDb(database, QStringLiteral("service_masters"));
    const QSet<QString> dstSettings = tableColumnsForDb(database, QStringLiteral("settings"));
    const QSet<QString> dstExpenses = tableColumnsForDb(database, QStringLiteral("expenses"));
    const QSet<QString> dstPlanning = tableColumnsForDb(database, QStringLiteral("planning_calendar"));
    const QSet<QString> dstCatalog = tableColumnsForDb(database, QStringLiteral("parts_catalog"));
    const QSet<QString> dstOutsource = tableColumnsForDb(database, QStringLiteral("outsource_repairs"));

    auto add = [](const QSet<QString>& dstCols, QStringList& insertCols, QStringList& selectExprs, const QString& dst, const QString& expr) {
        if (dstCols.contains(dst)) {
            insertCols << sqlQuoteIdent(dst);
            selectExprs << expr;
        }
    };
    const auto safeText = [](const QSet<QString>& srcCols, const QStringList& names, const QString& fallback = QStringLiteral("''")) {
        return nonNullTextSql(firstAvailableExpr(srcCols, names, fallback));
    };
    const auto safeReal = [](const QSet<QString>& srcCols, const QStringList& names, const QString& fallback = QStringLiteral("0")) {
        return nonNullRealSql(firstAvailableExpr(srcCols, names, fallback));
    };

    if (!srcServiceMasters.isEmpty()) {
        QStringList insertCols, selectExprs;
        add(dstServiceMasters, insertCols, selectExprs, QStringLiteral("service_id"), firstAvailableExpr(srcServiceMasters, {QStringLiteral("service_id")}, QStringLiteral("0")));
        add(dstServiceMasters, insertCols, selectExprs, QStringLiteral("master_id"), safeText(srcServiceMasters, {QStringLiteral("master_id")}));
        add(dstServiceMasters, insertCols, selectExprs, QStringLiteral("share_percent"), safeReal(srcServiceMasters, {QStringLiteral("share_percent")}, QStringLiteral("100")));
        add(dstServiceMasters, insertCols, selectExprs, QStringLiteral("role"), QStringLiteral("CASE WHEN TRIM(COALESCE(%1,''))='' THEN 'master' ELSE TRIM(COALESCE(%1,'')) END").arg(firstAvailableExpr(srcServiceMasters, {QStringLiteral("role")}, QStringLiteral("'master'"))));
        add(dstServiceMasters, insertCols, selectExprs, QStringLiteral("master_name_closed"), safeText(srcServiceMasters, {QStringLiteral("master_name_closed")}));
        if (!insertCols.isEmpty() && !execLabeledSql(database,
                QStringLiteral("INSERT INTO service_masters(%1) SELECT %2 FROM legacy_rewrite.service_masters WHERE service_id IN (SELECT id FROM work_order_services)").arg(insertCols.join(','), selectExprs.join(',')),
                QStringLiteral("import service_masters"), errorText)) {
            return false;
        }
    }

    if (!srcOutsource.isEmpty()) {
        QStringList insertCols, selectExprs;
        const QString outsourceOrderColumn = firstAvailableExpr(srcOutsource,
            {QStringLiteral("order_number"), QStringLiteral("work_order"),
             QStringLiteral("work_order_id"), QStringLiteral("work_order_number")}, QStringLiteral("0"));
        const QString outsourceOrderExpr = outsourceOrderColumn == QStringLiteral("0")
            ? outsourceOrderColumn : QStringLiteral("source_outsource.%1").arg(outsourceOrderColumn);
        const QString outsourceOrderUidExpr = QStringLiteral("COALESCE(NULLIF(TRIM(COALESCE(%1,'')),''), CASE WHEN COALESCE(%2,0)>0 THEN 'rewrite_' || %2 ELSE '' END)").arg(LegacyOrderChildMapper::activeParentOrderUidExpr(outsourceOrderExpr), outsourceOrderExpr);
        add(dstOutsource, insertCols, selectExprs, QStringLiteral("id"), firstAvailableExpr(srcOutsource, {QStringLiteral("id")}, QStringLiteral("rowid")));
        add(dstOutsource, insertCols, selectExprs, QStringLiteral("order_uid"), outsourceOrderUidExpr);
        add(dstOutsource, insertCols, selectExprs, QStringLiteral("order_number"), outsourceOrderExpr);
        add(dstOutsource, insertCols, selectExprs, QStringLiteral("sent_at"), safeText(srcOutsource, {QStringLiteral("sent_at")}));
        add(dstOutsource, insertCols, selectExprs, QStringLiteral("company"), safeText(srcOutsource, {QStringLiteral("company")}));
        add(dstOutsource, insertCols, selectExprs, QStringLiteral("part_name"), safeText(srcOutsource, {QStringLiteral("part_name")}));
        add(dstOutsource, insertCols, selectExprs, QStringLiteral("repair_cost"), safeReal(srcOutsource, {QStringLiteral("repair_cost")}));
        add(dstOutsource, insertCols, selectExprs, QStringLiteral("client_price"), safeReal(srcOutsource, {QStringLiteral("client_price")}));
        add(dstOutsource, insertCols, selectExprs, QStringLiteral("note"), safeText(srcOutsource, {QStringLiteral("note")}));
        add(dstOutsource, insertCols, selectExprs, QStringLiteral("created_at"), safeText(srcOutsource, {QStringLiteral("created_at")}, QStringLiteral("datetime('now')")));
        add(dstOutsource, insertCols, selectExprs, QStringLiteral("service_id"), QStringLiteral("CASE WHEN COALESCE(%1,0)<=0 THEN NULL ELSE %1 END").arg(firstAvailableExpr(srcOutsource, {QStringLiteral("service_id")}, QStringLiteral("NULL"))));
        if (!insertCols.isEmpty() && !execLabeledSql(database,
                QStringLiteral("INSERT INTO outsource_repairs(%1) SELECT %2 FROM legacy_rewrite.outsource_repairs AS source_outsource WHERE %3 IN (SELECT number FROM work_orders)").arg(insertCols.join(','), selectExprs.join(','), outsourceOrderExpr),
                QStringLiteral("import outsource_repairs"), errorText)) {
            return false;
        }
    }

    if (!srcCatalog.isEmpty()) {
        QStringList insertCols, selectExprs;
        const auto nonNullTextExpr = [](const QString& expr) { return QStringLiteral("TRIM(COALESCE(%1,''))").arg(expr); };
        const auto nonNullIntExpr = [](const QString& expr) { return QStringLiteral("COALESCE(%1,0)").arg(expr); };
        const auto nonNullRealExpr = [](const QString& expr) { return QStringLiteral("COALESCE(%1,0)").arg(expr); };
        const QString catalogPartExpr = nonNullTextExpr(firstAvailableExpr(srcCatalog, {QStringLiteral("part_number"), QStringLiteral("oem_number"), QStringLiteral("article"), QStringLiteral("article_number")}, QStringLiteral("''")));
        add(dstCatalog, insertCols, selectExprs, QStringLiteral("id"), firstAvailableExpr(srcCatalog, {QStringLiteral("id")}, QStringLiteral("rowid")));
        add(dstCatalog, insertCols, selectExprs, QStringLiteral("part_number"), catalogPartExpr);
        add(dstCatalog, insertCols, selectExprs, QStringLiteral("name"), nonNullTextExpr(firstAvailableExpr(srcCatalog, {QStringLiteral("name"), QStringLiteral("title")}, QStringLiteral("''"))));
        add(dstCatalog, insertCols, selectExprs, QStringLiteral("brand"), nonNullTextExpr(firstAvailableExpr(srcCatalog, {QStringLiteral("brand"), QStringLiteral("manufacturer")}, QStringLiteral("''"))));
        add(dstCatalog, insertCols, selectExprs, QStringLiteral("price"), nonNullRealExpr(firstAvailableExpr(srcCatalog, {QStringLiteral("price"), QStringLiteral("client_price"), QStringLiteral("sale_price")}, QStringLiteral("0"))));
        add(dstCatalog, insertCols, selectExprs, QStringLiteral("vehicle_brand"), nonNullTextExpr(firstAvailableExpr(srcCatalog, {QStringLiteral("vehicle_brand"), QStringLiteral("brand_auto")}, QStringLiteral("''"))));
        add(dstCatalog, insertCols, selectExprs, QStringLiteral("vehicle_model"), nonNullTextExpr(firstAvailableExpr(srcCatalog, {QStringLiteral("vehicle_model"), QStringLiteral("model_auto")}, QStringLiteral("''"))));
        add(dstCatalog, insertCols, selectExprs, QStringLiteral("year_from"), nonNullIntExpr(firstAvailableExpr(srcCatalog, {QStringLiteral("year_from")}, QStringLiteral("0"))));
        add(dstCatalog, insertCols, selectExprs, QStringLiteral("year_to"), nonNullIntExpr(firstAvailableExpr(srcCatalog, {QStringLiteral("year_to")}, QStringLiteral("0"))));
        add(dstCatalog, insertCols, selectExprs, QStringLiteral("fuel_type"), nonNullTextExpr(firstAvailableExpr(srcCatalog, {QStringLiteral("fuel_type"), QStringLiteral("fuel")}, QStringLiteral("''"))));
        add(dstCatalog, insertCols, selectExprs, QStringLiteral("group_name"), nonNullTextExpr(firstAvailableExpr(srcCatalog, {QStringLiteral("group_name"), QStringLiteral("group_key"), QStringLiteral("kind")}, QStringLiteral("''"))));
        add(dstCatalog, insertCols, selectExprs, QStringLiteral("supplier"), nonNullTextExpr(firstAvailableExpr(srcCatalog, {QStringLiteral("supplier"), QStringLiteral("vendor")}, QStringLiteral("''"))));
        add(dstCatalog, insertCols, selectExprs, QStringLiteral("note"), nonNullTextExpr(firstAvailableExpr(srcCatalog, {QStringLiteral("note"), QStringLiteral("node_tags"), QStringLiteral("comment")}, QStringLiteral("''"))));
        if (!insertCols.isEmpty() && !execLabeledSql(database,
                QStringLiteral("INSERT INTO parts_catalog(%1) SELECT %2 FROM legacy_rewrite.parts_catalog").arg(insertCols.join(','), selectExprs.join(',')),
                QStringLiteral("import parts_catalog"), errorText)) {
            return false;
        }
    }

    if (!srcExpenses.isEmpty()) {
        QStringList insertCols, selectExprs;
        add(dstExpenses, insertCols, selectExprs, QStringLiteral("id"), firstAvailableExpr(srcExpenses, {QStringLiteral("id")}, QStringLiteral("rowid")));
        add(dstExpenses, insertCols, selectExprs, QStringLiteral("period_from"), safeText(srcExpenses, {QStringLiteral("period_from")}));
        add(dstExpenses, insertCols, selectExprs, QStringLiteral("period_to"), safeText(srcExpenses, {QStringLiteral("period_to")}));
        add(dstExpenses, insertCols, selectExprs, QStringLiteral("elec_prev"), safeReal(srcExpenses, {QStringLiteral("elec_prev")}));
        add(dstExpenses, insertCols, selectExprs, QStringLiteral("elec_curr"), safeReal(srcExpenses, {QStringLiteral("elec_curr")}));
        add(dstExpenses, insertCols, selectExprs, QStringLiteral("elec_tariff"), safeReal(srcExpenses, {QStringLiteral("elec_tariff")}));
        add(dstExpenses, insertCols, selectExprs, QStringLiteral("cleaning"), safeReal(srcExpenses, {QStringLiteral("cleaning")}));
        add(dstExpenses, insertCols, selectExprs, QStringLiteral("heating"), safeReal(srcExpenses, {QStringLiteral("heating")}));
        add(dstExpenses, insertCols, selectExprs, QStringLiteral("rent"), safeReal(srcExpenses, {QStringLiteral("rent")}));
        add(dstExpenses, insertCols, selectExprs, QStringLiteral("amortization"), safeReal(srcExpenses, {QStringLiteral("amortization")}));
        add(dstExpenses, insertCols, selectExprs, QStringLiteral("sw_opendatabot"), safeReal(srcExpenses, {QStringLiteral("sw_opendatabot")}));
        add(dstExpenses, insertCols, selectExprs, QStringLiteral("sw_tecalliance"), safeReal(srcExpenses, {QStringLiteral("sw_tecalliance")}));
        add(dstExpenses, insertCols, selectExprs, QStringLiteral("other_amount"), safeReal(srcExpenses, {QStringLiteral("other_amount"), QStringLiteral("other_expenses")}));
        add(dstExpenses, insertCols, selectExprs, QStringLiteral("cleaning_note"), safeText(srcExpenses, {QStringLiteral("cleaning_note")}));
        add(dstExpenses, insertCols, selectExprs, QStringLiteral("software_note"), safeText(srcExpenses, {QStringLiteral("software_note")}));
        add(dstExpenses, insertCols, selectExprs, QStringLiteral("heating_note"), safeText(srcExpenses, {QStringLiteral("heating_note")}));
        add(dstExpenses, insertCols, selectExprs, QStringLiteral("amortization_note"), safeText(srcExpenses, {QStringLiteral("amortization_note")}));
        add(dstExpenses, insertCols, selectExprs, QStringLiteral("rent_note"), safeText(srcExpenses, {QStringLiteral("rent_note")}));
        add(dstExpenses, insertCols, selectExprs, QStringLiteral("other_note"), safeText(srcExpenses, {QStringLiteral("other_note"), QStringLiteral("notes_json")}));
        add(dstExpenses, insertCols, selectExprs, QStringLiteral("elec_kwh"), safeReal(srcExpenses, {QStringLiteral("elec_kwh")}));
        add(dstExpenses, insertCols, selectExprs, QStringLiteral("elec_cost"), safeReal(srcExpenses, {QStringLiteral("elec_cost")}));
        add(dstExpenses, insertCols, selectExprs, QStringLiteral("created_at"), safeText(srcExpenses, {QStringLiteral("created_at")}, QStringLiteral("datetime('now')")));
        if (!insertCols.isEmpty() && !execLabeledSql(database,
                QStringLiteral("INSERT INTO expenses(%1) SELECT %2 FROM legacy_rewrite.expenses").arg(insertCols.join(','), selectExprs.join(',')),
                QStringLiteral("import expenses"), errorText)) {
            return false;
        }
    }

    if (!srcPlanning.isEmpty() && dstPlanning.contains(QStringLiteral("day")) && dstPlanning.contains(QStringLiteral("note"))) {
        if (!execLabeledSql(database,
                QStringLiteral("INSERT INTO planning_calendar(day, note) SELECT %1, %2 FROM legacy_rewrite.planning_calendar")
                    .arg(safeText(srcPlanning, {QStringLiteral("day")}), safeText(srcPlanning, {QStringLiteral("note")})),
                QStringLiteral("import planning_calendar"), errorText)) {
            return false;
        }
    }

    if (!srcSettings.isEmpty() && dstSettings.contains(QStringLiteral("key")) && dstSettings.contains(QStringLiteral("value"))) {
        if (!execLabeledSql(database,
                QStringLiteral("INSERT INTO settings(\"key\", value) SELECT \"key\", value FROM legacy_rewrite.settings WHERE \"key\" NOT LIKE 'db_canonical_rewrite_%'"),
                QStringLiteral("import settings"), errorText)) {
            return false;
        }
    }

    return true;
}
