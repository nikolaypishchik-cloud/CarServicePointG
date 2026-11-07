#include "core/backup/BackupPaths.h"

#include "runtime/AppRuntime.h"
#include "infrastructure/db/sqlite/Db.h"
#include "core/io/SafeFileOps.h"

#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QFile>
#include <QSaveFile>
#include <QUuid>
#include <QStorageInfo>
#include <QSet>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#endif

namespace {
QString normalizedPath(QString path)
{
    path = QDir::fromNativeSeparators(path.trimmed());
    return QDir::cleanPath(path);
}

QMutex& pathsMutex()
{
    static QMutex m;
    return m;
}

QString& cachedBackupsRoot()
{
    static QString v;
    return v;
}

QString& cachedRecoveryRoot()
{
    static QString v;
    return v;
}

bool& pathsCacheInitialized()
{
    static bool inited = false;
    return inited;
}

bool canTouchRuntimeDatabase()
{
    auto* app = QCoreApplication::instance();
    return app && QThread::currentThread() == app->thread();
}

void ensureFallbackPathsInitialized()
{
    QMutexLocker locker(&pathsMutex());
    if (!pathsCacheInitialized()) {
        cachedBackupsRoot() = normalizedPath(AppRuntime::backupsDir());
        cachedRecoveryRoot() = normalizedPath(AppRuntime::recoveryDir());
        pathsCacheInitialized() = true;
    }
}
}

void BackupPaths::refreshFromRuntime()
{
    if (!canTouchRuntimeDatabase()) {
        ensureFallbackPathsInitialized();
        return;
    }

    QString backupsRoot = Database::getSetting(QStringLiteral("settings.int.autobackup.dir"), QString()).toString().trimmed();
    if (backupsRoot.isEmpty()) {
        backupsRoot = Database::getSetting(QStringLiteral("settings.int.backup_folder"), QString()).toString().trimmed();
    }
    if (backupsRoot.isEmpty()) {
        backupsRoot = AppRuntime::backupsDir();
    }

    const QString recoveryRoot = AppRuntime::recoveryDir();

    QMutexLocker locker(&pathsMutex());
    cachedBackupsRoot() = normalizedPath(backupsRoot);
    cachedRecoveryRoot() = normalizedPath(recoveryRoot);
    pathsCacheInitialized() = true;
}

QString BackupPaths::configuredBackupsRoot()
{
    if (canTouchRuntimeDatabase()) {
        refreshFromRuntime();
    } else {
        ensureFallbackPathsInitialized();
    }

    QString root;
    {
        QMutexLocker locker(&pathsMutex());
        root = cachedBackupsRoot();
    }
    if (root.isEmpty()) {
        root = normalizedPath(AppRuntime::backupsDir());
    }
    QDir().mkpath(root);
    return root;
}

QString BackupPaths::configuredRecoveryRoot()
{
    if (canTouchRuntimeDatabase()) {
        refreshFromRuntime();
    } else {
        ensureFallbackPathsInitialized();
    }

    QString root;
    {
        QMutexLocker locker(&pathsMutex());
        root = cachedRecoveryRoot();
    }
    if (root.isEmpty()) {
        root = normalizedPath(AppRuntime::recoveryDir());
    }
    QDir().mkpath(root);
    return root;
}

QString BackupPaths::backupDayDir(const QString& isoDate)
{
    const QString day = isoDate.trimmed().isEmpty() ? QDate::currentDate().toString(Qt::ISODate) : isoDate.trimmed();
    const QString dir = QDir(configuredBackupsRoot()).filePath(day);
    QDir().mkpath(dir);
    return normalizedPath(dir);
}


QString BackupPaths::backupTempDir()
{
    const QString dir = QDir(configuredBackupsRoot()).filePath(QStringLiteral(".tmp"));
    QDir().mkpath(dir);
    return normalizedPath(dir);
}

