#include "core/services/legacyimport/LegacyOrderChildMapper.h"

namespace LegacyOrderChildMapper {

QString activeParentOrderUidExpr(const QString& orderNumberExpr)
{
    return QStringLiteral("(SELECT NULLIF(TRIM(COALESCE(wo.order_uid,'')), '') FROM work_orders wo WHERE wo.number=%1)").arg(orderNumberExpr);
}

QString routerParentOrderUidExpr(const QString& orderNumberExpr)
{
    return QStringLiteral("(SELECT NULLIF(TRIM(COALESCE(wo.order_uid,'')), '') FROM router_cache_work_orders wo WHERE wo.number=%1)").arg(orderNumberExpr);
}

QString activeParentOrderUidExprForTable(const QString& tableName, const QString& orderNumberColumnExpr)
{
    return QStringLiteral("(SELECT NULLIF(TRIM(COALESCE(wo.order_uid,'')), '') FROM work_orders wo WHERE wo.number=%1.%2)").arg(tableName, orderNumberColumnExpr);
}

QString routerParentOrderUidExprForTable(const QString& tableName, const QString& orderNumberColumnExpr)
{
    return QStringLiteral("(SELECT NULLIF(TRIM(COALESCE(wo.order_uid,'')), '') FROM router_cache_work_orders wo WHERE wo.number=%1.%2)").arg(tableName, orderNumberColumnExpr);
}

} // namespace LegacyOrderChildMapper
