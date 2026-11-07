#pragma once

class BackupManager;
class RestoreCoordinator;

class RuntimeServices
{
public:
    static BackupManager* backupManager();
    static RestoreCoordinator* restoreCoordinator();
};
