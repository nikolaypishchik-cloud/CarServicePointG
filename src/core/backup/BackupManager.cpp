#include "AppVersion.h"
#include "core/backup/BackupManager.h"
#include "core/backup/BackupManagerSupport.h"

#include "core/backup/BackupJobQueue.h"
#include "core/backup/BackupLogging.h"
#include "core/backup/BackupManifestService.h"
#include "core/backup/BackupPaths.h"
#include "core/backup/BackupPolicy.h"
#include "core/backup/BackupSnapshotService.h"
#include "core/backup/DataProtectionCoordinator.h"
#include "infrastructure/db/sqlite/ScopedConnectionManager.h"
#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/audit/BackupRecoveryAuditTrail.h"
#include "runtime/AppRuntime.h"
#include "runtime/AsyncThreadScope.h"
#include "runtime/RuntimeJobDispatcher.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QFileInfo>
#include <QGuiApplication>
#include <QThread>
#include <QUuid>
#include <memory>


bool BackupManager::requestBackup(const DataCommitEvent& event, QString* errorText)
{
    BackupRecoveryAuditTrail::appendBackupEvent(QStringLiteral("requested"),
                                                event.operationType,
                                                !event.commitId.trimmed().isEmpty() ? event.commitId : event.eventId,
                                                event.module,
                                                event.entityType,
                                                event.entityId,
                                                QStringLiteral("backup requested after committed database change"),
                                                QString(),
                                                event.changedTables);
    if (m_shuttingDown) {
        const QString localError = QStringLiteral("backup manager is shutting down");
        BackupRecoveryAuditTrail::appendBackupEvent(QStringLiteral("cancelled"),
                                                    event.operationType,
                                                    !event.commitId.trimmed().isEmpty() ? event.commitId : event.eventId,
                                                    event.module,
                                                    event.entityType,
                                                    event.entityId,
                                                    QStringLiteral("backup request rejected during shutdown"),
                                                    localError,
                                                    event.changedTables);
        if (errorText) *errorText = localError;
        return false;
    }
    if (DataProtectionCoordinator::isRestoreRunning()) {
        const QString localError = QStringLiteral("backup request rejected: restore is running");
        BackupRecoveryAuditTrail::appendBackupEvent(QStringLiteral("deferred"),
                                                    event.operationType,
                                                    !event.commitId.trimmed().isEmpty() ? event.commitId : event.eventId,
                                                    event.module,
                                                    event.entityType,
                                                    event.entityId,
                                                    QStringLiteral("backup request blocked by exclusive restore gate"),
                                                    localError,
                                                    event.changedTables);
        if (errorText) *errorText = localError;
        return false;
    }
    QElapsedTimer timer;
    timer.start();
    const BackupPolicy::Config cfg = BackupPolicy::load();
    if (!cfg.enabled) {
        const QString localError = QStringLiteral("autobackup вимкнено");
        BackupRecoveryAuditTrail::appendBackupEvent(QStringLiteral("cancelled"),
                                                    event.operationType,
                                                    !event.commitId.trimmed().isEmpty() ? event.commitId : event.eventId,
                                                    event.module,
                                                    event.entityType,
                                                    event.entityId,
                                                    QStringLiteral("backup request rejected because autobackup is disabled"),
                                                    localError,
                                                    event.changedTables);
        if (errorText) *errorText = localError;
        return false;
    }
    if (!event.commitId.trimmed().isEmpty() && m_seenCommitIds.contains(event.commitId)) {
        BackupLogging::info(QStringLiteral("backup deduplicated commit=%1").arg(event.commitId));
        BackupRecoveryAuditTrail::appendBackupEvent(QStringLiteral("cancelled"),
                                                    event.operationType,
                                                    event.commitId,
                                                    event.module,
                                                    event.entityType,
                                                    event.entityId,
                                                    QStringLiteral("backup request deduplicated"),
                                                    QString(),
                                                    event.changedTables);
        if (errorText) errorText->clear();
        return true;
    }
    if (!event.commitId.trimmed().isEmpty()) {
        m_seenCommitIds.insert(event.commitId);
    }

    SnapshotJobContext ctx;
    ctx.event = event;
    if (!ctx.event.committedAt.isValid()) {
        ctx.event.committedAt = QDateTime::currentDateTime();
    }
    ctx.requestedBy = QStringLiteral("BackupManager");
    ctx.source = QStringLiteral("autobackup");
    ctx.enqueuedAtMs = QDateTime::currentMSecsSinceEpoch();
    const int coalescedCount = m_pendingAutobackups.size();
    m_pendingAutobackups.clear();
    m_pendingAutobackups.enqueue(ctx);
    // Debounce from the last successful commit event, not from the previous snapshot start.
    // This keeps automatic backup coalescing deterministic during rapid save/import bursts.
    m_lastEnqueueAt = QDateTime::currentDateTime();
    BackupLogging::info(QStringLiteral("backup queued/coalesced commit=%1 replaced_pending=%2 pending=%3 debounce_ms=%4")
                            .arg(event.commitId,
                                 QString::number(coalescedCount),
                                 QString::number(m_pendingAutobackups.size()),
                                 QString::number(BackupManagerSupport::kIdleBackupDelayMs)));
    BackupRecoveryAuditTrail::appendBackupEvent(QStringLiteral("deferred"),
                                                event.operationType,
                                                !event.commitId.trimmed().isEmpty() ? event.commitId : event.eventId,
                                                event.module,
                                                event.entityType,
                                                event.entityId,
                                                QStringLiteral("backup queued for coalesced execution"),
                                                QString(),
                                                event.changedTables);
    m_telemetry.recordEnqueueMs(timer.elapsed());
    scheduleCoalescedBackup(ctx);
    if (errorText) errorText->clear();
    emit backupStatusChanged();
    return true;
}

