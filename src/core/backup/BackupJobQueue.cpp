#include "core/backup/BackupJobQueue.h"

#include "core/backup/BackupLogging.h"
#include "core/backup/BackupManagerSupport.h"
#include "infrastructure/audit/BackupRecoveryAuditTrail.h"
#include "core/backup/BackupSnapshotService.h"
#include "core/backup/DataProtectionCoordinator.h"
#include "runtime/AsyncThreadScope.h"

#include <QDir>
#include <QPointer>
#include <QThread>
#include <QTimer>

#include <memory>
#include <atomic>

BackupJobQueue::BackupJobQueue(QObject* parent)
    : QObject(parent)
{
}

void BackupJobQueue::enqueue(const SnapshotJobContext& ctx)
{
    if (m_shuttingDown) {
        BackupLogging::warn(QStringLiteral("backup enqueue ignored during shutdown commit=%1").arg(ctx.event.commitId));
        BackupRecoveryAuditTrail::appendBackupEvent(QStringLiteral("cancelled"),
                                                    ctx.event.operationType,
                                                    ctx.event.commitId,
                                                    ctx.event.module,
                                                    ctx.event.entityType,
                                                    ctx.event.entityId,
                                                    QStringLiteral("backup enqueue ignored during shutdown"),
                                                    QString(),
                                                    ctx.event.changedTables);
        return;
    }
    m_queue.enqueue(ctx);
    emit queueChanged();
    QTimer::singleShot(0, this, &BackupJobQueue::processNext);
}

int BackupJobQueue::queuedCount() const
{
    return m_queue.size();
}

bool BackupJobQueue::isRunning() const
{
    return m_running;
}

void BackupJobQueue::setPaused(bool paused)
{
    if (m_paused == paused) return;
    m_paused = paused;
    emit queueChanged();
    if (!m_shuttingDown && !m_paused && !m_running && !m_queue.isEmpty()) {
        QTimer::singleShot(0, this, &BackupJobQueue::processNext);
    }
}

bool BackupJobQueue::isPaused() const
{
    return m_paused;
}

bool BackupJobQueue::drainAndPauseForRestore(int waitMs, QString* errorText)
{
    m_paused = true;
    while (!m_queue.isEmpty()) {
        const SnapshotJobContext ctx = m_queue.dequeue();
        BackupRecoveryAuditTrail::appendBackupEvent(QStringLiteral("cancelled"),
                                                    ctx.event.operationType,
                                                    ctx.event.commitId,
                                                    ctx.event.module,
                                                    ctx.event.entityType,
                                                    ctx.event.entityId,
                                                    QStringLiteral("backup queue drained before exclusive restore"),
                                                    QString(),
                                                    ctx.event.changedTables);
    }
    emit queueChanged();

    if (!m_activeThread) {
        if (errorText) errorText->clear();
        return true;
    }

    BackupLogging::warn(QStringLiteral("restore requested cancellation of active backup worker"));
    if (m_activeCancelToken) {
        m_activeCancelToken->store(true);
    }
    m_activeThread->requestInterruption();
    if (!m_activeThread->wait(qMax(0, waitMs))) {
        const QString msg = QStringLiteral("Active backup worker did not stop within restore timeout ms=%1").arg(waitMs);
        if (errorText) *errorText = msg;
        BackupRecoveryAuditTrail::appendBackupEvent(QStringLiteral("deferred"),
                                                    QStringLiteral("restore_wait_backup_worker"),
                                                    QStringLiteral("restore_lock"),
                                                    QStringLiteral("BackupJobQueue"),
                                                    QStringLiteral("backup"),
                                                    QString(),
                                                    msg);
        return false;
    }
    if (errorText) errorText->clear();
    return true;
}

void BackupJobQueue::resumeAfterRestore()
{
    if (m_shuttingDown) return;
    m_paused = false;
    emit queueChanged();
    if (!m_running && !m_activeThread && !m_queue.isEmpty()) {
        QTimer::singleShot(0, this, &BackupJobQueue::processNext);
    }
}

