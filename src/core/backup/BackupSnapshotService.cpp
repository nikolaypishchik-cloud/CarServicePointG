#include "core/backup/BackupSnapshotService.h"
#include <QTemporaryDir>

#include "runtime/AppRuntime.h"
#include "core/backup/BackupErrors.h"
#include "core/backup/BackupLogging.h"
#include "core/backup/BackupPaths.h"
#include "infrastructure/db/sqlite/ScopedConnectionManager.h"
#include "core/io/SafeFileOps.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <atomic>
#include <memory>

#if CS_HAS_SQLITE3_BACKUP_API
#include <sqlite3.h>
#endif

namespace {
QString escapedSqlitePath(QString path)
{
    return QDir::toNativeSeparators(path).replace("'", "''");
}

QString makeFileStem(const SnapshotJobContext& ctx)
{
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss_zzz"));
    const QString operation = ctx.event.operationType.trimmed().isEmpty() ? QStringLiteral("save") : ctx.event.operationType;
    const QString entity = ctx.event.entityType.trimmed().isEmpty() ? QStringLiteral("entity") : ctx.event.entityType;
    return QStringLiteral("snapshot_%1__%2__%3").arg(ts, operation, entity);
}

QString sha256ForFile(const QString& path, QString* errorText)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (errorText) *errorText = f.errorString();
        return QString();
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&f)) {
        if (errorText) *errorText = QStringLiteral("Не вдалося прочитати snapshot для SHA256");
        return QString();
    }
    if (errorText) errorText->clear();
    return QString::fromLatin1(hash.result().toHex());
}

qint64 readSqlitePragmaInt64(const QString& dbPath, const QString& pragmaName)
{
    const QString connectionName = QStringLiteral("backup_meta_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    qint64 value = 0;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(dbPath);
        if (db.open()) {
            QSqlQuery q(db);
            if (q.exec(QStringLiteral("PRAGMA %1").arg(pragmaName)) && q.next()) {
                value = q.value(0).toLongLong();
            }
            db.close();
        }
    }
    SqliteConnectionUtils::safeCloseAndRemoveDatabase(connectionName);
    return value;
}

bool runPassiveWalCheckpoint(QString* errorText)
{
    const QString connectionName = QStringLiteral("backup_checkpoint_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(AppRuntime::activeDbFilePath());
        if (db.open()) {
            QSqlQuery pragma(db);
            ok = pragma.exec(QStringLiteral("PRAGMA wal_checkpoint(PASSIVE)"));
            if (!ok && errorText) {
                *errorText = pragma.lastError().text();
            }
            db.close();
        } else if (errorText) {
            *errorText = db.lastError().text();
        }
    }
    SqliteConnectionUtils::safeCloseAndRemoveDatabase(connectionName);
    return ok;
}

