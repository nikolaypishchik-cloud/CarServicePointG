#include "core/cloudsync/DeviceIdentityService.h"

#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/db/sqlite/DbSettingsSqlSupport.h"

#include <QDateTime>
#include <QDebug>
#include <QHostInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

namespace {

QSqlDatabase effectiveDb(QSqlDatabase db)
{
    return db.isValid() ? db : Database::db();
}

QString newDesktopUid()
{
    return QStringLiteral("desktop-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString defaultDeviceName()
{
    const QString host = QHostInfo::localHostName().trimmed();
    return host.isEmpty() ? QStringLiteral("CarService Desktop") : QStringLiteral("CarService Desktop - %1").arg(host);
}

QString localBranchUid()
{
    return QStringLiteral("local-branch");
}

QString configuredOrLocalBranchUid(QSqlDatabase db)
{
    const QString configured = dbGetSettingForConnection(db,QStringLiteral("settings.cloudsync.branch_uid"), QString()).toString().trimmed();
    return configured.isEmpty() ? localBranchUid() : configured;
}

QString normalizedBranchUid(const QString& branch)
{
    const QString trimmed = branch.trimmed();
    return trimmed.isEmpty() ? localBranchUid() : trimmed;
}

void warnIdentityRepairOnce(QSqlDatabase db,const QString& detail)
{
    const QString key = QStringLiteral("settings.cloudsync.identity_repair_warning_logged");
    if (dbGetSettingForConnection(db,key, false).toBool()) return;
    qWarning().noquote() << QStringLiteral("SYNC_DEVICE_IDENTITY_REPAIRED %1").arg(detail);
    dbSetSettingForConnection(db,key, true);
}

bool execSql(QSqlDatabase db, const QString& sql, QString* errorText)
{
    QSqlQuery q(db);
    if (q.exec(sql)) return true;
    if (errorText) *errorText = q.lastError().text();
    return false;
}

bool tableExists(QSqlDatabase db, const QString& table)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type='table' AND name=? LIMIT 1"));
    q.addBindValue(table);
    return q.exec() && q.next();
}

bool columnExists(QSqlDatabase db, const QString& table, const QString& column)
{
    QSqlQuery q(db);
    q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table));
    while (q.next()) {
        if (q.value(1).toString().compare(column, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

bool ensureColumn(QSqlDatabase db, const QString& table, const QString& column, const QString& ddl, QString* errorText)
{
    if (!tableExists(db, table)) return true;
    if (columnExists(db, table, column)) return true;
    return execSql(db, QStringLiteral("ALTER TABLE %1 ADD COLUMN %2;").arg(table, ddl), errorText);
}

QString identityValue(QSqlDatabase db, int column)
{
    db = effectiveDb(db);
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT device_uid, device_name, branch_uid FROM sync_device_identity WHERE id=1 LIMIT 1"))) {
        return QString();
    }
    if (!q.next()) return QString();
    return q.value(column).toString();
}

} // namespace

namespace DeviceIdentityService {

bool ensureDeviceIdentity(QSqlDatabase db, QString* errorText)
{
    db = effectiveDb(db);
    if (!db.isValid()) {
        if (errorText) *errorText = QStringLiteral("Invalid database handle");
        return false;
    }
    if (!db.isOpen() && !db.open()) {
        if (errorText) *errorText = db.lastError().text();
        return false;
    }

    if (!execSql(db,
                 QStringLiteral("CREATE TABLE IF NOT EXISTS sync_device_identity ("
                                "id INTEGER PRIMARY KEY CHECK (id = 1),"
                                "device_uid TEXT NOT NULL DEFAULT '',"
                                "device_name TEXT NOT NULL DEFAULT '',"
                                "device_type TEXT NOT NULL DEFAULT 'desktop',"
                                "branch_uid TEXT NOT NULL DEFAULT 'local-branch',"
                                "created_at TEXT NOT NULL DEFAULT (datetime('now')),"
                                "updated_at TEXT NOT NULL DEFAULT (datetime('now'))"
                                ");"),
                 errorText)) {
        return false;
    }
    if (!ensureColumn(db,
                      QStringLiteral("sync_device_identity"),
                      QStringLiteral("branch_uid"),
                      QStringLiteral("branch_uid TEXT NOT NULL DEFAULT 'local-branch'"),
                      errorText)) {
        return false;
    }
    if (!execSql(db,
                 QStringLiteral("CREATE TABLE IF NOT EXISTS sync_registered_devices ("
                                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                "device_uid TEXT NOT NULL UNIQUE,"
                                "device_name TEXT NOT NULL DEFAULT '',"
                                "device_type TEXT NOT NULL DEFAULT '',"
                                "branch_uid TEXT NOT NULL DEFAULT '',"
                                "last_seen_at TEXT NOT NULL DEFAULT '',"
                                "status TEXT NOT NULL DEFAULT 'active'"
                                ");"),
                 errorText)) {
        return false;
    }

    QSqlQuery count(db);
    if (!count.exec(QStringLiteral("SELECT COUNT(*) FROM sync_device_identity WHERE id=1")) || !count.next()) {
        if (errorText) *errorText = count.lastError().text();
        return false;
    }

    if (count.value(0).toInt() <= 0) {
        QSqlQuery ins(db);
        ins.prepare(QStringLiteral("INSERT INTO sync_device_identity(id, device_uid, device_name, device_type, branch_uid, created_at, updated_at) "
                                   "VALUES(1, ?, ?, 'desktop', ?, datetime('now'), datetime('now'))"));
        ins.addBindValue(newDesktopUid());
        ins.addBindValue(defaultDeviceName());
        ins.addBindValue(configuredOrLocalBranchUid(db));
        if (!ins.exec()) {
            if (errorText) *errorText = ins.lastError().text();
            return false;
        }
        warnIdentityRepairOnce(db,QStringLiteral("created_missing_identity branch_uid=%1").arg(configuredOrLocalBranchUid(db)));
    } else {
        QSqlQuery existing(db);
        if (!existing.exec(QStringLiteral("SELECT device_uid, device_name, device_type, branch_uid FROM sync_device_identity WHERE id=1 LIMIT 1"))
            || !existing.next()) {
            if (errorText) *errorText = existing.lastError().text();
            return false;
        }
        const bool missingDeviceUid = existing.value(0).toString().trimmed().isEmpty();
        const bool missingDeviceName = existing.value(1).toString().trimmed().isEmpty();
        const bool missingDeviceType = existing.value(2).toString().trimmed().isEmpty();
        const bool missingBranchUid = existing.value(3).toString().trimmed().isEmpty();
        if (missingDeviceUid || missingDeviceName || missingDeviceType || missingBranchUid) {
            QSqlQuery upd(db);
            upd.prepare(QStringLiteral("UPDATE sync_device_identity "
                                       "SET device_uid=CASE WHEN TRIM(COALESCE(device_uid,''))='' THEN ? ELSE device_uid END, "
                                       "    device_name=CASE WHEN TRIM(COALESCE(device_name,''))='' THEN ? ELSE device_name END, "
                                       "    device_type=CASE WHEN TRIM(COALESCE(device_type,''))='' THEN 'desktop' ELSE device_type END, "
                                       "    branch_uid=CASE WHEN TRIM(COALESCE(branch_uid,''))='' THEN ? ELSE branch_uid END, "
                                       "    updated_at=datetime('now') "
                                       "WHERE id=1"));
            upd.addBindValue(newDesktopUid());
            upd.addBindValue(defaultDeviceName());
            upd.addBindValue(configuredOrLocalBranchUid(db));
            if (!upd.exec()) {
                if (errorText) *errorText = upd.lastError().text();
                return false;
            }
            warnIdentityRepairOnce(db,QStringLiteral("missing_device_uid=%1 missing_device_name=%2 missing_device_type=%3 missing_branch_uid=%4")
                                       .arg(missingDeviceUid ? 1 : 0)
                                       .arg(missingDeviceName ? 1 : 0)
                                       .arg(missingDeviceType ? 1 : 0)
                                       .arg(missingBranchUid ? 1 : 0));
        }
    }

    QSqlQuery mirror(db);
    mirror.prepare(QStringLiteral("INSERT INTO sync_registered_devices(device_uid, device_name, device_type, branch_uid, last_seen_at, status) "
                                  "SELECT device_uid, device_name, device_type, branch_uid, datetime('now'), 'active' "
                                  "FROM sync_device_identity WHERE id=1 "
                                  "ON CONFLICT(device_uid) DO UPDATE SET "
                                  "device_name=excluded.device_name, device_type=excluded.device_type, branch_uid=excluded.branch_uid, "
                                  "last_seen_at=excluded.last_seen_at, status='active'"));
    if (!mirror.exec()) {
        // Older SQLite builds may not support UPSERT; fall back silently.
        QSqlQuery legacy(db);
        legacy.exec(QStringLiteral("INSERT OR IGNORE INTO sync_registered_devices(device_uid, device_name, device_type, branch_uid, last_seen_at, status) "
                                   "SELECT device_uid, device_name, device_type, branch_uid, datetime('now'), 'active' FROM sync_device_identity WHERE id=1"));
        legacy.exec(QStringLiteral("UPDATE sync_registered_devices SET last_seen_at=datetime('now') WHERE device_uid=(SELECT device_uid FROM sync_device_identity WHERE id=1)"));
    }
    return true;
}

QString deviceUid(QSqlDatabase db)
{
    QString ignored;
    ensureDeviceIdentity(effectiveDb(db), &ignored);
    return identityValue(effectiveDb(db), 0);
}

QString deviceName(QSqlDatabase db)
{
    QString ignored;
    ensureDeviceIdentity(effectiveDb(db), &ignored);
    return identityValue(effectiveDb(db), 1);
}

QString branchUid(QSqlDatabase db)
{
    QString ignored;
    ensureDeviceIdentity(effectiveDb(db), &ignored);
    return identityValue(effectiveDb(db), 2);
}

bool updateDeviceName(QSqlDatabase db, const QString& name, QString* errorText)
{
    db = effectiveDb(db);
    if (!ensureDeviceIdentity(db, errorText)) return false;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE sync_device_identity SET device_name=?, updated_at=datetime('now') WHERE id=1"));
    q.addBindValue(name.trimmed().isEmpty() ? defaultDeviceName() : name.trimmed());
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    return ensureDeviceIdentity(db, errorText);
}

bool updateBranchUid(QSqlDatabase db, const QString& branch, QString* errorText)
{
    db = effectiveDb(db);
    if (!ensureDeviceIdentity(db, errorText)) return false;
    const QString effectiveBranch = normalizedBranchUid(branch);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE sync_device_identity SET branch_uid=?, updated_at=datetime('now') WHERE id=1"));
    q.addBindValue(effectiveBranch);
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    dbSetSettingForConnection(db,QStringLiteral("settings.cloudsync.branch_uid"), effectiveBranch);
    return ensureDeviceIdentity(db, errorText);
}

bool regenerateDeviceUid(QSqlDatabase db, QString* newDeviceUid, QString* errorText)
{
    db = effectiveDb(db);
    if (!ensureDeviceIdentity(db, errorText)) return false;
    const QString uid = newDesktopUid();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE sync_device_identity SET device_uid=?, updated_at=datetime('now') WHERE id=1"));
    q.addBindValue(uid);
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    if (newDeviceUid) *newDeviceUid = uid;
    return ensureDeviceIdentity(db, errorText);
}

} // namespace DeviceIdentityService
