#include "core/cloudsync/CloudSyncInboxService.h"

#include "infrastructure/db/sqlite/Db.h"

#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QtGlobal>

namespace {

QSqlDatabase effectiveDb(QSqlDatabase db)
{
    return db.isValid() ? db : Database::db();
}

QString compactJson(const QJsonObject& obj)
{
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QString objText(const QJsonObject& obj, const QString& key)
{
    const QString value = obj.value(key).toString().trimmed();
    return value.isNull() ? QStringLiteral("") : value;
}

bool ensureColumn(QSqlDatabase db, const QString& table, const QString& column, const QString& definition, QString* errorText)
{
    QSqlQuery info(db);
    info.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table));
    while (info.next()) {
        if (info.value(1).toString().compare(column, Qt::CaseInsensitive) == 0) return true;
    }
    QSqlQuery alter(db);
    if (!alter.exec(QStringLiteral("ALTER TABLE %1 ADD COLUMN %2").arg(table, definition))) {
        if (errorText) *errorText = alter.lastError().text();
        return false;
    }
    return true;
}

int countByStatus(QSqlDatabase db, const QString& status, QString* errorText)
{
    db = effectiveDb(db);
    if (!CloudSyncInboxService::ensureSchema(db, errorText)) return -1;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM sync_inbox WHERE status=?"));
    q.addBindValue(status);
    if (!q.exec() || !q.next()) {
        if (errorText) *errorText = q.lastError().text();
        return -1;
    }
    return q.value(0).toInt();
}

} // namespace

namespace CloudSyncInboxService {

bool ensureSchema(QSqlDatabase db, QString* errorText)
{
    db = effectiveDb(db);
    if (!db.isOpen() && !db.open()) {
        if (errorText) *errorText = db.lastError().text();
        return false;
    }
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS sync_inbox ("
                               "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                               "event_uid TEXT NOT NULL UNIQUE,"
                               "server_event_id TEXT NOT NULL DEFAULT '',"
                               "source_device_uid TEXT NOT NULL DEFAULT '',"
                               "entity_type TEXT NOT NULL DEFAULT '',"
                               "entity_uid TEXT NOT NULL DEFAULT '',"
                               "operation TEXT NOT NULL DEFAULT '',"
                               "payload_json TEXT NOT NULL DEFAULT '{}',"
                               "status TEXT NOT NULL DEFAULT 'pending',"
                               "apply_error TEXT NOT NULL DEFAULT '',"
                               "received_at TEXT NOT NULL DEFAULT (datetime('now')),"
                               "applied_at TEXT NOT NULL DEFAULT ''"
                               ");"))) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    if (!ensureColumn(db, QStringLiteral("sync_inbox"), QStringLiteral("acked_at"), QStringLiteral("acked_at TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("sync_inbox"), QStringLiteral("apply_started_at"), QStringLiteral("apply_started_at TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("sync_inbox"), QStringLiteral("inbox_uid"), QStringLiteral("inbox_uid TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("sync_inbox"), QStringLiteral("remote_event_uid"), QStringLiteral("remote_event_uid TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("sync_inbox"), QStringLiteral("device_uid"), QStringLiteral("device_uid TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("sync_inbox"), QStringLiteral("branch_uid"), QStringLiteral("branch_uid TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("sync_inbox"), QStringLiteral("payload"), QStringLiteral("payload TEXT NOT NULL DEFAULT '{}'"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("sync_inbox"), QStringLiteral("created_at"), QStringLiteral("created_at TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("sync_inbox"), QStringLiteral("error"), QStringLiteral("error TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("sync_inbox"), QStringLiteral("last_error"), QStringLiteral("last_error TEXT NOT NULL DEFAULT ''"), errorText)) return false;

    if (!q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_sync_inbox_status_received ON sync_inbox(status, received_at);"))) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    if (!q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_sync_inbox_entity ON sync_inbox(entity_type, entity_uid);"))) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    return true;
}

bool enqueueEvent(QSqlDatabase db, const QJsonObject& event, QString* errorText)
{
    db = effectiveDb(db);
    if (!ensureSchema(db, errorText)) return false;

    const QString eventUid = objText(event, QStringLiteral("event_uid"));
    if (eventUid.isEmpty()) {
        if (errorText) *errorText = QString::fromUtf8("Серверна sync-подія без event_uid.");
        return false;
    }

    // Store the full server event, not only event["payload"].
    // Stage 8 depends on envelope fields, and stage 5-7 work-order / CRM
    // snapshots can carry nested `related` rows that must survive pull -> inbox
    // -> RemoteMutationApplyService. Stripping the envelope here silently drops
    // services/parts/files/vehicles/tasks/events and prevents full desktop
    // materialization on another device.
    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT INTO sync_inbox("
                             "event_uid, server_event_id, source_device_uid, entity_type, entity_uid, operation, payload_json, status, received_at,"
                             "inbox_uid, remote_event_uid, device_uid, branch_uid, payload, created_at) "
                             "VALUES(?,?,?,?,?,?,?,'pending',datetime('now'),?,?,?,?,?,?) ON CONFLICT(event_uid) DO NOTHING"));
    const QString eventJson = compactJson(event);
    q.addBindValue(eventUid);
    q.addBindValue(objText(event, QStringLiteral("server_event_id")));
    q.addBindValue(objText(event, QStringLiteral("source_device_uid")));
    q.addBindValue(objText(event, QStringLiteral("entity_type")));
    q.addBindValue(objText(event, QStringLiteral("entity_uid")));
    q.addBindValue(objText(event, QStringLiteral("operation")));
    q.addBindValue(eventJson);
    q.addBindValue(eventUid);
    q.addBindValue(eventUid);
    q.addBindValue(objText(event, QStringLiteral("source_device_uid")));
    q.addBindValue(objText(event, QStringLiteral("branch_uid")));
    q.addBindValue(eventJson);
    q.addBindValue(objText(event, QStringLiteral("created_at")));
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    return true;
}

QList<InboxEvent> pendingEvents(QSqlDatabase db, int limit, QString* errorText)
{
    QList<InboxEvent> result;
    db = effectiveDb(db);
    if (!ensureSchema(db, errorText)) return result;

    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT id,event_uid,server_event_id,source_device_uid,entity_type,entity_uid,operation,payload_json,status,acked_at "
                             "FROM sync_inbox WHERE status IN ('pending','failed') ORDER BY received_at,id LIMIT ?"));
    q.addBindValue(qMax(1, limit));
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return result;
    }
    while (q.next()) {
        InboxEvent item;
        item.id = q.value(0).toInt();
        item.eventUid = q.value(1).toString();
        item.serverEventId = q.value(2).toString();
        item.sourceDeviceUid = q.value(3).toString();
        item.entityType = q.value(4).toString();
        item.entityUid = q.value(5).toString();
        item.operation = q.value(6).toString();
        item.payloadJson = q.value(7).toString();
        item.status = q.value(8).toString();
        item.ackedAt = q.value(9).toString();
        result.push_back(item);
    }
    return result;
}

