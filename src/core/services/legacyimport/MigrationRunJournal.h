#pragma once

#include "core/services/legacyimport/LegacyImportJournalSupport.h"

class MigrationRunJournal
{
public:
    static bool begin(const LegacyImportBundle& bundle,
                      const LegacyImportAuditReport& auditReport,
                      LegacyImportRunContext* context,
                      QString* errorText = nullptr);

    static bool stageStarted(const LegacyImportRunContext& context, const QString& stage, const QString& message, QString* errorText = nullptr);
    static bool stageSucceeded(const LegacyImportRunContext& context, const QString& stage, const QString& message, QString* errorText = nullptr);
    static bool stageFailed(const LegacyImportRunContext& context, const QString& stage, const QString& message, QString* errorText = nullptr);
    static bool info(const LegacyImportRunContext& context, const QString& stage, const QString& message, QString* errorText = nullptr);
    static bool warning(const LegacyImportRunContext& context, const QString& stage, const QString& message, QString* errorText = nullptr);
    static bool attachSnapshot(const LegacyImportRunContext& context, const LegacyImportSafetySnapshot& snapshot, QString* errorText = nullptr);
    static bool finishSuccess(const LegacyImportRunContext& context,
                              const LegacyMigrationResult& result,
                              const LegacyImportVerifyReport& verifyReport, QString* errorText = nullptr);
    static bool finishFailure(const LegacyImportRunContext& context,
                              const QString& stage,
                              const QString& message, QString* errorText = nullptr);
};