#if CS_HAS_SQLITE3_BACKUP_API
bool createSnapshotWithSqliteBackupApi(const QString& sourcePath, const QString& tempPath, QString* errorText, const std::shared_ptr<std::atomic_bool>& cancelToken, int timeoutMs)
{
    sqlite3* sourceDb = nullptr;
    sqlite3* tempDb = nullptr;
    sqlite3_backup* backup = nullptr;

    const QByteArray sourcePathBytes = QFile::encodeName(sourcePath);
    const QByteArray tempPathBytes = QFile::encodeName(tempPath);

    auto fail = [&](const QString& message) {
        if (backup) sqlite3_backup_finish(backup);
        if (tempDb) sqlite3_close(tempDb);
        if (sourceDb) sqlite3_close(sourceDb);
        if (errorText) *errorText = message;
        return false;
    };

    if (sqlite3_open_v2(sourcePathBytes.constData(), &sourceDb, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        const QString msg = sourceDb ? QString::fromUtf8(sqlite3_errmsg(sourceDb)) : QStringLiteral("sqlite3_open_v2(source) failed");
        return fail(msg);
    }
    if (sqlite3_open_v2(tempPathBytes.constData(), &tempDb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        const QString msg = tempDb ? QString::fromUtf8(sqlite3_errmsg(tempDb)) : QStringLiteral("sqlite3_open_v2(destination) failed");
        return fail(msg);
    }

    sqlite3_busy_timeout(sourceDb, 250);
    sqlite3_busy_timeout(tempDb, 250);

    backup = sqlite3_backup_init(tempDb, "main", sourceDb, "main");
    if (!backup) {
        const QString msg = QString::fromUtf8(sqlite3_errmsg(tempDb));
        return fail(msg);
    }

    QElapsedTimer backupTimer;
    backupTimer.start();
    int rc = SQLITE_OK;
    int busyRetryCount = 0;
    constexpr int kMaxBusyRetries = 80;
    do {
        if (cancelToken && cancelToken->load()) {
            return fail(QStringLiteral("sqlite3_backup cancelled"));
        }
        if (timeoutMs > 0 && backupTimer.elapsed() > timeoutMs) {
            return fail(QStringLiteral("sqlite3_backup timeout after %1 ms retry=%2").arg(timeoutMs).arg(busyRetryCount));
        }
        rc = sqlite3_backup_step(backup, 256);
        if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED) {
            ++busyRetryCount;
            if (busyRetryCount > kMaxBusyRetries) {
                return fail(QStringLiteral("sqlite3_backup busy retry budget exceeded retry=%1").arg(busyRetryCount));
            }
            const int backoffMs = qMin(250, 25 + busyRetryCount * 5);
            sqlite3_sleep(backoffMs);
        }
    } while (rc == SQLITE_OK || rc == SQLITE_BUSY || rc == SQLITE_LOCKED);

    const int finishRc = sqlite3_backup_finish(backup);
    backup = nullptr;
    if (rc != SQLITE_DONE || finishRc != SQLITE_OK) {
        const int finalRc = (rc == SQLITE_DONE) ? finishRc : rc;
        const QString msg = QStringLiteral("sqlite3_backup failed rc=%1 err=%2 retry=%3")
                .arg(finalRc)
                .arg(QString::fromUtf8(sqlite3_errmsg(tempDb)))
                .arg(busyRetryCount);
        return fail(msg);
    }

    if (sqlite3_exec(tempDb, "PRAGMA wal_checkpoint(TRUNCATE)", nullptr, nullptr, nullptr) != SQLITE_OK) {
        BackupLogging::warn(QStringLiteral("destination wal_checkpoint failed for temp snapshot"));
    }

    sqlite3_close(tempDb);
    tempDb = nullptr;
    sqlite3_close(sourceDb);
    sourceDb = nullptr;
    if (errorText) errorText->clear();
    return true;
}
#endif

bool createSnapshotWithVacuumInto(const QString& sourcePath, const QString& tempPath, QString* errorText)
{
    const QString connectionName = QStringLiteral("backup_snapshot_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    bool snapshotSqlOk = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(sourcePath);
        if (db.open()) {
            {
                QSqlQuery pragma(db);
                pragma.exec(QStringLiteral("PRAGMA busy_timeout = 250"));
                pragma.finish();
            }
            {
                QSqlQuery q(db);
                const QString sql = QStringLiteral("VACUUM INTO '%1'").arg(escapedSqlitePath(tempPath));
                if (q.exec(sql)) {
                    snapshotSqlOk = true;
                } else if (errorText) {
                    *errorText = q.lastError().text();
                }
                q.finish();
            }
            db.close();
        } else if (errorText) {
            *errorText = db.lastError().text();
        }
    }
    SqliteConnectionUtils::safeCloseAndRemoveDatabase(connectionName);
    return snapshotSqlOk;
}

bool createSnapshotTempFile(const SnapshotJobContext& ctx,
                            const QString& sourcePath,
                            const QString& tempPath,
                            QString* engineUsed,
                            qint64* snapshotDurationMs,
                            QString* errorText,
                            const std::shared_ptr<std::atomic_bool>& cancelToken)
{
    QString checkpointError;
    const bool allowCheckpoint = ctx.source != QStringLiteral("autobackup") || ctx.emergency;
    if (allowCheckpoint) {
        runPassiveWalCheckpoint(&checkpointError);
    }

    QElapsedTimer timer;
    timer.start();

#if CS_HAS_SQLITE3_BACKUP_API
    QString sqliteError;
    const int backupTimeoutMs = ctx.emergency || ctx.source == QStringLiteral("manual") ? 30000 : 15000;
    if (createSnapshotWithSqliteBackupApi(sourcePath, tempPath, &sqliteError, cancelToken, backupTimeoutMs)) {
        if (engineUsed) *engineUsed = QStringLiteral("sqlite3_backup_api");
        if (snapshotDurationMs) *snapshotDurationMs = timer.elapsed();
        BackupLogging::info(QStringLiteral("snapshot created via sqlite3_backup API"));
        if (errorText) errorText->clear();
        return true;
    }
    if (ctx.source == QStringLiteral("autobackup") && !ctx.emergency) {
        if (snapshotDurationMs) *snapshotDurationMs = timer.elapsed();
        if (errorText) *errorText = sqliteError.trimmed().isEmpty() ? checkpointError : sqliteError;
        BackupLogging::warn(QStringLiteral("sqlite3_backup snapshot failed without fallback for autobackup: %1").arg(sqliteError));
        return false;
    }
    BackupLogging::warn(QStringLiteral("sqlite3_backup snapshot failed, explicit fallback to VACUUM INTO: %1").arg(sqliteError));
#endif

    const bool fallbackOk = createSnapshotWithVacuumInto(sourcePath, tempPath, errorText);
    if (snapshotDurationMs) *snapshotDurationMs = timer.elapsed();
    if (fallbackOk) {
        if (engineUsed) *engineUsed = QStringLiteral("vacuum_into");
        BackupLogging::warn(QStringLiteral("snapshot created via VACUUM INTO fallback"));
    } else if (errorText && !checkpointError.trimmed().isEmpty() && errorText->trimmed().isEmpty()) {
        *errorText = checkpointError;
    }
    return fallbackOk;
}
}

