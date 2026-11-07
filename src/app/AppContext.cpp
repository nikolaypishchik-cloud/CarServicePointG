#include "app/AppContext.h"

#include "core/backup/BackupManager.h"
#include "core/backup/BackupPolicy.h"
#include "core/backup/CrashRecoveryManager.h"
#include "core/backup/RecoveryCoordinator.h"
#include "core/backup/RestoreCoordinator.h"
#include "core/cloudsync/CloudSyncWorker.h"
#include "runtime/OrderDialogRecoveryUiFactory.h"
#include "runtime/RecoveryUiOrchestrator.h"
#include "runtime/RuntimeJobDispatcher.h"

#include <QApplication>
#include <memory>
#include <functional>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QTimer>
#include <QWidget>

AppContext::AppContext() = default;
AppContext::~AppContext() = default;

AppContext& AppContext::instance()
{
    static AppContext ctx;
    return ctx;
}

namespace {

bool hasVisiblePrimaryWindow()
{
    const auto widgets = QApplication::topLevelWidgets();
    for (QWidget* widget : widgets) {
        if (!widget || !widget->isVisible() || !widget->isWindow()) continue;
        const QByteArray className = widget->metaObject() ? QByteArray(widget->metaObject()->className()) : QByteArray();
        if (className.contains("SplashScreen")) continue;
        if ((widget->windowFlags() & Qt::Tool) == Qt::Tool) continue;
        return true;
    }
    return false;
}

} // namespace

void AppContext::initializeRuntimeServices()
{
    if (m_runtimeServicesHealthy) {
        return;
    }

    RuntimeJob::RuntimeJobDispatcher::instance().leaveShutdown();
    m_runtimeServicesError.clear();

    try {
        if (!m_backupManager) {
            m_backupManager = std::make_unique<BackupManager>();
        }
        if (!m_crashRecoveryManager) {
            m_crashRecoveryManager = std::make_unique<CrashRecoveryManager>();
            try {
                m_crashRecoveryManager->start();
            } catch (const std::exception& e) {
                m_runtimeServicesError = QStringLiteral("CrashRecoveryManager::start failed: %1").arg(QString::fromLocal8Bit(e.what()));
                qCritical() << m_runtimeServicesError;
                m_crashRecoveryManager.reset();
            } catch (...) {
                m_runtimeServicesError = QStringLiteral("CrashRecoveryManager::start failed with unknown exception");
                qCritical() << m_runtimeServicesError;
                m_crashRecoveryManager.reset();
            }
        }
        if (!m_restoreCoordinator) {
            m_restoreCoordinator = std::make_unique<RestoreCoordinator>(m_backupManager.get(), m_crashRecoveryManager.get());
        }
        if (!m_recoveryCoordinator && m_crashRecoveryManager) {
            m_recoveryCoordinator = std::make_unique<RecoveryCoordinator>(m_crashRecoveryManager.get());
        }
        if (!m_recoveryUiFactory) {
            m_recoveryUiFactory = std::make_shared<OrderDialogRecoveryUiFactory>();
        }
        if (!m_recoveryUiOrchestrator) {
            m_recoveryUiOrchestrator = std::make_unique<RecoveryUiOrchestrator>(m_recoveryUiFactory);
        }
        if (m_recoveryCoordinator && m_recoveryUiOrchestrator) {
            m_recoveryCoordinator->setRestoreHandler([this](const RecoveryUiRestoreRequest& request, QWidget* parent) -> RecoveryUiRestoreResult {
                return m_recoveryUiOrchestrator->restoreDraft(request, parent, backupManager(), m_recoveryCoordinator.get());
            });
        }
        if (!m_cloudSyncWorker) {
            m_cloudSyncWorker = std::make_unique<CloudSyncWorker>();
        }
        m_cloudSyncWorker->start();

        if (m_backupManager) {
            auto deferredMaintenance = std::make_shared<std::function<void()>>();
            auto maintenanceAttempts = std::make_shared<int>(0);
            *deferredMaintenance = [this, deferredMaintenance, maintenanceAttempts]() {
                try {
                    if (!m_backupManager) return;
                    const auto* guiApp = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
                    const bool hasBlockingUi = QApplication::activeModalWidget() || QApplication::activePopupWidget();
                    const bool appActive = guiApp && guiApp->applicationState() == Qt::ApplicationActive;
                    const bool interactiveWindowVisible = hasVisiblePrimaryWindow();
                    const bool startupWindowNotReady = !interactiveWindowVisible;
                    const bool shouldDelay = hasBlockingUi
                        || startupWindowNotReady
                        || !appActive
                        || *maintenanceAttempts < 6;
                    if (shouldDelay) {
                        ++(*maintenanceAttempts);
                        qInfo() << "Deferred backup maintenance postponed"
                                << "attempt" << *maintenanceAttempts
                                << "appActive" << appActive
                                << "interactiveWindowVisible" << interactiveWindowVisible
                                << "hasBlockingUi" << hasBlockingUi;
                        QTimer::singleShot(5000, m_backupManager.get(), *deferredMaintenance);
                        return;
                    }
                    qInfo() << "Deferred backup maintenance started";
                    m_backupManager->reconcile(nullptr);
                    if (BackupPolicy::load().cleanupOnStart) {
                        m_backupManager->runCleanup(nullptr);
                    }
                    qInfo() << "Deferred backup maintenance finished";
                } catch (const std::exception& e) {
                    qCritical() << "Deferred backup maintenance failed:" << e.what();
                } catch (...) {
                    qCritical() << "Deferred backup maintenance failed with unknown exception";
                }
            };
            QTimer::singleShot(15000, m_backupManager.get(), *deferredMaintenance);
        }
        if (m_crashRecoveryManager) {
            try {
                m_crashRecoveryManager->cleanupExpiredData(nullptr);
            } catch (const std::exception& e) {
                const QString err = QStringLiteral("CrashRecoveryManager::cleanupExpiredData failed: %1").arg(QString::fromLocal8Bit(e.what()));
                qCritical() << err;
                if (m_runtimeServicesError.isEmpty()) {
                    m_runtimeServicesError = err;
                }
            } catch (...) {
                const QString err = QStringLiteral("CrashRecoveryManager::cleanupExpiredData failed with unknown exception");
                qCritical() << err;
                if (m_runtimeServicesError.isEmpty()) {
                    m_runtimeServicesError = err;
                }
            }
        }

        m_runtimeServicesHealthy = true;
    } catch (const std::exception& e) {
        m_runtimeServicesHealthy = false;
        m_runtimeServicesError = QStringLiteral("initializeRuntimeServices failed: %1").arg(QString::fromLocal8Bit(e.what()));
        qCritical() << m_runtimeServicesError;
        m_recoveryUiOrchestrator.reset();
        m_recoveryCoordinator.reset();
        m_restoreCoordinator.reset();
        m_crashRecoveryManager.reset();
        m_backupManager.reset();
    } catch (...) {
        m_runtimeServicesHealthy = false;
        m_runtimeServicesError = QStringLiteral("initializeRuntimeServices failed with unknown exception");
        qCritical() << m_runtimeServicesError;
        m_recoveryUiOrchestrator.reset();
        m_recoveryCoordinator.reset();
        m_restoreCoordinator.reset();
        m_crashRecoveryManager.reset();
        m_backupManager.reset();
    }
}