bool BackupManager::requestManualBackup(const QString& description, QString* errorText)
{
    if (m_shuttingDown) {
        if (errorText) *errorText = QStringLiteral("backup manager is shutting down");
        return false;
    }
    if (DataProtectionCoordinator::isRestoreRunning()) {
        if (errorText) *errorText = QStringLiteral("Manual backup відкладено: виконується restore");
        BackupRecoveryAuditTrail::appendBackupEvent(QStringLiteral("deferred"),
                                                    QStringLiteral("manual_backup"),
                                                    QStringLiteral("manual_backup_restore_lock"),
                                                    QStringLiteral("BackupManager"),
                                                    QStringLiteral("database"),
                                                    QString(),
                                                    QStringLiteral("manual backup blocked by exclusive restore gate"));
        return false;
    }
    SnapshotJobContext ctx;
    ctx.event = synthesizeEvent(QStringLiteral("manual_backup"), description);
    ctx.requestedBy = QStringLiteral("BackupManager");
    ctx.source = QStringLiteral("manual");
    ctx.enqueuedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_lastEnqueueAt = QDateTime::currentDateTime();
    BackupRecoveryAuditTrail::appendBackupEvent(QStringLiteral("started"),
                                                ctx.event.operationType,
                                                ctx.event.commitId,
                                                ctx.event.module,
                                                ctx.event.entityType,
                                                ctx.event.entityId,
                                                QStringLiteral("manual backup queued for immediate execution"),
                                                QString(),
                                                ctx.event.changedTables);
    m_queue->enqueue(ctx);
    if (errorText) errorText->clear();
    return true;
}

bool BackupManager::requestEmergencyBackup(const QString& description, QString* errorText)
{
    if (m_shuttingDown) {
        const QString localError = QStringLiteral("backup manager is shutting down");
        BackupRecoveryAuditTrail::appendBackupEvent(QStringLiteral("cancelled"),
                                                    QStringLiteral("emergency_backup"),
                                                    QStringLiteral("emergency_shutdown"),
                                                    QStringLiteral("BackupManager"),
                                                    QStringLiteral("database"),
                                                    QString(),
                                                    QStringLiteral("emergency backup request rejected during shutdown"),
                                                    localError);
        if (errorText) *errorText = localError;
        return false;
    }
    if (DataProtectionCoordinator::isRestoreRunning()) {
        if (errorText) *errorText = QStringLiteral("Emergency backup queue відкладено: виконується restore");
        BackupRecoveryAuditTrail::appendBackupEvent(QStringLiteral("deferred"),
                                                    QStringLiteral("emergency_backup"),
                                                    QStringLiteral("emergency_backup_restore_lock"),
                                                    QStringLiteral("BackupManager"),
                                                    QStringLiteral("database"),
                                                    QString(),
                                                    QStringLiteral("queued emergency backup blocked by exclusive restore gate"));
        return false;
    }
    SnapshotJobContext ctx;
    ctx.event = synthesizeEvent(QStringLiteral("emergency_backup"), description);
    ctx.source = QStringLiteral("emergency");
    ctx.emergency = true;
    ctx.enqueuedAtMs = QDateTime::currentMSecsSinceEpoch();
    BackupRecoveryAuditTrail::appendBackupEvent(QStringLiteral("started"),
                                                ctx.event.operationType,
                                                ctx.event.commitId,
                                                ctx.event.module,
                                                ctx.event.entityType,
                                                ctx.event.entityId,
                                                QStringLiteral("emergency backup queued for immediate execution"),
                                                QString(),
                                                ctx.event.changedTables);
    m_queue->enqueue(ctx);
    if (errorText) errorText->clear();
    return true;
}

bool BackupManager::createEmergencyBackupNow(const QString& description, SnapshotMetadata* outMeta, QString* errorText)
{
    auto emergencyGate = DataProtectionCoordinator::isRestoreRunning()
        ? DataProtectionCoordinator::Scope()
        : DataProtectionCoordinator::tryEnter(DataProtectionCoordinator::Operation::EmergencyBackup, description, errorText, 0);
    if (!DataProtectionCoordinator::isRestoreRunning() && !emergencyGate.isActive()) {
        return false;
    }

    SnapshotJobContext ctx;
    ctx.event = synthesizeEvent(QStringLiteral("emergency_backup"), description);
    ctx.source = QStringLiteral("emergency");
    ctx.emergency = true;

    BackupRecoveryAuditTrail::appendBackupEvent(QStringLiteral("started"),
                                                ctx.event.operationType,
                                                ctx.event.commitId,
                                                ctx.event.module,
                                                ctx.event.entityType,
                                                ctx.event.entityId,
                                                QStringLiteral("emergency backup started immediately"),
                                                QString(),
                                                ctx.event.changedTables);

    SnapshotMetadata meta;
    QString localError;
    if (!BackupSnapshotService::createSnapshot(ctx, &meta, &localError)) {
        m_engineState.degraded = true;
        m_engineState.lastEngineError = localError;
        m_engineState.updatedAt = QDateTime::currentDateTime();
        m_status.lastBackupAt = QDateTime::currentDateTime();
        m_status.lastBackupStatus = QStringLiteral("failed");
        m_status.lastError = localError;
        BackupRecoveryAuditTrail::appendBackupEvent(QStringLiteral("failed"),
                                                    ctx.event.operationType,
                                                    ctx.event.commitId,
                                                    ctx.event.module,
                                                    ctx.event.entityType,
                                                    ctx.event.entityId,
                                                    QStringLiteral("emergency backup failed"),
                                                    localError,
                                                    ctx.event.changedTables);
        if (errorText) *errorText = localError;
        emit backupStatusChanged();
        return false;
    }

    QString appendError;
    BackupManifestService::appendSnapshot(meta, &appendError);
    if (!appendError.trimmed().isEmpty()) {
        BackupLogging::warn(QStringLiteral("snapshot index append warning %1").arg(appendError));
    }

    m_engineState.activeMode = meta.engineUsed.trimmed().isEmpty() ? m_engineState.preferredMode : meta.engineUsed;
    m_engineState.degraded = false;
    m_engineState.lastFallbackReason = meta.engineUsed == QStringLiteral("vacuum_into")
        ? QStringLiteral("explicit manual/emergency fallback") : QString();
    m_engineState.lastEngineError.clear();
    m_engineState.updatedAt = QDateTime::currentDateTime();

    m_telemetry.recordSnapshotMs(meta.snapshotDurationMs);
    m_telemetry.recordValidateMs(meta.validationDurationMs);
    m_status.lastBackupAt = meta.createdAt;
    m_status.lastBackupStatus = QStringLiteral("completed");
    m_status.lastError.clear();
    if (outMeta) *outMeta = meta;
    BackupRecoveryAuditTrail::appendBackupEvent(QStringLiteral("completed"),
                                                ctx.event.operationType,
                                                ctx.event.commitId,
                                                ctx.event.module,
                                                ctx.event.entityType,
                                                ctx.event.entityId,
                                                QStringLiteral("emergency backup completed immediately"),
                                                QString(),
                                                ctx.event.changedTables);
    scheduleCleanupDebounced();
    emit backupStatusChanged();
    if (errorText) errorText->clear();
    return true;
}

