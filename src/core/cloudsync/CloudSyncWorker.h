#pragma once

#include <QObject>
#include <QTimer>
#include <QPointer>

class QThread;

class CloudSyncWorker : public QObject
{
    Q_OBJECT
public:
    explicit CloudSyncWorker(QObject* parent = nullptr);
    ~CloudSyncWorker() override;
    bool stopAndWait(int timeoutMs = 30000);

public slots:
    void start();
    void stop();
    void syncNow();

signals:
    void syncStarted();
    void syncFinished(bool ok, const QString& message);
    void dataReceived();

private:
    QTimer m_timer;
    bool m_running = false;
    bool m_stopped = false;
    quint64 m_generation = 0;
    QPointer<QThread> m_thread;
};
