#include "core/cloudsync/CloudSyncSchemaService.h"

#include "core/cloudsync/DeviceIdentityService.h"
#include "core/cloudsync/EntityUidService.h"
#include "core/backup/BackupSnapshotService.h"
#include "core/crm/CrmClientService.h"
#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/db/sqlite/DbSettingsSqlSupport.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QDebug>
#include <QList>
#include <QRegularExpression>

namespace {

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

bool isSafeIdentifier(const QString& value)
{
    static const QRegularExpression rx(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    return rx.match(value).hasMatch();
}

QString quoteIdent(const QString& value)
{
    QString out = value;
    out.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    return QStringLiteral("\"") + out + QStringLiteral("\"");
}

bool ensureColumn(QSqlDatabase db, const QString& table, const QString& column, const QString& ddl, QString* errorText)
{
    if (!tableExists(db, table)) return true;
    if (columnExists(db, table, column)) return true;
    return execSql(db, QStringLiteral("ALTER TABLE %1 ADD COLUMN %2;").arg(table, ddl), errorText);
}

bool ensureSchemaDestructiveOperationSnapshot(const QString& entityName, QString* errorText = nullptr)
{
    SnapshotJobContext ctx;
    ctx.requestedBy = QStringLiteral("CloudSyncSchemaService");
    ctx.source = QStringLiteral("crm_cloudsync_schema_guard");
    ctx.emergency = true;
    ctx.event.module = QStringLiteral("CloudSyncSchemaService");
    ctx.event.entityType = QStringLiteral("schema");
    ctx.event.entityId = entityName;
    ctx.event.operationType = QStringLiteral("destructive_schema_operation_guard");
    ctx.event.userVisibleDescription = QStringLiteral("Snapshot before CRM/cloudsync schema rebuild: %1").arg(entityName);
    ctx.event.changedTables = {entityName};
    SnapshotMetadata meta;
    QString localError;
    if (BackupSnapshotService::createSnapshot(ctx, &meta, &localError)) return true;
    if (errorText) {
        *errorText = QStringLiteral("Blocked CRM/cloudsync schema rebuild for %1 because pre-change snapshot failed: %2")
            .arg(entityName, localError);
    }
    qWarning().noquote() << QStringLiteral("CRM_CLOUDSYNC_SCHEMA_REBUILD_BLOCKED table=%1 error=%2").arg(entityName, localError);
    return false;
}

bool columnIsNotNull(QSqlDatabase db, const QString& table, const QString& column)
{
    QSqlQuery q(db);
    q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table));
    while (q.next()) {
        if (q.value(1).toString().compare(column, Qt::CaseInsensitive) == 0) {
            return q.value(3).toInt() != 0;
        }
    }
    return false;
}

bool ensureWorkOrdersCrmLinkColumnsOptional(QSqlDatabase db, QString* errorText)
{
    if (!tableExists(db, QStringLiteral("work_orders"))) return true;
    if (!ensureColumn(db, QStringLiteral("work_orders"), QStringLiteral("client_uid"), QStringLiteral("client_uid TEXT"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("work_orders"), QStringLiteral("vehicle_uid"), QStringLiteral("vehicle_uid TEXT"), errorText)) return false;
    if (!columnIsNotNull(db, QStringLiteral("work_orders"), QStringLiteral("client_uid"))
        && !columnIsNotNull(db, QStringLiteral("work_orders"), QStringLiteral("vehicle_uid"))) {
        return true;
    }

    if (!ensureSchemaDestructiveOperationSnapshot(QStringLiteral("work_orders"), errorText)) return false;
    if (!execSql(db, QStringLiteral("DROP TABLE IF EXISTS work_orders_rebuild;"), errorText)) return false;

    struct ColumnInfo {
        QString name;
        QString type;
        bool notNull = false;
        QString defaultValue;
        int pk = 0;
    };
    QList<ColumnInfo> columns;
    QSqlQuery info(db);
    if (!info.exec(QStringLiteral("PRAGMA table_info(work_orders)"))) {
        if (errorText) *errorText = info.lastError().text();
        return false;
    }
    while (info.next()) {
        ColumnInfo col;
        col.name = info.value(1).toString();
        col.type = info.value(2).toString().trimmed();
        col.notNull = info.value(3).toInt() != 0;
        col.defaultValue = info.value(4).toString().trimmed();
        col.pk = info.value(5).toInt();
        if (!isSafeIdentifier(col.name)) {
            if (errorText) *errorText = QStringLiteral("Unsafe work_orders column identifier: %1").arg(col.name);
            return false;
        }
        if (col.name.compare(QStringLiteral("client_uid"), Qt::CaseInsensitive) == 0
            || col.name.compare(QStringLiteral("vehicle_uid"), Qt::CaseInsensitive) == 0) {
            col.type = QStringLiteral("TEXT");
            col.notNull = false;
            col.defaultValue.clear();
        }
        columns.append(col);
    }
    if (columns.isEmpty()) {
        if (errorText) *errorText = QStringLiteral("work_orders table_info returned no columns");
        return false;
    }

    QStringList definitions;
    QStringList names;
    for (const ColumnInfo& col : columns) {
        QString definition = quoteIdent(col.name);
        definition += QLatin1Char(' ');
        definition += col.type.isEmpty() ? QStringLiteral("TEXT") : col.type;
        if (col.pk > 0) {
            definition += QStringLiteral(" PRIMARY KEY");
            if (col.type.compare(QStringLiteral("INTEGER"), Qt::CaseInsensitive) == 0) {
                // Preserve rowid primary-key behavior for work_orders.number.
            }
        } else {
            if (col.notNull) definition += QStringLiteral(" NOT NULL");
            if (!col.defaultValue.isEmpty()) definition += QStringLiteral(" DEFAULT ") + col.defaultValue;
        }
        definitions << definition;
        names << quoteIdent(col.name);
    }

    if (!execSql(db, QStringLiteral("CREATE TABLE work_orders_rebuild (%1);").arg(definitions.join(QStringLiteral(","))), errorText)) return false;
    const QString columnList = names.join(QStringLiteral(","));
    if (!execSql(db, QStringLiteral("INSERT INTO work_orders_rebuild(%1) SELECT %1 FROM work_orders;").arg(columnList), errorText)) return false;
    if (!execSql(db, QStringLiteral("DROP TABLE work_orders;"), errorText)) return false;
    if (!execSql(db, QStringLiteral("ALTER TABLE work_orders_rebuild RENAME TO work_orders;"), errorText)) return false;
    qWarning().noquote() << QStringLiteral("WORK_ORDERS_CRM_LINK_COLUMNS_REBUILT optional_client_vehicle_uid=1");
    return true;
}

bool columnNeedsNonNullDefaultRepair(QSqlDatabase db, const QString& table, const QString& column)
{
    QSqlQuery q(db);
    q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table));
    while (q.next()) {
        if (q.value(1).toString().compare(column, Qt::CaseInsensitive) != 0) continue;
        const bool notNull = q.value(3).toInt() != 0;
        const QString defaultValue = q.value(4).toString().trimmed();
        return notNull && defaultValue.isEmpty();
    }
    return false;
}

