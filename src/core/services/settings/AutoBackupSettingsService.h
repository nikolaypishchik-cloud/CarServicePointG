#pragma once

#include <QString>

namespace AutoBackupSettingsService {

struct AutoBackupSettingsPayload {
    bool enabled = true;
    QString protectionMode = QString::fromLatin1("auto");
    bool emergencyBeforeRestore = true;
    bool keepPreviousDayLastSnapshot = true;
    bool cleanupOnStart = true;
    bool cleanupAfterBackup = true;
    bool showBackupNotifications = true;
    bool detailedLogging = false;
    QString backupDir;
    int maxBackupsPerDay = 250;
    int maxTotalSizeMb = 1024;
    bool recoveryEnabled = true;
    bool recoveryAutosaveEnabled = true;
    bool recoveryShowStartupDialog = true;
    int recoveryAutosaveIntervalMs = 15000;
    int recoveryRetentionDays = 3;
};

AutoBackupSettingsPayload load();
QString effectiveBackupDir();
bool save(const AutoBackupSettingsPayload& payload, bool* changed = nullptr);

} // namespace AutoBackupSettingsService
