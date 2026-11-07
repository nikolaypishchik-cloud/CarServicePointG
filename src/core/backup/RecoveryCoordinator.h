#pragma once

#include "core/backup/BackupDtos.h"
#include "core/backup/RecoveryUiContracts.h"

#include <QObject>

class CrashRecoveryManager;
class QWidget;

class RecoveryCoordinator : public QObject
{
    Q_OBJECT
public:
    explicit RecoveryCoordinator(CrashRecoveryManager* manager, QObject* parent = nullptr);

    void setRestoreHandler(RecoveryUiRestoreHandler handler);

    QVector<DraftPayload> recoverableDrafts() const;
    RecoveryRestoreResult restoreRecoverableDrafts(QWidget* parent = nullptr);
    RecoveryRestoreResult restoreSelectedDrafts(const QVector<DraftPayload>& drafts, QWidget* parent = nullptr);
    bool deleteDrafts(const QVector<DraftPayload>& drafts, QString* errorText = nullptr);
    bool deleteRecoverableSession(QString* errorText = nullptr);
    QString conflictReasonForDraft(const DraftPayload& draft) const;
    QString effectiveConflictState(const DraftPayload& draft) const;
    QString runtimeConflictReason(const DraftPayload& draft, const QString& currentCommittedVersion) const;
    RecoveryMetadataResolution resolveDraftMetadata(const DraftPayload& draft) const;
    RecoveryConflictCheck detectRuntimeConflict(const DraftPayload& draft, const QString& currentCommittedVersion) const;
    RecoveryUiRestoreRequest buildUiRestoreRequest(const DraftPayload& draft) const;

private:
    bool removeHandledDrafts(const QVector<DraftPayload>& drafts, QString* errorText = nullptr);

    CrashRecoveryManager* m_manager = nullptr;
    RecoveryUiRestoreHandler m_restoreHandler;
};
