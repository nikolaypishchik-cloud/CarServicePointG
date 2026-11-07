#pragma once

#include "core/services/legacyimport/LegacyImportTypes.h"

class QSqlDatabase;

class LegacyImportAncillaryVerifySupport
{
public:
    static void appendEntityResults(QSqlDatabase db, const LegacyImportBundle& bundle, LegacyImportVerifyReport* report);
};
