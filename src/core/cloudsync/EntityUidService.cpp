#include "core/cloudsync/EntityUidService.h"

#include "core/services/OrderIdentityService.h"
#include "infrastructure/db/sqlite/Db.h"

#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace {

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

bool tableExists(QSqlDatabase db, const QString& table)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type='table' AND name=? LIMIT 1"));
    q.addBindValue(table);
    return q.exec() && q.next();
}

bool columnExists(QSqlDatabase db, const QString& table, const QString& column)
{
    if (!isSafeIdentifier(table)) return false;
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(quoteIdent(table)))) return false;
    while (q.next()) {
        if (q.value(1).toString().compare(column, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

bool execSql(QSqlDatabase db, const QString& sql, QString* errorText)
{
    QSqlQuery q(db);
    if (q.exec(sql)) return true;
    if (errorText) *errorText = q.lastError().text();
    return false;
}

bool ensureColumn(QSqlDatabase db, const QString& table, const QString& column, const QString& ddl, QString* errorText)
{
    if (!tableExists(db, table)) return true;
    if (columnExists(db, table, column)) return true;
    if (!isSafeIdentifier(table)) {
        if (errorText) *errorText = QStringLiteral("Unsafe table identifier: %1").arg(table);
        return false;
    }
    return execSql(db, QStringLiteral("ALTER TABLE %1 ADD COLUMN %2;").arg(quoteIdent(table), ddl), errorText);
}

bool createIndex(QSqlDatabase db, const QString& sql)
{
    QSqlQuery q(db);
    return q.exec(sql);
}

bool backfillTable(QSqlDatabase db, const QString& table, const QString& uidColumn, const QString& prefix, QString* errorText)
{
    if (!tableExists(db, table) || !columnExists(db, table, uidColumn)) return true;
    if (!isSafeIdentifier(table) || !isSafeIdentifier(uidColumn)) {
        if (errorText) *errorText = QStringLiteral("Unsafe UID backfill identifier: %1.%2").arg(table, uidColumn);
        return false;
    }

    QSqlQuery select(db);
    const QString sql = QStringLiteral("SELECT id FROM %1 WHERE TRIM(COALESCE(%2,''))='' ORDER BY id ASC")
                            .arg(quoteIdent(table), quoteIdent(uidColumn));
    if (!select.exec(sql)) {
        if (errorText) *errorText = select.lastError().text();
        return false;
    }
    while (select.next()) {
        const int id = select.value(0).toInt();
        QSqlQuery update(db);
        update.prepare(QStringLiteral("UPDATE %1 SET %2=? WHERE id=? AND TRIM(COALESCE(%2,''))=''")
                           .arg(quoteIdent(table), quoteIdent(uidColumn)));
        update.addBindValue(EntityUidService::generateUid(prefix));
        update.addBindValue(id);
        if (!update.exec()) {
            if (errorText) *errorText = update.lastError().text();
            return false;
        }
    }
    return true;
}

} // namespace

namespace EntityUidService {

QString generateUid(const QString& prefix)
{
    const QString normalized = prefix.trimmed().isEmpty() ? QStringLiteral("uid") : prefix.trimmed().toLower();
    return normalized + QStringLiteral("-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool ensureUidForRow(QSqlDatabase db,
                     const QString& table,
                     const QString& uidColumn,
                     int id,
                     QString* errorText)
{
    if (!db.isValid()) db = Database::db();
    if (!db.isOpen() && !db.open()) {
        if (errorText) *errorText = db.lastError().text();
        return false;
    }
    if (id <= 0) return true;
    if (!tableExists(db, table) || !columnExists(db, table, uidColumn)) return true;
    if (!isSafeIdentifier(table) || !isSafeIdentifier(uidColumn)) {
        if (errorText) *errorText = QStringLiteral("Unsafe UID identifier: %1.%2").arg(table, uidColumn);
        return false;
    }

    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT COALESCE(%1,'') FROM %2 WHERE id=? LIMIT 1")
                  .arg(quoteIdent(uidColumn), quoteIdent(table)));
    q.addBindValue(id);
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    if (!q.next() || !q.value(0).toString().trimmed().isEmpty()) return true;

    QString prefix = QStringLiteral("uid");
    if (uidColumn == QStringLiteral("service_uid")) prefix = QStringLiteral("svc");
    else if (uidColumn == QStringLiteral("part_uid")) prefix = QStringLiteral("part");
    else if (uidColumn == QStringLiteral("client_uid")) prefix = QStringLiteral("client");
    else if (uidColumn == QStringLiteral("vehicle_uid")) prefix = QStringLiteral("vehicle");
    else if (uidColumn == QStringLiteral("appointment_uid")) prefix = QStringLiteral("appt");
    else if (uidColumn == QStringLiteral("task_uid")) prefix = QStringLiteral("task");
    else if (uidColumn == QStringLiteral("event_uid")) prefix = QStringLiteral("event");
    else if (uidColumn == QStringLiteral("file_uid")) prefix = QStringLiteral("file");
    else if (uidColumn == QStringLiteral("intake_uid")) prefix = QStringLiteral("intake");
    else if (uidColumn == QStringLiteral("photo_uid")) prefix = QStringLiteral("photo");

    QSqlQuery upd(db);
    upd.prepare(QStringLiteral("UPDATE %1 SET %2=? WHERE id=? AND TRIM(COALESCE(%2,''))=''")
                    .arg(quoteIdent(table), quoteIdent(uidColumn)));
    upd.addBindValue(generateUid(prefix));
    upd.addBindValue(id);
    if (!upd.exec()) {
        if (errorText) *errorText = upd.lastError().text();
        return false;
    }
    return true;
}

bool backfillMissingUids(QSqlDatabase db, QString* errorText)
{
    if (!db.isValid()) db = Database::db();
    if (!db.isOpen() && !db.open()) {
        if (errorText) *errorText = db.lastError().text();
        return false;
    }

    if (!ensureColumn(db, QStringLiteral("work_order_services"), QStringLiteral("service_uid"), QStringLiteral("service_uid TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("work_order_parts"), QStringLiteral("part_uid"), QStringLiteral("part_uid TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("work_orders"), QStringLiteral("client_uid"), QStringLiteral("client_uid TEXT"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("work_orders"), QStringLiteral("vehicle_uid"), QStringLiteral("vehicle_uid TEXT"), errorText)) return false;
    // Optional/future entities: keep stage 3 UID coverage ready without forcing later modules on.
    if (!ensureColumn(db, QStringLiteral("appointments"), QStringLiteral("appointment_uid"), QStringLiteral("appointment_uid TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("online_appointments"), QStringLiteral("appointment_uid"), QStringLiteral("appointment_uid TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("file_attachments"), QStringLiteral("file_uid"), QStringLiteral("file_uid TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("online_booking_requests"), QStringLiteral("appointment_uid"), QStringLiteral("appointment_uid TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("vehicle_intake"), QStringLiteral("intake_uid"), QStringLiteral("intake_uid TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("vehicle_intake_photos"), QStringLiteral("photo_uid"), QStringLiteral("photo_uid TEXT NOT NULL DEFAULT ''"), errorText)) return false;

    if (!backfillTable(db, QStringLiteral("work_order_services"), QStringLiteral("service_uid"), QStringLiteral("svc"), errorText)) return false;
    if (!backfillTable(db, QStringLiteral("work_order_parts"), QStringLiteral("part_uid"), QStringLiteral("part"), errorText)) return false;
    if (!backfillTable(db, QStringLiteral("crm_clients"), QStringLiteral("client_uid"), QStringLiteral("client"), errorText)) return false;
    if (!backfillTable(db, QStringLiteral("crm_vehicles"), QStringLiteral("vehicle_uid"), QStringLiteral("vehicle"), errorText)) return false;
    if (!backfillTable(db, QStringLiteral("crm_client_tasks"), QStringLiteral("task_uid"), QStringLiteral("task"), errorText)) return false;
    if (!backfillTable(db, QStringLiteral("crm_client_events"), QStringLiteral("event_uid"), QStringLiteral("event"), errorText)) return false;
    if (!backfillTable(db, QStringLiteral("appointments"), QStringLiteral("appointment_uid"), QStringLiteral("appt"), errorText)) return false;
    if (!backfillTable(db, QStringLiteral("online_appointments"), QStringLiteral("appointment_uid"), QStringLiteral("appt"), errorText)) return false;
    if (!backfillTable(db, QStringLiteral("online_booking_requests"), QStringLiteral("appointment_uid"), QStringLiteral("appt"), errorText)) return false;
    if (!backfillTable(db, QStringLiteral("file_attachments"), QStringLiteral("file_uid"), QStringLiteral("file"), errorText)) return false;
    if (!backfillTable(db, QStringLiteral("vehicle_intake"), QStringLiteral("intake_uid"), QStringLiteral("intake"), errorText)) return false;
    if (!backfillTable(db, QStringLiteral("vehicle_intake_photos"), QStringLiteral("photo_uid"), QStringLiteral("photo"), errorText)) return false;

    // Existing order_uid remains the canonical work-order UID. Keep the legacy generator for blank rows.
    if (tableExists(db, QStringLiteral("work_orders")) && columnExists(db, QStringLiteral("work_orders"), QStringLiteral("order_uid"))) {
        QSqlQuery sel(db);
        if (sel.exec(QStringLiteral("SELECT number FROM work_orders WHERE TRIM(COALESCE(order_uid,''))='' ORDER BY number ASC"))) {
            while (sel.next()) {
                QSqlQuery upd(db);
                upd.prepare(QStringLiteral("UPDATE work_orders SET order_uid=? WHERE number=? AND TRIM(COALESCE(order_uid,''))=''"));
                upd.addBindValue(OrderIdentityService::createOrderUid());
                upd.addBindValue(sel.value(0));
                if (!upd.exec()) {
                    if (errorText) *errorText = upd.lastError().text();
                    return false;
                }
            }
        }
    }

    createIndex(db, QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_work_order_services_service_uid ON work_order_services(service_uid) WHERE TRIM(COALESCE(service_uid,''))<>''"));
    createIndex(db, QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_work_order_parts_part_uid ON work_order_parts(part_uid) WHERE TRIM(COALESCE(part_uid,''))<>''"));
    createIndex(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_work_orders_client_uid ON work_orders(client_uid)"));
    createIndex(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_work_orders_vehicle_uid ON work_orders(vehicle_uid)"));
    if (tableExists(db, QStringLiteral("appointments"))) createIndex(db, QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_appointments_appointment_uid ON appointments(appointment_uid) WHERE TRIM(COALESCE(appointment_uid,''))<>''"));
    if (tableExists(db, QStringLiteral("online_appointments"))) createIndex(db, QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_online_appointments_appointment_uid ON online_appointments(appointment_uid) WHERE TRIM(COALESCE(appointment_uid,''))<>''"));
    if (tableExists(db, QStringLiteral("crm_clients"))) createIndex(db, QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_crm_clients_client_uid_partial ON crm_clients(client_uid) WHERE TRIM(COALESCE(client_uid,''))<>''"));
    if (tableExists(db, QStringLiteral("crm_vehicles"))) createIndex(db, QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_crm_vehicles_vehicle_uid_partial ON crm_vehicles(vehicle_uid) WHERE TRIM(COALESCE(vehicle_uid,''))<>''"));
    if (tableExists(db, QStringLiteral("crm_client_tasks"))) createIndex(db, QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_crm_tasks_task_uid_partial ON crm_client_tasks(task_uid) WHERE TRIM(COALESCE(task_uid,''))<>''"));
    if (tableExists(db, QStringLiteral("crm_client_events"))) createIndex(db, QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_crm_events_event_uid_partial ON crm_client_events(event_uid) WHERE TRIM(COALESCE(event_uid,''))<>''"));
    if (tableExists(db, QStringLiteral("online_booking_requests"))) createIndex(db, QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_online_booking_requests_appointment_uid_partial ON online_booking_requests(appointment_uid) WHERE TRIM(COALESCE(appointment_uid,''))<>''"));
    if (tableExists(db, QStringLiteral("file_attachments"))) createIndex(db, QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_file_attachments_file_uid ON file_attachments(file_uid) WHERE TRIM(COALESCE(file_uid,''))<>''"));
    if (tableExists(db, QStringLiteral("vehicle_intake"))) createIndex(db, QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_vehicle_intake_intake_uid_partial ON vehicle_intake(intake_uid) WHERE TRIM(COALESCE(intake_uid,''))<>''"));
    if (tableExists(db, QStringLiteral("vehicle_intake_photos"))) createIndex(db, QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_vehicle_intake_photos_photo_uid_partial ON vehicle_intake_photos(photo_uid) WHERE TRIM(COALESCE(photo_uid,''))<>''"));
    return true;
}

} // namespace EntityUidService
