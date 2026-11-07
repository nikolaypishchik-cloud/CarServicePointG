#pragma once

#include <QString>

struct LegacyImportAuditReport;
struct LegacyImportBundle;
struct LegacyImportRunContext;
struct LegacyImportVerifyReport;
struct LegacyMigrationResult;

class LegacyImportReportSupport
{
public:
    static QString reportRootDir();
    static bool writePreviewArtifacts(const LegacyImportAuditReport& auditReport,
                                      QString* reportDirPath = nullptr,
                                      QString* errorText = nullptr);
    static bool writeVerifyArtifacts(const QString& sourcePath,
                                     const LegacyImportVerifyReport& verifyReport,
                                     QString* reportDirPath = nullptr,
                                     QString* errorText = nullptr);
    static bool writeExecuteArtifacts(const LegacyImportBundle& bundle,
                                      const LegacyImportRunContext& runContext,
                                      const LegacyImportAuditReport& auditReport,
                                      const LegacyMigrationResult& migrationResult,
                                      const LegacyImportVerifyReport& verifyReport,
                                      QString* reportDirPath = nullptr,
                                      QString* errorText = nullptr);
};
