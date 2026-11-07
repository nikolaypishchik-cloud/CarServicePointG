#include "core/cloudsync/CloudSyncWorker.h"

#include "core/cloudsync/CloudSyncClient.h"
#include "core/cloudsync/CloudSyncSettings.h"
#include "infrastructure/db/sqlite/Db.h"
#include "runtime/RuntimeJobDispatcher.h"

#include <QMetaObject>
#include <QPointer>
#include <QThread>
#include <QtGlobal>
#include <QScopeGuard>
#include <QDebug>

CloudSyncWorker::CloudSyncWorker(QObject* parent)
    : QObject(parent)
{
    m_timer.setSingleShot(false);
    connect(&m_timer, &QTimer::timeout, this, &CloudSyncWorker::syncNow);
}

CloudSyncWorker::~CloudSyncWorker()
{
    stop();
    // Never detach a writer while runtime services or database files are torn down.
    if (m_thread) m_thread->wait();
}

bool CloudSyncWorker::stopAndWait(int timeoutMs)
{
    stop();
    const bool stopped = !m_thread || m_thread->wait(qMax(0, timeoutMs));
    if (stopped) m_running = false;
    return stopped;
}

void CloudSyncWorker::start()
{
    if (RuntimeJob::RuntimeJobDispatcher::instance().isShutdownRequested()) return;
    m_stopped = false;
    const auto generation = ++m_generation;
    const auto settings = CloudSyncSettings::load();
    if (!settings.enabled) {
        stop();
        return;
    }
    m_timer.start(qMax(10, settings.intervalSeconds) * 1000);
    QTimer::singleShot(1000, this, [this, generation] {
        if (!m_stopped && generation == m_generation) syncNow();
    });
}

void CloudSyncWorker::stop()
{
    m_timer.stop();
    m_stopped = true;
    ++m_generation;
    if (m_thread) m_thread->requestInterruption();
}

void CloudSyncWorker::syncNow()
{
    if (m_stopped || m_running || (m_thread && m_thread->isRunning())
        || RuntimeJob::RuntimeJobDispatcher::instance().isShutdownRequested()) return;
    m_running = true;
    const auto generation = m_generation;
    emit syncStarted();

    QPointer<CloudSyncWorker> guard(this);
    auto* thread = QThread::create([guard, generation]() {
        const auto closeConnection = qScopeGuard([] { Database::close(); });
        QString error;
        int appliedEvents = 0;
        bool ok = false;
        try {
            ok = CloudSyncClient::syncNow(&error, &appliedEvents);
        } catch (...) {
            error = QString::fromUtf8("Не вдалося завершити синхронізацію. Перевірте журнал помилок.");
            qCritical() << "Cloud sync worker failed";
        }
        // CloudSync opens a thread-local SQLite connection, so it must close
        // that connection in the same worker thread before it exits.
        if (!guard) return;
        QMetaObject::invokeMethod(guard, [guard, generation, ok, error, appliedEvents]() {
            if (!guard || guard->m_stopped || generation != guard->m_generation) return;
            // Committed changes remain valid even if another event conflicts.
            if (appliedEvents > 0) emit guard->dataReceived();
            emit guard->syncFinished(ok, ok ? QString::fromUtf8("Синхронізацію завершено.") : error);
            guard->m_running = false;
        }, Qt::QueuedConnection);
    });
    m_thread = thread;
    connect(thread, &QThread::finished, this, [this, thread] {
        if (m_thread == thread) {
            m_thread.clear();
            m_running = false;
        }
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}
