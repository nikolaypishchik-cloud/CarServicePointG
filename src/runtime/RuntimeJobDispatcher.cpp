#include "runtime/RuntimeJobDispatcher.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QList>
#include <QMutexLocker>
#include <QThread>

namespace RuntimeJob {
namespace {
int familyKey(Family family)
{
    return static_cast<int>(family);
}

QList<Family> allFamilies()
{
    return {Family::CurrentOrders,
            Family::History,
            Family::Parts,
            Family::WorkPartsDialog,
            Family::Reports,
            Family::Import,
            Family::Backup,
            Family::Update,
            Family::Other};
}
}

RuntimeJobDispatcher& RuntimeJobDispatcher::instance()
{
    static RuntimeJobDispatcher dispatcher;
    return dispatcher;
}

Ticket RuntimeJobDispatcher::startLatest(Family family,
                                         const QString& owner,
                                         int timeoutMs,
                                         int priority,
                                         const QString& cacheKey)
{
    QMutexLocker locker(&m_mutex);
    const int key = familyKey(family);
    if (auto existing = m_cancelByFamily.value(key)) {
        existing->store(true);
    }

    auto cancelToken = std::make_shared<std::atomic_bool>(m_shutdownRequested);
    m_cancelByFamily.insert(key, cancelToken);

    Ticket ticket;
    ticket.jobId = ++m_nextJobId;
    ticket.family = family;
    ticket.generation = ++m_generationByFamily[key];
    ticket.timeoutMs = timeoutMs;
    ticket.priority = priority;
    ticket.owner = owner;
    ticket.cacheKey = cacheKey.trimmed().left(512);
    ticket.startedAtUtc = QDateTime::currentDateTimeUtc();
    ticket.cancelToken = cancelToken;

    Snapshot snapshot;
    snapshot.jobId = ticket.jobId;
    snapshot.family = ticket.family;
    snapshot.generation = ticket.generation;
    snapshot.timeoutMs = ticket.timeoutMs;
    snapshot.priority = ticket.priority;
    snapshot.owner = ticket.owner;
    snapshot.cacheKey = ticket.cacheKey;
    snapshot.startedAtUtc = ticket.startedAtUtc;
    snapshot.status = m_shutdownRequested ? Status::Cancelled : Status::Queued;
    if (m_shutdownRequested) {
        snapshot.finishedAtUtc = QDateTime::currentDateTimeUtc();
    }
    m_latestByFamily.insert(key, snapshot);

    qInfo().noquote() << QStringLiteral("RUNTIME_JOB queued family=") << familyName(family)
                      << QStringLiteral(" jobId=") << ticket.jobId
                      << QStringLiteral(" generation=") << ticket.generation
                      << QStringLiteral(" owner=") << owner
                      << QStringLiteral(" cacheKey=") << ticket.cacheKey
                      << QStringLiteral(" timeoutMs=") << timeoutMs
                      << QStringLiteral(" shutdownRequested=") << m_shutdownRequested
                      << QStringLiteral(" shutdownReason=") << m_shutdownReason;
    return ticket;
}

void RuntimeJobDispatcher::markRunning(const Ticket& ticket)
{
    QMutexLocker locker(&m_mutex);
    const int key = familyKey(ticket.family);
    auto snapshot = m_latestByFamily.value(key);
    if (snapshot.jobId != ticket.jobId) return;
    snapshot.status = Status::Running;
    m_latestByFamily.insert(key, snapshot);
}

void RuntimeJobDispatcher::finish(const Ticket& ticket, Status status)
{
    QMutexLocker locker(&m_mutex);
    const int key = familyKey(ticket.family);
    auto snapshot = m_latestByFamily.value(key);
    if (snapshot.jobId != ticket.jobId) return;
    snapshot.status = status;
    snapshot.finishedAtUtc = QDateTime::currentDateTimeUtc();
    m_latestByFamily.insert(key, snapshot);
    if (auto current = m_cancelByFamily.value(key); current == ticket.cancelToken) {
        m_cancelByFamily.remove(key);
    }
    qInfo().noquote() << QStringLiteral("RUNTIME_JOB finished family=") << familyName(ticket.family)
                      << QStringLiteral(" jobId=") << ticket.jobId
                      << QStringLiteral(" status=") << statusName(status);
}

void RuntimeJobDispatcher::cancelFamily(Family family)
{
    QMutexLocker locker(&m_mutex);
    const int key = familyKey(family);
    if (auto existing = m_cancelByFamily.value(key)) {
        existing->store(true);
    }
    auto snapshot = m_latestByFamily.value(key);
    if (snapshot.jobId != 0) {
        snapshot.status = Status::Cancelled;
        snapshot.finishedAtUtc = QDateTime::currentDateTimeUtc();
        m_latestByFamily.insert(key, snapshot);
    }
}
void RuntimeJobDispatcher::cancelAll(const QString& reason)
{
    QMutexLocker locker(&m_mutex);
    for (Family family : allFamilies()) {
        const int key = familyKey(family);
        if (auto existing = m_cancelByFamily.value(key)) {
            existing->store(true);
        }
        auto snapshot = m_latestByFamily.value(key);
        if (snapshot.jobId != 0 && snapshot.status != Status::Succeeded && snapshot.status != Status::Failed) {
            snapshot.status = Status::Cancelled;
            snapshot.finishedAtUtc = QDateTime::currentDateTimeUtc();
            m_latestByFamily.insert(key, snapshot);
        }
    }
    qWarning().noquote() << QStringLiteral("RUNTIME_JOB cancel_all reason=") << reason;
}

void RuntimeJobDispatcher::enterShutdown(const QString& reason)
{
    {
        QMutexLocker locker(&m_mutex);
        m_shutdownRequested = true;
        m_shutdownReason = reason;
    }
    cancelAll(reason);
    qWarning().noquote() << QStringLiteral("RUNTIME_JOB shutdown_enter reason=") << reason;
}

void RuntimeJobDispatcher::leaveShutdown()
{
    QMutexLocker locker(&m_mutex);
    m_shutdownRequested = false;
    m_shutdownReason.clear();
    qInfo().noquote() << QStringLiteral("RUNTIME_JOB shutdown_leave");
}

bool RuntimeJobDispatcher::isShutdownRequested() const
{
    QMutexLocker locker(&m_mutex);
    return m_shutdownRequested;
}

bool RuntimeJobDispatcher::shouldApply(const Ticket& ticket) const
{
    QMutexLocker locker(&m_mutex);
    const int key = familyKey(ticket.family);
    const auto snapshot = m_latestByFamily.value(key);
    return !m_shutdownRequested
        && ticket.isValid()
        && !ticket.isCancelled()
        && snapshot.jobId == ticket.jobId
        && snapshot.generation == ticket.generation;
}

Snapshot RuntimeJobDispatcher::familySnapshot(Family family) const
{
    QMutexLocker locker(&m_mutex);
    return m_latestByFamily.value(familyKey(family));
}

QString RuntimeJobDispatcher::familyName(Family family)
{
    switch (family) {
    case Family::CurrentOrders: return QStringLiteral("current_orders");
    case Family::History: return QStringLiteral("history");
    case Family::Parts: return QStringLiteral("parts");
    case Family::WorkPartsDialog: return QStringLiteral("workparts_dialog");
    case Family::Reports: return QStringLiteral("reports");
    case Family::Import: return QStringLiteral("import");
    case Family::Backup: return QStringLiteral("backup");
    case Family::Update: return QStringLiteral("update");
    case Family::Other: return QStringLiteral("other");
    }
    return QStringLiteral("other");
}

QString RuntimeJobDispatcher::statusName(Status status)
{
    switch (status) {
    case Status::Queued: return QStringLiteral("queued");
    case Status::Running: return QStringLiteral("running");
    case Status::Cancelled: return QStringLiteral("cancelled");
    case Status::Failed: return QStringLiteral("failed");
    case Status::Succeeded: return QStringLiteral("succeeded");
    }
    return QStringLiteral("unknown");
}

bool RuntimeJobDispatcher::isUiThread()
{
    auto* app = QCoreApplication::instance();
    return app && QThread::currentThread() == app->thread();
}

void RuntimeJobDispatcher::warnIfUiThreadBlockingCall(const QString& operationName)
{
    if (!isUiThread()) return;

    // P0/P2 performance logging: blocking guards can be reached from hot UI
    // fallback paths. Keep the signal, but rate-limit identical operations so
    // production logs do not flood while a user clicks/imports repeatedly.
    static QMutex rateLimitMutex;
    static QHash<QString, qint64> lastWarningMs;
    const QString key = operationName.trimmed().left(240);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    {
        QMutexLocker locker(&rateLimitMutex);
        const auto it = lastWarningMs.constFind(key);
        if (it != lastWarningMs.constEnd() && now - *it < 10000) {
            return;
        }
        if (lastWarningMs.size() > 512) {
            lastWarningMs.clear();
        }
        lastWarningMs.insert(key, now);
    }

    qWarning().noquote() << QStringLiteral("RUNTIME_BLOCKING_GUARD ui_thread_heavy_call operation=")
                         << operationName
                         << QStringLiteral(" action=move_to_RuntimeJobDispatcher_worker");
}

} // namespace RuntimeJob