void BackupManager::scheduleCoalescedBackup(const SnapshotJobContext& ctx)
{
    if (m_shuttingDown) return;
    Q_UNUSED(ctx);
    if (shouldDeferAutomaticBackup()) {
        maybeKickPendingBackupSoon(BackupManagerSupport::kUiBusyRetryDelayMs);
        return;
    }
    const qint64 msSinceLast = m_lastEnqueueAt.isValid() ? m_lastEnqueueAt.msecsTo(QDateTime::currentDateTime()) : 999999;
    const int delayMs = msSinceLast >= BackupManagerSupport::kIdleBackupDelayMs ? 0 : qMax(0, BackupManagerSupport::kIdleBackupDelayMs - static_cast<int>(msSinceLast));
    maybeKickPendingBackupSoon(delayMs);
}

void BackupManager::maybeKickPendingBackupSoon(int delayMs)
{
    if (m_shuttingDown) return;
    if (m_pendingAutobackups.isEmpty()) return;
    const int safeDelay = qMax(0, delayMs);
    if (m_coalesceTimer->isActive()) {
        const int remaining = m_coalesceTimer->remainingTime();
        // Immediate kicks should still run immediately; coalesced commit debounce should be extended
        // to the newest commit so a burst of DB commits produces one snapshot of the final state.
        if (safeDelay == 0 || safeDelay >= BackupManagerSupport::kIdleBackupDelayMs) {
            m_coalesceTimer->start(safeDelay);
            return;
        }
        if (remaining <= safeDelay) {
            return;
        }
    }
    m_coalesceTimer->start(safeDelay);
}

void BackupManager::enqueuePendingBackupIfPossible()
{
    if (m_shuttingDown) return;
    if (m_pendingAutobackups.isEmpty()) return;
    auto appendDeferredAuditIfNeeded = [this](const SnapshotJobContext& pending,
                                               const QString& reasonKey,
                                               const QString& message) {
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const QString key = reasonKey + QStringLiteral("|") + pending.event.commitId + QStringLiteral("|")
            + pending.event.operationType + QStringLiteral("|") + pending.event.entityId;
        if (key == m_lastDeferredAuditKey
            && nowMs - m_lastDeferredAuditAtMs < BackupManagerSupport::kDeferredAuditThrottleMs) {
            return;
        }
        m_lastDeferredAuditKey = key;
        m_lastDeferredAuditAtMs = nowMs;
        BackupRecoveryAuditTrail::appendBackupEvent(QStringLiteral("deferred"),
                                                    pending.event.operationType,
                                                    pending.event.commitId,
                                                    pending.event.module,
                                                    pending.event.entityType,
                                                    pending.event.entityId,
                                                    message,
                                                    QString(),
                                                    pending.event.changedTables);
    };

    if (shouldDeferAutomaticBackup()) {
        const SnapshotJobContext& pending = m_pendingAutobackups.head();
        appendDeferredAuditIfNeeded(pending,
                                    QStringLiteral("ui_busy"),
                                    QStringLiteral("automatic backup deferred because UI is busy"));
        m_coalesceTimer->start(BackupManagerSupport::kUiBusyRetryDelayMs);
        return;
    }
    if (m_queue->isPaused() || m_queue->isRunning() || m_queue->queuedCount() > 0) {
        const SnapshotJobContext& pending = m_pendingAutobackups.head();
        appendDeferredAuditIfNeeded(pending,
                                    QStringLiteral("queue_busy"),
                                    QStringLiteral("automatic backup deferred because queue is busy"));
        m_coalesceTimer->start(BackupManagerSupport::kQueueBusyRetryDelayMs);
        return;
    }

    SnapshotJobContext ctx = m_pendingAutobackups.dequeue();
    m_status.pendingJobs = m_pendingAutobackups.size();
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (ctx.enqueuedAtMs > 0) {
        m_telemetry.recordQueueWaitMs(qMax<qint64>(0, nowMs - ctx.enqueuedAtMs));
    }
    m_lastEnqueueAt = QDateTime::currentDateTime();
    BackupLogging::info(QStringLiteral("backup started commit=%1 retry=%2 pending_left=%3")
                            .arg(ctx.event.commitId,
                                 QString::number(ctx.retryCount),
                                 QString::number(m_pendingAutobackups.size())));
    BackupRecoveryAuditTrail::appendBackupEvent(QStringLiteral("started"),
                                                ctx.event.operationType,
                                                ctx.event.commitId,
                                                ctx.event.module,
                                                ctx.event.entityType,
                                                ctx.event.entityId,
                                                QStringLiteral("backup dequeued for execution"),
                                                QString(),
                                                ctx.event.changedTables);
    m_queue->enqueue(ctx);
    emit backupStatusChanged();
}

