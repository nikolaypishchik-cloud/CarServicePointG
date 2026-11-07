#pragma once

#include "core/backup/BackupDtos.h"
#include "core/services/NativeBackupRestoreService.h"

#include <QObject>
#include <QString>
#include <QVector>

class BackupManager;
class CrashRecoveryManager;

class RestoreCoordinator : public QObject
{
    Q_OBJECT
public:
    explicit RestoreCoordinator(BackupManager* backupManager, CrashRecoveryManager* crashRecoveryManager = nullptr, QObject* parent = nullptr);

    QVector<SnapshotMetadata> listSnapshots() const;
    bool restoreSnapshot(const SnapshotMetadata& meta, NativeBackupRestoreResult* result, QString* errorText = nullptr);

private:
    BackupManager* m_backupManager = nullptr;
    CrashRecoveryManager* m_crashRecoveryManager = nullptr;
};
