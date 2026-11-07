#pragma once

#include "core/services/legacyimport/LegacyImportTypes.h"

#include <QVector>

class LegacyImportAncillarySchemaMapSupport
{
public:
    static QVector<LegacyImportEntitySchemaMap> buildAncillarySchemaMap();
};