bool BackupManager::shouldDeferAutomaticBackup() const
{
    if (m_interactiveHoldDepth > 0) {
        return true;
    }
    if (QApplication::activeModalWidget() || QApplication::activePopupWidget()) {
        return true;
    }

    const auto* guiApp = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
    if (guiApp && guiApp->applicationState() == Qt::ApplicationActive) {
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (m_lastUiInteractionAtMs <= 0 || (nowMs - m_lastUiInteractionAtMs) < BackupManagerSupport::kInteractiveBackupQuietWindowMs) {
            return true;
        }
    }
#if !CS_HAS_SQLITE3_BACKUP_API
    if (guiApp && guiApp->applicationState() == Qt::ApplicationActive) {
        return true;
    }
#endif
    return false;
}

void BackupManager::flushPendingAutobackupOnShutdown()
{
    if (m_pendingAutobackups.isEmpty()) {
        return;
    }
    if (m_queue->isRunning() || m_queue->queuedCount() > 0 || shouldDeferAutomaticBackup()) {
        BackupLogging::warn(QStringLiteral("queue flush on shutdown skipped/deferred pending=%1 because queue is busy or UI is still active")
                                .arg(QString::number(m_pendingAutobackups.size())));
        BackupManagerSupport::auditAndDiscardPendingShutdownBackups(m_pendingAutobackups,
                                              QStringLiteral("shutdown flush intentionally deferred to avoid UI freeze"));
        return;
    }

    QElapsedTimer budget;
    budget.start();
    int processedSnapshots = 0;
    while (!m_pendingAutobackups.isEmpty() && budget.elapsed() < BackupManagerSupport::kShutdownFlushBudgetMs && processedSnapshots < BackupManagerSupport::kShutdownFlushMaxSnapshots) {
        const int activeConnCount = SqliteConnectionUtils::countOpenConnectionsForPath(AppRuntime::activeDbFilePath());
        if (activeConnCount > 0) {
            BackupLogging::warn(QStringLiteral("queue flush on shutdown deferred because runtime DB still has active SQLite connections pending=%1 active_connections=%2")
                                    .arg(QString::number(m_pendingAutobackups.size()), QString::number(activeConnCount)));
            BackupManagerSupport::auditAndDiscardPendingShutdownBackups(m_pendingAutobackups,
                                                  QStringLiteral("shutdown flush intentionally deferred because runtime DB still has active SQLite connections"));
            break;
        }
        SnapshotJobContext ctx = m_pendingAutobackups.dequeue();
        SnapshotMetadata meta;
        QString localError;
        BackupLogging::info(QStringLiteral("queue flush on shutdown commit=%1").arg(ctx.event.commitId));
        BackupRecoveryAuditTrail::appendBackupEvent(QStringLiteral("started"),
                                                    ctx.event.operationType,
                                                    ctx.event.commitId,
                                                    ctx.event.module,
                                                    ctx.event.entityType,
                                                    ctx.event.entityId,
                                                    QStringLiteral("shutdown flush snapshot started"),
                                                    QString(),
                                                    ctx.event.changedTables);
        if (!BackupSnapshotService::createSnapshot(ctx, &meta, &localError)) {
            BackupLogging::warn(QStringLiteral("queue flush skipped/deferred commit=%1 error=%2")
                                    .arg(ctx.event.commitId, localError));
            BackupRecoveryAuditTrail::appendBackupEvent(QStringLiteral("deferred"),
                                                        ctx.event.operationType,
                                                        ctx.event.commitId,
                                                        ctx.event.module,
                                                        ctx.event.entityType,
                                                        ctx.event.entityId,
                                                        QStringLiteral("shutdown flush deferred after snapshot failure"),
                                                        localError,
                                                        ctx.event.changedTables);
            m_engineState.degraded = true;
            m_engineState.degradedReason = QStringLiteral("shutdown_flush_incomplete");
            m_engineState.lastEngineError = localError;
            BackupRecoveryAuditTrail::appendBackupEvent(QStringLiteral("degraded"),
                                                        ctx.event.operationType,
                                                        ctx.event.commitId,
                                                        ctx.event.module,
                                                        ctx.event.entityType,
                                                        ctx.event.entityId,
                                                        QStringLiteral("backup engine degraded: shutdown flush incomplete"),
                                                        localError,
                                                        ctx.event.changedTables);
            m_pendingAutobackups.prepend(ctx);
            break;
        }
        ++processedSnapshots;
        BackupManifestService::appendSnapshot(meta, nullptr);
        m_status.lastBackupAt = meta.createdAt;
        m_status.lastBackupStatus = QStringLiteral("completed");
        m_status.lastError.clear();
        BackupRecoveryAuditTrail::appendBackupEvent(QStringLiteral("completed"),
                                                    ctx.event.operationType,
                                                    ctx.event.commitId,
                                                    ctx.event.module,
                                                    ctx.event.entityType,
                                                    ctx.event.entityId,
                                                    QStringLiteral("shutdown flush snapshot completed"),
                                                    QString(),
                                                    ctx.event.changedTables);
    }

    if (!m_pendingAutobackups.isEmpty()) {
        BackupLogging::warn(QStringLiteral("queue flush on shutdown incomplete remaining=%1 (soft-shutdown path)")
                                .arg(QString::number(m_pendingAutobackups.size())));
        BackupManagerSupport::auditAndDiscardPendingShutdownBackups(m_pendingAutobackups,
                                              QStringLiteral("shutdown flush intentionally truncated to stay within freeze-prevention budget"));
    }
}

