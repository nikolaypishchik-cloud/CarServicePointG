#pragma once

#include <QObject>

#include <atomic>
#include <memory>
#include <QString>
#include <QList>
#include <functional>

class QTimer;
class QThread;
class QEvent;

class IdleMaintenanceScheduler final : public QObject
{
    Q_OBJECT
public:
    explicit IdleMaintenanceScheduler(QObject* parent = nullptr);
    ~IdleMaintenanceScheduler() override;

    void start(int initialDelayMs = 12000, int idleDelayMs = 6000);
    void noteUserInteraction();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Task {
        QString key;
        std::function<bool(QString*)> fn;
    };

    void scheduleNextIdleCheck(int delayMs = -1);
    void tryRunNextTask();
    void runTask(const Task& task);
    QList<Task> m_tasks;
    QTimer* m_idleTimer = nullptr;
    QThread* m_activeThread = nullptr;
    std::shared_ptr<std::atomic_bool> m_cancelToken;
    int m_idleDelayMs = 6000;
    qint64 m_lastInteractionMs = 0;
    int m_nextTaskIndex = 0;
    bool m_started = false;
};
