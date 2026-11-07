#pragma once

#include <QString>
#include <QVector>

struct LegacyImportAuditReport;
struct LegacyImportVerifyReport;
struct LegacyMigrationResult;

struct LegacyImportScenarioItem {
    QString id;
    QString title;
    QString status;
    QString note;
    bool automated = false;
};

class LegacyImportScenarioSupport
{
public:
    static QVector<LegacyImportScenarioItem> buildScenarioMatrix(const LegacyImportAuditReport* auditReport,
                                                                 const LegacyMigrationResult* migrationResult,
                                                                 const LegacyImportVerifyReport* verifyReport);
};
