#include "AppVersion.h"
#include "core/backup/BackupManager.h"
#include "core/backup/BackupManagerSupport.h"

#include "core/backup/BackupJobQueue.h"
#include "core/backup/BackupLogging.h"
#include "core/backup/BackupManifestService.h"
#include "core/backup/BackupPaths.h"
#include "core/backup/BackupPolicy.h"
#include "core/backup/BackupSnapshotService.h"
#include "infrastructure/db/sqlite/ScopedConnectionManager.h"
#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/audit/BackupRecoveryAuditTrail.h"
#include "runtime/AppRuntime.h"
#include "runtime/AsyncThreadScope.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QFileInfo>
#include <QGuiApplication>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <memory>


DataCommitEvent BackupManager::synthesizeEvent(const QString& operationType, const QString& description)
{
    DataCommitEvent event;
    event.eventId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    event.commitId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    event.committedAt = QDateTime::currentDateTime();
    event.module = QStringLiteral("settings");
    event.page = QStringLiteral("int.autobackup");
    event.entityType = QStringLiteral("database");
    event.operationType = operationType;
    event.userVisibleDescription = description;
    event.appVersion = CarService::AppVersion::versionString();
    event.schemaVersion = Database::getSetting(QStringLiteral("app.schema.version"), QStringLiteral("runtime")).toString();
    event.changedTables = QStringList{QStringLiteral("work_orders"), QStringLiteral("work_order_services"), QStringLiteral("work_order_parts")};
    return event;
}

BackupManager::~BackupManager()
{
    shutdown();
}

BackupManager::BackupManager(QObject* parent)
    : QObject(parent)
    , m_queue(new BackupJobQueue(this))
    , m_coalesceTimer(new QTimer(this))
    , m_interactiveHoldTimer(new QTimer(this))
    , m_cleanupDebounceTimer(new QTimer(this))
    , m_manifestDebounceTimer(new QTimer(this))
{
#if !CS_HAS_SQLITE3_BACKUP_API
    m_engineState.preferredMode = QStringLiteral("vacuum_into");
    m_engineState.activeMode = QStringLiteral("vacuum_into");
#endif
    m_engineState.updatedAt = QDateTime::currentDateTime();
    m_lastUiInteractionAtMs = QDateTime::currentMSecsSinceEpoch();
    BackupPolicy::refreshFromRuntime();
    BackupPaths::refreshFromRuntime();

    m_coalesceTimer->setSingleShot(true);
    m_interactiveHoldTimer->setSingleShot(true);
    m_cleanupDebounceTimer->setSingleShot(true);
    m_manifestDebounceTimer->setSingleShot(true);

    connect(m_interactiveHoldTimer, &QTimer::timeout, this, [this]() {
        releaseInteractiveMode();
    });
    connect(m_coalesceTimer, &QTimer::timeout, this, [this]() {
        enqueuePendingBackupIfPossible();
    });
    connect(m_cleanupDebounceTimer, &QTimer::timeout, this, [this]() {
        if (m_pendingAutobackups.isEmpty() && !m_queue->isRunning() && m_queue->queuedCount() == 0 && !shouldDeferAutomaticBackup()) {
            runCleanup(nullptr);
        } else {
            maybeKickPendingBackupSoon(5000);
            m_cleanupDebounceTimer->start(30000);
        }
    });
    connect(m_manifestDebounceTimer, &QTimer::timeout, this, &BackupManager::flushPendingManifestWrites);

    connect(m_queue, &BackupJobQueue::jobCompleted, this, &BackupManager::onJobCompleted);
    connect(m_queue, &BackupJobQueue::jobFailed, this, &BackupManager::onJobFailed);
    connect(m_queue, &BackupJobQueue::queueChanged, this, [this]() {
        m_status.queuedJobs = m_queue->queuedCount();
        m_status.runningJobs = m_queue->isRunning() ? 1 : 0;
        m_status.pendingJobs = m_pendingAutobackups.size();
        emit backupStatusChanged();
    });

    if (auto* guiApp = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        guiApp->installEventFilter(this);
        connect(guiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
            if (state != Qt::ApplicationActive) {
                maybeKickPendingBackupSoon(0);
            }
        });
    }
    if (auto* coreApp = QCoreApplication::instance()) {
        connect(coreApp, &QCoreApplication::aboutToQuit, this, [this]() {
            shutdown();
        });
    }

    QTimer::singleShot(0, this, [this]() {
        QString journalError;
        QQueue<SnapshotJobContext> startupPending = BackupManagerSupport::readPendingBackupJournal(&journalError);
        if (!journalError.trimmed().isEmpty()) {
            BackupLogging::warn(QStringLiteral("pending backup journal read failed %1").arg(journalError));
        }
        if (startupPending.isEmpty()) return;
        SnapshotJobContext ctx = startupPending.back();
        ctx.source = QStringLiteral("startup-pending");
        ctx.event.operationType = QStringLiteral("startup_pending_backup");
        ctx.event.userVisibleDescription = QStringLiteral("Startup pending backup from previous shutdown");
        ctx.enqueuedAtMs = QDateTime::currentMSecsSinceEpoch();
        m_pendingAutobackups.clear();
        m_pendingAutobackups.enqueue(ctx);
        BackupRecoveryAuditTrail::appendBackupEvent(QStringLiteral("started"),
                                                    ctx.event.operationType,
                                                    ctx.event.commitId,
                                                    ctx.event.module,
                                                    ctx.event.entityType,
                                                    ctx.event.entityId,
                                                    QStringLiteral("startup pending backup restored from shutdown journal"),
                                                    QString(),
                                                    ctx.event.changedTables);
        maybeKickPendingBackupSoon(0);
        emit backupStatusChanged();
    });
}

