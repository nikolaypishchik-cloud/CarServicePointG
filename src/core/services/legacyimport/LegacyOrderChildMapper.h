#pragma once

#include <QString>

namespace LegacyOrderChildMapper {

QString activeParentOrderUidExpr(const QString& orderNumberExpr);
QString routerParentOrderUidExpr(const QString& orderNumberExpr);
QString activeParentOrderUidExprForTable(const QString& tableName, const QString& orderNumberColumnExpr);
QString routerParentOrderUidExprForTable(const QString& tableName, const QString& orderNumberColumnExpr);

} // namespace LegacyOrderChildMapper
