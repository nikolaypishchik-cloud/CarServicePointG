#pragma once

#include "core/services/legacyimport/LegacyImportTypes.h"

class QSqlDatabase;

class LegacyImportDetailedAuditSupport
{
public:
    static void appendDetailedFindings(QSqlDatabase db, LegacyImportAuditReport* report);
};