bool ensureCrmClientsDefaultCompatible(QSqlDatabase db, QString* errorText)
{
    if (!tableExists(db, QStringLiteral("crm_clients"))) return true;
    if (!columnNeedsNonNullDefaultRepair(db, QStringLiteral("crm_clients"), QStringLiteral("last_name"))) return true;

    if (!ensureSchemaDestructiveOperationSnapshot(QStringLiteral("crm_clients"), errorText)) return false;
    if (!execSql(db, QStringLiteral("DROP TABLE IF EXISTS crm_clients_rebuild;"), errorText)) return false;
    if (!execSql(db,
                 QStringLiteral("CREATE TABLE crm_clients_rebuild ("
                                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                "client_uid TEXT NOT NULL UNIQUE,"
                                "full_name TEXT NOT NULL DEFAULT '',"
                                "first_name TEXT NOT NULL DEFAULT '',"
                                "last_name TEXT NOT NULL DEFAULT '',"
                                "phone TEXT NOT NULL DEFAULT '',"
                                "phone_alt TEXT NOT NULL DEFAULT '',"
                                "email TEXT NOT NULL DEFAULT '',"
                                "source TEXT NOT NULL DEFAULT '',"
                                "status TEXT NOT NULL DEFAULT 'active',"
                                "loyalty_level TEXT NOT NULL DEFAULT '',"
                                "discount_percent REAL NOT NULL DEFAULT 0,"
                                "notes TEXT NOT NULL DEFAULT '',"
                                "created_at TEXT NOT NULL DEFAULT (datetime('now')),"
                                "updated_at TEXT NOT NULL DEFAULT (datetime('now'))"
                                ");"),
                 errorText)) return false;
    if (!execSql(db,
                 QStringLiteral("INSERT OR IGNORE INTO crm_clients_rebuild("
                                "id,client_uid,full_name,first_name,last_name,phone,phone_alt,email,source,status,loyalty_level,discount_percent,notes,created_at,updated_at) "
                                "SELECT id,"
                                "CASE WHEN TRIM(COALESCE(client_uid,''))='' THEN 'client-migrated-' || id ELSE client_uid END,"
                                "COALESCE(full_name,''),"
                                "COALESCE(first_name,''),"
                                "COALESCE(last_name,''),"
                                "COALESCE(phone,''),"
                                "COALESCE(phone_alt,''),"
                                "COALESCE(email,''),"
                                "COALESCE(source,''),"
                                "COALESCE(NULLIF(status,''),'active'),"
                                "COALESCE(loyalty_level,''),"
                                "COALESCE(discount_percent,0),"
                                "COALESCE(notes,''),"
                                "COALESCE(NULLIF(created_at,''),datetime('now')),"
                                "COALESCE(NULLIF(updated_at,''),datetime('now')) "
                                "FROM crm_clients;"),
                 errorText)) return false;
    if (!execSql(db, QStringLiteral("DROP TABLE crm_clients;"), errorText)) return false;
    if (!execSql(db, QStringLiteral("ALTER TABLE crm_clients_rebuild RENAME TO crm_clients;"), errorText)) return false;
    const QString key = QStringLiteral("settings.crm.clients_schema_repair_warning_logged");
    if (!dbGetSettingForConnection(db,key, false).toBool()) {
        qWarning().noquote() << QStringLiteral("CRM_CLIENTS_SCHEMA_REPAIRED last_name_default=1");
        dbSetSettingForConnection(db,key, true);
    }
    return true;
}

bool ensureCrmVehiclesDefaultCompatible(QSqlDatabase db, QString* errorText)
{
    if (!tableExists(db, QStringLiteral("crm_vehicles"))) return true;
    if (!columnNeedsNonNullDefaultRepair(db, QStringLiteral("crm_vehicles"), QStringLiteral("client_uid"))) return true;

    if (!ensureSchemaDestructiveOperationSnapshot(QStringLiteral("crm_vehicles"), errorText)) return false;
    if (!execSql(db, QStringLiteral("DROP TABLE IF EXISTS crm_vehicles_rebuild;"), errorText)) return false;
    if (!execSql(db,
                 QStringLiteral("CREATE TABLE crm_vehicles_rebuild ("
                                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                "vehicle_uid TEXT NOT NULL UNIQUE,"
                                "client_uid TEXT NOT NULL DEFAULT '',"
                                "plate TEXT NOT NULL DEFAULT '',"
                                "vin TEXT NOT NULL DEFAULT '',"
                                "brand TEXT NOT NULL DEFAULT '',"
                                "model TEXT NOT NULL DEFAULT '',"
                                "year INTEGER NOT NULL DEFAULT 0,"
                                "engine TEXT NOT NULL DEFAULT '',"
                                "fuel_type TEXT NOT NULL DEFAULT '',"
                                "mileage INTEGER NOT NULL DEFAULT 0,"
                                "color TEXT NOT NULL DEFAULT '',"
                                "notes TEXT NOT NULL DEFAULT '',"
                                "created_at TEXT NOT NULL DEFAULT (datetime('now')),"
                                "updated_at TEXT NOT NULL DEFAULT (datetime('now'))"
                                ");"),
                 errorText)) return false;
    if (!execSql(db,
                 QStringLiteral("INSERT OR IGNORE INTO crm_vehicles_rebuild("
                                "id,vehicle_uid,client_uid,plate,vin,brand,model,year,engine,fuel_type,mileage,color,notes,created_at,updated_at) "
                                "SELECT id,"
                                "CASE WHEN TRIM(COALESCE(vehicle_uid,''))='' THEN 'vehicle-migrated-' || id ELSE vehicle_uid END,"
                                "COALESCE(client_uid,''),"
                                "COALESCE(plate,''),"
                                "COALESCE(vin,''),"
                                "COALESCE(brand,''),"
                                "COALESCE(model,''),"
                                "COALESCE(year,0),"
                                "COALESCE(engine,''),"
                                "COALESCE(fuel_type,''),"
                                "COALESCE(mileage,0),"
                                "COALESCE(color,''),"
                                "COALESCE(notes,''),"
                                "COALESCE(NULLIF(created_at,''),datetime('now')),"
                                "COALESCE(NULLIF(updated_at,''),datetime('now')) "
                                "FROM crm_vehicles;"),
                 errorText)) return false;
    if (!execSql(db, QStringLiteral("DROP TABLE crm_vehicles;"), errorText)) return false;
    if (!execSql(db, QStringLiteral("ALTER TABLE crm_vehicles_rebuild RENAME TO crm_vehicles;"), errorText)) return false;
    const QString key = QStringLiteral("settings.crm.vehicles_schema_repair_warning_logged");
    if (!dbGetSettingForConnection(db,key, false).toBool()) {
        qWarning().noquote() << QStringLiteral("CRM_VEHICLES_SCHEMA_REPAIRED client_uid_default=1");
        dbSetSettingForConnection(db,key, true);
    }
    return true;
}

