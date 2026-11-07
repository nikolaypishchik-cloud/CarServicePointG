#pragma once

#include "core/backup/BackupContracts.h"
#include "core/backup/BackupDtos.h"

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVector>

class CrashRecoveryManager : public QObject
{
    Q_OBJECT
public:
    explicit CrashRecoveryManager(QObject* parent = nullptr);

    void start();
    void reloadPolicy();
    void markCleanShutdown();
    bool flushDirtyDraftsBeforeCriticalOperation(QString* errorText = nullptr);

    void registerParticipant(RecoveryDraftParticipant* participant);
    void unregisterParticipant(RecoveryDraftParticipant* participant);
    void notifyParticipantDirty(RecoveryDraftParticipant* participant);

    bool hasRecoverableSession() const;
    RecoverySessionState recoverableSession() const;
    QVector<DraftPayload> recoverableDrafts() const;
    RecoverySessionState currentSession() const;

    void cleanupExpiredData(QString* errorText = nullptr);
    void refreshRecoverableSessionState();
    void forgetRecoverableSession();

signals:
    void recoverableSessionChanged();

private:
    struct Entry {
        QPointer<QObject> object;
    };

    void updateHeartbeat();
    bool autosaveDirtyDrafts(bool acquireGate = true, QString* errorText = nullptr);
    DraftPayload buildPayload(RecoveryDraftParticipant* participant) const;
    void persistSessionState();
    void detectPreviousRecoverableSession();

    QVector<Entry> m_entries;
    RecoverySessionState m_currentSession;
    RecoverySessionState m_recoverableSession;
    QVector<DraftPayload> m_recoverableDrafts;
    QTimer m_heartbeatTimer;
    QTimer m_autosaveTimer;
};