void AppContext::shutdownRuntimeServices()
{
    qInfo() << "Runtime services shutdown started";
    RuntimeJob::RuntimeJobDispatcher::instance().enterShutdown(QStringLiteral("AppContext::shutdownRuntimeServices"));

    const bool syncStopped = !m_cloudSyncWorker || m_cloudSyncWorker->stopAndWait();
    if (!syncStopped) qCritical() << "Cloud sync shutdown timed out; recovery session remains dirty";

    try {
        if (m_crashRecoveryManager) {
            if (syncStopped) {
                m_crashRecoveryManager->markCleanShutdown();
            } else {
                QString error;
                if (!m_crashRecoveryManager->flushDirtyDraftsBeforeCriticalOperation(&error))
                    qCritical() << "Shutdown draft flush failed";
            }
        }
    } catch (const std::exception& e) {
        qCritical() << "shutdownRuntimeServices markCleanShutdown failed:" << e.what();
    } catch (...) {
        qCritical() << "shutdownRuntimeServices markCleanShutdown failed with unknown exception";
    }

    try {
        // Reset in dependency order. BackupManager::~BackupManager() performs
        // queue shutdown, pending autobackup flush, manifest worker stop and
        // cleanup worker stop before Database::close() is called by the update
        // handoff path.
        if (m_cloudSyncWorker) {
            m_cloudSyncWorker->stop();
        }
        m_cloudSyncWorker.reset();
        m_recoveryUiOrchestrator.reset();
        m_recoveryCoordinator.reset();
        m_restoreCoordinator.reset();
        m_backupManager.reset();
        m_crashRecoveryManager.reset();
        m_recoveryUiFactory.reset();

        m_runtimeServicesHealthy = false;
        m_runtimeServicesError.clear();

        qInfo() << "Runtime services shutdown completed";
    } catch (const std::exception& e) {
        qCritical() << "shutdownRuntimeServices reset failed:" << e.what();
    } catch (...) {
        qCritical() << "shutdownRuntimeServices reset failed with unknown exception";
    }
}

void AppContext::refreshCloudSync()
{
    if (!m_cloudSyncWorker) return;
    m_cloudSyncWorker->start();
}

CloudSyncWorker* AppContext::cloudSyncWorker() const
{
    return m_cloudSyncWorker.get();
}

bool AppContext::runtimeServicesHealthy() const
{
    return m_runtimeServicesHealthy;
}

QString AppContext::runtimeServicesError() const
{
    return m_runtimeServicesError;
}

BackupManager* AppContext::backupManager() const
{
    return m_backupManager.get();
}

CrashRecoveryManager* AppContext::crashRecoveryManager() const
{
    return m_crashRecoveryManager.get();
}

RestoreCoordinator* AppContext::restoreCoordinator() const
{
    return m_restoreCoordinator.get();
}

RecoveryCoordinator* AppContext::recoveryCoordinator() const
{
    return m_recoveryCoordinator.get();
}