bool BackupManager::prepareForRestore(QString* errorText, int timeoutMs)
{
    if (m_shuttingDown) {
        if (errorText) *errorText = QStringLiteral("backup manager is shutting down");
        return false;
    }
    BackupLogging::warn(QStringLiteral("restore preflight requested backup manager pause/drain"));
    BackupRecoveryAuditTrail::appendBackupEvent(QStringLiteral("started"),
                                                QStringLiteral("restore_backup_pause"),
                                                QStringLiteral("restore_lock"),
                                                QStringLiteral("BackupManager"),
                                                QStringLiteral("database"),
                                                QString(),
                                                QStringLiteral("pausing backup manager before restore"));
    m_acceptAsyncApply = false;
    ++m_cleanupGeneration;
    ++m_manifestGeneration;
    if (m_coalesceTimer) m_coalesceTimer->stop();
    if (m_cleanupDebounceTimer) m_cleanupDebounceTimer->stop();
    if (m_manifestDebounceTimer) m_manifestDebounceTimer->stop();
    if (m_interactiveHoldTimer) m_interactiveHoldTimer->stop();
    m_interactiveHoldDepth = 1;

    while (!m_pendingAutobackups.isEmpty()) {
        const SnapshotJobContext ctx = m_pendingAutobackups.dequeue();
        BackupRecoveryAuditTrail::appendBackupEvent(QStringLiteral("cancelled"),
                                                    ctx.event.operationType,
                                                    ctx.event.commitId,
                                                    ctx.event.module,
                                                    ctx.event.entityType,
                                                    ctx.event.entityId,
                                                    QStringLiteral("pending autobackup discarded before restore because it belongs to the pre-restore DB state"),
                                                    QString(),
                                                    ctx.event.changedTables);
    }

    QString queueError;
    if (m_queue && !m_queue->drainAndPauseForRestore(timeoutMs, &queueError)) {
        if (errorText) *errorText = queueError;
        m_status.lastError = queueError;
        emit backupStatusChanged();
        return false;
    }

    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + qMax(0, timeoutMs);
    auto waitThread = [&](QThread* thread, const QString& name) -> bool {
        if (!thread) return true;
        thread->requestInterruption();
        const int remaining = static_cast<int>(qMax<qint64>(0, deadline - QDateTime::currentMSecsSinceEpoch()));
        if (!thread->wait(remaining)) {
            const QString msg = QStringLiteral("%1 worker did not stop before restore timeout").arg(name);
            if (errorText) *errorText = msg;
            BackupRecoveryAuditTrail::appendBackupEvent(QStringLiteral("deferred"),
                                                        QStringLiteral("restore_wait_worker"),
                                                        QStringLiteral("restore_lock"),
                                                        QStringLiteral("BackupManager"),
                                                        QStringLiteral("database"),
                                                        QString(),
                                                        msg);
            return false;
        }
        return true;
    };
    if (!waitThread(m_cleanupThread, QStringLiteral("cleanup"))) return false;
    if (!waitThread(m_manifestThread, QStringLiteral("manifest"))) return false;

    m_status.pendingJobs = 0;
    m_status.queuedJobs = m_queue ? m_queue->queuedCount() : 0;
    m_status.runningJobs = m_queue && m_queue->isRunning() ? 1 : 0;
    emit backupStatusChanged();
    BackupRecoveryAuditTrail::appendBackupEvent(QStringLiteral("completed"),
                                                QStringLiteral("restore_backup_pause"),
                                                QStringLiteral("restore_lock"),
                                                QStringLiteral("BackupManager"),
                                                QStringLiteral("database"),
                                                QString(),
                                                QStringLiteral("backup manager paused and drained before restore"));
    if (errorText) errorText->clear();
    return true;
}

void BackupManager::finishRestore()
{
    m_acceptAsyncApply = true;
    m_interactiveHoldDepth = 0;
    if (m_queue) {
        m_queue->resumeAfterRestore();
    }
    BackupRecoveryAuditTrail::appendBackupEvent(QStringLiteral("completed"),
                                                QStringLiteral("restore_backup_resume"),
                                                QStringLiteral("restore_lock"),
                                                QStringLiteral("BackupManager"),
                                                QStringLiteral("database"),
                                                QString(),
                                                QStringLiteral("backup manager released after restore"));
    emit backupStatusChanged();
}

bool BackupManager::validateSnapshots(BackupValidationSummary* summary, QString* errorText)
{
    QVector<SnapshotMetadata> items = BackupManifestService::listSnapshots();
    BackupValidationSummary local;
    QVector<SnapshotMetadata> updated;
    updated.reserve(items.size());

    for (SnapshotMetadata meta : items) {
        ++local.checkedCount;
        const QString nativePath = QDir::toNativeSeparators(meta.snapshotPath);
        QFileInfo fi(meta.snapshotPath);
        if (meta.snapshotPath.trimmed().isEmpty() || !fi.exists()) {
            meta.validationStatus = QStringLiteral("missing");
            ++local.invalidCount;
            ++local.missingCount;
            local.problems << QStringLiteral("Файл snapshot відсутній: %1").arg(nativePath);
            updated.push_back(meta);
            continue;
        }

        meta.snapshotSizeBytes = fi.size();
        QString validationError;
        if (BackupSnapshotService::validateSnapshot(meta.snapshotPath, &validationError)) {
            meta.validationStatus = QStringLiteral("valid");
            ++local.validCount;
        } else {
            meta.validationStatus = QStringLiteral("invalid");
            ++local.invalidCount;
            local.problems << QStringLiteral("Пошкоджений snapshot: %1 (%2)").arg(nativePath, validationError);
        }
        updated.push_back(meta);
    }

    QString replaceError;
    if (!BackupManifestService::replaceSnapshotIndex(updated, &replaceError)) {
        if (errorText) *errorText = replaceError;
        if (summary) *summary = local;
        m_status.lastError = replaceError;
        emit backupStatusChanged();
        return false;
    }

    m_status.lastError = local.problems.isEmpty() ? QString() : local.problems.constFirst();
    emit backupStatusChanged();
    if (summary) *summary = local;
    if (errorText) errorText->clear();
    return true;
}

