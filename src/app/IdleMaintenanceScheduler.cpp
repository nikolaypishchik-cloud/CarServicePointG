#include "app/IdleMaintenanceScheduler.h"
#include "runtime/AsyncTaskHandle.h"
#include "runtime/AsyncThreadScope.h"

#include "core/services/startup/StartupDatabaseFacade.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QEvent>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QDebug>
#include <QtGlobal>

namespace {
qint64 monotonicMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

bool isUserInteractionEvent(QEvent::Type type)
{
    switch (type) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::Wheel:
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::FocusIn:
    case QEvent::Shortcut:
    case QEvent::ShortcutOverride:
        return true;
    default:
        return false;
    }
}
}

IdleMaintenanceScheduler::IdleMaintenanceScheduler(QObject* parent)
    : QObject(parent)
    , m_idleTimer(new QTimer(this))
{
    m_idleTimer->setSingleShot(true);
    connect(m_idleTimer, &QTimer::timeout, this, &IdleMaintenanceScheduler::tryRunNextTask);

    m_tasks = {
        { QStringLiteral("migration_check"), &StartupDatabaseFacade::runStartupMigrationCheck },
        { QStringLiteral("archive_rollover_maintenance"), &StartupDatabaseFacade::runStartupArchiveMaintenance },
    };
}

IdleMaintenanceScheduler::~IdleMaintenanceScheduler()
{
    if (QCoreApplication::instance()) {
        QCoreApplication::instance()->removeEventFilter(this);
    }
    if (m_idleTimer) {
        m_idleTimer->stop();
    }
    AsyncTaskHandle::cancel(m_cancelToken);
    if (m_activeThread) {
        m_activeThread->requestInterruption();
        if (!m_activeThread->wait(1200)) {
            qWarning().noquote() << QStringLiteral("STARTUP [idle_maintenance] [SHUTDOWN_TIMEOUT]");
        }
    }
}

void IdleMaintenanceScheduler::start(int initialDelayMs, int idleDelayMs)
{
    if (m_started) return;
    m_started = true;
    m_idleDelayMs = qMax(3000, idleDelayMs);
    m_lastInteractionMs = monotonicMs();
    if (QCoreApplication::instance()) {
        QCoreApplication::instance()->installEventFilter(this);
    }
    scheduleNextIdleCheck(qMax(0, initialDelayMs));
}

void IdleMaintenanceScheduler::noteUserInteraction()
{
    m_lastInteractionMs = monotonicMs();
    if (m_nextTaskIndex < m_tasks.size()) {
        scheduleNextIdleCheck(m_idleDelayMs);
    }
}

bool IdleMaintenanceScheduler::eventFilter(QObject* watched, QEvent* event)
{
    Q_UNUSED(watched);
    if (event && isUserInteractionEvent(event->type())) {
        noteUserInteraction();
    }
    return QObject::eventFilter(watched, event);
}

void IdleMaintenanceScheduler::scheduleNextIdleCheck(int delayMs)
{
    if (m_activeThread || m_nextTaskIndex >= m_tasks.size()) return;
    const int delay = (delayMs >= 0) ? delayMs : m_idleDelayMs;
    if (m_idleTimer->isActive()) {
        const int remaining = m_idleTimer->remainingTime();
        if (remaining >= 0 && remaining <= delay) return;
    }
    m_idleTimer->start(delay);
}

void IdleMaintenanceScheduler::tryRunNextTask()
{
    if (m_activeThread || m_nextTaskIndex >= m_tasks.size()) return;

    const qint64 idleForMs = monotonicMs() - m_lastInteractionMs;
    if (idleForMs < m_idleDelayMs) {
        scheduleNextIdleCheck(m_idleDelayMs - static_cast<int>(idleForMs));
        return;
    }

    runTask(m_tasks.at(m_nextTaskIndex));
    ++m_nextTaskIndex;
}

void IdleMaintenanceScheduler::runTask(const Task& task)
{
    QPointer<IdleMaintenanceScheduler> guard(this);
    const auto cancelToken = AsyncTaskHandle::renewCancellationToken(m_cancelToken);
    const AsyncTaskHandle asyncHandle(this, 0, cancelToken);
    auto* thread = AsyncThreadScope::startWorker(this,
                                               QStringLiteral("idle_maintenance_%1").arg(task.key),
                                               cancelToken,
                                               [task](const std::shared_ptr<std::atomic_bool>& cancelToken) {
        QString errorText;
        qInfo().noquote() << QStringLiteral("STARTUP [%1] [IDLE_BEGIN]").arg(task.key);
        if (!task.fn(&errorText)) {
            qWarning().noquote() << QStringLiteral("STARTUP [%1] [IDLE_FAIL] %2").arg(task.key, errorText);
        } else {
            qInfo().noquote() << QStringLiteral("STARTUP [%1] [IDLE_OK]").arg(task.key);
        }
        Q_UNUSED(cancelToken);
    });
    m_activeThread = thread;
    connect(thread, &QThread::finished, this, [guard, asyncHandle]() {
        if (!guard || !asyncHandle.shouldApply()) {
            return;
        }
        guard->m_activeThread = nullptr;
        guard->scheduleNextIdleCheck(guard->m_idleDelayMs);
    });
    thread->start();
}
