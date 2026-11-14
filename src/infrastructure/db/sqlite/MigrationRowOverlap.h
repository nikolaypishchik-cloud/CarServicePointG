#pragma once
#include "infrastructure/db/sqlite/DbFacadeSupport.h"

namespace MigrationRowOverlap {
// A local numeric key is reusable only when the source supplies a stable UID
// and every stored source field agrees, including NULLs and storage types.
inline QString exactRowPredicate(QSqlDatabase db, const QString& schema, const QString& table,
                                 const QString& uid, const QString& alias = QStringLiteral("s"))
{
    const auto source = tableColumnsForDb(db, table, schema);
    const auto target = tableColumnsForDb(db, table, QStringLiteral("main"));
    if (!source.contains(uid) || !target.contains(source)) return QStringLiteral("0");
    QStringList checks{QStringLiteral("length(trim(coalesce(%2.%1,'')))>0").arg(sqlQuoteIdent(uid), alias)};
    for (const auto& column : source) {
        const auto field = sqlQuoteIdent(column);
        checks << QStringLiteral("(typeof(%2.%1)=typeof(d.%1) AND %2.%1 COLLATE BINARY IS d.%1)").arg(field, alias);
    }
    return checks.join(QStringLiteral(" AND "));
}
inline QString match(QSqlDatabase db, const QString& sourceSchema, const QString& table, const QString& alias)
{
    QString identity = QStringLiteral("id");
    if (table == "work_orders") identity = QStringLiteral("order_uid");
    else if (table == "work_order_services") identity = QStringLiteral("service_uid");
    else if (table == "work_order_parts") identity = QStringLiteral("part_uid");
    else if (table == "service_masters") identity = QStringLiteral("service_id");
    else if (table == "planning_calendar") identity = QStringLiteral("day");
    else if (table == "settings") identity = QStringLiteral("key");
    return "EXISTS(SELECT 1 FROM main."+sqlQuoteIdent(table)+" d WHERE "
        +exactRowPredicate(db, sourceSchema, table, identity, alias)+")";
}
}
