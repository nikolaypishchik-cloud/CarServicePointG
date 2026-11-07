#include "core/services/orders/OrderStatusService.h"
#include "core/diagnostics/RuntimeLoggingControl.h"

#include "core/services/OrderIdentityResolver.h"
#include "core/services/PartsQueryService.h"
#include "core/services/PartOverduePolicy.h"
#include "infrastructure/db/sqlite/Db.h"

#include <QDateTime>
#include <QDebug>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

namespace {
QString normalizedWorkOrderStatus(const QString& value)
{
    const QString trimmed = value.trimmed().toLower();
    if (trimmed == QStringLiteral("closed") || trimmed == QString::fromUtf8("закритий") || trimmed == QString::fromUtf8("закрыт")) {
        return QStringLiteral("closed");
    }
    if (trimmed == QStringLiteral("cancelled") || trimmed == QString::fromUtf8("скасований") || trimmed == QString::fromUtf8("отменён") || trimmed == QString::fromUtf8("отменен")) {
        return QStringLiteral("cancelled");
    }
    if (trimmed == QStringLiteral("in_progress") || trimmed == QString::fromUtf8("в роботі") || trimmed == QString::fromUtf8("в работе")) {
        return QStringLiteral("in_progress");
    }
    return trimmed;
}
}

namespace OrderStatusService {

bool promoteToInProgressIfNeeded(int orderNumber, QString* errorText)
{
    return promoteToInProgressIfNeeded(Database::db(), orderNumber, errorText);
}

bool promoteToInProgressIfNeeded(const QString& connectionName, int orderNumber, QString* errorText)
{
    return promoteToInProgressIfNeeded(QSqlDatabase::database(connectionName), orderNumber, errorText);
}

bool promoteToInProgressIfNeeded(QSqlDatabase db, int orderNumber, QString* errorText)
{
    if (orderNumber <= 0) return true;
    if (!db.isValid()) {
        if (errorText) *errorText = QStringLiteral("БД недоступна");
        return false;
    }
    if (!db.isOpen() && !db.open()) {
        if (errorText) *errorText = db.lastError().text();
        return false;
    }

    QSqlQuery read(db);
    read.prepare(QStringLiteral("SELECT COALESCE(status,'') FROM work_orders WHERE number=?"));
    read.addBindValue(orderNumber);
    if (!read.exec()) {
        if (errorText) *errorText = read.lastError().text();
        return false;
    }
    if (!read.next()) return true;

    const QString normalizedStatus = normalizedWorkOrderStatus(read.value(0).toString());
    if (PartOverduePolicy::isClosedOrderStatus(PartOverduePolicy::normalizedOrderStatus(normalizedStatus))
        || normalizedStatus == QStringLiteral("cancelled") || normalizedStatus == QStringLiteral("in_progress")
        || normalizedStatus == QStringLiteral("deleted") || normalizedStatus == QStringLiteral("planned")
        || normalizedStatus == QString::fromUtf8("заплановано")) {
        return true;
    }

    const QString orderUid = OrderIdentityResolver::canonicalOrderUidForInternalNumber(db, orderNumber).trimmed();
    if (orderUid.isEmpty()) return true;

    QSqlQuery content(db);
    content.prepare(QStringLiteral(
        "SELECT CASE WHEN EXISTS(SELECT 1 FROM work_order_services WHERE order_uid=? LIMIT 1) "
        "OR EXISTS(SELECT 1 FROM work_order_parts WHERE order_uid=? LIMIT 1) THEN 1 ELSE 0 END"));
    content.addBindValue(orderUid);
    content.addBindValue(orderUid);
    if (!content.exec()) {
        if (errorText) *errorText = content.lastError().text();
        return false;
    }
    if (!content.next() || content.value(0).toInt() == 0) return true;

    QSqlQuery up(db);
    up.prepare(QStringLiteral("UPDATE work_orders SET status='in_progress' WHERE number=?"));
    up.addBindValue(orderNumber);
    if (!up.exec()) {
        if (errorText) *errorText = up.lastError().text();
        return false;
    }
    return true;
}


bool setClosed(QSqlDatabase db, int orderNumber, bool closed, QString* errorText)
{
    if (orderNumber <= 0) return true;
    if (!db.isValid()) {
        if (errorText) *errorText = QStringLiteral("БД недоступна");
        return false;
    }
    if (!db.isOpen() && !db.open()) {
        if (errorText) *errorText = db.lastError().text();
        return false;
    }

    QString masterName;
    if (closed) {
        QSqlQuery masterQuery(db);
        masterQuery.prepare(QStringLiteral("SELECT COALESCE((SELECT name FROM masters WHERE id=work_orders.master_id), '') FROM work_orders WHERE number=?"));
        masterQuery.addBindValue(orderNumber);
        if (!masterQuery.exec()) {
            if (errorText) *errorText = masterQuery.lastError().text();
            return false;
        }
        if (masterQuery.next()) masterName = masterQuery.value(0).toString();
    }

    QSqlQuery q(db);
    if (closed) {
        q.prepare(QStringLiteral("UPDATE work_orders SET status='closed', closed_at=?, master_name_closed=? WHERE number=?"));
        q.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
        q.addBindValue(masterName);
        q.addBindValue(orderNumber);
    } else {
        q.prepare(QStringLiteral("UPDATE work_orders SET status='open', closed_at=NULL WHERE number=?"));
        q.addBindValue(orderNumber);
    }
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }

    if (closed) {
        const QString orderUid = OrderIdentityResolver::canonicalOrderUidForInternalNumber(db, orderNumber).trimmed();
        if (!orderUid.isEmpty()) {
            QSqlQuery sm(db);
            sm.prepare(QStringLiteral("UPDATE service_masters SET master_name_closed=(SELECT name FROM masters WHERE id=service_masters.master_id) WHERE service_id IN (SELECT id FROM work_order_services WHERE order_uid=?)"));
            sm.addBindValue(orderUid);
            if (!sm.exec() && errorText && errorText->isEmpty()) *errorText = sm.lastError().text();
        }
    }
    PartsQueryService::invalidateCache();
    if (RuntimeLoggingControl::deepDebugEnabled()) qInfo().noquote() << QStringLiteral("ORDER_STATUS_OVERDUE_INVALIDATE orderNumber=") << orderNumber
                      << QStringLiteral(" closed=") << closed;
    return true;
}

} // namespace OrderStatusService
