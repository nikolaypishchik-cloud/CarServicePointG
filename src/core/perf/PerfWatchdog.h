#pragma once

#include <QElapsedTimer>
#include <QString>

namespace PerfWatchdog {

void noteSlowOperation(const QString& operation, qint64 elapsedMs, qint64 thresholdMs = 120);
void asyncJobStarted(const QString& family, const QString& key = QString());
void asyncJobFinished(const QString& family, const QString& key = QString());
int activeAsyncJobs(const QString& family = QString());

class ScopedTimer final
{
public:
    explicit ScopedTimer(QString operation, qint64 thresholdMs = 120);
    ~ScopedTimer();

    qint64 elapsed() const;

private:
    QString m_operation;
    qint64 m_thresholdMs = 120;
    QElapsedTimer m_timer;
};

} // namespace PerfWatchdog
