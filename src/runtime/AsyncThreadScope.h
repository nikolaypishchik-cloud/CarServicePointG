#pragma once

#include <QCoreApplication>
#include <QObject>
#include <QPointer>
#include <QThread>
#include <QString>
#include <QDebug>

#include "core/diagnostics/StructuredDiagnosticsLogger.h"
#include "runtime/RuntimeJobDispatcher.h"

#include <atomic>
#include <exception>
#include <memory>
#include <type_traits>
#include <utility>

namespace AsyncThreadScope {

inline bool isCancellationRequested(const std::shared_ptr<std::atomic_bool>& cancelToken)
{
    return (cancelToken && cancelToken->load()) ||
           (QThread::currentThread() && QThread::currentThread()->isInterruptionRequested());
}

inline bool shouldContinue(const std::shared_ptr<std::atomic_bool>& cancelToken)
{
    return !isCancellationRequested(cancelToken);
}

template <typename Fn>
QThread* startWorker(QObject* owner,
                     const QString& objectName,
                     const std::shared_ptr<std::atomic_bool>& cancelToken,
                     Fn&& fn)
{
    const QPointer<QObject> ownerGuard(owner);
    auto* thread = QThread::create([owner, ownerGuard, objectName, cancelToken, task = std::forward<Fn>(fn)]() mutable {
        qInfo().noquote() << QStringLiteral("ASYNC_LIFECYCLE worker_enter name=") << objectName
                          << QStringLiteral(" threadId=") << QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()));
        if (RuntimeJob::RuntimeJobDispatcher::instance().isShutdownRequested()) {
            if (cancelToken) cancelToken->store(true);
            qWarning().noquote() << QStringLiteral("ASYNC_LIFECYCLE worker_cancelled_shutdown_requested name=") << objectName;
            return;
        }
        if (isCancellationRequested(cancelToken)) {
            qWarning().noquote() << QStringLiteral("ASYNC_LIFECYCLE worker_cancelled_before_start name=") << objectName;
            return;
        }
        if (owner && ownerGuard.isNull()) {
            if (cancelToken) cancelToken->store(true);
            qWarning().noquote() << QStringLiteral("ASYNC_LIFECYCLE worker_owner_dead_before_start name=") << objectName;
            return;
        }
        try {
            if constexpr (std::is_invocable_v<Fn, const std::shared_ptr<std::atomic_bool>&>) {
                task(cancelToken);
            } else if constexpr (std::is_invocable_v<Fn>) {
                task();
            }
        } catch (const std::exception& ex) {
            if (cancelToken) cancelToken->store(true);
            StructuredDiagnosticsLogger::logException(
                QStringLiteral("runtime.AsyncThreadScope"),
                QStringLiteral("worker_exception_std"),
                QStringLiteral("Async worker failed and cancellation token was requested."),
                QString::fromUtf8(ex.what()),
                {{QStringLiteral("workerName"), objectName},
                 {QStringLiteral("status"), QStringLiteral("failed_not_success")},
                 {QStringLiteral("cancelRequested"), isCancellationRequested(cancelToken)}});
            qCritical().noquote() << QStringLiteral("ASYNC_LIFECYCLE worker_exception name=") << objectName
                                  << QStringLiteral(" what=") << QString::fromUtf8(ex.what());
        } catch (...) {
            if (cancelToken) cancelToken->store(true);
            StructuredDiagnosticsLogger::logException(
                QStringLiteral("runtime.AsyncThreadScope"),
                QStringLiteral("worker_exception_unknown"),
                QStringLiteral("Async worker failed with an unknown exception and cancellation token was requested."),
                QStringLiteral("unknown_non_std_exception"),
                {{QStringLiteral("workerName"), objectName},
                 {QStringLiteral("status"), QStringLiteral("failed_not_success")},
                 {QStringLiteral("cancelRequested"), isCancellationRequested(cancelToken)}});
            qCritical().noquote() << QStringLiteral("ASYNC_LIFECYCLE worker_unknown_exception name=") << objectName;
        }
        qInfo().noquote() << QStringLiteral("ASYNC_LIFECYCLE worker_leave name=") << objectName
                          << QStringLiteral(" cancelled=") << isCancellationRequested(cancelToken);
    });
    thread->setObjectName(objectName);
    qInfo().noquote() << QStringLiteral("ASYNC_LIFECYCLE worker_created name=") << objectName
                      << QStringLiteral(" ownerAlive=") << !ownerGuard.isNull();

    if (auto* app = QCoreApplication::instance()) {
        QObject::connect(app, &QCoreApplication::aboutToQuit, thread, [thread, cancelToken]() {
            qWarning().noquote() << QStringLiteral("ASYNC_LIFECYCLE worker_cancel_requested_about_to_quit name=") << thread->objectName();
            if (cancelToken) cancelToken->store(true);
            thread->requestInterruption();
        });
    }

    QObject::connect(thread, &QThread::finished, thread, [thread]() {
        qInfo().noquote() << QStringLiteral("ASYNC_LIFECYCLE worker_finished name=") << thread->objectName();
        thread->deleteLater();
    });
    if (owner) {
        QObject::connect(owner, &QObject::destroyed, thread, [thread, cancelToken]() {
            qWarning().noquote() << QStringLiteral("ASYNC_LIFECYCLE worker_cancel_requested_owner_destroyed name=") << thread->objectName();
            if (cancelToken) cancelToken->store(true);
            thread->requestInterruption();
        });
    }

    return thread;
}

} // namespace AsyncThreadScope
