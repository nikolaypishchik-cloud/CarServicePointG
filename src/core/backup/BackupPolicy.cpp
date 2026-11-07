#include "core/backup/BackupPolicy.h"

#include "core/backup/BackupLogging.h"
#include "core/backup/BackupManifestService.h"
#include "core/backup/BackupPaths.h"
#include "core/io/SafeFileOps.h"
#include "infrastructure/db/sqlite/Db.h"

#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QThread>
#include <algorithm>
#include <utility>

namespace {
BackupPolicy::Config defaultConfig()
{
    return BackupPolicy::Config{};
}

BackupPolicy::Config readConfigFromDatabase()
{
    BackupPolicy::Config c = defaultConfig();
    c.enabled = Database::getSetting(QStringLiteral("settings.int.autobackup.enabled"), c.enabled).toBool();
    c.protectionMode = Database::getSetting(QStringLiteral("settings.int.autobackup.protection_mode"), c.protectionMode).toString().trimmed();
    if (c.protectionMode.isEmpty()) c.protectionMode = QStringLiteral("auto");
    c.maxBackupsPerDay = qMax(1, Database::getSetting(QStringLiteral("settings.int.autobackup.max_backups_per_day"), c.maxBackupsPerDay).toInt());
    c.maxTotalSizeMb = qMax(128, Database::getSetting(QStringLiteral("settings.int.autobackup.max_total_size_mb"), c.maxTotalSizeMb).toInt());
    c.keepPreviousDayLastSnapshot = Database::getSetting(QStringLiteral("settings.int.autobackup.keep_previous_day_last_snapshot"), c.keepPreviousDayLastSnapshot).toBool();
    c.cleanupOnStart = Database::getSetting(QStringLiteral("settings.int.autobackup.cleanup_on_start"), c.cleanupOnStart).toBool();
    c.cleanupAfterBackup = Database::getSetting(QStringLiteral("settings.int.autobackup.cleanup_after_backup"), c.cleanupAfterBackup).toBool();
    c.emergencyBeforeRestore = Database::getSetting(QStringLiteral("settings.int.autobackup.emergency_before_restore"), c.emergencyBeforeRestore).toBool();
    c.showNotifications = Database::getSetting(QStringLiteral("settings.int.autobackup.show_backup_notifications"), c.showNotifications).toBool();
    c.detailedLogging = Database::getSetting(QStringLiteral("settings.int.autobackup.detailed_logging"), c.detailedLogging).toBool();
    c.recoveryEnabled = Database::getSetting(QStringLiteral("settings.int.autobackup.recovery_enabled"), c.recoveryEnabled).toBool();
    c.recoveryAutosaveEnabled = Database::getSetting(QStringLiteral("settings.int.autobackup.recovery_autosave_enabled"), c.recoveryAutosaveEnabled).toBool();
    c.recoveryAutosaveIntervalMs = qMax(3000, Database::getSetting(QStringLiteral("settings.int.autobackup.recovery_autosave_interval_ms"), c.recoveryAutosaveIntervalMs).toInt());
    c.recoveryRetentionDays = qMax(1, Database::getSetting(QStringLiteral("settings.int.autobackup.recovery_retention_days"), c.recoveryRetentionDays).toInt());
    c.recoveryShowStartupDialog = Database::getSetting(QStringLiteral("settings.int.autobackup.recovery_show_startup_dialog"), c.recoveryShowStartupDialog).toBool();

    const QString mode = c.protectionMode.toLower();
    if (mode == QStringLiteral("maximum")) {
        c.maxBackupsPerDay = qMax(c.maxBackupsPerDay, 500);
        c.maxTotalSizeMb = qMax(c.maxTotalSizeMb, 4096);
        c.recoveryAutosaveIntervalMs = qMin(c.recoveryAutosaveIntervalMs, 5000);
        c.cleanupAfterBackup = false;
    } else if (mode == QStringLiteral("economy")) {
        c.maxBackupsPerDay = qMin(c.maxBackupsPerDay, 30);
        c.maxTotalSizeMb = qMin(c.maxTotalSizeMb, 512);
        c.recoveryAutosaveIntervalMs = qMax(c.recoveryAutosaveIntervalMs, 30000);
    } else if (mode == QStringLiteral("manual")) {
        c.enabled = false;
        c.cleanupAfterBackup = false;
    }
    // Weekly retention applies even when backup creation is manual or maximum protection is selected.
    c.cleanupOnStart = true;
    c.cleanupAfterBackup = true;
    return c;
}

QMutex& configMutex()
{
    static QMutex m;
    return m;
}

BackupPolicy::Config& cachedConfig()
{
    static BackupPolicy::Config c = defaultConfig();
    return c;
}

bool& cacheInitialized()
{
    static bool inited = false;
    return inited;
}

bool canTouchRuntimeDatabase()
{
    auto* app = QCoreApplication::instance();
    return app && QThread::currentThread() == app->thread();
}
}

