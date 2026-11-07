#pragma once

#include <QString>
#include <QStringList>
#include <functional>

#include "core/services/DatabaseImportClassifier.h"
#include "core/services/legacyimport/LegacyImportTypes.h"


struct LegacyImportProgressState {
    QString stageKey;
    QString title;
    QString status;
    QString metricsCaption;
    int stepCurrent = 0;
    int stepTotal = 0;
    int mainPercent = -1;
    int processedCount = 0;
    int createdCount = 0;
    int updatedCount = 0;
    int skippedCount = 0;
    int orderCount = 0;
    int serviceCount = 0;
    int partCount = 0;
    int warningCount = 0;
    int errorCount = 0;
    bool indeterminate = false;
    bool finished = false;
};

using LegacyImportProgressCallback = std::function<void(const LegacyImportProgressState&)>;

struct LegacyMigrationResult {
    int sourceOrderCount = 0;
    int importedOrderCount = 0;
    int sourceServiceCount = 0;
    int importedServiceCount = 0;
    int sourcePartCount = 0;
    int importedPartCount = 0;
    int sourceServiceMasterCount = 0;
    int importedServiceMasterCount = 0;
    int sourceOutsourceCount = 0;
    int importedOutsourceCount = 0;
    int sourceMasterCount = 0;
    int importedMasterCount = 0;
    int sourceCatalogCount = 0;
    int importedCatalogCount = 0;
    int sourcePlanningCount = 0;
    int importedPlanningCount = 0;
    int sourceExpenseCount = 0;
    int importedExpenseCount = 0;
    int updatedCount = 0;
    int skippedCount = 0;
    int conflictCount = 0;
    int criticalErrorCount = 0;
    QString sourceKind;
    QString sourceDbPath;
    QString targetDbPath;
    QString snapshotDirPath;
    QString reportDirPath;
    QString summaryReportPath;
    QString summaryTextPath;
    QString validationReportPath;
    QString conflictsJsonPath;
    QString conflictsCsvPath;
    QString developerReportPath;
    QString userGuidePath;
    QString acceptanceReportPath;
    QString testScenarioReportPath;
    QString importLogPath;
    QStringList auditWarnings;
    QStringList verifyWarnings;
    QStringList verifyErrors;
};

class LegacyMigrationService
{
public:
    static bool detectDatabaseKind(const QString& sourcePath,
                                   DatabaseImportClassification* classification,
                                   QString* errorText = nullptr);
    static bool previewLegacyImport(const QString& sourcePath,
                                    LegacyImportAuditReport* report,
                                    QString* errorText = nullptr,
                                    const LegacyImportProgressCallback& progress = LegacyImportProgressCallback());
    static bool executeLegacyImport(const QString& sourcePath,
                                    LegacyMigrationResult* result,
                                    QString* errorText = nullptr,
                                    const LegacyImportProgressCallback& progress = LegacyImportProgressCallback());
    static bool verifyImportedSource(const QString& sourcePath, LegacyImportVerifyReport* report, QString* errorText = nullptr);

    // compatibility wrappers
    static bool auditBackupSource(const QString& sourcePath, LegacyImportAuditReport* report, QString* errorText = nullptr);
    static bool validateBackupFile(const QString& sourcePath, int* orderCount, QString* errorText = nullptr);
    static bool restoreBackupViaStaging(const QString& sourcePath, LegacyMigrationResult* result, QString* errorText = nullptr);
};