int pendingCrmWorkOrderLinks(QSqlDatabase db)
{
    int pending = 0;
    for (const QString& table : {QStringLiteral("work_orders"), QStringLiteral("router_cache_work_orders")}) {
        if (!tableExists(db, table)) continue;
        if (columnExists(db, table, QStringLiteral("crm_backfilled_at"))) {
            QSqlQuery q(db);
            if (q.exec(QStringLiteral("SELECT COUNT(*) FROM %1 WHERE TRIM(COALESCE(crm_backfilled_at,''))=''").arg(table)) && q.next()) {
                pending += q.value(0).toInt();
            }
            continue;
        }
        if (!columnExists(db, table, QStringLiteral("client_uid")) || !columnExists(db, table, QStringLiteral("vehicle_uid"))) continue;
        QSqlQuery q(db);
        if (q.exec(QStringLiteral("SELECT COUNT(*) FROM %1 WHERE TRIM(COALESCE(client_uid,''))='' OR TRIM(COALESCE(vehicle_uid,''))=''").arg(table)) && q.next()) {
            pending += q.value(0).toInt();
        }
    }
    return pending;
}

bool ensureUidTriggers(QSqlDatabase db, QString* errorText)
{
    if (tableExists(db, QStringLiteral("work_order_services")) && columnExists(db, QStringLiteral("work_order_services"), QStringLiteral("service_uid"))) {
        if (!execSql(db,
                     QStringLiteral("CREATE TRIGGER IF NOT EXISTS trg_work_order_services_service_uid_ai "
                                    "AFTER INSERT ON work_order_services "
                                    "WHEN TRIM(COALESCE(NEW.service_uid,''))='' "
                                    "BEGIN "
                                    "  UPDATE work_order_services SET service_uid='svc-' || lower(hex(randomblob(16))) WHERE id=NEW.id; "
                                    "END;"),
                     errorText)) return false;
    }
    if (tableExists(db, QStringLiteral("work_order_parts")) && columnExists(db, QStringLiteral("work_order_parts"), QStringLiteral("part_uid"))) {
        if (!execSql(db,
                     QStringLiteral("CREATE TRIGGER IF NOT EXISTS trg_work_order_parts_part_uid_ai "
                                    "AFTER INSERT ON work_order_parts "
                                    "WHEN TRIM(COALESCE(NEW.part_uid,''))='' "
                                    "BEGIN "
                                    "  UPDATE work_order_parts SET part_uid='part-' || lower(hex(randomblob(16))) WHERE id=NEW.id; "
                                    "END;"),
                     errorText)) return false;
    }
    if (tableExists(db, QStringLiteral("appointments")) && columnExists(db, QStringLiteral("appointments"), QStringLiteral("appointment_uid"))) {
        if (!execSql(db,
                     QStringLiteral("CREATE TRIGGER IF NOT EXISTS trg_appointments_appointment_uid_ai "
                                    "AFTER INSERT ON appointments "
                                    "WHEN TRIM(COALESCE(NEW.appointment_uid,''))='' "
                                    "BEGIN "
                                    "  UPDATE appointments SET appointment_uid='appt-' || lower(hex(randomblob(16))) WHERE id=NEW.id; "
                                    "END;"),
                     errorText)) return false;
    }
    if (tableExists(db, QStringLiteral("online_appointments")) && columnExists(db, QStringLiteral("online_appointments"), QStringLiteral("appointment_uid"))) {
        if (!execSql(db,
                     QStringLiteral("CREATE TRIGGER IF NOT EXISTS trg_online_appointments_appointment_uid_ai "
                                    "AFTER INSERT ON online_appointments "
                                    "WHEN TRIM(COALESCE(NEW.appointment_uid,''))='' "
                                    "BEGIN "
                                    "  UPDATE online_appointments SET appointment_uid='appt-' || lower(hex(randomblob(16))) WHERE id=NEW.id; "
                                    "END;"),
                     errorText)) return false;
    }
    if (tableExists(db, QStringLiteral("vehicle_intake")) && columnExists(db, QStringLiteral("vehicle_intake"), QStringLiteral("intake_uid"))) {
        if (!execSql(db,
                     QStringLiteral("CREATE TRIGGER IF NOT EXISTS trg_vehicle_intake_uid_ai "
                                    "AFTER INSERT ON vehicle_intake "
                                    "WHEN TRIM(COALESCE(NEW.intake_uid,''))='' "
                                    "BEGIN "
                                    "  UPDATE vehicle_intake SET intake_uid='intake-' || lower(hex(randomblob(16))) WHERE id=NEW.id; "
                                    "END;"),
                     errorText)) return false;
    }
    if (tableExists(db, QStringLiteral("vehicle_intake_photos")) && columnExists(db, QStringLiteral("vehicle_intake_photos"), QStringLiteral("photo_uid"))) {
        if (!execSql(db,
                     QStringLiteral("CREATE TRIGGER IF NOT EXISTS trg_vehicle_intake_photos_uid_ai "
                                    "AFTER INSERT ON vehicle_intake_photos "
                                    "WHEN TRIM(COALESCE(NEW.photo_uid,''))='' "
                                    "BEGIN "
                                    "  UPDATE vehicle_intake_photos SET photo_uid='photo-' || lower(hex(randomblob(16))) WHERE id=NEW.id; "
                                    "END;"),
                     errorText)) return false;
    }
    if (tableExists(db, QStringLiteral("file_attachments")) && columnExists(db, QStringLiteral("file_attachments"), QStringLiteral("file_uid"))) {
        if (!execSql(db,
                     QStringLiteral("CREATE TRIGGER IF NOT EXISTS trg_file_attachments_file_uid_ai "
                                    "AFTER INSERT ON file_attachments "
                                    "WHEN TRIM(COALESCE(NEW.file_uid,''))='' "
                                    "BEGIN "
                                    "  UPDATE file_attachments SET file_uid='file-' || lower(hex(randomblob(16))) WHERE id=NEW.id; "
                                    "END;"),
                     errorText)) return false;
    }
    return true;
}

bool ensureOutboxColumn(QSqlDatabase db,
                        const QString& column,
                        const QString& ddl,
                        bool* repaired,
                        QString* errorText)
{
    if (columnExists(db, QStringLiteral("sync_outbox"), column)) return true;
    if (repaired) *repaired = true;
    return ensureColumn(db, QStringLiteral("sync_outbox"), column, ddl, errorText);
}

