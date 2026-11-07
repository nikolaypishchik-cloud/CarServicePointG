#pragma once

#include <QSqlDatabase>
#include <QString>
#include <QVariant>

namespace OrderRoutingLookupService {

int orderYearFromQueryValue(const QVariant& value);
QString activeOrderUidForInternalNumber(QSqlDatabase database, int orderNumber);
int activeInternalNumberForOrderUid(QSqlDatabase database, const QString& orderUid);
QString routerCacheOrderUidForInternalNumber(QSqlDatabase database, int orderNumber);
int routerCacheInternalNumberForOrderUid(QSqlDatabase database, const QString& orderUid);
bool upsertWorkspaceOrderState(QSqlDatabase database,
                               int orderNumber,
                               const QString& origin,
                               const QString& sourceDb = QStringLiteral(""),
                               const QString& orderUid = QString());

} // namespace OrderRoutingLookupService