void BackupManager::shutdown()
{
    if (m_shuttingDown) {
        return;
    }
    m_shuttingDown = true;
    m_acceptAsyncApply = false;
    ++m_cleanupGeneration;
    ++m_manifestGeneration;

    BackupLogging::info(QStringLiteral("backup manager shutdown started"));

    if (auto* guiApp = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        guiApp->removeEventFilter(this);
    }

    if (m_interactiveHoldTimer) m_interactiveHoldTimer->stop();
    if (m_coalesceTimer) m_coalesceTimer->stop();
    if (m_cleanupDebounceTimer) m_cleanupDebounceTimer->stop();
    if (m_manifestDebounceTimer) m_manifestDebounceTimer->stop();

    disconnect(this, nullptr, nullptr, nullptr);
    if (m_queue) {
        disconnect(m_queue, nullptr, this, nullptr);
    }

    flushPendingAutobackupOnShutdown();

    if (!m_pendingManifestWrites.isEmpty()) {
        flushPendingManifestWrites();
    }

    if (m_queue) {
        m_queue->shutdown(BackupManagerSupport::kShutdownFlushBudgetMs);
    }
    if (m_manifestThread) {
        m_manifestThread->requestInterruption();
        if (!m_manifestThread->wait(BackupManagerSupport::kShutdownFlushBudgetMs)) {
            BackupLogging::warn(QStringLiteral("manifest worker did not finish within shutdown budget"));
        }
    }
    if (m_cleanupThread) {
        m_cleanupThread->requestInterruption();
        if (!m_cleanupThread->wait(BackupManagerSupport::kShutdownFlushBudgetMs)) {
            BackupLogging::warn(QStringLiteral("cleanup worker did not finish within shutdown budget"));
        }
    }

    discardQueuedCallbacks();

    BackupLogging::info(QStringLiteral("backup manager shutdown completed"));
}

void BackupManager::discardQueuedCallbacks()
{
    if (auto* coreApp = QCoreApplication::instance()) {
        coreApp->removePostedEvents(this, QEvent::MetaCall);
    }
}

bool BackupManager::eventFilter(QObject* watched, QEvent* event)
{
    Q_UNUSED(watched);
    if (event) {
        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
        case QEvent::Wheel:
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::FocusIn:
        case QEvent::Shortcut:
        case QEvent::InputMethod:
            m_lastUiInteractionAtMs = QDateTime::currentMSecsSinceEpoch();
            break;
        default:
            break;
        }
    }
    return QObject::eventFilter(watched, event);
}

