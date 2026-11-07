#include "ui/tabs/orders/PartImportDraftUiSupport.h"

#include "core/services/orders/PartImportTextSupport.h"

namespace PartImportDraftUiSupport {

UiRow buildUiRow(const WorkPartsDialogExcelSupport::ImportedPartDraft& draft)
{
    UiRow row;
    // invoice/document date is ordered_at metadata; delivery_date stays empty until delivery/install flow.
    row.orderedAt = draft.orderedAt.trimmed();
    row.deliveryDate = draft.deliveryDate.trimmed();
    row.store = PartImportTextSupport::titleCaseWords(draft.store).trimmed();
    row.oemNumber = draft.oemNumber.trimmed();
    row.name = draft.name.trimmed();
    row.brandName = draft.brandName.trimmed();
    row.qtyText = draft.qtyText.trimmed();
    row.priceText = draft.priceText.trimmed();
    return row;
}

} // namespace PartImportDraftUiSupport