BackupStatusSnapshot BackupManager::status() const
{
    BackupStatusSnapshot s = m_status;
    const QVector<SnapshotMetadata> items = BackupManifestService::listSnapshots();
    qint64 total = 0;
    int today = 0;
    for (const SnapshotMetadata& meta : items) {
        total += meta.snapshotSizeBytes;
        if (meta.createdAt.date() == QDate::currentDate()) ++today;
    }
    s.totalSizeBytes = total;
    s.snapshotsToday = today;
    s.queuedJobs = m_queue->queuedCount();
    s.runningJobs = m_queue->isRunning() ? 1 : 0;
    s.pendingJobs = m_pendingAutobackups.size();
    s.engineMode = m_engineState.activeMode;
    s.engineDegraded = m_engineState.degraded;
    s.degradedReason = m_engineState.degradedReason;
    s.lastEngineError = m_engineState.lastEngineError;
    s.lastFallbackReason = m_engineState.lastFallbackReason;
    const BackupTelemetrySnapshot t = m_telemetry.snapshot();
    s.lastEnqueueMs = t.lastEnqueueMs;
    s.lastQueueWaitMs = t.lastQueueWaitMs;
    s.lastSnapshotDurationMs = t.lastSnapshotMs;
    s.lastValidateDurationMs = t.lastValidateMs;
    s.lastManifestWriteDurationMs = t.lastManifestWriteMs;
    s.lastCleanupDurationMs = t.lastCleanupMs;
    return s;
}

const BackupEngineState& BackupManager::engineState() const
{
    return m_engineState;
}

BackupTelemetrySnapshot BackupManager::telemetry() const
{
    return m_telemetry.snapshot();
}

void BackupManager::runCleanup(QString* errorText)
{
    if (m_shuttingDown) {
        if (errorText) errorText->clear();
        return;
    }
    if (DataProtectionCoordinator::isRestoreRunning()) {
        if (errorText) *errorText = QStringLiteral("Cleanup відкладено: виконується restore");
        BackupRecoveryAuditTrail::appendBackupEvent(QStringLiteral("deferred"),
                                                    QStringLiteral("cleanup"),
                                                    QStringLiteral("cleanup_restore_lock"),
                                                    QStringLiteral("BackupManager"),
                                                    QStringLiteral("backup"),
                                                    QString(),
                                                    QStringLiteral("cleanup deferred because restore is running"));
        return;
    }
    if (errorText) {
        auto cleanupGate = DataProtectionCoordinator::tryEnter(DataProtectionCoordinator::Operation::Cleanup,
                                                               QStringLiteral("manual cleanup"),
                                                               errorText,
                                                               0);
        if (!cleanupGate.isActive()) return;
        QElapsedTimer timer;
        timer.start();
        BackupLogging::info(QStringLiteral("cleanup started"));
        BackupPolicy::cleanupSnapshots(errorText);
        m_telemetry.recordCleanupMs(timer.elapsed());
        BackupLogging::info(QStringLiteral("cleanup completed"));
        emit backupStatusChanged();
        return;
    }

    const auto* guiApp = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
    if (m_cleanupThread || shouldDeferAutomaticBackup() || m_queue->isRunning() || m_queue->queuedCount() > 0 || !m_pendingAutobackups.isEmpty()
        || (guiApp && guiApp->applicationState() == Qt::ApplicationActive)) {
        m_cleanupDebounceTimer->start(30000);
        return;
    }

    const auto ctx = std::make_shared<BackupManagerSupport::AsyncWorkerContext>();
    ctx->owner = QPointer<BackupManager>(this);
    ctx->generation = ++m_cleanupGeneration;
    ctx->cancelToken = std::make_shared<std::atomic_bool>(false);
    auto* thread = AsyncThreadScope::startWorker(this, QStringLiteral("backup_cleanup_worker"), ctx->cancelToken, [ctx](const std::shared_ptr<std::atomic_bool>& cancelToken) {
        if (!ctx->owner || (cancelToken && cancelToken->load())) return;
        QString localError;
        auto cleanupGate = DataProtectionCoordinator::tryEnter(DataProtectionCoordinator::Operation::Cleanup,
                                                               QStringLiteral("automatic cleanup"),
                                                               &localError,
                                                               0);
        if (!cleanupGate.isActive()) return;
        QElapsedTimer timer;
        timer.start();
        BackupPolicy::cleanupSnapshots(&localError);
        const qint64 elapsed = timer.elapsed();
        if (!ctx->owner || (cancelToken && cancelToken->load())) return;
        QMetaObject::invokeMethod(ctx->owner.data(), [ctx, elapsed, localError]() {
            auto* self = ctx->owner.data();
            if (!self || self->m_shuttingDown || !self->m_acceptAsyncApply || self->m_cleanupGeneration != ctx->generation) return;
            self->m_telemetry.recordCleanupMs(elapsed);
            if (!localError.trimmed().isEmpty()) {
                BackupLogging::warn(QStringLiteral("cleanup failed %1").arg(localError));
                self->m_status.lastError = localError;
            } else {
                BackupLogging::info(QStringLiteral("cleanup completed"));
            }
            emit self->backupStatusChanged();
        }, Qt::QueuedConnection);
    });
    m_cleanupThread = thread;
    QPointer<BackupManager> cleanupSelf(this);
    connect(thread, &QThread::finished, this, [cleanupSelf]() {
        if (!cleanupSelf) return;
        cleanupSelf->m_cleanupThread = nullptr;
        if (cleanupSelf->m_shuttingDown) {
            cleanupSelf->discardQueuedCallbacks();
        }
    });
    BackupLogging::info(QStringLiteral("cleanup started"));
    thread->start();
}

