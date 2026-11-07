#include "core/backup/RecoveryMetadataResolver.h"

RecoveryMetadataResolution RecoveryMetadataResolver::resolve(const DraftPayload& draft)
{
    RecoveryMetadataResolution out;
    out.effectiveFormType = draft.formType.trimmed();

    if (out.effectiveFormType == QStringLiteral("OrderDialog")) {
        out.orderNumber = draft.entityId.toInt();
        out.valid = true;
        return out;
    }

    if (out.effectiveFormType == QStringLiteral("WorkPartsDialog")) {
        const int effectiveOrderNumber = draft.entityId.toInt() > 0
            ? draft.entityId.toInt()
            : draft.payloadData.value(QStringLiteral("orderNumber")).toInt(-1);
        if (effectiveOrderNumber <= 0) {
            out.error = QStringLiteral("Draft WorkPartsDialog не містить коректного номера наряду для відновлення.");
            return out;
        }
        out.orderNumber = effectiveOrderNumber;
        out.valid = true;
        return out;
    }

    out.error = QStringLiteral("Невідомий тип recovery форми: %1").arg(draft.formType);
    return out;
}

RecoveryUiRestoreRequest RecoveryMetadataResolver::buildUiRestoreRequest(const DraftPayload& draft, const RecoveryMetadataResolution& metadata)
{
    RecoveryUiRestoreRequest out;
    out.draft = draft;
    out.effectiveFormType = metadata.effectiveFormType;
    out.orderNumber = metadata.orderNumber;

    if (!metadata.valid) {
        return out;
    }

    if (metadata.effectiveFormType == QStringLiteral("OrderDialog")) {
        out.handled = true;
        out.target = RecoveryUiTarget::OrderEditor;
        out.restoreOrderDraft = true;
        return out;
    }

    if (metadata.effectiveFormType == QStringLiteral("WorkPartsDialog")) {
        out.handled = true;
        out.target = RecoveryUiTarget::WorkItemsEditor;
        out.restoreWorkPartsDraft = true;
        out.openWorkPartsTab = true;
        return out;
    }

    return out;
}
