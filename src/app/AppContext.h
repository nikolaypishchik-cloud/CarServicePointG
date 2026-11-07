#pragma once

#include <memory>
#include <QString>

class BackupManager;
class CrashRecoveryManager;
class RestoreCoordinator;
class RecoveryCoordinator;
class RecoveryUiFactory;
class RecoveryUiOrchestrator;
class CloudSyncWorker;

class AppContext {
public:
    AppContext();
    ~AppContext();

    static AppContext& instance();

    void initializeRuntimeServices();
    void shutdownRuntimeServices();
    void refreshCloudSync();
    CloudSyncWorker* cloudSyncWorker() const;

    bool runtimeServicesHealthy() const;
    QString runtimeServicesError() const;

    BackupManager* backupManager() const;
    CrashRecoveryManager* crashRecoveryManager() const;
    RestoreCoordinator* restoreCoordinator() const;
    RecoveryCoordinator* recoveryCoordinator() const;

private:
    std::unique_ptr<BackupManager> m_backupManager;
    std::unique_ptr<CrashRecoveryManager> m_crashRecoveryManager;
    std::unique_ptr<RestoreCoordinator> m_restoreCoordinator;
    std::unique_ptr<RecoveryCoordinator> m_recoveryCoordinator;
    std::shared_ptr<const RecoveryUiFactory> m_recoveryUiFactory;
    std::unique_ptr<RecoveryUiOrchestrator> m_recoveryUiOrchestrator;
    std::unique_ptr<CloudSyncWorker> m_cloudSyncWorker;
    bool m_runtimeServicesHealthy = false;
    QString m_runtimeServicesError;
};
