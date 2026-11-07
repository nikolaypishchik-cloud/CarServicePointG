#include "app/DeferredStartupMaintenance.h"
#include "runtime/AsyncTaskHandle.h"
#include "runtime/AsyncThreadScope.h"

#include "core/services/startup/StartupDatabaseFacade.h"

#include <QCoreApplication>
#include <QDebug>
#include <QPointer>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <memory>

namespace {
void runMaintenanceTask(const char* key, bool (*fn)(QString*))
{
    QString errorText;
    qInfo().noquote() << QStringLiteral("STARTUP [%1] [DEFERRED_BEGIN]").arg(QString::fromLatin1(key));
    if (!fn(&errorText)) {
        qWarning().noquote() << QStringLiteral("STARTUP [%1] [DEFERRED_FAIL] %2")
                                .arg(QString::fromLatin1(key), errorText);
    } else {
        qInfo().noquote() << QStringLiteral("STARTUP [%1] [DEFERRED_OK]").arg(QString::fromLatin1(key));
    }
}
}

void DeferredStartupMaintenance::schedule(QObject* owner, int initialDelayMs)
{
    QPointer<QObject> guard(owner);
    QTimer::singleShot(qMax(0, initialDelayMs), owner, [guard]() {
        if (!guard) return;
        const auto cancelToken = std::make_shared<std::atomic_bool>(false);
        const AsyncTaskHandle asyncHandle(guard.data(), 0, cancelToken);
        auto* thread = AsyncThreadScope::startWorker(guard.data(),
                                                   QStringLiteral("startup_deferred_maintenance"),
                                                   cancelToken,
                                                   [cancelToken]() {
            runMaintenanceTask("migration_check", &StartupDatabaseFacade::runStartupMigrationCheck);
            if (cancelToken->load()) return;
            runMaintenanceTask("archive_rollover_maintenance", &StartupDatabaseFacade::runStartupArchiveMaintenance);
        });
        QObject::connect(thread, &QThread::finished, guard.data(), [asyncHandle]() { if (!asyncHandle.shouldApply()) return; });
        thread->start();
    });
}
