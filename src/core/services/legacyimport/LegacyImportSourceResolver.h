#pragma once

#include "core/services/legacyimport/LegacyImportTypes.h"

class LegacyImportSourceResolver
{
public:
    static bool resolve(const QString& sourcePath, LegacyImportBundle* bundle, QString* errorText = nullptr);
    static void cleanup(LegacyImportBundle* bundle);
};
