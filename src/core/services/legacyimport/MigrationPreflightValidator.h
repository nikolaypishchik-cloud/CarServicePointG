#pragma once

#include "core/services/legacyimport/LegacyImportTypes.h"

class MigrationPreflightValidator {
public:
    static bool validateSourceBundle(const LegacyImportBundle& bundle,
                                     const LegacyImportAuditReport& auditReport,
                                     QString* errorText);

    static bool validateRuntimeTargets(QString* errorText);
};
