#pragma once

#include "core/services/legacyimport/LegacyImportTypes.h"

class LegacyImportVerifySupport
{
public:
    static bool runVerifyAgainstCurrentWorkspace(const LegacyImportBundle& bundle, LegacyImportVerifyReport* report, QString* errorText = nullptr);
};
