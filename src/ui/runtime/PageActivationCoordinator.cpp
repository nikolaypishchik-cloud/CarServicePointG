#include "ui/runtime/PageActivationCoordinator.h"
#include <functional>

#include "app/AppContext.h"
#include "core/backup/BackupPerformanceGuard.h"
#include "core/perf/PerfWatchdog.h"
#include "core/diagnostics/RuntimeLoggingControl.h"

#include <QDebug>
#include <QtGlobal>
#include <QTimer>
#include <exception>
#include <new>

PageActivationCoordinator::PageActivationCoordinator(QObject* parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
{
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        const int generation = m_generation;
        const QString pageName = m_pendingPageName;
        const auto callback = m_pendingCallback;
        m_pendingCallback = {};
        if (callback) {
            if (RuntimeLoggingControl::deepDebugEnabled()) {
                qInfo().noquote() << QStringLiteral("PAGE_OPEN [%1] [activation_begin]").arg(pageName);
            }
            m_pageTimer.restart();
            try {
                callback();
            } catch (const std::bad_alloc& e) {
                qCritical().noquote() << QStringLiteral("PAGE_OPEN [%1] [activation_bad_alloc] %2")
                                             .arg(pageName, QString::fromUtf8(e.what()));
                return;
            } catch (const std::exception& e) {
                qCritical().noquote() << QStringLiteral("PAGE_OPEN [%1] [activation_exception] %2")
                                             .arg(pageName, QString::fromUtf8(e.what()));
                return;
            } catch (...) {
                qCritical().noquote() << QStringLiteral("PAGE_OPEN [%1] [activation_unknown_exception]").arg(pageName);
                return;
            }
            const qint64 elapsed = m_pageTimer.elapsed();
            if (RuntimeLoggingControl::deepDebugEnabled()) {
                qInfo().noquote() << QStringLiteral("PAGE_OPEN [%1] [activation_done] %2 ms")
                                         .arg(pageName, QString::number(elapsed));
            }
            PerfWatchdog::noteSlowOperation(QStringLiteral("page-activation:%1").arg(pageName), elapsed, 220);
        }
        if (generation != m_generation) {
            if (RuntimeLoggingControl::deepDebugEnabled()) {
                qInfo().noquote() << QStringLiteral("PAGE_OPEN [%1] [stale_generation_skipped]").arg(pageName);
            }
        }
    });
}

void PageActivationCoordinator::requestActivation(int pageIndex,
                                                  const QString& pageName,
                                                  std::function<void()> callback,
                                                  int delayMs)
{
    m_pendingPageIndex = pageIndex;
    m_pendingPageName = pageName.trimmed().isEmpty() ? QStringLiteral("page_%1").arg(pageIndex) : pageName.trimmed();
    m_pendingCallback = std::move(callback);
    ++m_generation;
    if (RuntimeLoggingControl::deepDebugEnabled()) {
        qInfo().noquote() << QStringLiteral("PAGE_OPEN [%1] [activation_requested] delay=%2")
                                 .arg(m_pendingPageName, QString::number(qMax(0, delayMs)));
    }
    BackupPerformanceGuard::hold(AppContext::instance().backupManager(), qMax(1500, delayMs + 1200));
    m_timer->start(qMax(0, delayMs));
}