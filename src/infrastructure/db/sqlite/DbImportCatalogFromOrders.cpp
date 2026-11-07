// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

#include "infrastructure/db/sqlite/DbImportSupportModulePrelude.h"


// P2 split: catalog import from orders is preserved through ordered implementation fragments.
#include "infrastructure/db/sqlite/internal/DbImportCatalogRunSetup.ipp"
#include "infrastructure/db/sqlite/internal/DbImportCatalogRows.ipp"
#include "infrastructure/db/sqlite/internal/DbImportCatalogPersistence.ipp"
