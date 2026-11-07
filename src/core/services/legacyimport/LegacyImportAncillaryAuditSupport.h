#pragma once

#include "core/services/legacyimport/LegacyImportTypes.h"

class QSqlDatabase;

class LegacyImportAncillaryAuditSupport
{
public:
    static void appendTableFindings(QSqlDatabase db, LegacyImportAuditReport* report);
    static void appendBundleFindings(const LegacyImportBundle& bundle, LegacyImportAuditReport* report);
};
