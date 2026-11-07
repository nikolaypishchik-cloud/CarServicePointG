#include "runtime/RecoveryUiOrchestrator.h"
#include <memory>

#include "core/backup/BackupManager.h"
#include "core/backup/BackupPerformanceGuard.h"
#include "core/backup/RecoveryConflictDetector.h"
#include "core/backup/RecoveryCoordinator.h"
#include "runtime/RecoveryUiFactory.h"

RecoveryUiOrchestrator::RecoveryUiOrchestrator(std::shared_ptr<const RecoveryUiFactory> factory)
    : m_factory(std::move(factory))
{
}

RecoveryUiRestoreResult RecoveryUiOrchestrator::restoreDraft(const RecoveryUiRestoreRequest& request,
                                                             QWidget* parent,
                                                             BackupManager* backupManager,
                                                             RecoveryCoordinator* recoveryCoordinator) const
{
    RecoveryUiRestoreResult out;
    out.handled = request.handled;

    if (!request.handled) {
        return out;
    }
    if (!m_factory) {
        out.error = QStringLiteral("Recovery UI factory не налаштований.");
        return out;
    }

    QString localError;
    const DraftPayload& draft = request.draft;

    BackupPerformanceGuard::hold(backupManager,
                                 request.restoreWorkPartsDraft ? 3000 : 2500);

    std::unique_ptr<RecoveryDialogSession> session =
        m_factory->createSession(parent, request.target, request.orderNumber);
    if (!session) {
        out.error = QStringLiteral("Не вдалося створити recovery UI session для відновлення draft.");
        return out;
    }

    const RecoveryConflictCheck runtimeConflict = recoveryCoordinator
        ? recoveryCoordinator->detectRuntimeConflict(draft, session->committedBaseVersion())
        : RecoveryConflictDetector::checkRuntimeConflict(draft, session->committedBaseVersion());
    if (runtimeConflict.conflict) {
        out.conflict = true;
        out.warning = runtimeConflict.warning;
        return out;
    }

    if (request.restoreOrderDraft) {
        if (!session->restoreOrderDraft(draft.payloadData, &localError)) {
            out.error = localError.trimmed().isEmpty()
                ? QStringLiteral("Не вдалося відновити draft для OrderDialog/%1").arg(draft.entityId)
                : localError;
            return out;
        }
    }

    if (request.restoreWorkPartsDraft) {
        if (!session->ensureWorkPartsPage(&localError)) {
            out.error = localError.trimmed().isEmpty()
                ? QStringLiteral("Не вдалося підготувати вкладку робіт/запчастин для recovery order=%1").arg(request.orderNumber)
                : localError;
            return out;
        }

        if (!session->restoreWorkPartsDraft(draft.payloadData, &localError)) {
            out.error = localError.trimmed().isEmpty()
                ? QStringLiteral("Не вдалося відновити draft WorkPartsDialog/%1").arg(draft.entityId)
                : localError;
            return out;
        }
    }

    if (request.openWorkPartsTab) {
        session->openWorkPartsTab();
    }

    session->showAndActivate();
    out.restored = true;
    return out;
}
