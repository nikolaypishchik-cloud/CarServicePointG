#include "ui/common/ContextMenuAppearance.h"
#include "core/util/DateFormats.h"
// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

#include "ui/tabs/orders/WorkPartsDialogModulePrelude.h"

#include "core/services/orders/PartPricingEngine.h"
#include "core/services/WorkshopInventoryService.h"
#include "core/services/settings/FinanceSettingsService.h"

#include <algorithm>
// Extracted support/helpers for WorkPartsDialog decomposition (p4 continue)


// P2 split: work parts dialog support is preserved through ordered implementation fragments.
#include "ui/tabs/orders/internal/WorkPartsDialogBackupAndImport.ipp"
#include "ui/tabs/orders/internal/WorkPartsDialogRowState.ipp"
#include "ui/tabs/orders/internal/WorkPartsDialogPersistence.ipp"
