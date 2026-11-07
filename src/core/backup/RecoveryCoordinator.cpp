#include "core/backup/RecoveryCoordinator.h"

#include "core/backup/CrashRecoveryManager.h"
#include "core/backup/RecoveryConflictDetector.h"
#include "core/backup/RecoveryDraftStore.h"
#include "core/backup/RecoveryMetadataResolver.h"
#include "infrastructure/audit/BackupRecoveryAuditTrail.h"
#include "core/services/PartsQueryService.h"

#include <QFileInfo>

RecoveryCoordinator::RecoveryCoordinator(CrashRecoveryManager* manager, QObject* parent)
    : QObject(parent)
    , m_manager(manager)
{
}

void RecoveryCoordinator::setRestoreHandler(RecoveryUiRestoreHandler handler)
{
    m_restoreHandler = std::move(handler);
}

QVector<DraftPayload> RecoveryCoordinator::recoverableDrafts() const
{
    return m_manager ? m_manager->recoverableDrafts() : QVector<DraftPayload>{};
}

QString RecoveryCoordinator::conflictReasonForDraft(const DraftPayload& draft) const
{
    return RecoveryConflictDetector::initialConflictReason(draft);
}

QString RecoveryCoordinator::effectiveConflictState(const DraftPayload& draft) const
{
    return RecoveryConflictDetector::effectiveConflictState(draft);
}

RecoveryMetadataResolution RecoveryCoordinator::resolveDraftMetadata(const DraftPayload& draft) const
{
    return RecoveryMetadataResolver::resolve(draft);
}

RecoveryConflictCheck RecoveryCoordinator::detectRuntimeConflict(const DraftPayload& draft, const QString& currentCommittedVersion) const
{
    return RecoveryConflictDetector::checkRuntimeConflict(draft, currentCommittedVersion);
}

RecoveryUiRestoreRequest RecoveryCoordinator::buildUiRestoreRequest(const DraftPayload& draft) const
{
    return RecoveryMetadataResolver::buildUiRestoreRequest(draft, resolveDraftMetadata(draft));
}

RecoveryRestoreResult RecoveryCoordinator::restoreRecoverableDrafts(QWidget* parent)
{
    return restoreSelectedDrafts(recoverableDrafts(), parent);
}

QString RecoveryCoordinator::runtimeConflictReason(const DraftPayload& draft, const QString& currentCommittedVersion) const
{
    return RecoveryConflictDetector::runtimeConflictReason(draft, currentCommittedVersion);
}

