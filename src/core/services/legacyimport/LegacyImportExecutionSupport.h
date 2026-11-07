#pragma once

#include "core/services/LegacyMigrationService.h"
#include "core/services/legacyimport/LegacyImportTypes.h"

class LegacyImportExecutionSupport
{
public:
    static bool runPreview(const QString& sourcePath, LegacyImportAuditReport* report, QString* errorText = nullptr, const LegacyImportProgressCallback& progress = LegacyImportProgressCallback());
    static bool runExecute(const QString& sourcePath, LegacyMigrationResult* result, QString* errorText = nullptr, const LegacyImportProgressCallback& progress = LegacyImportProgressCallback());
    static bool runVerify(const QString& sourcePath, LegacyImportVerifyReport* report, QString* errorText = nullptr);
};
