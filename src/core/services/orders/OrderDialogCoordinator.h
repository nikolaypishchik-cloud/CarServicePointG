#pragma once

#include "core/services/orders/OrderSnapshotService.h"
#include "core/services/orders/OrderDialogValidationService.h"
#include "core/services/OrderWorkspaceService.h"

class OrderDialogCoordinator
{
public:
    struct SubmissionRequest {
        OrderWorkspaceInput input;
        QString requestedStatus;
        double worksSum = 0.0;
        double partsSum = 0.0;
        int requestedVisibleNumber = 0;
        QString pendingSyncFile;
        bool importSyncParts = true;
    };

    struct LoadResult {
        bool ok = false;
        OrderSnapshotService::OrderSnapshot snapshot;
        QString error;
    };

    struct MutationResult {
        bool ok = false;
        int orderNumber = -1;
        int displayNumber = -1;
        QString error;
    };

    static LoadResult loadEditableOrder(int orderNumber);
    static OrderDialogValidationService::ValidationResult validateDraftInput(const OrderWorkspaceInput& input);
    static OrderDialogValidationService::ValidationResult validateSubmissionInput(const SubmissionRequest& request);
    static MutationResult createDraftEditableOrder(const OrderWorkspaceInput& input, int requestedVisibleNumber);
    struct SubmissionResult {
        bool ok = false;
        int orderNumber = -1;
        int displayNumber = -1;
        int importedPartsCount = 0;
        QString error;
        QString syncWarning;
    };

    static MutationResult saveEditableOrder(const OrderWorkspaceInput& input, int requestedVisibleNumber);
    static SubmissionResult submitEditableOrder(const SubmissionRequest& request);
};
