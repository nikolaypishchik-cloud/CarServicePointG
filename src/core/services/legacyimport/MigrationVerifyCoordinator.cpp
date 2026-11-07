#include "core/services/legacyimport/MigrationVerifyCoordinator.h"

#include "core/services/legacyimport/LegacyImportVerifySupport.h"
#include "core/services/legacyimport/TargetConsistencyGate.h"

bool MigrationVerifyCoordinator::verifyCurrentStagingTarget(const LegacyImportBundle& bundle,
                                                            const LegacyImportAuditReport& auditReport,
                                                            LegacyImportVerifyReport* verifyReport,
                                                            QString* errorText)
{
    QString consistencyError;
    if (!TargetConsistencyGate::validateCurrentRuntime(auditReport, &consistencyError)) {
        if (errorText) *errorText = QStringLiteral("Target consistency gate failed: %1").arg(consistencyError);
        return false;
    }

    QString verifyError;
    if (!LegacyImportVerifySupport::runVerifyAgainstCurrentWorkspace(bundle, verifyReport, &verifyError)) {
        if (errorText) *errorText = verifyError;
        return false;
    }
    return true;
}