void BackupManager::reconcile(QString* errorText)
{
    // Manual reconcile keeps the synchronous error contract for settings/tools.
    // Automatic startup/idle reconcile must never block the UI thread.
    if (errorText) {
        RuntimeJob::RuntimeJobDispatcher::warnIfUiThreadBlockingCall(QStringLiteral("BackupManager::reconcile manual_sync_path"));
        BackupManifestService::reconcile(errorText);
        emit backupStatusChanged();
        return;
    }

    if (m_shuttingDown) return;
    if (m_manifestThread) {
        if (m_manifestDebounceTimer) m_manifestDebounceTimer->start(5000);
        BackupLogging::warn(QStringLiteral("manifest reconcile deferred: manifest worker already running"));
        return;
    }

    const auto ctx = std::make_shared<BackupManagerSupport::AsyncWorkerContext>();
    ctx->owner = QPointer<BackupManager>(this);
    ctx->generation = ++m_manifestGeneration;
    const auto runtimeTicket = RuntimeJob::RuntimeJobDispatcher::instance().startLatest(
        RuntimeJob::Family::Backup,
        QStringLiteral("BackupManager/reconcile"),
        20000,
        25);
    ctx->cancelToken = runtimeTicket.cancelToken;

    auto localError = std::make_shared<QString>();
    auto elapsedMs = std::make_shared<qint64>(0);
    auto* thread = AsyncThreadScope::startWorker(this, QStringLiteral("backup_manifest_reconcile_worker"), ctx->cancelToken,
                                                 [ctx, localError, elapsedMs, runtimeTicket](const std::shared_ptr<std::atomic_bool>& cancelToken) {
        RuntimeJob::RuntimeJobDispatcher::instance().markRunning(runtimeTicket);
        if (!ctx->owner || (cancelToken && cancelToken->load())) return;
        QElapsedTimer timer;
        timer.start();
        BackupManifestService::reconcile(localError.get());
        *elapsedMs = timer.elapsed();
    });

    m_manifestThread = thread;
    QPointer<BackupManager> reconcileSelf(this);
    connect(thread, &QThread::finished, this, [reconcileSelf, ctx, localError, elapsedMs, runtimeTicket]() {
        auto* self = reconcileSelf.data();
        const bool canApply = self
            && !self->m_shuttingDown
            && self->m_acceptAsyncApply
            && self->m_manifestGeneration == ctx->generation
            && RuntimeJob::RuntimeJobDispatcher::instance().shouldApply(runtimeTicket);
        RuntimeJob::RuntimeJobDispatcher::instance().finish(
            runtimeTicket,
            canApply ? RuntimeJob::Status::Succeeded : RuntimeJob::Status::Cancelled);
        if (!self) return;
        if (self->m_manifestThread) self->m_manifestThread = nullptr;
        if (!canApply) return;
        self->m_telemetry.recordManifestWriteMs(*elapsedMs);
        if (!localError->trimmed().isEmpty()) {
            self->m_status.lastError = *localError;
            BackupLogging::warn(QStringLiteral("manifest reconcile failed %1").arg(*localError));
        } else {
            BackupLogging::info(QStringLiteral("manifest reconcile completed"));
        }
        emit self->backupStatusChanged();
    });
    BackupLogging::info(QStringLiteral("manifest reconcile queued"));
    thread->start();
}

void BackupManager::scheduleCleanupDebounced()
{
    if (m_shuttingDown) return;
    if (!BackupPolicy::load().cleanupAfterBackup) {
        return;
    }
    m_cleanupDebounceTimer->start(BackupManagerSupport::kCleanupDebounceMs);
}

void BackupManager::flushPendingManifestWrites()
{
    if (m_manifestThread || m_pendingManifestWrites.isEmpty()) {
        return;
    }
    if (m_shuttingDown) {
        BackupLogging::warn(QStringLiteral("manifest/index flush deferred on shutdown pending=%1; startup reconcile will rebuild index")
                                .arg(QString::number(m_pendingManifestWrites.size())));
        m_pendingManifestWrites.clear();
        return;
    }

    const QVector<SnapshotMetadata> batch = m_pendingManifestWrites;
    m_pendingManifestWrites.clear();
    const auto ctx = std::make_shared<BackupManagerSupport::AsyncWorkerContext>();
    ctx->owner = QPointer<BackupManager>(this);
    ctx->generation = ++m_manifestGeneration;
    ctx->cancelToken = std::make_shared<std::atomic_bool>(false);
    auto* thread = AsyncThreadScope::startWorker(this, QStringLiteral("backup_manifest_worker"), ctx->cancelToken, [ctx, batch](const std::shared_ptr<std::atomic_bool>& cancelToken) {
        if (!ctx->owner || (cancelToken && cancelToken->load())) return;
        QElapsedTimer timer;
        timer.start();
        QString localError;
        for (const SnapshotMetadata& meta : batch) {
            if (cancelToken && cancelToken->load()) return;
            if (!BackupManifestService::appendSnapshot(meta, &localError)) {
                break;
            }
        }
        const qint64 elapsed = timer.elapsed();
        if (!ctx->owner || (cancelToken && cancelToken->load())) return;
        QMetaObject::invokeMethod(ctx->owner.data(), [ctx, elapsed, localError]() {
            auto* self = ctx->owner.data();
            if (!self || self->m_shuttingDown || !self->m_acceptAsyncApply || self->m_manifestGeneration != ctx->generation) return;
            self->m_telemetry.recordManifestWriteMs(elapsed);
            if (!localError.trimmed().isEmpty()) {
                BackupLogging::warn(QStringLiteral("manifest/index write failed %1").arg(localError));
                self->m_status.lastError = localError;
            }
            emit self->backupStatusChanged();
        }, Qt::QueuedConnection);
    });
    m_manifestThread = thread;
    QPointer<BackupManager> manifestSelf(this);
    connect(thread, &QThread::finished, this, [manifestSelf]() {
        if (!manifestSelf) return;
        manifestSelf->m_manifestThread = nullptr;
        if (manifestSelf->m_shuttingDown) {
            manifestSelf->discardQueuedCallbacks();
            return;
        }
        if (!manifestSelf->m_pendingManifestWrites.isEmpty()) {
            manifestSelf->m_manifestDebounceTimer->start(BackupManagerSupport::kManifestFlushDelayMs);
        }
    });
    thread->start();
}

