#include "core/services/LegacyMigrationService.h"

#include "core/services/DatabaseImportClassifier.h"
#include "core/services/legacyimport/LegacyImportExecutionSupport.h"

bool LegacyMigrationService::detectDatabaseKind(const QString& sourcePath,
                                                DatabaseImportClassification* classification,
                                                QString* errorText)
{
    return DatabaseImportClassifier::classify(sourcePath, classification, errorText);
}

bool LegacyMigrationService::previewLegacyImport(const QString& sourcePath,
                                                 LegacyImportAuditReport* report,
                                                 QString* errorText,
                                                 const LegacyImportProgressCallback& progress)
{
    return LegacyImportExecutionSupport::runPreview(sourcePath, report, errorText, progress);
}

bool LegacyMigrationService::auditBackupSource(const QString& sourcePath, LegacyImportAuditReport* report, QString* errorText)
{
    return previewLegacyImport(sourcePath, report, errorText);
}

bool LegacyMigrationService::validateBackupFile(const QString& sourcePath, int* orderCount, QString* errorText)
{
    LegacyImportAuditReport report;
    if (!previewLegacyImport(sourcePath, &report, errorText)) {
        return false;
    }
    if (report.orderCount <= 0) {
        if (errorText) *errorText = QStringLiteral("У вибраному legacy джерелі немає жодного наряду");
        return false;
    }
    if (orderCount) *orderCount = report.orderCount;
    return true;
}

bool LegacyMigrationService::executeLegacyImport(const QString& sourcePath,
                                                 LegacyMigrationResult* result,
                                                 QString* errorText,
                                                 const LegacyImportProgressCallback& progress)
{
    return LegacyImportExecutionSupport::runExecute(sourcePath, result, errorText, progress);
}

bool LegacyMigrationService::restoreBackupViaStaging(const QString& sourcePath, LegacyMigrationResult* result, QString* errorText)
{
    return executeLegacyImport(sourcePath, result, errorText);
}

bool LegacyMigrationService::verifyImportedSource(const QString& sourcePath, LegacyImportVerifyReport* report, QString* errorText)
{
    return LegacyImportExecutionSupport::runVerify(sourcePath, report, errorText);
}
