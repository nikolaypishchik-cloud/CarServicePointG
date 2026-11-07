// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

#include "ui/tabs/settings/SettingsPageModulePrelude.h"

#include "core/util/StoreNameNormalizer.h"

// P2 split: settings local support is preserved through ordered implementation fragments.
#include "ui/tabs/settings/internal/SettingsLocalLegacyPreview.ipp"
#include "ui/tabs/settings/internal/SettingsLocalBackupRestore.ipp"
#include "ui/tabs/settings/internal/SettingsLocalUiActions.ipp"
