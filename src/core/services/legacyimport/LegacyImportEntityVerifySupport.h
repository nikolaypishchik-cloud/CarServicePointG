#pragma once

#include "core/services/legacyimport/LegacyImportTypes.h"

#include <QSqlDatabase>

class LegacyImportEntityVerifySupport
{
public:
    static void appendEntityResults(QSqlDatabase db, LegacyImportVerifyReport* report);
};
