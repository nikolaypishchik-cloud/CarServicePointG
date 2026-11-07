#pragma once

#include "core/services/legacyimport/LegacyImportTypes.h"

#include <QSqlDatabase>

class LegacyImportEntityAuditSupport
{
public:
    static void appendEntityFindings(QSqlDatabase db, LegacyImportAuditReport* report);
};
