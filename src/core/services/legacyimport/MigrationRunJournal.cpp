#include "core/services/legacyimport/MigrationRunJournal.h"

bool MigrationRunJournal::begin(const LegacyImportBundle& bundle,
                                const LegacyImportAuditReport& auditReport,
                                LegacyImportRunContext* context,
                                QString* errorText)
{
    return LegacyImportJournalSupport::beginRun(bundle, auditReport, context, errorText);
}

bool MigrationRunJournal::stageStarted(const LegacyImportRunContext& context, const QString& stage, const QString& message, QString* errorText)
{
    return LegacyImportJournalSupport::logInfo(context, stage, QStringLiteral("STAGE_BEGIN | %1").arg(message), errorText);
}

bool MigrationRunJournal::stageSucceeded(const LegacyImportRunContext& context, const QString& stage, const QString& message, QString* errorText)
{
    return LegacyImportJournalSupport::logInfo(context, stage, QStringLiteral("STAGE_OK | %1").arg(message), errorText);
}

bool MigrationRunJournal::stageFailed(const LegacyImportRunContext& context, const QString& stage, const QString& message, QString* errorText)
{
    return LegacyImportJournalSupport::logError(context, stage, QStringLiteral("STAGE_FAIL | %1").arg(message), errorText);
}

bool MigrationRunJournal::info(const LegacyImportRunContext& context, const QString& stage, const QString& message, QString* errorText)
{
    return LegacyImportJournalSupport::logInfo(context, stage, message, errorText);
}

bool MigrationRunJournal::warning(const LegacyImportRunContext& context, const QString& stage, const QString& message, QString* errorText)
{
    return LegacyImportJournalSupport::logWarning(context, stage, message, errorText);
}

bool MigrationRunJournal::attachSnapshot(const LegacyImportRunContext& context, const LegacyImportSafetySnapshot& snapshot, QString* errorText)
{
    return LegacyImportJournalSupport::attachSnapshot(context, snapshot, errorText);
}

bool MigrationRunJournal::finishSuccess(const LegacyImportRunContext& context,
                                        const LegacyMigrationResult& result,
                                        const LegacyImportVerifyReport& verifyReport, QString* errorText)
{
    return LegacyImportJournalSupport::finishSuccess(context, result, verifyReport, errorText);
}

bool MigrationRunJournal::finishFailure(const LegacyImportRunContext& context,
                                        const QString& stage,
                                        const QString& message, QString* errorText)
{
    return LegacyImportJournalSupport::finishFailure(context, stage, message, errorText);
}
