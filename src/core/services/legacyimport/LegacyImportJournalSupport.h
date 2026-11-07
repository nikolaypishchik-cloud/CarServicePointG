#pragma once

#include <QString>

struct LegacyImportAuditReport;
struct LegacyImportBundle;
struct LegacyImportSafetySnapshot;
struct LegacyImportVerifyReport;
struct LegacyMigrationResult;

struct LegacyImportRunContext {
    QString runId;
    QString logFilePath;
    QString sourceFingerprint;
};

class LegacyImportJournalSupport
{
public:
    static bool ensureCanImportFingerprint(const QString& sourceFingerprint, QString* errorText = nullptr);
    static bool beginRun(const LegacyImportBundle& bundle,
                         const LegacyImportAuditReport& auditReport,
                         LegacyImportRunContext* context,
                         QString* errorText = nullptr);
    static bool logInfo(const LegacyImportRunContext& context, const QString& stage, const QString& message, QString* errorText = nullptr);
    static bool logWarning(const LegacyImportRunContext& context, const QString& stage, const QString& message, QString* errorText = nullptr);
    static bool logError(const LegacyImportRunContext& context, const QString& stage, const QString& message, QString* errorText = nullptr);
    static bool attachSnapshot(const LegacyImportRunContext& context, const LegacyImportSafetySnapshot& snapshot, QString* errorText = nullptr);
    static bool finishSuccess(const LegacyImportRunContext& context,
                              const LegacyMigrationResult& result,
                              const LegacyImportVerifyReport& verifyReport, QString* errorText = nullptr);
    static bool finishFailure(const LegacyImportRunContext& context, const QString& failureStage, const QString& message, QString* errorText = nullptr);
};
