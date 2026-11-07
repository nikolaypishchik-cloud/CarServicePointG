#pragma once

#include "core/services/legacyimport/LegacyImportTypes.h"

class TargetConsistencyGate {
public:
    static bool validateCurrentRuntime(const LegacyImportAuditReport& report,
                                       QString* errorText);
};
