#pragma once

#include <QString>
#include <QVector>

struct LegacyImportAuditReport;
struct LegacyImportVerifyReport;
struct LegacyMigrationResult;

struct LegacyImportAcceptanceItem {
    QString id;
    QString title;
    QString status;
    QString note;
};

class LegacyImportAcceptanceSupport
{
public:
    static QVector<LegacyImportAcceptanceItem> evaluate(const LegacyImportAuditReport* auditReport,
                                                        const LegacyMigrationResult* migrationResult,
                                                        const LegacyImportVerifyReport* verifyReport);
};
