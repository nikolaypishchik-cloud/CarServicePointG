#pragma once

#include "core/services/legacyimport/LegacyImportTypes.h"
#include "core/services/legacyimport/LegacyImportJournalSupport.h"

class MigrationPromoteCoordinator {
public:
    static bool reopenProjectedRuntime(const LegacyImportRunContext& runContext,
                                       QString* errorText = nullptr);
};
