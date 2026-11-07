#pragma once

#include "core/backup/BackupDtos.h"

#include <QObject>
#include <QQueue>

#include <atomic>
#include <memory>

class QThread;

class BackupJobQueue : public QObject
{
    Q_OBJECT
public:
    explicit BackupJobQueue(QObject* parent = nullptr);

    void enqueue(const SnapshotJobContext& ctx);
    int queuedCount() const;
    bool isRunning() const;
    void setPaused(bool paused);
    bool isPaused() const;
    bool drainAndPauseForRestore(int waitMs, QString* errorText = nullptr);
    void resumeAfterRestore();
    void shutdown(int waitMs = 3000);

signals:
    void jobCompleted(const SnapshotJobContext& ctx, const SnapshotMetadata& meta);
    void jobFailed(const SnapshotJobContext& ctx, const QString& errorText);
    void queueChanged();

private:
    void processNext();

    QQueue<SnapshotJobContext> m_queue;
    bool m_running = false;
    bool m_paused = false;
    bool m_shuttingDown = false;
    QThread* m_activeThread = nullptr;
    std::shared_ptr<std::atomic_bool> m_activeCancelToken;
};