RecoveryRestoreResult RecoveryCoordinator::restoreSelectedDrafts(const QVector<DraftPayload>& drafts, QWidget* parent)
{
    RecoveryRestoreResult result;
    QVector<DraftPayload> restoredDrafts;

    for (const DraftPayload& draft : drafts) {
        BackupRecoveryAuditTrail::appendRecoveryEvent(QStringLiteral("started"),
                                                      QStringLiteral("restore_draft"),
                                                      draft.draftId,
                                                      QStringLiteral("RecoveryCoordinator"),
                                                      draft.entityType,
                                                      draft.entityId,
                                                      QStringLiteral("draft restore started"));
        const QString reason = conflictReasonForDraft(draft);
        if (!reason.isEmpty()) {
            ++result.skippedCount;
            ++result.conflictCount;
            result.warnings << reason;
            BackupRecoveryAuditTrail::appendRecoveryEvent(QStringLiteral("deferred"),
                                                          QStringLiteral("restore_draft"),
                                                          draft.draftId,
                                                          QStringLiteral("RecoveryCoordinator"),
                                                          draft.entityType,
                                                          draft.entityId,
                                                          QStringLiteral("draft restore deferred because initial conflict was detected"),
                                                          reason);
            continue;
        }

        if (!m_restoreHandler) {
            ++result.skippedCount;
            result.errors << QStringLiteral("Recovery restore handler не налаштований.");
            BackupRecoveryAuditTrail::appendRecoveryEvent(QStringLiteral("cancelled"),
                                                          QStringLiteral("restore_draft"),
                                                          draft.draftId,
                                                          QStringLiteral("RecoveryCoordinator"),
                                                          draft.entityType,
                                                          draft.entityId,
                                                          QStringLiteral("draft restore cancelled because restore handler is not configured"),
                                                          QStringLiteral("restore handler not configured"));
            continue;
        }

        const RecoveryUiRestoreRequest request = buildUiRestoreRequest(draft);
        if (!request.handled) {
            ++result.skippedCount;
            result.warnings << QStringLiteral("Невідомий тип recovery форми: %1").arg(draft.formType);
            BackupRecoveryAuditTrail::appendRecoveryEvent(QStringLiteral("cancelled"),
                                                          QStringLiteral("restore_draft"),
                                                          draft.draftId,
                                                          QStringLiteral("RecoveryCoordinator"),
                                                          draft.entityType,
                                                          draft.entityId,
                                                          QStringLiteral("draft restore cancelled because request target is unknown"),
                                                          draft.formType);
            continue;
        }

        const RecoveryMetadataResolution metadata = resolveDraftMetadata(draft);
        if (!metadata.valid) {
            ++result.skippedCount;
            ++result.conflictCount;
            result.warnings << metadata.error;
            BackupRecoveryAuditTrail::appendRecoveryEvent(QStringLiteral("failed"),
                                                          QStringLiteral("restore_draft"),
                                                          draft.draftId,
                                                          QStringLiteral("RecoveryCoordinator"),
                                                          draft.entityType,
                                                          draft.entityId,
                                                          QStringLiteral("draft restore failed metadata resolution"),
                                                          metadata.error);
            continue;
        }

        const RecoveryUiRestoreResult uiResult = m_restoreHandler(request, parent);
        if (!uiResult.handled) {
            ++result.skippedCount;
            result.warnings << QStringLiteral("Невідомий тип recovery форми: %1").arg(draft.formType);
            BackupRecoveryAuditTrail::appendRecoveryEvent(QStringLiteral("cancelled"),
                                                          QStringLiteral("restore_draft"),
                                                          draft.draftId,
                                                          QStringLiteral("RecoveryCoordinator"),
                                                          draft.entityType,
                                                          draft.entityId,
                                                          QStringLiteral("draft restore cancelled because UI did not handle request"),
                                                          draft.formType);
            continue;
        }
        if (uiResult.conflict) {
            ++result.skippedCount;
            ++result.conflictCount;
            if (!uiResult.warning.trimmed().isEmpty()) result.warnings << uiResult.warning;
            BackupRecoveryAuditTrail::appendRecoveryEvent(QStringLiteral("deferred"),
                                                          QStringLiteral("restore_draft"),
                                                          draft.draftId,
                                                          QStringLiteral("RecoveryCoordinator"),
                                                          draft.entityType,
                                                          draft.entityId,
                                                          QStringLiteral("draft restore deferred because runtime conflict was reported by UI"),
                                                          uiResult.warning);
            continue;
        }
        if (!uiResult.restored) {
            ++result.skippedCount;
            const QString finalError = (uiResult.error.trimmed().isEmpty()
                                  ? QStringLiteral("Не вдалося відновити draft для %1/%2").arg(draft.formType, draft.entityId)
                                  : uiResult.error);
            result.errors << finalError;
            BackupRecoveryAuditTrail::appendRecoveryEvent(QStringLiteral("failed"),
                                                          QStringLiteral("restore_draft"),
                                                          draft.draftId,
                                                          QStringLiteral("RecoveryCoordinator"),
                                                          draft.entityType,
                                                          draft.entityId,
                                                          QStringLiteral("draft restore failed in UI restore flow"),
                                                          finalError);
            continue;
        }

        if (!uiResult.warning.trimmed().isEmpty()) {
            result.warnings << uiResult.warning;
        }
        restoredDrafts.push_back(draft);
        ++result.restoredCount;
        BackupRecoveryAuditTrail::appendRecoveryEvent(QStringLiteral("completed"),
                                                      QStringLiteral("restore_draft"),
                                                      draft.draftId,
                                                      QStringLiteral("RecoveryCoordinator"),
                                                      draft.entityType,
                                                      draft.entityId,
                                                      QStringLiteral("draft restore completed"));
    }

    if (!restoredDrafts.isEmpty()) {
        PartsQueryService::invalidateCache();
        QString cleanupError;
        if (!removeHandledDrafts(restoredDrafts, &cleanupError) && !cleanupError.trimmed().isEmpty()) {
            result.warnings << cleanupError;
        }
    }
    return result;
}

bool RecoveryCoordinator::removeHandledDrafts(const QVector<DraftPayload>& drafts, QString* errorText)
{
    return deleteDrafts(drafts, errorText);
}

bool RecoveryCoordinator::deleteDrafts(const QVector<DraftPayload>& drafts, QString* errorText)
{
    bool ok = true;
    for (const DraftPayload& draft : drafts) {
        BackupRecoveryAuditTrail::appendRecoveryEvent(QStringLiteral("started"),
                                                      QStringLiteral("restore_draft"),
                                                      draft.draftId,
                                                      QStringLiteral("RecoveryCoordinator"),
                                                      draft.entityType,
                                                      draft.entityId,
                                                      QStringLiteral("draft restore started"));
        QString localError;
        if (!RecoveryDraftStore::removeDraft(draft, &localError)) {
            ok = false;
            if (errorText && errorText->trimmed().isEmpty()) {
                *errorText = localError;
            }
        }
    }

    if (m_manager && m_manager->hasRecoverableSession()) {
        const QString sessionDir = m_manager->recoverableSession().sessionDir;
        const QVector<DraftPayload> remaining = RecoveryDraftStore::listDrafts(sessionDir);
        if (remaining.isEmpty()) {
            QString clearError;
            deleteRecoverableSession(&clearError);
            if (!clearError.trimmed().isEmpty() && errorText && errorText->trimmed().isEmpty()) {
                *errorText = clearError;
            }
        } else {
            m_manager->refreshRecoverableSessionState();
        }
    }
    return ok;
}

bool RecoveryCoordinator::deleteRecoverableSession(QString* errorText)
{
    if (!m_manager || !m_manager->hasRecoverableSession()) return true;
    if (!RecoveryDraftStore::clearSession(m_manager->recoverableSession().sessionDir, errorText)) {
        return false;
    }
    m_manager->forgetRecoverableSession();
    return true;
}