bool BackupSnapshotService::createSnapshot(const SnapshotJobContext& ctx, SnapshotMetadata* outMeta, QString* errorText, const std::shared_ptr<std::atomic_bool>& cancelToken)
{
    QString probeError;
    if (!BackupPaths::probeBackupRoot(BackupPaths::configuredBackupsRoot(), &probeError)) {
        if (errorText) *errorText = probeError;
        return false;
    }
    const QString rootDir = BackupPaths::backupDayDir();
    const QString tempRoot = BackupPaths::backupTempDir();
    QDir().mkpath(rootDir);
    QDir().mkpath(tempRoot);

    const QString stem = makeFileStem(ctx);
    const QString tempPath = QDir(tempRoot).filePath(stem + QStringLiteral(".tmp.db"));
    const QString finalPath = QDir(rootDir).filePath(stem + QStringLiteral(".db"));
    SafeFileOps::safeRemoveFile(tempPath, {tempRoot}, {}, {QStringLiteral(".tmp.db")});
    SafeFileOps::safeRemoveFile(finalPath, {rootDir}, {}, {QStringLiteral(".db")});

    QString engineUsed;
    qint64 snapshotDurationMs = 0;
    if (!createSnapshotTempFile(ctx, AppRuntime::activeDbFilePath(), tempPath, &engineUsed, &snapshotDurationMs, errorText, cancelToken)) {
        SafeFileOps::safeRemoveFile(tempPath, {tempRoot}, {}, {QStringLiteral(".tmp.db")});
        return false;
    }

    QString validationError;
    QElapsedTimer validateTimer;
    validateTimer.start();
    if (!validateSnapshot(tempPath, &validationError) || !rehearseRestore(tempPath, &validationError)) {
        if (errorText) *errorText = BackupErrors::invalidSnapshot(validationError);
        SafeFileOps::safeRemoveFile(tempPath, {tempRoot}, {}, {QStringLiteral(".tmp.db")});
        return false;
    }
    const qint64 validationDurationMs = validateTimer.elapsed();
    if (!QFile::rename(tempPath, finalPath)) {
        if (errorText) *errorText = QStringLiteral("Не вдалося виконати atomic rename temp snapshot у фінальний файл у межах backup root");
        SafeFileOps::safeRemoveFile(tempPath, {tempRoot}, {}, {QStringLiteral(".tmp.db")});
        SafeFileOps::safeRemoveFile(finalPath, {rootDir}, {}, {QStringLiteral(".db")});
        return false;
    }

    QString hashError;
    const QString sha256 = sha256ForFile(finalPath, &hashError);
    if (sha256.isEmpty()) {
        if (errorText) *errorText = QStringLiteral("Snapshot створено, але SHA256 не розраховано: %1").arg(hashError);
        SafeFileOps::safeRemoveFile(finalPath, {rootDir}, {}, {QStringLiteral(".db")});
        return false;
    }

    SnapshotMetadata meta;
    meta.snapshotId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    meta.createdAt = QDateTime::currentDateTime();
    meta.operationType = ctx.event.operationType;
    meta.module = ctx.event.module;
    meta.page = ctx.event.page;
    meta.entityType = ctx.event.entityType;
    meta.entityId = ctx.event.entityId;
    meta.orderUid = ctx.event.orderUid;
    meta.orderNumber = ctx.event.orderNumber;
    meta.description = ctx.event.userVisibleDescription;
    meta.appVersion = ctx.event.appVersion;
    meta.schemaVersion = ctx.event.schemaVersion;
    meta.dbPath = AppRuntime::activeDbFilePath();
    meta.snapshotPath = finalPath;
    meta.snapshotSizeBytes = QFileInfo(finalPath).size();
    meta.dbFileSizeBytes = QFileInfo(AppRuntime::activeDbFilePath()).size();
    meta.pageCount = readSqlitePragmaInt64(finalPath, QStringLiteral("page_count"));
    meta.backupRoot = BackupPaths::configuredBackupsRoot();
    meta.hashSha256 = sha256;
    BackupPaths::isIndependentStorage(meta.dbPath, meta.backupRoot, &meta.warning);
    meta.changedTables = ctx.event.changedTables;
    meta.validationStatus = QStringLiteral("valid");
    meta.commitId = ctx.event.commitId;
    meta.eventId = ctx.event.eventId;
    meta.source = ctx.source;
    meta.engineUsed = engineUsed;
    meta.snapshotDurationMs = snapshotDurationMs;
    meta.validationDurationMs = validationDurationMs;

    if (outMeta) *outMeta = meta;
    BackupLogging::info(QStringLiteral("snapshot validated %1").arg(QDir::toNativeSeparators(finalPath)));
    return true;
}

