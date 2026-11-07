#pragma once

#include "core/services/legacyimport/LegacyImportTypes.h"

#include <QSqlDatabase>

class LegacyImportPostCheckSupport
{
public:
    static void appendPostImportChecks(QSqlDatabase db, LegacyImportVerifyReport* report);
};
