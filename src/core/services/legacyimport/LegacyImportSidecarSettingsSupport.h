#pragma once

#include "core/services/legacyimport/LegacyImportTypes.h"

struct LegacyImportSidecarApplyResult {
    int appliedSettings = 0;
    int appliedUiStateEntries = 0;
    QStringList infoMessages;
    QStringList warningMessages;
};

class LegacyImportSidecarSettingsSupport
{
public:
    static bool applyCompatibleSidecars(const LegacyImportBundle& bundle,
                                        LegacyImportSidecarApplyResult* result = nullptr,
                                        QString* errorText = nullptr);
};
