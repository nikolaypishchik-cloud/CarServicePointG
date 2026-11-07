#pragma once

#include "core/backup/BackupDtos.h"
#include "core/backup/BackupEngineState.h"
#include "core/backup/BackupTelemetry.h"

#include <QObject>
#include <QQueue>
#include <QSet>
#include <QTimer>

class BackupJobQueue;
class QEvent;
class QThread;

class BackupManager : public QObject
{
    Q_OBJECT
public:
    explicit BackupManager(QObject* parent = nullptr);
    ~BackupManager() override;

    bool requestBackup(const DataCommitEvent& event, QString* errorText = nullptr);
    bool requestManualBackup(const QString& description, QString* errorText = nullptr);
    bool requestEmergencyBackup(const QString& description, QString* errorText = nullptr);
    bool createEmergencyBackupNow(const QString& description, SnapshotMetadata* outMeta = nullptr, QString* errorText = nullptr);
    bool validateSnapshots(BackupValidationSummary* summary = nullptr, QString* errorText = nullptr);
    BackupStatusSnapshot status() const;
    const BackupEngineState& engineState() const;
    BackupTelemetrySnapshot telemetry() const;
    void runCleanup(QString* errorText = nullptr);
    void reconcile(QString* errorText = nullptr);
    void holdInteractiveMode(int holdMs = 15000);
    void releaseInteractiveMode();
    bool prepareForRestore(QString* errorText = nullptr, int timeoutMs = 5000);
    void finishRestore();

signals:
    void backupStatusChanged();
    void userNotificationRequested(const QString& message, int timeoutMs);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void onJobCompleted(const SnapshotJobContext& ctx, const SnapshotMetadata& meta);
    void onJobFailed(const SnapshotJobContext& ctx, const QString& errorText);
    void scheduleCoalescedBackup(const SnapshotJobContext& ctx);
    void maybeKickPendingBackupSoon(int delayMs = 0);
    void enqueuePendingBackupIfPossible();
    bool shouldDeferAutomaticBackup() const;
    void flushPendingAutobackupOnShutdown();
    void scheduleCleanupDebounced();
    void flushPendingManifestWrites();
    void shutdown();
    void discardQueuedCallbacks();
    static DataCommitEvent synthesizeEvent(const QString& operationType, const QString& description);

    BackupJobQueue* m_queue = nullptr;
    QSet<QString> m_seenCommitIds;
    BackupStatusSnapshot m_status;
    BackupEngineState m_engineState;
    BackupTelemetry m_telemetry;
    QThread* m_cleanupThread = nullptr;
    QThread* m_manifestThread = nullptr;
    QQueue<SnapshotJobContext> m_pendingAutobackups;
    QVector<SnapshotMetadata> m_pendingManifestWrites;
    QDateTime m_lastEnqueueAt;
    QTimer* m_coalesceTimer = nullptr;
    QTimer* m_interactiveHoldTimer = nullptr;
    QTimer* m_cleanupDebounceTimer = nullptr;
    QTimer* m_manifestDebounceTimer = nullptr;
    int m_interactiveHoldDepth = 0;
    qint64 m_lastUiInteractionAtMs = 0;
    QString m_lastDeferredAuditKey;
    qint64 m_lastDeferredAuditAtMs = 0;
    bool m_shuttingDown = false;
    bool m_acceptAsyncApply = true;
    quint64 m_cleanupGeneration = 0;
    quint64 m_manifestGeneration = 0;
};