bool ensureSyncOutboxOptionalColumns(QSqlDatabase db, QString* errorText)
{
    if (!tableExists(db, QStringLiteral("sync_outbox"))) return true;

    bool repaired = false;
    if (!ensureOutboxColumn(db, QStringLiteral("mutation_uid"), QStringLiteral("mutation_uid TEXT NOT NULL DEFAULT ''"), &repaired, errorText)) return false;
    if (!ensureOutboxColumn(db, QStringLiteral("idempotency_key"), QStringLiteral("idempotency_key TEXT NOT NULL DEFAULT ''"), &repaired, errorText)) return false;
    if (!ensureOutboxColumn(db, QStringLiteral("branch_uid"), QStringLiteral("branch_uid TEXT NOT NULL DEFAULT ''"), &repaired, errorText)) return false;
    if (!ensureOutboxColumn(db, QStringLiteral("updated_at"), QStringLiteral("updated_at TEXT NOT NULL DEFAULT ''"), &repaired, errorText)) return false;
    if (!ensureOutboxColumn(db, QStringLiteral("next_retry_at"), QStringLiteral("next_retry_at TEXT NOT NULL DEFAULT ''"), &repaired, errorText)) return false;

    if (repaired) {
        execSql(db,
                QStringLiteral("UPDATE sync_outbox "
                               "SET mutation_uid=CASE WHEN TRIM(COALESCE(mutation_uid,''))='' THEN COALESCE(NULLIF(event_uid,''), operation_id) ELSE mutation_uid END, "
                               "    idempotency_key=CASE WHEN TRIM(COALESCE(idempotency_key,''))='' THEN COALESCE(NULLIF(event_uid,''), operation_id) ELSE idempotency_key END, "
                               "    updated_at=CASE WHEN TRIM(COALESCE(updated_at,''))='' THEN COALESCE(NULLIF(created_at,''), datetime('now')) ELSE updated_at END "
                               "WHERE 1=1"),
                nullptr);
        const QString key = QStringLiteral("settings.cloudsync.outbox_schema_repair_warning_logged");
        if (!dbGetSettingForConnection(db,key, false).toBool()) {
            qWarning().noquote() << QStringLiteral("SYNC_OUTBOX_SCHEMA_REPAIRED optional_stage05_columns=1");
            dbSetSettingForConnection(db,key, true);
        }
    }
    return true;
}

} // namespace

