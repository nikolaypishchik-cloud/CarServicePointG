#include "OrderBatchMutationService.h"

#include "infrastructure/db/sqlite/Db.h"
#include "core/services/orders/OrderStatusService.h"
#include "core/services/MutationExecutor.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include "core/services/AccessControlService.h"
#include "core/services/PartOverduePolicy.h"
#include "core/services/PartsQueryService.h"

MutationResult OrderBatchMutationService::reopen(int orderNumber, const QString& status)
{
    auto db = Database::db();
    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("OrderBatchMutationService");
    request.entityType = QStringLiteral("work_order");
    request.entityId = QString::number(orderNumber);
    request.operation = QStringLiteral("reopen_order");
    request.description = QString::fromUtf8("Повторне відкриття наряду: %1").arg(status);
    request.changedTables = {QStringLiteral("work_orders"), QStringLiteral("orders_registry")};
    auto result = MutationExecutor::execute(db, request, [&](QSqlDatabase& tx, QString* error) {
        if ((status != QStringLiteral("open") && status != QStringLiteral("in_progress"))
            || !AccessControlService::currentUserHasPermission(tx, QStringLiteral("orders.edit"))
            || !AccessControlService::canAccessOrder(tx, orderNumber)) {
            *error = QString::fromUtf8("Зміна статусу недоступна."); return false;
        }
        QSqlQuery read(tx);
        read.prepare(QStringLiteral("SELECT status, closed_at FROM work_orders WHERE number=?"));
        read.addBindValue(orderNumber);
        if (!read.exec()) { *error = read.lastError().text(); return false; }
        if (!read.next() || PartOverduePolicy::isActiveOrder(read.value(0).toString(), read.value(1).toString())) {
            *error = QString::fromUtf8("Наряд уже змінено або видалено. Оновіть список."); return false;
        }
        QSqlQuery update(tx);
        update.prepare(QStringLiteral("UPDATE work_orders SET status=?, closed_at=NULL WHERE number=? AND status=? AND COALESCE(closed_at,'')=?"));
        update.addBindValue(status);
        update.addBindValue(orderNumber);
        update.addBindValue(read.value(0));
        update.addBindValue(read.value(1).toString());
        if (!update.exec()) { *error = update.lastError().text(); return false; }
        if (update.numRowsAffected()!=1) { *error=QString::fromUtf8("Наряд змінено в іншому вікні. Оновіть список."); return false; }
        return true;
    });
    if (result.ok) {
        PartsQueryService::invalidateCache();
        Database::refreshOrderRegistry(orderNumber, nullptr);
    }
    return result;
}

MutationResult OrderBatchMutationService::setClosedDetailed(const QList<int>& orderNumbers,
                                                           bool closed)
{
    if (orderNumbers.isEmpty()) { MutationResult r; r.ok = true; r.sourceModule = QStringLiteral("OrderBatchMutationService"); r.entityType = QStringLiteral("work_order_batch"); r.operation = closed ? QStringLiteral("close_batch") : QStringLiteral("reopen_batch"); return r; }

    QSqlDatabase db = Database::db();
    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("OrderBatchMutationService");
    request.entityType = QStringLiteral("work_order_batch");
    request.entityId = QString::number(orderNumbers.size());
    request.operation = closed ? QStringLiteral("close_batch") : QStringLiteral("reopen_batch");
    request.description = closed ? QString::fromUtf8("Пакетне закриття нарядів") : QString::fromUtf8("Пакетне перевідкриття нарядів");
    request.changedTables = {QStringLiteral("work_orders"), QStringLiteral("orders_registry")};
    request.beginErrorFallback = QString::fromUtf8("Не можу відкрити транзакцію");
    request.commitErrorFallback = QString::fromUtf8("Не вдалося завершити зміну статусів нарядів.");

    const auto execResult = MutationExecutor::execute(db, request, [&](QSqlDatabase& txDb, QString* txError) {
        for (int orderNumber : orderNumbers) {
            if (!OrderStatusService::setClosed(txDb, orderNumber, closed, txError)) {
                return false;
            }
        }
        return true;
    });
    if (execResult.ok) {
        for (int orderNumber : orderNumbers) {
            Database::refreshOrderRegistry(orderNumber, nullptr);
        }
    }
    return execResult;
}

bool OrderBatchMutationService::setClosed(const QList<int>& orderNumbers,
                                          bool closed,
                                          QString* errorText)
{
    const auto result = setClosedDetailed(orderNumbers, closed);
    if (!result.ok && errorText) *errorText = result.errorText;
    return result.ok;
}
