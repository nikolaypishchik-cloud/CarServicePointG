#pragma once

#include "core/backup/BackupDtos.h"
#include "core/backup/RecoveryUiContracts.h"

#include <memory>

class BackupManager;
class RecoveryCoordinator;
class RecoveryUiFactory;
class QWidget;

class RecoveryUiOrchestrator
{
public:
    explicit RecoveryUiOrchestrator(std::shared_ptr<const RecoveryUiFactory> factory);

    RecoveryUiRestoreResult restoreDraft(const RecoveryUiRestoreRequest& request,
                                         QWidget* parent,
                                         BackupManager* backupManager,
                                         RecoveryCoordinator* recoveryCoordinator) const;

private:
    std::shared_ptr<const RecoveryUiFactory> m_factory;
};
