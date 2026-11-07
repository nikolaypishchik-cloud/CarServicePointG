#pragma once

#include "ui/tabs/orders/WorkPartsDialogExcelSupport.h"

namespace PartImportDraftUiSupport {

struct UiRow {
    QString orderedAt;
    QString deliveryDate;
    QString store;
    QString oemNumber;
    QString name;
    QString brandName;
    QString qtyText;
    QString priceText;
};

UiRow buildUiRow(const WorkPartsDialogExcelSupport::ImportedPartDraft& draft);

} // namespace PartImportDraftUiSupport
