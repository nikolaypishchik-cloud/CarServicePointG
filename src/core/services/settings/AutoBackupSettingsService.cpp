#include "core/services/settings/AutoBackupSettingsService.h"

#include "core/backup/BackupPaths.h"
#include "infrastructure/db/sqlite/Db.h"

namespace AutoBackupSettingsService {


AutoBackupSettingsPayload load()
{
    AutoBackupSettingsPayload payload;
    payload.enabled = Database::getSetting(QStringLiteral("settings.int.autobackup.enabled"), true).toBool();
    payload.protectionMode = Database::getSetting(QStringLiteral("settings.int.autobackup.protection_mode"), QStringLiteral("auto")).toString().trimmed();
    if (payload.protectionMode.isEmpty()) payload.protectionMode = QStringLiteral("auto");
    payload.emergencyBeforeRestore = Database::getSetting(QStringLiteral("settings.int.autobackup.emergency_before_restore"), true).toBool();
    payload.keepPreviousDayLastSnapshot = Database::getSetting(QStringLiteral("settings.int.autobackup.keep_previous_day_last_snapshot"), true).toBool();
    payload.cleanupOnStart = Database::getSetting(QStringLiteral("settings.int.autobackup.cleanup_on_start"), true).toBool();
    payload.cleanupAfterBackup = Database::getSetting(QStringLiteral("settings.int.autobackup.cleanup_after_backup"), true).toBool();
    payload.showBackupNotifications = Database::getSetting(QStringLiteral("settings.int.autobackup.show_backup_notifications"), true).toBool();
    payload.detailedLogging = Database::getSetting(QStringLiteral("settings.int.autobackup.detailed_logging"), false).toBool();
    payload.backupDir = Database::getSetting(QStringLiteral("settings.int.autobackup.dir"), BackupPaths::configuredBackupsRoot()).toString().trimmed();
    payload.maxBackupsPerDay = Database::getSetting(QStringLiteral("settings.int.autobackup.max_backups_per_day"), 250).toInt();
    payload.maxTotalSizeMb = Database::getSetting(QStringLiteral("settings.int.autobackup.max_total_size_mb"), 1024).toInt();
    payload.recoveryEnabled = Database::getSetting(QStringLiteral("settings.int.autobackup.recovery_enabled"), true).toBool();
    payload.recoveryAutosaveEnabled = Database::getSetting(QStringLiteral("settings.int.autobackup.recovery_autosave_enabled"), true).toBool();
    payload.recoveryShowStartupDialog = Database::getSetting(QStringLiteral("settings.int.autobackup.recovery_show_startup_dialog"), true).toBool();
    payload.recoveryAutosaveIntervalMs = Database::getSetting(QStringLiteral("settings.int.autobackup.recovery_autosave_interval_ms"), 15000).toInt();
    payload.recoveryRetentionDays = Database::getSetting(QStringLiteral("settings.int.autobackup.recovery_retention_days"), 3).toInt();
    return payload;
}

QString effectiveBackupDir()
{
    const QString legacyBackupDir = Database::getSetting(QStringLiteral("settings.int.backup_folder"), QVariant()).toString().trimmed();
    const QString configured = Database::getSetting(QStringLiteral("settings.int.autobackup.dir"), BackupPaths::configuredBackupsRoot()).toString().trimmed();
    return configured.isEmpty() ? legacyBackupDir : configured;
}

bool save(const AutoBackupSettingsPayload& payload, bool* changed)
{
    const QString legacyBackupDir = Database::getSetting(QStringLiteral("settings.int.backup_folder"), QVariant()).toString().trimmed();
    const QString effectiveDir = payload.backupDir.trimmed().isEmpty() ? legacyBackupDir : payload.backupDir.trimmed();

    const bool hasChanges =
        Database::getSetting(QStringLiteral("settings.int.autobackup.enabled"), true).toBool() != payload.enabled ||
        Database::getSetting(QStringLiteral("settings.int.autobackup.protection_mode"), QStringLiteral("auto")).toString().trimmed() != payload.protectionMode ||
        Database::getSetting(QStringLiteral("settings.int.autobackup.dir"), BackupPaths::configuredBackupsRoot()).toString().trimmed() != effectiveDir ||
        Database::getSetting(QStringLiteral("settings.int.autobackup.max_backups_per_day"), 250).toInt() != payload.maxBackupsPerDay ||
        Database::getSetting(QStringLiteral("settings.int.autobackup.max_total_size_mb"), 1024).toInt() != payload.maxTotalSizeMb ||
        Database::getSetting(QStringLiteral("settings.int.autobackup.keep_previous_day_last_snapshot"), true).toBool() != payload.keepPreviousDayLastSnapshot ||
        Database::getSetting(QStringLiteral("settings.int.autobackup.cleanup_on_start"), true).toBool() != payload.cleanupOnStart ||
        Database::getSetting(QStringLiteral("settings.int.autobackup.cleanup_after_backup"), true).toBool() != payload.cleanupAfterBackup ||
        Database::getSetting(QStringLiteral("settings.int.autobackup.emergency_before_restore"), true).toBool() != payload.emergencyBeforeRestore ||
        Database::getSetting(QStringLiteral("settings.int.autobackup.show_backup_notifications"), true).toBool() != payload.showBackupNotifications ||
        Database::getSetting(QStringLiteral("settings.int.autobackup.detailed_logging"), false).toBool() != payload.detailedLogging ||
        Database::getSetting(QStringLiteral("settings.int.autobackup.recovery_enabled"), true).toBool() != payload.recoveryEnabled ||
        Database::getSetting(QStringLiteral("settings.int.autobackup.recovery_autosave_enabled"), true).toBool() != payload.recoveryAutosaveEnabled ||
        Database::getSetting(QStringLiteral("settings.int.autobackup.recovery_autosave_interval_ms"), 15000).toInt() != payload.recoveryAutosaveIntervalMs ||
        Database::getSetting(QStringLiteral("settings.int.autobackup.recovery_retention_days"), 3).toInt() != payload.recoveryRetentionDays ||
        Database::getSetting(QStringLiteral("settings.int.autobackup.recovery_show_startup_dialog"), true).toBool() != payload.recoveryShowStartupDialog;

    if (changed) *changed = hasChanges;

    Database::setSetting(QStringLiteral("settings.int.autobackup.enabled"), payload.enabled);
    Database::setSetting(QStringLiteral("settings.int.autobackup.protection_mode"), payload.protectionMode.trimmed().isEmpty() ? QStringLiteral("auto") : payload.protectionMode.trimmed());
    Database::setSetting(QStringLiteral("settings.int.autobackup.dir"), effectiveDir);
    Database::setSetting(QStringLiteral("settings.int.autobackup.max_backups_per_day"), payload.maxBackupsPerDay);
    Database::setSetting(QStringLiteral("settings.int.autobackup.max_total_size_mb"), payload.maxTotalSizeMb);
    Database::setSetting(QStringLiteral("settings.int.autobackup.keep_previous_day_last_snapshot"), payload.keepPreviousDayLastSnapshot);
    Database::setSetting(QStringLiteral("settings.int.autobackup.cleanup_on_start"), payload.cleanupOnStart);
    Database::setSetting(QStringLiteral("settings.int.autobackup.cleanup_after_backup"), payload.cleanupAfterBackup);
    Database::setSetting(QStringLiteral("settings.int.autobackup.emergency_before_restore"), payload.emergencyBeforeRestore);
    Database::setSetting(QStringLiteral("settings.int.autobackup.show_backup_notifications"), payload.showBackupNotifications);
    Database::setSetting(QStringLiteral("settings.int.autobackup.detailed_logging"), payload.detailedLogging);
    Database::setSetting(QStringLiteral("settings.int.autobackup.recovery_enabled"), payload.recoveryEnabled);
    Database::setSetting(QStringLiteral("settings.int.autobackup.recovery_autosave_enabled"), payload.recoveryAutosaveEnabled);
    Database::setSetting(QStringLiteral("settings.int.autobackup.recovery_autosave_interval_ms"), payload.recoveryAutosaveIntervalMs);
    Database::setSetting(QStringLiteral("settings.int.autobackup.recovery_retention_days"), payload.recoveryRetentionDays);
    Database::setSetting(QStringLiteral("settings.int.autobackup.recovery_show_startup_dialog"), payload.recoveryShowStartupDialog);
    return true;
}

} // namespace AutoBackupSettingsService
