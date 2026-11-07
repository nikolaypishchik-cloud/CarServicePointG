#include "core/perf/PerfWatchdog.h"

#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QDebug>

namespace {
QMutex& perfMutex()
{
    static QMutex m;
    return m;
}

QHash<QString, int>& jobCounters()
{
    static QHash<QString, int> counters;
    return counters;
}
}

namespace PerfWatchdog {

void noteSlowOperation(const QString& operation, qint64 elapsedMs, qint64 thresholdMs)
{
    if (elapsedMs < thresholdMs) return;
    qWarning().noquote() << QStringLiteral("[perf] %1 took %2 ms (threshold %3 ms)")
                                .arg(operation, QString::number(elapsedMs), QString::number(thresholdMs));
}

void asyncJobStarted(const QString& family, const QString& key)
{
    QMutexLocker lock(&perfMutex());
    const QString bucket = key.trimmed().isEmpty() ? family.trimmed() : (family.trimmed() + QStringLiteral("|") + key.trimmed());
    jobCounters()[bucket] += 1;
    qInfo().noquote() << QStringLiteral("RUNTIME_JOB started family=%1 key=%2 active=%3")
                             .arg(family.trimmed(), key.trimmed(), QString::number(jobCounters().value(bucket)));
    if (jobCounters().value(bucket) > 1) {
        qWarning().noquote() << QStringLiteral("[perf] duplicated async job: %1 active=%2")
                                    .arg(bucket, QString::number(jobCounters().value(bucket)));
    }
}

void asyncJobFinished(const QString& family, const QString& key)
{
    QMutexLocker lock(&perfMutex());
    const QString bucket = key.trimmed().isEmpty() ? family.trimmed() : (family.trimmed() + QStringLiteral("|") + key.trimmed());
    const int current = jobCounters().value(bucket);
    if (current <= 1) {
        jobCounters().remove(bucket);
        qInfo().noquote() << QStringLiteral("RUNTIME_JOB finished family=%1 key=%2 active=0")
                                 .arg(family.trimmed(), key.trimmed());
        return;
    }
    jobCounters()[bucket] = current - 1;
    qInfo().noquote() << QStringLiteral("RUNTIME_JOB finished family=%1 key=%2 active=%3")
                             .arg(family.trimmed(), key.trimmed(), QString::number(jobCounters().value(bucket)));
}

int activeAsyncJobs(const QString& family)
{
    QMutexLocker lock(&perfMutex());
    if (family.trimmed().isEmpty()) {
        int total = 0;
        for (auto it = jobCounters().cbegin(); it != jobCounters().cend(); ++it) total += it.value();
        return total;
    }

    int total = 0;
    const QString prefix = family.trimmed();
    for (auto it = jobCounters().cbegin(); it != jobCounters().cend(); ++it) {
        if (it.key() == prefix || it.key().startsWith(prefix + QStringLiteral("|"))) total += it.value();
    }
    return total;
}

ScopedTimer::ScopedTimer(QString operation, qint64 thresholdMs)
    : m_operation(std::move(operation))
    , m_thresholdMs(thresholdMs)
{
    m_timer.start();
}

ScopedTimer::~ScopedTimer()
{
    noteSlowOperation(m_operation, m_timer.elapsed(), m_thresholdMs);
}

qint64 ScopedTimer::elapsed() const
{
    return m_timer.elapsed();
}

} // namespace PerfWatchdog
