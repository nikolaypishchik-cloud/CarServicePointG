#pragma once

#include <QString>

class AppRuntime
{
public:
    static QString dataDir();
    static QString logsDir();
    static QString logFilePath();
    static QString dbFilePath();
    static QString activeDbFilePath();
    static QString legacyDbFilePath();
    static QString archiveDbFilePath();
    static QString registryDbFilePath();

    static QString securityDir();
    static QString sessionDir();
    static QString backupsDir();
    static QString backupTempDir();
    static QString recoveryDir();
    static QString recoveryTempDir();
    static QString sealedActiveDbFilePath();
    static QString sealedLegacyDbFilePath();
    static QString sealedArchiveDbFilePath();
    static QString sealedRegistryDbFilePath();
    static QString masterKeyFilePath();
    static QString pendingLegacyImportFilePath();

    static void ensureDataAtRestProtection();
    static bool protectPathAtRest(const QString& path);
};
