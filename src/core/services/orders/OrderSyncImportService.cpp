#include "core/services/orders/OrderSyncImportService.h"

#include "core/backup/BackupCommitPublisher.h"
#include "core/repository/PartRepository.h"
#include "core/sync/SyncPayloadUtils.h"
#include "infrastructure/db/sqlite/Db.h"
#include "core/services/MutationExecutor.h"

#include <QSqlDatabase>
#include <QSqlError>

namespace {

bool fail(QString* errorText, const QString& text)
{
    if (errorText) *errorText = text;
    return false;
}

} // namespace

MutationResult OrderSyncImportService::importPartsFromPendingPayloadDetailed(int orderNumber,
                                                           const QString& payloadFilePath,
                                                           bool importParts,
                                                           int* importedPartsCount,
                                                           QString* errorText)
{
    if (importedPartsCount) *importedPartsCount = 0;

    const QString filePath = payloadFilePath.trimmed();
    if (filePath.isEmpty()) {
        MutationResult result;
        result.ok = true;
        return result;
    }

    const auto syncPayload = SyncPayloadUtils::loadPayloadFromFile(filePath);
    if (!syncPayload.isValid()) {
        MutationResult result;
        result.ok = true;
        return result;
    }
    if (!importParts || syncPayload.parts.isEmpty()) {
        MutationResult result;
        result.ok = true;
        return result;
    }

    QSqlDatabase db = Database::db();
    int importedCount = 0;
    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("OrderSyncImportService");
    request.entityType = QStringLiteral("work_order_parts_sync_batch");
    request.entityId = QString::number(orderNumber);
    request.operation = QStringLiteral("sync_import_parts");
    request.description = QString::fromUtf8("Імпорт запчастин із мобільного sync-пакета");
    request.changedTables = {QStringLiteral("work_order_parts")};
    request.beginErrorFallback = QString::fromUtf8("Не вдалося почати транзакцію імпорту запчастин із мобільного sync-пакета.");
    request.commitErrorFallback = QString::fromUtf8("Не вдалося завершити імпорт запчастин із мобільного sync-пакета.");

    const auto execResult = MutationExecutor::execute(db, request, [&](QSqlDatabase& txDb, QString* txError) {
        for (const WorkOrderPartRow& row : syncPayload.parts) {
            int newPartId = 0;
            if (!PartRepository::insert(txDb, orderNumber, row, &newPartId, txError)) {
                return false;
            }
            ++importedCount;
        }
        return true;
    });
    if (!execResult.ok) {
        MutationResult result;
        result.ok = false;
        result.errorText = execResult.errorText;
        if (errorText) *errorText = execResult.errorText;
        return result;
    }

    BackupCommitPublisher::publish(
        BackupCommitPublisher::makeEvent(QStringLiteral("OrderDialog"),
                                         QStringLiteral("order.parts"),
                                         QStringLiteral("part_batch"),
                                         QString::number(importedCount),
                                         Database::resolveOrderUid(orderNumber),
                                         orderNumber,
                                         QStringLiteral("sync_import_parts"),
                                         QString::fromUtf8("Імпортовано запчастини з мобільного sync-пакета"),
                                         {QStringLiteral("work_order_parts")}));

    if (importedPartsCount) *importedPartsCount = importedCount;
    return execResult;
}

bool OrderSyncImportService::importPartsFromPendingPayload(int orderNumber,
                                              const QString& payloadFilePath,
                                              bool importParts,
                                              int* importedPartsCount,
                                              QString* errorText)
{
    const auto result = importPartsFromPendingPayloadDetailed(orderNumber, payloadFilePath, importParts, importedPartsCount, errorText);
    if (!result.ok && errorText) *errorText = result.errorText;
    return result.ok;
}
