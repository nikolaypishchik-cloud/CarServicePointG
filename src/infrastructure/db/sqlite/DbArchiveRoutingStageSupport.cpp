// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

#include "infrastructure/db/sqlite/DbArchiveRoutingSupportModulePrelude.h"
#include "infrastructure/db/sqlite/ArchiveTransferJournal.h"
#include "infrastructure/db/sqlite/DbContext.h"
#include <QMap>


// P2 split: archive routing stage support is preserved through ordered implementation fragments.
#include "infrastructure/db/sqlite/internal/DbArchiveRoutingRegistryMirror.ipp"
#include "infrastructure/db/sqlite/internal/DbArchiveRoutingShardImport.ipp"
#include "infrastructure/db/sqlite/internal/DbArchiveRoutingWorkspaceState.ipp"