void BackupManager::onJobCompleted(const SnapshotJobContext& ctx, const SnapshotMetadata& meta)
{
    if (m_shuttingDown) return;
    m_engineState.activeMode = meta.engineUsed.trimmed().isEmpty() ? m_engineState.preferredMode : meta.engineUsed;
    m_engineState.degraded = false;
    m_engineState.degradedReason.clear();
    m_engineState.lastEngineError.clear();
    m_engineState.lastFallbackReason = meta.engineUsed == QStringLiteral("vacuum_into")
        ? QStringLiteral("explicit manual/emergency fallback") : QString();
    m_engineState.updatedAt = QDateTime::currentDateTime();

    m_telemetry.recordSnapshotMs(meta.snapshotDurationMs);
    m_telemetry.recordValidateMs(meta.validationDurationMs);
    m_pendingManifestWrites.push_back(meta);
    m_manifestDebounceTimer->start(BackupManagerSupport::kManifestFlushDelayMs);
    m_status.lastBackupAt = meta.createdAt;
    m_status.lastBackupStatus = QStringLiteral("completed");
    m_status.lastError.clear();
    if (ctx.source == QStringLiteral("startup-pending")) {
        BackupManagerSupport::clearPendingBackupJournal();
    }
    if (BackupPolicy::load().showNotifications && (ctx.source != QStringLiteral("autobackup") || ctx.retryCount > 0)) {
        emit userNotificationRequested(QString::fromUtf8("Backup завершено: %1").arg(meta.description.trimmed().isEmpty() ? meta.operationType : meta.description), 5000);
    }
    scheduleCleanupDebounced();
    maybeKickPendingBackupSoon(0);
    emit backupStatusChanged();
}

void BackupManager::onJobFailed(const SnapshotJobContext& ctx, const QString& errorText)
{
    if (m_shuttingDown) return;
    m_status.lastBackupAt = QDateTime::currentDateTime();
    m_status.lastBackupStatus = QStringLiteral("failed");
    m_status.lastError = errorText;
    m_engineState.lastEngineError = errorText;
    m_engineState.updatedAt = QDateTime::currentDateTime();

    const QString lowerError = errorText.toLower();
    const bool busyOrLocked = lowerError.contains(QStringLiteral("busy"))
                           || lowerError.contains(QStringLiteral("locked"))
                           || lowerError.contains(QStringLiteral("database is locked"))
                           || lowerError.contains(QStringLiteral("sql statements in progress"))
                           || lowerError.contains(QStringLiteral("cannot vacuum"));

    if (busyOrLocked && ctx.source == QStringLiteral("autobackup") && ctx.retryCount < BackupManagerSupport::kMaxBusyRetryCount) {
        SnapshotJobContext retryCtx = ctx;
        retryCtx.retryCount += 1;
        retryCtx.enqueuedAtMs = QDateTime::currentMSecsSinceEpoch();
        m_pendingAutobackups.prepend(retryCtx);
        BackupLogging::warn(QStringLiteral("backup retry scheduled commit=%1 retry=%2")
                                .arg(ctx.event.commitId, QString::number(retryCtx.retryCount)));
        BackupRecoveryAuditTrail::appendBackupEvent(QStringLiteral("deferred"),
                                                    ctx.event.operationType,
                                                    ctx.event.commitId,
                                                    ctx.event.module,
                                                    ctx.event.entityType,
                                                    ctx.event.entityId,
                                                    QStringLiteral("backup retry scheduled after transient busy/locked error"),
                                                    errorText,
                                                    ctx.event.changedTables);
        m_coalesceTimer->start(lowerError.contains(QStringLiteral("vacuum")) ? 30000 : BackupManagerSupport::kBusyRetryDelayMs);
        emit backupStatusChanged();
        return;
    }

    if (busyOrLocked && ctx.source == QStringLiteral("autobackup")) {
        m_engineState.degraded = true;
        m_engineState.degradedReason = QStringLiteral("busy_locked_retry_exhausted");
        BackupRecoveryAuditTrail::appendBackupEvent(QStringLiteral("degraded"),
                                                    ctx.event.operationType,
                                                    ctx.event.commitId,
                                                    ctx.event.module,
                                                    ctx.event.entityType,
                                                    ctx.event.entityId,
                                                    QStringLiteral("backup engine degraded: busy/locked retry budget exhausted"),
                                                    errorText,
                                                    ctx.event.changedTables);
    }
    BackupRecoveryAuditTrail::appendBackupEvent(QStringLiteral("failed"),
                                                ctx.event.operationType,
                                                ctx.event.commitId,
                                                ctx.event.module,
                                                ctx.event.entityType,
                                                ctx.event.entityId,
                                                QStringLiteral("backup job failed"),
                                                errorText,
                                                ctx.event.changedTables);
    if (BackupPolicy::load().showNotifications) {
        const QString what = ctx.event.userVisibleDescription.trimmed().isEmpty() ? ctx.event.operationType : ctx.event.userVisibleDescription;
        emit userNotificationRequested(QString::fromUtf8("Backup помилка: %1").arg(what), 7000);
    }
    emit backupStatusChanged();
    maybeKickPendingBackupSoon(0);
}

void BackupManager::holdInteractiveMode(int holdMs)
{
    if (m_shuttingDown) return;
    m_interactiveHoldDepth = 1;
    m_queue->setPaused(true);
    m_interactiveHoldTimer->start(qMax(1000, holdMs));
    BackupLogging::info(QStringLiteral("backup interactive-hold enabled hold_ms=%1")
                            .arg(QString::number(holdMs)));
    emit backupStatusChanged();
}

void BackupManager::releaseInteractiveMode()
{
    if (m_shuttingDown) return;
    m_interactiveHoldDepth = 0;
    m_interactiveHoldTimer->stop();
    m_queue->setPaused(false);
    maybeKickPendingBackupSoon(0);
    BackupLogging::info(QStringLiteral("backup interactive-hold released"));
    emit backupStatusChanged();
}
