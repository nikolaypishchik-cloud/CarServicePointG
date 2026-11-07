#include "core/services/orders/OrderDialogCoordinator.h"
#include "core/services/AccessControlService.h"

#include "infrastructure/db/sqlite/Db.h"
#include "core/services/orders/OrderFinancialService.h"
#include "core/services/orders/OrderSyncImportService.h"
#include "core/sync/SyncPayloadUtils.h"

#include <QObject>

namespace {
bool authorizeWorkspaceWrite(const OrderWorkspaceInput& input, bool creating, QString* error)
{
    const auto db = Database::db();
    const QString permission = creating ? QStringLiteral("orders.create") : QStringLiteral("orders.edit");
    if (!AccessControlService::currentUserHasPermission(db, permission)
        || (!creating && !AccessControlService::canAccessOrder(db, input.number))
        || (AccessControlService::currentActorIsMaster()
            && input.masterId.trimmed() != AccessControlService::currentMasterId(db))) {
        if (error) *error = QString::fromUtf8("Немає дозволу на збереження цього наряду. Перевірте обліковий запис і призначеного майстра.");
        return false;
    }
    const QString status = input.status.trimmed().toLower();
    const bool closing = status == QStringLiteral("closed")
        || status == QString::fromUtf8("закритий") || status == QString::fromUtf8("закрыт");
    if (closing && !AccessControlService::currentUserHasPermission(db, QStringLiteral("orders.close"))) {
        if (error) *error = QString::fromUtf8("Немає дозволу на закриття наряду.");
        return false;
    }
    return true;
}
}

OrderDialogCoordinator::LoadResult OrderDialogCoordinator::loadEditableOrder(int orderNumber)
{
    LoadResult result;
    if (!AccessControlService::canAccessOrder(Database::db(), orderNumber)) {
        result.error = QString::fromUtf8("Немає доступу до наряду іншого майстра.");
        return result;
    }
    QString routeError;
    if (!Database::ensureOrderAvailableInActive(orderNumber, &routeError)
        || !Database::finalizeNewOrderWorkspaceState(orderNumber, &routeError)) {
        result.error = routeError;
        return result;
    }

    QString snapshotError;
    if (!OrderSnapshotService::loadByNumber(Database::db(), orderNumber, &result.snapshot, &snapshotError)) {
        result.error = snapshotError;
        return result;
    }

    result.ok = true;
    return result;
}


OrderDialogValidationService::ValidationResult OrderDialogCoordinator::validateDraftInput(const OrderWorkspaceInput& input)
{
    return OrderDialogValidationService::validateForDraft(input);
}

OrderDialogValidationService::ValidationResult OrderDialogCoordinator::validateSubmissionInput(const SubmissionRequest& request)
{
    return OrderDialogValidationService::validateForSubmission(request.input,
                                                               request.requestedStatus,
                                                               request.worksSum,
                                                               request.partsSum);
}

OrderDialogCoordinator::MutationResult OrderDialogCoordinator::createDraftEditableOrder(const OrderWorkspaceInput& input,
                                                                                        int requestedVisibleNumber)
{
    MutationResult result;
    if (!authorizeWorkspaceWrite(input, true, &result.error)) return result;
    int number = input.number;
    QString error;
    if (!OrderWorkspaceService::createDraftEditableOrder(input, &number, &error)) {
        result.error = error;
        return result;
    }

    result.ok = true;
    result.orderNumber = number;
    result.displayNumber = OrderFinancialService::visibleOrderNumber(Database::db(), number, requestedVisibleNumber);
    return result;
}

OrderDialogCoordinator::MutationResult OrderDialogCoordinator::saveEditableOrder(const OrderWorkspaceInput& input,
                                                                                int requestedVisibleNumber)
{
    MutationResult result;
    if (!authorizeWorkspaceWrite(input, input.number <= 0, &result.error)) return result;
    int number = input.number;
    QString error;
    if (!OrderWorkspaceService::saveEditableOrder(input, &number, &error)) {
        result.error = error;
        return result;
    }

    result.ok = true;
    result.orderNumber = number;
    result.displayNumber = OrderFinancialService::visibleOrderNumber(Database::db(), number, requestedVisibleNumber);
    return result;
}


OrderDialogCoordinator::SubmissionResult OrderDialogCoordinator::submitEditableOrder(const SubmissionRequest& request)
{
    SubmissionResult result;

    const auto validation = validateSubmissionInput(request);
    if (!validation.ok) {
        if (!validation.missingFields.isEmpty()) {
            result.error = OrderDialogValidationService::requiredFieldsWarning(validation.missingFields);
        } else if (validation.masterMissing) {
            result.error = QObject::tr("Оберіть майстра");
        } else {
            result.error = QObject::tr("Дані наряду не пройшли перевірку");
        }
        return result;
    }

    OrderWorkspaceInput input = request.input;
    input.status = validation.effectiveStatus;

    const auto saveResult = saveEditableOrder(input, request.requestedVisibleNumber);
    if (!saveResult.ok) {
        result.error = saveResult.error;
        return result;
    }

    result.ok = true;
    result.orderNumber = saveResult.orderNumber;
    result.displayNumber = saveResult.displayNumber;

    if (!request.pendingSyncFile.trimmed().isEmpty()) {
        QString syncError;
        int importedPartsCount = 0;
        const bool syncOk = OrderSyncImportService::importPartsFromPendingPayload(saveResult.orderNumber,
                                                                                  request.pendingSyncFile,
                                                                                  request.importSyncParts,
                                                                                  &importedPartsCount,
                                                                                  &syncError);
        if (!syncOk) {
            result.syncWarning = syncError;
        } else {
            SyncPayloadUtils::markPayloadProcessed(request.pendingSyncFile);
            result.importedPartsCount = importedPartsCount;
        }
    }

    return result;
}
