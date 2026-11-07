#pragma once

#include "core/services/legacyimport/LegacyImportTypes.h"

class LegacyImportAuditSupport
{
public:
    static bool runAudit(const LegacyImportBundle& bundle, LegacyImportAuditReport* report, QString* errorText = nullptr);
};
