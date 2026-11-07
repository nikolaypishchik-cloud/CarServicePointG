#pragma once

#include <QDateTime>
#include <QHash>
#include <QMutex>
#include <QString>
#include <QtGlobal>

#include <atomic>
#include <memory>

namespace RuntimeJob {

enum class Family {
    CurrentOrders,
    History,
    Parts,
    WorkPartsDialog,
    Reports,
    Import,
    Backup,
    Update,
    Other,
};

enum class Status {
    Queued,
    Running,
    Cancelled,
    Failed,
    Succeeded,
};

struct Ticket {
    quint64 jobId = 0;
    Family family = Family::Other;
    quint64 generation = 0;
    int timeoutMs = 0;
    int priority = 0;
    QString owner;
    QString cacheKey;
    QDateTime startedAtUtc;
    std::shared_ptr<std::atomic_bool> cancelToken;

    [[nodiscard]] bool isValid() const { return jobId != 0 && cancelToken != nullptr; }
    [[nodiscard]] bool isCancelled() const { return cancelToken && cancelToken->load(); }
};

struct Snapshot {
    quint64 jobId = 0;
    Family family = Family::Other;
    quint64 generation = 0;
    int timeoutMs = 0;
    int priority = 0;
    QString owner;
    QString cacheKey;
    QDateTime startedAtUtc;
    QDateTime finishedAtUtc;
    Status status = Status::Queued;
};

class RuntimeJobDispatcher final
{
public:
    static RuntimeJobDispatcher& instance();

    Ticket startLatest(Family family,
                       const QString& owner,
                       int timeoutMs,
                       int priority = 0,
                       const QString& cacheKey = QString());
    void markRunning(const Ticket& ticket);
    void finish(const Ticket& ticket, Status status);
    void cancelFamily(Family family);
    void cancelAll(const QString& reason = QString());
    void enterShutdown(const QString& reason = QString());
    void leaveShutdown();
    [[nodiscard]] bool isShutdownRequested() const;
    [[nodiscard]] bool shouldApply(const Ticket& ticket) const;
    [[nodiscard]] Snapshot familySnapshot(Family family) const;

    static QString familyName(Family family);
    static QString statusName(Status status);
    static bool isUiThread();
    static void warnIfUiThreadBlockingCall(const QString& operationName);

private:
    RuntimeJobDispatcher() = default;

    mutable QMutex m_mutex;
    QHash<int, Snapshot> m_latestByFamily;
    QHash<int, std::shared_ptr<std::atomic_bool>> m_cancelByFamily;
    quint64 m_nextJobId = 0;
    QHash<int, quint64> m_generationByFamily;
    bool m_shutdownRequested = false;
    QString m_shutdownReason;
};

} // namespace RuntimeJob