BackupPolicy::Config BackupPolicy::load()
{
    if (canTouchRuntimeDatabase()) {
        refreshFromRuntime();
    }

    QMutexLocker locker(&configMutex());
    if (!cacheInitialized()) {
        cachedConfig() = defaultConfig();
        cacheInitialized() = true;
    }
    return cachedConfig();
}

void BackupPolicy::refreshFromRuntime()
{
    if (!canTouchRuntimeDatabase()) {
        return;
    }

    const Config loaded = readConfigFromDatabase();
    QMutexLocker locker(&configMutex());
    cachedConfig() = loaded;
    cacheInitialized() = true;
}

void BackupPolicy::cleanupSnapshots(QString* errorText)
{
    if (errorText) {
        errorText->clear();
    }

    const Config cfg = load();
    QVector<SnapshotMetadata> snapshots = BackupManifestService::listSnapshots();
    if (snapshots.isEmpty()) {
        return;
    }

    QVector<SnapshotMetadata> existing;
    existing.reserve(snapshots.size());
    for (SnapshotMetadata meta : snapshots) {
        if (meta.snapshotPath.trimmed().isEmpty()) continue;
        QFileInfo fi(meta.snapshotPath);
        if (!fi.exists() || fi.size() <= 0) continue;
        meta.snapshotSizeBytes = fi.size();
        if (meta.validationStatus.trimmed().isEmpty()) meta.validationStatus = QStringLiteral("valid");
        existing.push_back(meta);
    }
    if (existing.isEmpty()) {
        BackupManifestService::replaceSnapshotIndex({}, errorText);
        return;
    }

    auto newestFirst = [](const SnapshotMetadata& a, const SnapshotMetadata& b) {
        if (a.createdAt != b.createdAt) return a.createdAt > b.createdAt;
        return a.snapshotId > b.snapshotId;
    };
    auto oldestFirst = [](const SnapshotMetadata& a, const SnapshotMetadata& b) {
        if (a.createdAt != b.createdAt) return a.createdAt < b.createdAt;
        return a.snapshotId < b.snapshotId;
    };
    std::sort(existing.begin(), existing.end(), newestFirst);

    const QDate today = QDate::currentDate();
    const QDateTime retentionCutoff = QDateTime::currentDateTime().addDays(-7);
    const qint64 maxBytes = static_cast<qint64>(cfg.maxTotalSizeMb) * 1024 * 1024;

    QSet<QString> keepIds;
    QSet<QString> newestFiveIds;
    auto idFor = [](const SnapshotMetadata& meta) {
        return !meta.snapshotId.trimmed().isEmpty() ? meta.snapshotId : meta.snapshotPath;
    };

    // Always keep at least the five newest valid snapshots and never delete the last valid snapshot.
    for (int i = 0; i < existing.size() && i < 5; ++i) {
        if (existing.at(i).createdAt <= retentionCutoff) continue;
        const QString id = idFor(existing.at(i));
        keepIds.insert(id);
        newestFiveIds.insert(id);
    }

    QSet<QDate> dailyKeeper;
    int keptToday = 0;
    for (const SnapshotMetadata& meta : std::as_const(existing)) {
        if (meta.createdAt <= retentionCutoff) continue;
        const QString id = idFor(meta);
        const QString source = (meta.source + QLatin1Char(' ') + meta.operationType).toLower();
        if (source.contains(QStringLiteral("emergency"))) {
            keepIds.insert(id);
            continue;
        }
        const QDate day = meta.createdAt.date();
        if (day == today) {
            if (keptToday < cfg.maxBackupsPerDay) {
                keepIds.insert(id);
                ++keptToday;
            }
            continue;
        }
        if (!dailyKeeper.contains(day)) {
            keepIds.insert(id);
            dailyKeeper.insert(day);
        }
    }

    QVector<SnapshotMetadata> keepList;
    QVector<SnapshotMetadata> removeList;
    for (const SnapshotMetadata& meta : std::as_const(existing)) {
        if (keepIds.contains(idFor(meta))) keepList.push_back(meta);
        else removeList.push_back(meta);
    }

    qint64 usedBytes = 0;
    for (const SnapshotMetadata& meta : std::as_const(keepList)) usedBytes += meta.snapshotSizeBytes;

    if (usedBytes > maxBytes && keepList.size() > 5) {
        std::sort(keepList.begin(), keepList.end(), oldestFirst);
        for (int i = 0; i < keepList.size() && usedBytes > maxBytes; ) {
            const SnapshotMetadata meta = keepList.at(i);
            const QString source = (meta.source + QLatin1Char(' ') + meta.operationType).toLower();
            const bool protectedEmergency = source.contains(QStringLiteral("emergency"));
            const bool protectedNewestFive = newestFiveIds.contains(idFor(meta));
            if (!protectedEmergency && !protectedNewestFive && keepList.size() > 5) {
                usedBytes -= meta.snapshotSizeBytes;
                removeList.push_back(meta);
                keepList.removeAt(i);
                continue;
            }
            ++i;
        }
        std::sort(keepList.begin(), keepList.end(), newestFirst);
    }

    // Never remove the only remaining valid snapshot.
    if (keepList.isEmpty() && !existing.isEmpty()) {
        keepList.push_back(existing.constFirst());
        for (int i = removeList.size() - 1; i >= 0; --i) {
            if (idFor(removeList.at(i)) == idFor(existing.constFirst())) removeList.removeAt(i);
        }
    }

    const QString backupRootPath = BackupPaths::configuredBackupsRoot();
    for (const SnapshotMetadata& meta : std::as_const(removeList)) {
        if (!meta.snapshotPath.trimmed().isEmpty()) {
            QString deleteError;
            if (!SafeFileOps::safeRemoveFile(meta.snapshotPath, {backupRootPath}, {}, {QStringLiteral(".db")}, &deleteError)) {
                BackupLogging::warn(deleteError);
                // Keep failed deletions in the index so cleanup can retry safely.
                keepList.push_back(meta);
            }
        }
    }

    QString replaceError;
    if (!BackupManifestService::replaceSnapshotIndex(keepList, &replaceError)) {
        if (errorText) *errorText = replaceError;
        return;
    }

    QDir root(BackupPaths::configuredBackupsRoot());
    const QFileInfoList dayDirs = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& fi : dayDirs) {
        if (fi.fileName() == QStringLiteral(".tmp")) continue;
        QDir dayDir(fi.absoluteFilePath());
        const bool hasDbFiles = !dayDir.entryList(QStringList() << QStringLiteral("*.db"), QDir::Files).isEmpty();
        if (!hasDbFiles) {
            QString deleteError;
            if (!SafeFileOps::safeRemoveRecursively(dayDir.absolutePath(), {backupRootPath}, {}, &deleteError)) {
                BackupLogging::warn(deleteError);
            }
        }
    }

    BackupLogging::info(QStringLiteral("cleanup completed kept=%1 removed=%2 retention_days=7 min_valid=1")
                            .arg(keepList.size()).arg(removeList.size()));
}

void BackupPolicy::cleanupRecovery(QString* errorText)
{
    if (errorText) {
        errorText->clear();
    }
    const Config cfg = load();
    QDir root(BackupPaths::configuredRecoveryRoot());
    if (!root.exists()) {
        return;
    }
    const QFileInfoList days = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    const QDate cutoff = QDate::currentDate().addDays(-cfg.recoveryRetentionDays);
    for (const QFileInfo& fi : days) {
        const QDate day = QDate::fromString(fi.fileName(), Qt::ISODate);
        if (day.isValid() && day < cutoff) {
            QString deleteError;
            if (!SafeFileOps::safeRemoveRecursively(fi.absoluteFilePath(), {BackupPaths::configuredRecoveryRoot()}, {}, &deleteError)
                && errorText && errorText->isEmpty()) {
                *errorText = QStringLiteral("Не вдалося очистити recovery-каталог: %1 (%2)")
                    .arg(QDir::toNativeSeparators(fi.absoluteFilePath()), deleteError);
            }
        }
    }
}