bool markStatus(QSqlDatabase db, const QString& eventUid, const QString& status, const QString& error, QString* errorText)
{
    db = effectiveDb(db);
    if (!ensureSchema(db, errorText)) return false;
    const QString normalizedStatus = status.trimmed();
    // SQLite columns are NOT NULL. A default-constructed QString is null, not
    // an empty string, so a clean applying transition must bind an explicit empty value.
    QString safeError = error.trimmed();
    if (safeError.isNull()) safeError = QStringLiteral("");
    const bool finalStatus = normalizedStatus == QStringLiteral("applied")
        || normalizedStatus == QStringLiteral("ignored_self_event")
        || normalizedStatus == QStringLiteral("ignored_metadata");
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "UPDATE sync_inbox SET "
        "status=?, "
        "apply_error=?, "
        "error=?, "
        "last_error=?, "
        "apply_started_at=CASE WHEN ?='applying' THEN datetime('now') WHEN ?='pending' THEN '' ELSE apply_started_at END, "
        "applied_at=CASE WHEN ? THEN datetime('now') ELSE applied_at END "
        "WHERE event_uid=?"));
    q.addBindValue(normalizedStatus);
    q.addBindValue(safeError);
    q.addBindValue(safeError);
    q.addBindValue(safeError);
    q.addBindValue(normalizedStatus);
    q.addBindValue(normalizedStatus);
    q.addBindValue(finalStatus ? 1 : 0);
    q.addBindValue(eventUid.trimmed());
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    return true;
}

bool recoverInterruptedApplying(QSqlDatabase db, int olderThanSeconds, QString* errorText)
{
    db = effectiveDb(db);
    if (!ensureSchema(db, errorText)) return false;
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "UPDATE sync_inbox SET "
        "status='pending', "
        "apply_error='recovered from stale applying state', "
        "apply_started_at='' "
        "WHERE status='applying' "
        "AND TRIM(COALESCE(apply_started_at,''))<>'' "
        "AND datetime(apply_started_at) <= datetime('now', ?)"));
    q.addBindValue(QStringLiteral("-%1 seconds").arg(qMax(60, olderThanSeconds)));
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    return true;
}

QList<InboxEvent> unackedAppliedEvents(QSqlDatabase db, int limit, QString* errorText)
{
    QList<InboxEvent> result;
    db = effectiveDb(db);
    if (!ensureSchema(db, errorText)) return result;

    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT id,event_uid,server_event_id,source_device_uid,entity_type,entity_uid,operation,payload_json,status,acked_at "
                             "FROM sync_inbox WHERE status IN ('applied','ignored_self_event','ignored_metadata') AND TRIM(COALESCE(acked_at,''))='' "
                             "ORDER BY applied_at,id LIMIT ?"));
    q.addBindValue(qMax(1, limit));
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return result;
    }
    while (q.next()) {
        InboxEvent item;
        item.id = q.value(0).toInt();
        item.eventUid = q.value(1).toString();
        item.serverEventId = q.value(2).toString();
        item.sourceDeviceUid = q.value(3).toString();
        item.entityType = q.value(4).toString();
        item.entityUid = q.value(5).toString();
        item.operation = q.value(6).toString();
        item.payloadJson = q.value(7).toString();
        item.status = q.value(8).toString();
        item.ackedAt = q.value(9).toString();
        result.push_back(item);
    }
    return result;
}

bool markAcknowledged(QSqlDatabase db, const QString& eventUid, QString* errorText)
{
    db = effectiveDb(db);
    if (!ensureSchema(db, errorText)) return false;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE sync_inbox SET acked_at=datetime('now') WHERE event_uid=?"));
    q.addBindValue(eventUid.trimmed());
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    return true;
}

int pendingCount(QSqlDatabase db, QString* errorText)
{
    db = effectiveDb(db);
    if (!ensureSchema(db, errorText)) return -1;
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM sync_inbox WHERE status IN ('pending','failed','applying')"))) {
        if (errorText) *errorText = q.lastError().text();
        return -1;
    }
    return q.next() ? q.value(0).toInt() : 0;
}

int conflictCount(QSqlDatabase db, QString* errorText)
{
    return countByStatus(effectiveDb(db), QStringLiteral("conflict"), errorText);
}

} // namespace CloudSyncInboxService
