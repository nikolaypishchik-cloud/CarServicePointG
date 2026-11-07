#pragma once

#include "core/services/legacyimport/LegacyImportTypes.h"

class LegacyImportSchemaMapSupport
{
public:
    static QVector<LegacyImportEntitySchemaMap> buildSchemaMap();
};
