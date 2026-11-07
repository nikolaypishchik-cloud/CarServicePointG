#include "core/services/orders/ServiceBatchMutationService.h"

#include "core/services/orders/OrderStatusService.h"
#include "infrastructure/db/sqlite/Db.h"
#include "core/services/MutationExecutor.h"

#include <QSqlDatabase>
#include <QSqlError>

namespace ServiceBatchMutationService {

ReplaceRowsResult replaceRows(int orderNumber,
                             const QList<WorkOrderServiceRow>& rows)
{
    return replaceRows(Database::db(), orderNumber, rows);
}


ReplaceRowsResult replaceRows(QSqlDatabase db,
                             int orderNumber,
                             const QList<WorkOrderServiceRow>& rows)
{
    ReplaceRowsResult result;
    if (!db.isValid()) {
        result.errorText = QStringLiteral("БД недоступна");
        return result;
    }
    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("ServiceBatchMutationService");
    request.entityType = QStringLiteral("work_order_service_batch");
    request.entityId = QString::number(orderNumber);
    request.operation = QStringLiteral("replace_rows");
    request.description = QString::fromUtf8("Повна заміна робіт наряду");
    request.changedTables = {QStringLiteral("work_order_services"), QStringLiteral("work_orders")};
    request.beginErrorFallback = QString::fromUtf8("Не вдалося відкрити транзакцію заміни робіт.");
    request.commitErrorFallback = QString::fromUtf8("Не вдалося завершити заміну робіт.");

    const auto execResult = MutationExecutor::execute(db, request, [&](QSqlDatabase& txDb, QString* errorText) {
        const QList<WorkOrderServiceRow> existing = ServiceRepository::listByOrder(txDb, orderNumber);
        for (const WorkOrderServiceRow& row : existing) {
            if (!ServiceRepository::remove(txDb, orderNumber, row.id, errorText)) {
                return false;
            }
        }

        for (const WorkOrderServiceRow& row : rows) {
            WorkOrderServiceRow saveRow = row;
            int newServiceId = 0;
            if (!ServiceRepository::insert(txDb, orderNumber, saveRow, &newServiceId, errorText)) {
                return false;
            }
            result.lastServiceId = newServiceId;
        }

        return OrderStatusService::promoteToInProgressIfNeeded(txDb, orderNumber, errorText);
    });
    if (!execResult.ok) {
        result.errorText = execResult.errorText;
        return result;
    }

    result.ok = true;
    return result;
}

} // namespace ServiceBatchMutationService
