#pragma once

#include "core/services/legacyimport/LegacyImportTypes.h"

class MigrationVerifyCoordinator {
public:
    static bool verifyCurrentStagingTarget(const LegacyImportBundle& bundle,
                                           const LegacyImportAuditReport& auditReport,
                                           LegacyImportVerifyReport* verifyReport,
                                           QString* errorText = nullptr);
};