bool BackupSnapshotService::validateSnapshot(const QString& snapshotPath, QString* errorText)
{
    if (errorText) errorText->clear();
    if (!QFileInfo(snapshotPath).isFile()) {
        if (errorText) *errorText = QStringLiteral("Snapshot file does not exist");
        return false;
    }
    const QString connectionName = QStringLiteral("backup_validate_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(snapshotPath);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (db.open()) {
            QSqlQuery q(db);
            if (q.exec(QStringLiteral("SELECT COUNT(*) FROM sqlite_master")) && q.next()) {
                QSqlQuery integrity(db);
                if (integrity.exec(QStringLiteral("PRAGMA quick_check(1)")) && integrity.next()) {
                    const QString result = integrity.value(0).toString().trimmed();
                    ok = result.compare(QStringLiteral("ok"), Qt::CaseInsensitive) == 0;
                    if (!ok && errorText) {
                        *errorText = result;
                    }
                    if (ok) {
                        QSqlQuery tables(db);
                        if (tables.exec(QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name IN ('work_orders','work_order_parts')")) && tables.next()) {
                            const int keyTables = tables.value(0).toInt();
                            if (keyTables <= 0) {
                                ok = false;
                                if (errorText) *errorText = QStringLiteral("snapshot не містить ключових таблиць застосунку");
                            }
                        } else {
                            ok = false;
                            if (errorText) *errorText = tables.lastError().text();
                        }
                    }
                } else if (errorText) {
                    *errorText = integrity.lastError().text();
                }
            } else if (errorText) {
                *errorText = q.lastError().text();
            }
            db.close();
        } else if (errorText) {
            *errorText = db.lastError().text();
        }
    }
    SqliteConnectionUtils::safeCloseAndRemoveDatabase(connectionName);
    return ok;
}

bool BackupSnapshotService::rehearseRestore(const QString& snapshotPath, QString* errorText)
{
    // Restore into a disposable database; never touch the active database.
    QTemporaryDir scratch;
    const QString restored = scratch.filePath(QStringLiteral("restore-test.db"));
    if (!scratch.isValid() || !QFile::copy(snapshotPath, restored)) {
        if (errorText) *errorText = QStringLiteral("Cannot copy snapshot for restore rehearsal");
        return false;
    }
    const QString expectedHash = sha256ForFile(snapshotPath, errorText);
    if (expectedHash.isEmpty() || expectedHash != sha256ForFile(restored, errorText)) {
        if (errorText) *errorText = QStringLiteral("Restore rehearsal hash mismatch");
        return false;
    }
    if (!validateSnapshot(restored, errorText)) return false;
    const QString connectionName = QStringLiteral("restore_drill_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    bool ok = false;
    {
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(restored);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (db.open()) {
            QSqlQuery query(db);
            ok = query.exec(QStringLiteral("PRAGMA integrity_check")) && query.next()
                && query.value(0).toString() == QStringLiteral("ok");
            if (!ok && errorText) *errorText = QStringLiteral("Restore rehearsal integrity check failed: %1").arg(query.lastError().text());
        } else if (errorText) *errorText = db.lastError().text();
        db.close();
    }
    SqliteConnectionUtils::safeCloseAndRemoveDatabase(connectionName);
    return ok;
}
