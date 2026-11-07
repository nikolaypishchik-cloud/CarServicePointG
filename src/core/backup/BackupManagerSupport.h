#pragma once

#include "core/backup/BackupDtos.h"

#include <QPointer>
#include <QQueue>

#include <atomic>
#include <memory>

class BackupManager;

namespace BackupManagerSupport {

struct AsyncWorkerContext {
    QPointer<BackupManager> owner;
    quint64 generation = 0;
    std::shared_ptr<std::atomic_bool> cancelToken;
};

constexpr int kIdleBackupDelayMs = 10000;
constexpr int kBusyRetryDelayMs = 15000;
constexpr int kUiBusyRetryDelayMs = 30000;
constexpr int kQueueBusyRetryDelayMs = 5000;
constexpr int kDeferredAuditThrottleMs = 60000;
constexpr int kManifestFlushDelayMs = 1200;
constexpr int kCleanupDebounceMs = 120000;
constexpr int kShutdownFlushBudgetMs = 1800;
constexpr int kShutdownFlushMaxSnapshots = 1;
constexpr int kMaxBusyRetryCount = 4;
constexpr int kInteractiveBackupQuietWindowMs = 10000;

QString pendingBackupJournalPath();
bool writePendingBackupJournal(const QQueue<SnapshotJobContext>& pending, const QString& reason, QString* errorText = nullptr);
QQueue<SnapshotJobContext> readPendingBackupJournal(QString* errorText = nullptr);
void clearPendingBackupJournal();
void auditAndDiscardPendingShutdownBackups(QQueue<SnapshotJobContext>& pending, const QString& reason);

} // namespace BackupManagerSupport