namespace CloudSyncSchemaService {

bool ensureSchema(QSqlDatabase db, QString* errorText)
{
    if (!db.isValid()) db = Database::db();
    if (!db.isOpen() && !db.open()) {
        if (errorText) *errorText = db.lastError().text();
        return false;
    }

    // Етап 1: cloud sync вимкнений за замовчуванням, локальна робота не залежить від сервера.
    if (dbGetSettingForConnection(db,QStringLiteral("settings.cloudsync.defaults_seeded"), false).toBool() == false) {
        dbSetSettingForConnection(db,QStringLiteral("settings.cloudsync.enabled"), false);
        dbSetSettingForConnection(db,QStringLiteral("cloudsync.enabled"), false);
        dbSetSettingForConnection(db,QStringLiteral("settings.cloudsync.interval_seconds"), 60);
        dbSetSettingForConnection(db,QStringLiteral("settings.cloudsync.defaults_seeded"), true);
    }

    if (!DeviceIdentityService::ensureDeviceIdentity(db, errorText)) return false;

    if (!execSql(db,
                 QStringLiteral("CREATE TABLE IF NOT EXISTS sync_outbox ("
                                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                "event_uid TEXT NOT NULL UNIQUE,"
                                "mutation_uid TEXT NOT NULL DEFAULT '',"
                                "idempotency_key TEXT NOT NULL DEFAULT '',"
                                "operation_id TEXT NOT NULL DEFAULT '',"
                                "device_uid TEXT NOT NULL DEFAULT '',"
                                "branch_uid TEXT NOT NULL DEFAULT '',"
                                "entity_type TEXT NOT NULL DEFAULT '',"
                                "entity_uid TEXT NOT NULL DEFAULT '',"
                                "operation TEXT NOT NULL DEFAULT '',"
                                "changed_tables TEXT NOT NULL DEFAULT '',"
                                "payload_json TEXT NOT NULL DEFAULT '{}',"
                                "status TEXT NOT NULL DEFAULT 'pending',"
                                "retry_count INTEGER NOT NULL DEFAULT 0,"
                                "last_error TEXT NOT NULL DEFAULT '',"
                                "created_at TEXT NOT NULL DEFAULT (datetime('now')),"
                                "updated_at TEXT NOT NULL DEFAULT (datetime('now')),"
                                "next_retry_at TEXT NOT NULL DEFAULT '',"
                                "sent_at TEXT NOT NULL DEFAULT '',"
                                "server_ack_at TEXT NOT NULL DEFAULT ''"
                                ");"),
                 errorText)) return false;
    if (!ensureSyncOutboxOptionalColumns(db, errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_sync_outbox_status_created ON sync_outbox(status, created_at);"), errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_sync_outbox_entity ON sync_outbox(entity_type, entity_uid);"), errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_sync_outbox_idempotency ON sync_outbox(idempotency_key);"), errorText)) return false;

    // Етап 8: sync_inbox — контрольована черга серверних змін.
    if (!execSql(db,
                 QStringLiteral("CREATE TABLE IF NOT EXISTS sync_inbox ("
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
                                ");"),
                 errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("sync_inbox"), QStringLiteral("acked_at"), QStringLiteral("acked_at TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("sync_inbox"), QStringLiteral("apply_started_at"), QStringLiteral("apply_started_at TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_sync_inbox_status_received ON sync_inbox(status, received_at);"), errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_sync_inbox_entity ON sync_inbox(entity_type, entity_uid);"), errorText)) return false;

    // Етап 9: sync_conflicts — конфлікти кількох пристроїв не ламають локальну БД.
    if (!execSql(db,
                 QStringLiteral("CREATE TABLE IF NOT EXISTS sync_conflicts ("
                                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                "conflict_uid TEXT NOT NULL UNIQUE,"
                                "entity_type TEXT NOT NULL DEFAULT '',"
                                "entity_uid TEXT NOT NULL DEFAULT '',"
                                "local_payload_json TEXT NOT NULL DEFAULT '{}',"
                                "remote_payload_json TEXT NOT NULL DEFAULT '{}',"
                                "conflict_type TEXT NOT NULL DEFAULT '',"
                                "resolution TEXT NOT NULL DEFAULT 'pending',"
                                "resolved_by TEXT NOT NULL DEFAULT '',"
                                "created_at TEXT NOT NULL DEFAULT (datetime('now')),"
                                "resolved_at TEXT NOT NULL DEFAULT ''"
                                ");"),
                 errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_sync_conflicts_resolution_created ON sync_conflicts(resolution, created_at);"), errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_sync_conflicts_entity ON sync_conflicts(entity_type, entity_uid);"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("sync_conflicts"), QStringLiteral("deferred_at"), QStringLiteral("deferred_at TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("sync_conflicts"), QStringLiteral("defer_reason"), QStringLiteral("defer_reason TEXT NOT NULL DEFAULT ''"), errorText)) return false;

    // Етап 9: audit log рішень sync/conflict resolver.
    if (!execSql(db,
                 QStringLiteral("CREATE TABLE IF NOT EXISTS sync_audit_log ("
                                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                "created_at TEXT NOT NULL DEFAULT (datetime('now')),"
                                "action TEXT NOT NULL DEFAULT '',"
                                "entity_type TEXT NOT NULL DEFAULT '',"
                                "entity_uid TEXT NOT NULL DEFAULT '',"
                                "conflict_uid TEXT NOT NULL DEFAULT '',"
                                "resolution TEXT NOT NULL DEFAULT '',"
                                "result TEXT NOT NULL DEFAULT '',"
                                "actor TEXT NOT NULL DEFAULT '',"
                                "reason TEXT NOT NULL DEFAULT '',"
                                "details_json TEXT NOT NULL DEFAULT '{}'"
                                ");"),
                 errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_sync_audit_log_created ON sync_audit_log(created_at);"), errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_sync_audit_log_action ON sync_audit_log(action);"), errorText)) return false;

    if (!execSql(db,
                 QStringLiteral("CREATE TABLE IF NOT EXISTS crm_clients ("
                                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                "client_uid TEXT NOT NULL UNIQUE,"
                                "full_name TEXT NOT NULL DEFAULT '',"
                                "first_name TEXT NOT NULL DEFAULT '',"
                                "last_name TEXT NOT NULL DEFAULT '',"
                                "phone TEXT NOT NULL DEFAULT '',"
                                "phone_alt TEXT NOT NULL DEFAULT '',"
                                "email TEXT NOT NULL DEFAULT '',"
                                "source TEXT NOT NULL DEFAULT '',"
                                "status TEXT NOT NULL DEFAULT 'active',"
                                "loyalty_level TEXT NOT NULL DEFAULT '',"
                                "discount_percent REAL NOT NULL DEFAULT 0,"
                                "notes TEXT NOT NULL DEFAULT '',"
                                "created_at TEXT NOT NULL DEFAULT (datetime('now')),"
                                "updated_at TEXT NOT NULL DEFAULT (datetime('now'))"
                                ");"),
                 errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_clients"), QStringLiteral("client_uid"), QStringLiteral("client_uid TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_clients"), QStringLiteral("full_name"), QStringLiteral("full_name TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_clients"), QStringLiteral("first_name"), QStringLiteral("first_name TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_clients"), QStringLiteral("last_name"), QStringLiteral("last_name TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_clients"), QStringLiteral("phone"), QStringLiteral("phone TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_clients"), QStringLiteral("phone_alt"), QStringLiteral("phone_alt TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_clients"), QStringLiteral("email"), QStringLiteral("email TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_clients"), QStringLiteral("source"), QStringLiteral("source TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_clients"), QStringLiteral("status"), QStringLiteral("status TEXT NOT NULL DEFAULT 'active'"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_clients"), QStringLiteral("loyalty_level"), QStringLiteral("loyalty_level TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_clients"), QStringLiteral("discount_percent"), QStringLiteral("discount_percent REAL NOT NULL DEFAULT 0"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_clients"), QStringLiteral("notes"), QStringLiteral("notes TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_clients"), QStringLiteral("created_at"), QStringLiteral("created_at TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_clients"), QStringLiteral("updated_at"), QStringLiteral("updated_at TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureCrmClientsDefaultCompatible(db, errorText)) return false;

    if (!execSql(db,
                 QStringLiteral("CREATE TABLE IF NOT EXISTS crm_vehicles ("
                                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                "vehicle_uid TEXT NOT NULL UNIQUE,"
                                "client_uid TEXT NOT NULL DEFAULT '',"
                                "plate TEXT NOT NULL DEFAULT '',"
                                "vin TEXT NOT NULL DEFAULT '',"
                                "brand TEXT NOT NULL DEFAULT '',"
                                "model TEXT NOT NULL DEFAULT '',"
                                "year INTEGER NOT NULL DEFAULT 0,"
                                "engine TEXT NOT NULL DEFAULT '',"
                                "fuel_type TEXT NOT NULL DEFAULT '',"
                                "mileage INTEGER NOT NULL DEFAULT 0,"
                                "color TEXT NOT NULL DEFAULT '',"
                                "notes TEXT NOT NULL DEFAULT '',"
                                "created_at TEXT NOT NULL DEFAULT (datetime('now')),"
                                "updated_at TEXT NOT NULL DEFAULT (datetime('now'))"
                                ");"),
                 errorText)) return false;
    if (!ensureCrmVehiclesDefaultCompatible(db, errorText)) return false;

    if (!execSql(db,
                 QStringLiteral("CREATE TABLE IF NOT EXISTS crm_client_events ("
                                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                "event_uid TEXT NOT NULL UNIQUE,"
                                "client_uid TEXT NOT NULL DEFAULT '',"
                                "vehicle_uid TEXT NOT NULL DEFAULT '',"
                                "order_uid TEXT NOT NULL DEFAULT '',"
                                "event_type TEXT NOT NULL DEFAULT '',"
                                "title TEXT NOT NULL DEFAULT '',"
                                "body TEXT NOT NULL DEFAULT '',"
                                "created_by TEXT NOT NULL DEFAULT '',"
                                "created_at TEXT NOT NULL DEFAULT (datetime('now'))"
                                ");"),
                 errorText)) return false;

    if (!execSql(db,
                 QStringLiteral("CREATE TABLE IF NOT EXISTS crm_client_tasks ("
                                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                "task_uid TEXT NOT NULL UNIQUE,"
                                "client_uid TEXT NOT NULL DEFAULT '',"
                                "vehicle_uid TEXT NOT NULL DEFAULT '',"
                                "order_uid TEXT NOT NULL DEFAULT '',"
                                "title TEXT NOT NULL DEFAULT '',"
                                "due_at TEXT NOT NULL DEFAULT '',"
                                "status TEXT NOT NULL DEFAULT 'open',"
                                "priority TEXT NOT NULL DEFAULT 'normal',"
                                "assigned_to TEXT NOT NULL DEFAULT '',"
                                "created_at TEXT NOT NULL DEFAULT (datetime('now')),"
                                "completed_at TEXT NOT NULL DEFAULT ''"
                                ");"),
                 errorText)) return false;

    if (!ensureWorkOrdersCrmLinkColumnsOptional(db, errorText)) return false;
    // Mobile workflow data is stored alongside the canonical desktop order.
    // Keep it as JSON so new diagnostic fields can travel without destructive
    // migrations, while the important service/part lifecycle fields stay queryable.
    if (!ensureColumn(db, QStringLiteral("work_orders"), QStringLiteral("mobile_state_json"), QStringLiteral("mobile_state_json TEXT NOT NULL DEFAULT '{}'"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("work_orders"), QStringLiteral("updated_at"), QStringLiteral("updated_at TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("work_order_services"), QStringLiteral("service_uid"), QStringLiteral("service_uid TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("work_order_services"), QStringLiteral("status"), QStringLiteral("status TEXT NOT NULL DEFAULT 'open'"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("work_order_services"), QStringLiteral("completed_at"), QStringLiteral("completed_at TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("work_order_services"), QStringLiteral("completed_by"), QStringLiteral("completed_by TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("work_order_services"), QStringLiteral("assigned_to"), QStringLiteral("assigned_to TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("work_order_services"), QStringLiteral("assigned_to_name"), QStringLiteral("assigned_to_name TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("work_order_services"), QStringLiteral("source"), QStringLiteral("source TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("work_order_services"), QStringLiteral("diagnostic_uid"), QStringLiteral("diagnostic_uid TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("work_order_services"), QStringLiteral("source_suggestion_uid"), QStringLiteral("source_suggestion_uid TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("work_order_services"), QStringLiteral("metadata_json"), QStringLiteral("metadata_json TEXT NOT NULL DEFAULT '{}'"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("work_order_services"), QStringLiteral("created_at"), QStringLiteral("created_at TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("work_order_services"), QStringLiteral("updated_at"), QStringLiteral("updated_at TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("work_order_parts"), QStringLiteral("part_uid"), QStringLiteral("part_uid TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("work_order_parts"), QStringLiteral("status"), QStringLiteral("status TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("work_order_parts"), QStringLiteral("installed"), QStringLiteral("installed INTEGER NOT NULL DEFAULT 0"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("work_order_parts"), QStringLiteral("installed_by"), QStringLiteral("installed_by TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("work_order_parts"), QStringLiteral("request_uid"), QStringLiteral("request_uid TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("work_order_parts"), QStringLiteral("requested_at"), QStringLiteral("requested_at TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("work_order_parts"), QStringLiteral("requested_by"), QStringLiteral("requested_by TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("work_order_parts"), QStringLiteral("requested_by_uid"), QStringLiteral("requested_by_uid TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("work_order_parts"), QStringLiteral("ordered_by"), QStringLiteral("ordered_by TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("work_order_parts"), QStringLiteral("ordered_by_uid"), QStringLiteral("ordered_by_uid TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("work_order_parts"), QStringLiteral("order_comment"), QStringLiteral("order_comment TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("work_order_parts"), QStringLiteral("node_group"), QStringLiteral("node_group TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("work_order_parts"), QStringLiteral("component"), QStringLiteral("component TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("work_order_parts"), QStringLiteral("metadata_json"), QStringLiteral("metadata_json TEXT NOT NULL DEFAULT '{}'"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("work_order_parts"), QStringLiteral("created_at"), QStringLiteral("created_at TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("work_order_parts"), QStringLiteral("updated_at"), QStringLiteral("updated_at TEXT NOT NULL DEFAULT ''"), errorText)) return false;

    // Older mobile events were marked applied before their lifecycle columns
    // existed locally. Replay them once after this schema upgrade so completed
    // work, diagnostics, and part-order states become visible in the desktop UI.
    const QString mobileReplayKey = QStringLiteral("settings.cloudsync.mobile_lifecycle_replay_v1");
    if (!dbGetSettingForConnection(db,mobileReplayKey, false).toBool()
        && tableExists(db, QStringLiteral("sync_inbox"))) {
        QSqlQuery replay(db);
        replay.prepare(QStringLiteral(
            "UPDATE sync_inbox SET status='pending', apply_error='', acked_at='' "
            "WHERE status='applied' AND source_device_uid='mobile-panel' "
            "AND entity_type IN ('work_order','work_order_service','work_order_part')"));
        if (!replay.exec()) {
            if (errorText) *errorText = replay.lastError().text();
            return false;
        }
        dbSetSettingForConnection(db,mobileReplayKey, true);
        qInfo().noquote() << QStringLiteral("MOBILE_LIFECYCLE_EVENTS_REQUEUED count=%1").arg(replay.numRowsAffected());
    }

    // Publish one new full order snapshot after the lifecycle replay. The
    // mobile API reads the server materialized state, so this closes the
    // desktop-to-phone gap for orders that existed before bidirectional sync.
    const QString mobileSnapshotKey = QStringLiteral("settings.cloudsync.mobile_lifecycle_snapshot_v1");
    if (!dbGetSettingForConnection(db,mobileSnapshotKey, false).toBool()) {
        dbSetSettingForConnection(db,QStringLiteral("settings.cloudsync.initial_work_order_snapshot_server"), QString());
        dbSetSettingForConnection(db,mobileSnapshotKey, true);
        qInfo().noquote() << QStringLiteral("MOBILE_LIFECYCLE_SNAPSHOT_REQUEUED");
    }

    // Етап 11: локальна черга онлайн-заявок із сайту / CRM.
    if (!execSql(db,
                 QStringLiteral("CREATE TABLE IF NOT EXISTS online_booking_requests ("
                                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                "appointment_uid TEXT NOT NULL UNIQUE,"
                                "client_uid TEXT NOT NULL DEFAULT '',"
                                "vehicle_uid TEXT NOT NULL DEFAULT '',"
                                "client_name TEXT NOT NULL DEFAULT '',"
                                "phone TEXT NOT NULL DEFAULT '',"
                                "plate TEXT NOT NULL DEFAULT '',"
                                "car_text TEXT NOT NULL DEFAULT '',"
                                "service_reason TEXT NOT NULL DEFAULT '',"
                                "desired_start_at TEXT NOT NULL DEFAULT '',"
                                "desired_end_at TEXT NOT NULL DEFAULT '',"
                                "status TEXT NOT NULL DEFAULT 'new',"
                                "source TEXT NOT NULL DEFAULT 'website',"
                                "comment TEXT NOT NULL DEFAULT '',"
                                "created_at TEXT NOT NULL DEFAULT (datetime('now')),"
                                "updated_at TEXT NOT NULL DEFAULT (datetime('now'))"
                                ");"),
                 errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_online_booking_status_created ON online_booking_requests(status, created_at);"), errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_online_booking_uid ON online_booking_requests(appointment_uid);"), errorText)) return false;

    // Етап 3: глобальний file_uid для вкладень. Повний file-sync належить
    // наступним етапам, але локальна схема вже має бути готовою, щоб CRM/sync
    // не втрачали посилання на PDF/фото/документи під час outbox snapshot.
    if (!execSql(db,
                 QStringLiteral("CREATE TABLE IF NOT EXISTS file_attachments ("
                                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                "file_uid TEXT NOT NULL UNIQUE,"
                                "entity_type TEXT NOT NULL DEFAULT '',"
                                "entity_uid TEXT NOT NULL DEFAULT '',"
                                "local_path TEXT NOT NULL DEFAULT '',"
                                "server_url TEXT NOT NULL DEFAULT '',"
                                "mime_type TEXT NOT NULL DEFAULT '',"
                                "file_name TEXT NOT NULL DEFAULT '',"
                                "sha256 TEXT NOT NULL DEFAULT '',"
                                "size_bytes INTEGER NOT NULL DEFAULT 0,"
                                "sync_status TEXT NOT NULL DEFAULT 'local',"
                                "created_at TEXT NOT NULL DEFAULT (datetime('now'))"
                                ");"),
                 errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_file_attachments_entity ON file_attachments(entity_type, entity_uid);"), errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_file_attachments_sha256 ON file_attachments(sha256);"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("file_attachments"), QStringLiteral("filename"), QStringLiteral("filename TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("file_attachments"), QStringLiteral("storage_provider"), QStringLiteral("storage_provider TEXT NOT NULL DEFAULT 'local'"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("file_attachments"), QStringLiteral("remote_url"), QStringLiteral("remote_url TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("file_attachments"), QStringLiteral("size"), QStringLiteral("size INTEGER NOT NULL DEFAULT 0"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("file_attachments"), QStringLiteral("metadata_json"), QStringLiteral("metadata_json TEXT NOT NULL DEFAULT '{}'"), errorText)) return false;

    // Етап 13: активне приймання авто з фото, чек-листом і підписом.
    if (!execSql(db,
                 QStringLiteral("CREATE TABLE IF NOT EXISTS vehicle_intake ("
                                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                "intake_uid TEXT NOT NULL UNIQUE,"
                                "order_uid TEXT NOT NULL DEFAULT '',"
                                "client_uid TEXT NOT NULL DEFAULT '',"
                                "vehicle_uid TEXT NOT NULL DEFAULT '',"
                                "mileage INTEGER NOT NULL DEFAULT 0,"
                                "fuel_level TEXT NOT NULL DEFAULT '',"
                                "exterior_state TEXT NOT NULL DEFAULT '',"
                                "interior_state TEXT NOT NULL DEFAULT '',"
                                "checklist_json TEXT NOT NULL DEFAULT '{}',"
                                "customer_signature_path TEXT NOT NULL DEFAULT '',"
                                "pdf_path TEXT NOT NULL DEFAULT '',"
                                "act_number TEXT NOT NULL DEFAULT '',"
                                "act_date TEXT NOT NULL DEFAULT '',"
                                "accepted_at TEXT NOT NULL DEFAULT '',"
                                "sync_status TEXT NOT NULL DEFAULT 'local',"
                                "created_at TEXT NOT NULL DEFAULT (datetime('now')),"
                                "updated_at TEXT NOT NULL DEFAULT (datetime('now'))"
                                ");"),
                 errorText)) return false;
    if (!execSql(db,
                 QStringLiteral("CREATE TABLE IF NOT EXISTS vehicle_intake_photos ("
                                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                "photo_uid TEXT NOT NULL UNIQUE,"
                                "intake_uid TEXT NOT NULL DEFAULT '',"
                                "order_uid TEXT NOT NULL DEFAULT '',"
                                "local_path TEXT NOT NULL DEFAULT '',"
                                "server_url TEXT NOT NULL DEFAULT '',"
                                "photo_type TEXT NOT NULL DEFAULT '',"
                                "created_at TEXT NOT NULL DEFAULT (datetime('now'))"
                                ");"),
                 errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("vehicle_intake"), QStringLiteral("act_number"), QStringLiteral("act_number TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("vehicle_intake"), QStringLiteral("act_date"), QStringLiteral("act_date TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("vehicle_intake"), QStringLiteral("accepted_at"), QStringLiteral("accepted_at TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_vehicle_intake_order ON vehicle_intake(order_uid);"), errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_vehicle_intake_photos_intake ON vehicle_intake_photos(intake_uid);"), errorText)) return false;

    // Етапи 14-23: додаткові CRM/server сутності із безпечними default-off інтеграціями.
    if (!execSql(db,
                 QStringLiteral("CREATE TABLE IF NOT EXISTS estimate_approvals ("
                                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                "approval_uid TEXT NOT NULL UNIQUE,"
                                "order_uid TEXT NOT NULL DEFAULT '',"
                                "client_uid TEXT NOT NULL DEFAULT '',"
                                "vehicle_uid TEXT NOT NULL DEFAULT '',"
                                "estimate_json TEXT NOT NULL DEFAULT '{}',"
                                "status TEXT NOT NULL DEFAULT 'pending',"
                                "decision_comment TEXT NOT NULL DEFAULT '',"
                                "approved_at TEXT NOT NULL DEFAULT '',"
                                "declined_at TEXT NOT NULL DEFAULT '',"
                                "created_at TEXT NOT NULL DEFAULT (datetime('now')),"
                                "updated_at TEXT NOT NULL DEFAULT (datetime('now'))"
                                ");"),
                 errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_estimate_approvals_order ON estimate_approvals(order_uid);"), errorText)) return false;

    if (!execSql(db,
                 QStringLiteral("CREATE TABLE IF NOT EXISTS supplier_price_quotes ("
                                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                "quote_uid TEXT NOT NULL UNIQUE,"
                                "supplier_code TEXT NOT NULL DEFAULT '',"
                                "part_number TEXT NOT NULL DEFAULT '',"
                                "brand TEXT NOT NULL DEFAULT '',"
                                "name TEXT NOT NULL DEFAULT '',"
                                "price REAL NOT NULL DEFAULT 0,"
                                "currency TEXT NOT NULL DEFAULT '',"
                                "available_qty INTEGER NOT NULL DEFAULT 0,"
                                "status TEXT NOT NULL DEFAULT 'cached',"
                                "raw_payload_json TEXT NOT NULL DEFAULT '{}',"
                                "created_at TEXT NOT NULL DEFAULT (datetime('now')),"
                                "updated_at TEXT NOT NULL DEFAULT (datetime('now'))"
                                ");"),
                 errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_supplier_quotes_lookup ON supplier_price_quotes(supplier_code, part_number, brand);"), errorText)) return false;

    if (!execSql(db,
                 QStringLiteral("CREATE TABLE IF NOT EXISTS communication_messages ("
                                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                "message_uid TEXT NOT NULL UNIQUE,"
                                "client_uid TEXT NOT NULL DEFAULT '',"
                                "order_uid TEXT NOT NULL DEFAULT '',"
                                "channel TEXT NOT NULL DEFAULT '',"
                                "recipient TEXT NOT NULL DEFAULT '',"
                                "subject TEXT NOT NULL DEFAULT '',"
                                "body TEXT NOT NULL DEFAULT '',"
                                "status TEXT NOT NULL DEFAULT 'queued',"
                                "provider TEXT NOT NULL DEFAULT '',"
                                "last_error TEXT NOT NULL DEFAULT '',"
                                "created_at TEXT NOT NULL DEFAULT (datetime('now')),"
                                "sent_at TEXT NOT NULL DEFAULT ''"
                                ");"),
                 errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_communication_status ON communication_messages(status, created_at);"), errorText)) return false;

    if (!execSql(db,
                 QStringLiteral("CREATE TABLE IF NOT EXISTS owner_dashboard_snapshots ("
                                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                "snapshot_uid TEXT NOT NULL UNIQUE,"
                                "period_start TEXT NOT NULL DEFAULT '',"
                                "period_end TEXT NOT NULL DEFAULT '',"
                                "metrics_json TEXT NOT NULL DEFAULT '{}',"
                                "created_at TEXT NOT NULL DEFAULT (datetime('now'))"
                                ");"),
                 errorText)) return false;

    if (!execSql(db,
                 QStringLiteral("CREATE TABLE IF NOT EXISTS crm_audit_events ("
                                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                "audit_uid TEXT NOT NULL UNIQUE,"
                                "actor TEXT NOT NULL DEFAULT '',"
                                "actor_user_uid TEXT NOT NULL DEFAULT '',"
                                "actor_login TEXT NOT NULL DEFAULT '',"
                                "actor_role TEXT NOT NULL DEFAULT '',"
                                "action TEXT NOT NULL DEFAULT '',"
                                "entity_type TEXT NOT NULL DEFAULT '',"
                                "entity_uid TEXT NOT NULL DEFAULT '',"
                                "before_json TEXT NOT NULL DEFAULT '{}',"
                                "after_json TEXT NOT NULL DEFAULT '{}',"
                                "details_json TEXT NOT NULL DEFAULT '{}',"
                                "source TEXT NOT NULL DEFAULT 'desktop',"
                                "created_at TEXT NOT NULL DEFAULT (datetime('now'))"
                                ");"),
                 errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_crm_audit_entity ON crm_audit_events(entity_type, entity_uid);"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_audit_events"), QStringLiteral("actor_user_uid"), QStringLiteral("actor_user_uid TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_audit_events"), QStringLiteral("actor_login"), QStringLiteral("actor_login TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_audit_events"), QStringLiteral("actor_role"), QStringLiteral("actor_role TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_audit_events"), QStringLiteral("before_json"), QStringLiteral("before_json TEXT NOT NULL DEFAULT '{}'"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_audit_events"), QStringLiteral("after_json"), QStringLiteral("after_json TEXT NOT NULL DEFAULT '{}'"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("crm_audit_events"), QStringLiteral("source"), QStringLiteral("source TEXT NOT NULL DEFAULT 'desktop'"), errorText)) return false;

    if (!execSql(db,
                 QStringLiteral("CREATE TABLE IF NOT EXISTS cloud_config_migrations ("
                                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                "migration_uid TEXT NOT NULL DEFAULT '',"
                                "config_version TEXT NOT NULL DEFAULT '',"
                                "migration_key TEXT NOT NULL UNIQUE,"
                                "from_version TEXT NOT NULL DEFAULT '',"
                                "to_version TEXT NOT NULL DEFAULT '',"
                                "status TEXT NOT NULL DEFAULT 'pending',"
                                "error TEXT NOT NULL DEFAULT '',"
                                "details_json TEXT NOT NULL DEFAULT '{}',"
                                "created_at TEXT NOT NULL DEFAULT (datetime('now')),"
                                "applied_at TEXT NOT NULL DEFAULT ''"
                                ");"),
                 errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("cloud_config_migrations"), QStringLiteral("migration_uid"), QStringLiteral("migration_uid TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("cloud_config_migrations"), QStringLiteral("config_version"), QStringLiteral("config_version TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("cloud_config_migrations"), QStringLiteral("from_version"), QStringLiteral("from_version TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("cloud_config_migrations"), QStringLiteral("to_version"), QStringLiteral("to_version TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("cloud_config_migrations"), QStringLiteral("error"), QStringLiteral("error TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("cloud_config_migrations"), QStringLiteral("created_at"), QStringLiteral("created_at TEXT NOT NULL DEFAULT ''"), errorText)) return false;

    if (!execSql(db,
                 QStringLiteral("CREATE TABLE IF NOT EXISTS server_backup_jobs ("
                                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                "backup_uid TEXT NOT NULL UNIQUE,"
                                "device_uid TEXT NOT NULL DEFAULT '',"
                                "branch_uid TEXT NOT NULL DEFAULT '',"
                                "scope TEXT NOT NULL DEFAULT 'business_server',"
                                "status TEXT NOT NULL DEFAULT 'planned',"
                                "db_path TEXT NOT NULL DEFAULT '',"
                                "backup_path TEXT NOT NULL DEFAULT '',"
                                "size INTEGER NOT NULL DEFAULT 0,"
                                "sha256 TEXT NOT NULL DEFAULT '',"
                                "storage_path TEXT NOT NULL DEFAULT '',"
                                "checksum TEXT NOT NULL DEFAULT '',"
                                "last_error TEXT NOT NULL DEFAULT '',"
                                "created_at TEXT NOT NULL DEFAULT (datetime('now')),"
                                "completed_at TEXT NOT NULL DEFAULT ''"
                                ");"),
                 errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("server_backup_jobs"), QStringLiteral("device_uid"), QStringLiteral("device_uid TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("server_backup_jobs"), QStringLiteral("branch_uid"), QStringLiteral("branch_uid TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("server_backup_jobs"), QStringLiteral("db_path"), QStringLiteral("db_path TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("server_backup_jobs"), QStringLiteral("backup_path"), QStringLiteral("backup_path TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("server_backup_jobs"), QStringLiteral("size"), QStringLiteral("size INTEGER NOT NULL DEFAULT 0"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("server_backup_jobs"), QStringLiteral("sha256"), QStringLiteral("sha256 TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("server_backup_jobs"), QStringLiteral("last_error"), QStringLiteral("last_error TEXT NOT NULL DEFAULT ''"), errorText)) return false;

    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_crm_clients_phone ON crm_clients(phone);"), errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_crm_clients_phone_alt ON crm_clients(phone_alt);"), errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_crm_clients_status_updated ON crm_clients(status, updated_at);"), errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_crm_vehicles_client_uid ON crm_vehicles(client_uid);"), errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_crm_vehicles_plate ON crm_vehicles(plate);"), errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_crm_vehicles_client_plate ON crm_vehicles(client_uid, plate);"), errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_crm_vehicles_client_vin ON crm_vehicles(client_uid, vin);"), errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_crm_events_client_created ON crm_client_events(client_uid, created_at);"), errorText)) return false;
    if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_crm_tasks_client_status ON crm_client_tasks(client_uid, status);"), errorText)) return false;

    QString uidBackfillError;
    if (!EntityUidService::backfillMissingUids(db, &uidBackfillError)) {
        qWarning().noquote() << QStringLiteral("ENTITY_UID_BACKFILL_SKIPPED error=%1").arg(uidBackfillError.trimmed());
        // UID enrichment is sync-readiness metadata. Local desktop saves must
        // remain available even when an older table cannot be enriched in one pass.
    }
    QString uidTriggerError;
    if (!ensureUidTriggers(db, &uidTriggerError)) {
        qWarning().noquote() << QStringLiteral("ENTITY_UID_TRIGGER_SETUP_SKIPPED error=%1").arg(uidTriggerError.trimmed());
    }
    const QString crmBackfillKey = QStringLiteral("settings.crm.stage1_5_backfill.completed");
    if (!dbGetSettingForConnection(db,crmBackfillKey, false).toBool()) {
        QString crmBackfillError;
        if (!CrmClientService::backfillWorkOrders(db, &crmBackfillError)) {
            qWarning().noquote() << QStringLiteral("CRM_BACKFILL_SKIPPED error=%1").arg(crmBackfillError);
            // Existing local work must remain available even if legacy data is too inconsistent
            // for a one-shot CRM backfill. New/edited orders still receive CRM links through services.
        }
        const int pendingLinks = pendingCrmWorkOrderLinks(db);
        dbSetSettingForConnection(db,QStringLiteral("settings.crm.stage1_5_backfill.pending_count"), pendingLinks);
        if (pendingLinks <= 0) {
            dbSetSettingForConnection(db,crmBackfillKey, true);
        } else {
            qInfo().noquote() << QStringLiteral("CRM_BACKFILL_PENDING pending_order_links=%1").arg(pendingLinks);
        }
    }
    return true;
}

} // namespace CloudSyncSchemaService
