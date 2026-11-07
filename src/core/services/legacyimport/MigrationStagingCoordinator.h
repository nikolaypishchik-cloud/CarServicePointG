#pragma once

#include "core/services/legacyimport/LegacyImportTypes.h"
#include "core/services/legacyimport/LegacyImportJournalSupport.h"

class MigrationStagingCoordinator {
public:
    static bool importSourceIntoIsolatedTarget(const LegacyImportBundle& bundle,
                                               const LegacyImportRunContext& runContext,
                                               QString* errorText = nullptr);
};