void BackupJobQueue::shutdown(int waitMs)
{
    m_shuttingDown = true;
    m_paused = true;
    if (!m_queue.isEmpty()) {
        BackupManagerSupport::auditAndDiscardPendingShutdownBackups(
            m_queue,
            QStringLiteral("queued backup jobs persisted for next startup during non-blocking shutdown"));
    }
    emit queueChanged();

    if (!m_activeThread) {
        return;
    }

    if (m_activeCancelToken) {
        m_activeCancelToken->store(true);
    }
    m_activeThread->requestInterruption();
    if (!m_activeThread->wait(qMax(0, waitMs))) {
        BackupLogging::warn(QStringLiteral("backup active worker did not finish within shutdown budget ms=%1")
                                .arg(QString::number(waitMs)));
    }
}

void BackupJobQueue::processNext()
{
    if (m_shuttingDown || m_paused || m_running || m_activeThread || m_queue.isEmpty()) return;
    m_running = true;
    emit queueChanged();

    const SnapshotJobContext ctx = m_queue.dequeue();
    BackupLogging::info(QStringLiteral("backup started commit=%1 operation=%2").arg(ctx.event.commitId, ctx.event.operationType));
    BackupRecoveryAuditTrail::appendBackupEvent(QStringLiteral("started"),
                                                ctx.event.operationType,
                                                ctx.event.commitId,
                                                ctx.event.module,
                                                ctx.event.entityType,
                                                ctx.event.entityId,
                                                QStringLiteral("backup worker started snapshot creation"),
                                                QString(),
                                                ctx.event.changedTables);

    struct AsyncResult {
        SnapshotMetadata meta;
        QString errorText;
        bool ok = false;
    };
    auto result = std::make_shared<AsyncResult>();

    const auto cancelToken = std::make_shared<std::atomic_bool>(false);
    m_activeCancelToken = cancelToken;
    auto* thread = AsyncThreadScope::startWorker(this, QStringLiteral("backup_job_worker"), cancelToken, [ctx, result](const std::shared_ptr<std::atomic_bool>& cancelToken) {
        if (cancelToken && cancelToken->load()) return;
        DataProtectionCoordinator::Operation operation = DataProtectionCoordinator::Operation::AutomaticBackup;
        if (ctx.emergency || ctx.source == QStringLiteral("emergency")) {
            operation = DataProtectionCoordinator::Operation::EmergencyBackup;
        } else if (ctx.source == QStringLiteral("manual")) {
            operation = DataProtectionCoordinator::Operation::ManualBackup;
        }
        auto backupGate = DataProtectionCoordinator::tryEnter(operation,
                                                              QStringLiteral("backup worker snapshot source=%1").arg(ctx.source),
                                                              &result->errorText,
                                                              0);
        if (!backupGate.isActive()) {
            result->ok = false;
            return;
        }
        result->ok = BackupSnapshotService::createSnapshot(ctx, &result->meta, &result->errorText, cancelToken);
    });
    m_activeThread = thread;
    QPointer<BackupJobQueue> self(this);
    connect(thread, &QThread::finished, this, [self, ctx, result]() {
        if (!self) return;
        if (result->ok) {
            BackupLogging::info(QStringLiteral("backup completed snapshot=%1").arg(QDir::toNativeSeparators(result->meta.snapshotPath)));
            BackupRecoveryAuditTrail::appendBackupEvent(QStringLiteral("completed"),
                                                        ctx.event.operationType,
                                                        ctx.event.commitId,
                                                        ctx.event.module,
                                                        ctx.event.entityType,
                                                        ctx.event.entityId,
                                                        QStringLiteral("backup worker completed snapshot creation"),
                                                        QString(),
                                                        ctx.event.changedTables);
            emit self->jobCompleted(ctx, result->meta);
        } else {
            BackupLogging::warn(QStringLiteral("backup failed commit=%1 error=%2").arg(ctx.event.commitId, result->errorText));
            BackupRecoveryAuditTrail::appendBackupEvent(QStringLiteral("failed"),
                                                        ctx.event.operationType,
                                                        ctx.event.commitId,
                                                        ctx.event.module,
                                                        ctx.event.entityType,
                                                        ctx.event.entityId,
                                                        QStringLiteral("backup worker failed snapshot creation"),
                                                        result->errorText,
                                                        ctx.event.changedTables);
            emit self->jobFailed(ctx, result->errorText);
        }

        self->m_activeThread = nullptr;
        self->m_activeCancelToken.reset();
        self->m_running = false;
        emit self->queueChanged();
        if (!self->m_shuttingDown && !self->m_queue.isEmpty()) {
            QTimer::singleShot(0, self.data(), &BackupJobQueue::processNext);
        }
    });
    thread->start();
}
