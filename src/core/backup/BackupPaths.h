#pragma once

#include <QString>

class BackupPaths
{
public:
    static void refreshFromRuntime();
    static QString configuredBackupsRoot();
    static QString configuredRecoveryRoot();
    static QString backupDayDir(const QString& isoDate = QString());
    static QString backupTempDir();
    static bool probeBackupRoot(const QString& rootPath, QString* errorText = nullptr);
    static bool isIndependentStorage(const QString& databasePath, const QString& backupPath, QString* detail = nullptr);
    static QString recoveryDayDir(const QString& isoDate = QString());
    static QString backupIndexPath();
    static QString backupManifestPath();
    static QString recoverySessionDir(const QString& sessionId, const QString& isoDate = QString());
};
