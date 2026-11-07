#include "runtime/RuntimeServices.h"

#include "app/AppContext.h"

#include "core/backup/BackupManager.h"
#include "core/backup/RestoreCoordinator.h"

BackupManager* RuntimeServices::backupManager()
{
    return AppContext::instance().backupManager();
}

RestoreCoordinator* RuntimeServices::restoreCoordinator()
{
    return AppContext::instance().restoreCoordinator();
}