bool BackupPaths::probeBackupRoot(const QString& rootPath, QString* errorText)
{
    const QString root = normalizedPath(rootPath.trimmed().isEmpty() ? configuredBackupsRoot() : rootPath);
    QDir dir(root);
    if (!dir.exists() && !QDir().mkpath(root)) {
        if (errorText) *errorText = QStringLiteral("Не вдалося створити backup-папку: %1").arg(QDir::toNativeSeparators(root));
        return false;
    }

    const QString probeName = QStringLiteral(".probe_%1.tmp").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    const QString finalName = probeName + QStringLiteral(".renamed");
    const QString probePath = dir.filePath(probeName);
    const QString finalPath = dir.filePath(finalName);
    SafeFileOps::safeRemoveFile(probePath, {root}, {QStringLiteral(".probe_")}, {QStringLiteral(".tmp")});
    SafeFileOps::safeRemoveFile(finalPath, {root}, {QStringLiteral(".probe_")}, {QStringLiteral(".renamed")});

    {
        QFile f(probePath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (errorText) *errorText = QStringLiteral("Backup-папка недоступна для запису: %1 (%2)").arg(QDir::toNativeSeparators(root), f.errorString());
            return false;
        }
        if (f.write("probe") != 5) {
            if (errorText) *errorText = QStringLiteral("Не вдалося записати probe-файл у backup-папку: %1").arg(QDir::toNativeSeparators(root));
            f.close();
            SafeFileOps::safeRemoveFile(probePath, {root}, {QStringLiteral(".probe_")}, {QStringLiteral(".tmp")});
            return false;
        }
        f.close();
    }

    {
        QFile f(probePath);
        if (!f.open(QIODevice::ReadOnly) || f.readAll() != QByteArrayLiteral("probe")) {
            if (errorText) *errorText = QStringLiteral("Не вдалося прочитати probe-файл у backup-папці: %1").arg(QDir::toNativeSeparators(root));
            SafeFileOps::safeRemoveFile(probePath, {root}, {QStringLiteral(".probe_")}, {QStringLiteral(".tmp")});
            return false;
        }
    }

    if (!QFile::rename(probePath, finalPath)) {
        if (errorText) *errorText = QStringLiteral("Atomic rename у backup-папці не працює: %1").arg(QDir::toNativeSeparators(root));
        SafeFileOps::safeRemoveFile(probePath, {root}, {QStringLiteral(".probe_")}, {QStringLiteral(".tmp")});
        SafeFileOps::safeRemoveFile(finalPath, {root}, {QStringLiteral(".probe_")}, {QStringLiteral(".renamed")});
        return false;
    }
    SafeFileOps::safeRemoveFile(finalPath, {root}, {QStringLiteral(".probe_")}, {QStringLiteral(".renamed")});

    const QStorageInfo storage(root);
    if (storage.isValid() && storage.isReady() && storage.bytesAvailable() < 64ll * 1024ll * 1024ll) {
        if (errorText) {
            *errorText = QStringLiteral("У backup-папці недостатньо вільного місця: %1").arg(QDir::toNativeSeparators(root));
        }
        return false;
    }

    if (errorText) errorText->clear();
    return true;
}

bool BackupPaths::isIndependentStorage(const QString& databasePath, const QString& backupPath, QString* detail)
{
    if (detail) *detail = QStringLiteral("Окремий фізичний носій не підтверджено. Для мережевого сховища потрібна перевірка адміністратором.");
#ifdef Q_OS_WIN
    const auto disksForPath = [](const QString& path) {
        QSet<DWORD> disks;
        const QString resolved = QFileInfo(path).canonicalFilePath();
        if (resolved.isEmpty()) return disks;
        const auto native = QDir::toNativeSeparators(resolved).toStdWString();
        wchar_t mount[MAX_PATH] = {}, volume[MAX_PATH] = {};
        if (!GetVolumePathNameW(native.c_str(), mount, MAX_PATH)
            || !GetVolumeNameForVolumeMountPointW(mount, volume, MAX_PATH)) return disks;
        QString volumeName = QString::fromWCharArray(volume);
        if (volumeName.endsWith(QLatin1Char('\\'))) volumeName.chop(1);
        HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(volumeName.utf16()), 0,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (handle == INVALID_HANDLE_VALUE) return disks;
        QByteArray buffer(65536, '\0');
        DWORD returned = 0;
        if (DeviceIoControl(handle, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0,
                            buffer.data(), DWORD(buffer.size()), &returned, nullptr)) {
            const auto* extents = reinterpret_cast<const VOLUME_DISK_EXTENTS*>(buffer.constData());
            if (returned >= sizeof(VOLUME_DISK_EXTENTS) && extents->NumberOfDiskExtents > 0
                && extents->NumberOfDiskExtents <= (returned - offsetof(VOLUME_DISK_EXTENTS, Extents)) / sizeof(DISK_EXTENT)) {
                for (DWORD i = 0; i < extents->NumberOfDiskExtents; ++i) disks.insert(extents->Extents[i].DiskNumber);
            }
        }
        CloseHandle(handle);
        return disks;
    };
    const auto sourceDisks = disksForPath(databasePath);
    const auto backupDisks = disksForPath(backupPath);
    if (!sourceDisks.isEmpty() && !backupDisks.isEmpty()) {
        if (sourceDisks.intersects(backupDisks)) {
            if (detail) *detail = QStringLiteral("Резерв і база на одному фізичному диску. Такий резерв не захищає від відмови диска.");
            return false;
        }
        if (detail) *detail = QStringLiteral("Резерв на окремому фізичному диску.");
        return true;
    }
#else
    Q_UNUSED(databasePath);
    Q_UNUSED(backupPath);
#endif
    return false;
}

QString BackupPaths::recoveryDayDir(const QString& isoDate)
{
    const QString day = isoDate.trimmed().isEmpty() ? QDate::currentDate().toString(Qt::ISODate) : isoDate.trimmed();
    const QString dir = QDir(configuredRecoveryRoot()).filePath(day);
    QDir().mkpath(dir);
    return normalizedPath(dir);
}

QString BackupPaths::backupIndexPath()
{
    return QDir(configuredBackupsRoot()).filePath(QStringLiteral("index.json"));
}

QString BackupPaths::backupManifestPath()
{
    return QDir(configuredBackupsRoot()).filePath(QStringLiteral("manifest.json"));
}

QString BackupPaths::recoverySessionDir(const QString& sessionId, const QString& isoDate)
{
    const QString dir = QDir(recoveryDayDir(isoDate)).filePath(QStringLiteral("session_%1").arg(sessionId));
    QDir().mkpath(dir);
    return normalizedPath(dir);
}
