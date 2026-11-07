#pragma once

#include "core/backup/BackupDtos.h"

#include <QString>
#include <QVector>

class BackupPolicy
{
public:
    struct Config {
        bool enabled = true;
        QString protectionMode = QString::fromLatin1("auto");
        int maxBackupsPerDay = 250;
        int maxTotalSizeMb = 1024;
        bool keepPreviousDayLastSnapshot = true;
        bool cleanupOnStart = true;
        bool cleanupAfterBackup = true;
        bool emergencyBeforeRestore = true;
        bool showNotifications = true;
        bool detailedLogging = false;
        bool recoveryEnabled = true;
        bool recoveryAutosaveEnabled = true;
        int recoveryAutosaveIntervalMs = 15000;
        int recoveryRetentionDays = 3;
        bool recoveryShowStartupDialog = true;
    };

    static Config load();
    static void refreshFromRuntime();
    static void cleanupSnapshots(QString* errorText = nullptr);
    static void cleanupRecovery(QString* errorText = nullptr);
};
