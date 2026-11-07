#pragma once

#include <QString>

class App
{
public:
    static QString appName();
    static QString orgName();

    // %APPDATA%/CarServicePointG (Windows) / writable location cross-platform
    static QString dataDir();
    static QString logsDir();
    static QString logFilePath();
    static QString dbFilePath();
    static QString activeDbFilePath();
    static QString legacyDbFilePath();
    static QString archiveDbFilePath();
    static QString registryDbFilePath();

    // Persistent encrypted-at-rest containers (sealed on disk).
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

    static void installLogHandler();
    static void shutdownLogHandler();

    // Best-effort Windows at-rest protection for local app data.
    // On NTFS this uses EFS; on other platforms/filesystems it is a no-op.
    static void ensureDataAtRestProtection();
    static bool protectPathAtRest(const QString& path);
};
