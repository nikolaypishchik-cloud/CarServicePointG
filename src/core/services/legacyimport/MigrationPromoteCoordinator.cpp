#include "core/services/legacyimport/MigrationPromoteCoordinator.h"

#include "core/services/legacyimport/MigrationRunJournal.h"
#include "core/services/legacyimport/RuntimeRecoveryCoordinator.h"
#include "infrastructure/db/sqlite/Db.h"

bool MigrationPromoteCoordinator::reopenProjectedRuntime(const LegacyImportRunContext& runContext,
                                                         QString* errorText)
{
    if (!MigrationRunJournal::stageStarted(runContext, QStringLiteral("promote_prepare"), QStringLiteral("Prepare runtime projections before promote"), errorText)) return false;
    if (!RuntimeRecoveryCoordinator::prepareMaterializedRuntime(errorText)
            || !Database::openPrepared(errorText)
            || !Database::runStartupMigrationCheck(errorText)
            || !Database::runStartupSchemaCheck(errorText)
            || !Database::runStartupArchiveMaintenance(errorText)
            || !Database::runStartupRegistryRefresh(errorText)) {
        MigrationRunJournal::stageFailed(runContext, QStringLiteral("promote_prepare"), errorText ? *errorText : QStringLiteral("runtime reopen failed"));
        return false;
    }
    return MigrationRunJournal::stageSucceeded(runContext, QStringLiteral("promote_prepare"), QStringLiteral("Runtime projections rebuilt for staged target"), errorText);

}
