#include "core/cloudsync/WorkOrderSyncApplyService.h"

#include "infrastructure/db/sqlite/Db.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {

struct WorkOrderApplyMap {
    QString table;
    QString uidColumn;
    QSet<QString> writableFields;
};

QSqlDatabase effectiveDb(QSqlDatabase db)
{
    return db.isValid() ? db : Database::db();
}

bool safeIdentifier(const QString& identifier)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    return re.match(identifier).hasMatch();
}

bool tableExists(QSqlDatabase db, const QString& table)
{
    if (!safeIdentifier(table)) return false;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type='table' AND name=? LIMIT 1"));
    q.addBindValue(table);
    return q.exec() && q.next();
}

QSet<QString> tableColumns(QSqlDatabase db, const QString& table)
{
    QSet<QString> columns;
    if (!safeIdentifier(table)) return columns;
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table))) return columns;
    while (q.next()) {
        const QString column = q.value(1).toString();
        if (safeIdentifier(column)) columns.insert(column);
    }
    return columns;
}

bool upsertAllowedFieldsByUid(QSqlDatabase db,
                              const WorkOrderApplyMap& map,
                              const QString& uid,
                              const QVariantMap& fields,
                              QString* errorText)
{
    if (uid.trimmed().isEmpty()) {
        if (errorText) *errorText = QString::fromUtf8("Серверна подія наряду/робіт/запчастин без UID.");
        return false;
    }
    if (!safeIdentifier(map.table) || !safeIdentifier(map.uidColumn)) {
        if (errorText) *errorText = QString::fromUtf8("Небезпечна схема WorkOrderSyncApplyService.");
        return false;
    }
    db = effectiveDb(db);
    if (!db.isOpen() && !db.open()) {
        if (errorText) *errorText = db.lastError().text();
        return false;
    }
    if (!tableExists(db, map.table)) {
        if (errorText) *errorText = QString::fromUtf8("Локальна таблиця відсутня: %1").arg(map.table);
        return false;
    }
    const QSet<QString> columns = tableColumns(db, map.table);
    if (!columns.contains(map.uidColumn)) {
        if (errorText) *errorText = QString::fromUtf8("У таблиці %1 відсутня UID-колонка %2.").arg(map.table, map.uidColumn);
        return false;
    }

    QStringList insertColumns{map.uidColumn};
    QList<QVariant> insertValues{uid.trimmed()};
    for (auto it = fields.constBegin(); it != fields.constEnd(); ++it) {
        const QString key = it.key().trimmed();
        if (key == map.uidColumn || key == QStringLiteral("id")) continue;
        if (!safeIdentifier(key) || !columns.contains(key) || !map.writableFields.contains(key)) continue;
        insertColumns.append(key);
        insertValues.append(it.value());
    }
    if (columns.contains(QStringLiteral("updated_at")) && !insertColumns.contains(QStringLiteral("updated_at"))) {
        insertColumns.append(QStringLiteral("updated_at"));
        insertValues.append(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    }

    QStringList assignments;
    QList<QVariant> updateValues;
    for (int i = 0; i < insertColumns.size(); ++i) {
        if (insertColumns.at(i) == map.uidColumn) continue;
        assignments.append(QStringLiteral("%1=?").arg(insertColumns.at(i)));
        updateValues.append(insertValues.at(i));
    }
    if (!assignments.isEmpty()) {
        QSqlQuery update(db);
        update.prepare(QStringLiteral("UPDATE %1 SET %2 WHERE %3=?")
            .arg(map.table, assignments.join(QLatin1Char(',')), map.uidColumn));
        for (const QVariant& value : updateValues) update.addBindValue(value);
        update.addBindValue(uid.trimmed());
        if (!update.exec()) {
            if (errorText) *errorText = update.lastError().text();
            return false;
        }
        if (update.numRowsAffected() > 0) return true;
    }

    QStringList placeholders;
    for (int i = 0; i < insertColumns.size(); ++i) placeholders.append(QStringLiteral("?"));
    QSqlQuery insert(db);
    insert.prepare(QStringLiteral("INSERT INTO %1(%2) VALUES(%3)")
        .arg(map.table, insertColumns.join(QLatin1Char(',')), placeholders.join(QLatin1Char(','))));
    for (const QVariant& value : insertValues) insert.addBindValue(value);
    if (!insert.exec()) {
        if (errorText) *errorText = insert.lastError().text();
        return false;
    }
    return true;
}

} // namespace

namespace WorkOrderSyncApplyService {

bool upsertWorkOrderDirect(QSqlDatabase db, const QString& orderUid, const QVariantMap& fields, QString* errorText)
{
    static const QSet<QString> writable = {
        QStringLiteral("client_uid"), QStringLiteral("vehicle_uid"), QStringLiteral("number"),
        QStringLiteral("order_number"), QStringLiteral("order_year"), QStringLiteral("status"),
        QStringLiteral("total_amount"), QStringLiteral("opened_at"), QStringLiteral("plate"),
        QStringLiteral("plate_norm"), QStringLiteral("vin"), QStringLiteral("car"),
        QStringLiteral("client"), QStringLiteral("client_phone"), QStringLiteral("master_id"),
        QStringLiteral("mobile_state_json"), QStringLiteral("created_at"), QStringLiteral("updated_at")
        , QStringLiteral("closed_at"), QStringLiteral("car_brand"), QStringLiteral("car_model"), QStringLiteral("car_year")
    };
    return upsertAllowedFieldsByUid(db, {QStringLiteral("work_orders"), QStringLiteral("order_uid"), writable}, orderUid, fields, errorText);
}

bool upsertServiceDirect(QSqlDatabase db, const QString& serviceUid, const QVariantMap& fields, QString* errorText)
{
    static const QSet<QString> writable = {
        QStringLiteral("order_uid"), QStringLiteral("order_number"), QStringLiteral("title"),
        QStringLiteral("name"), QStringLiteral("qty"), QStringLiteral("quantity"), QStringLiteral("price"),
        QStringLiteral("amount"), QStringLiteral("note"), QStringLiteral("paid"), QStringLiteral("parent_id"),
        QStringLiteral("status"), QStringLiteral("completed_at"), QStringLiteral("completed_by"),
        QStringLiteral("assigned_to"), QStringLiteral("assigned_to_name"), QStringLiteral("source"),
        QStringLiteral("diagnostic_uid"), QStringLiteral("source_suggestion_uid"), QStringLiteral("metadata_json"),
        QStringLiteral("created_at"), QStringLiteral("updated_at")
    };
    QVariantMap resolved = fields;
    db = effectiveDb(db);
    const QString orderUid = fields.value(QStringLiteral("order_uid")).toString().trimmed();
    if (!orderUid.isEmpty()) {
        QSqlQuery parent(db);
        parent.prepare(QStringLiteral("SELECT number FROM work_orders WHERE order_uid=?"));
        parent.addBindValue(orderUid);
        if (!parent.exec() || !parent.next()) {
            if (errorText) *errorText = QString::fromUtf8("Наряд для роботи ще не синхронізовано.");
            return false;
        }
        resolved.insert(QStringLiteral("order_number"), parent.value(0));
        QSqlQuery existing(db);
        existing.prepare(QStringLiteral("SELECT order_uid FROM work_order_services WHERE service_uid=?"));
        existing.addBindValue(serviceUid);
        if (existing.exec() && existing.next() && !existing.value(0).toString().isEmpty()
            && existing.value(0).toString() != orderUid) {
            if (errorText) *errorText = QString::fromUtf8("UID роботи належить іншому наряду.");
            return false;
        }
    }
    if (!upsertAllowedFieldsByUid(db, {QStringLiteral("work_order_services"), QStringLiteral("service_uid"), writable}, serviceUid, resolved, errorText)) return false;
    if (fields.value(QStringLiteral("source")).toString() == QStringLiteral("mobile_manual")
        && tableExists(db, QStringLiteral("service_masters"))) {
        // Bind the phone author by stable master ID, without replacing existing payroll shares.
        QSqlQuery assignment(db);
        assignment.prepare(QStringLiteral(
            "INSERT OR IGNORE INTO service_masters(service_id,master_id,share_percent,role,master_name_closed) "
            "SELECT s.id,m.id,100,'master','' FROM work_order_services s JOIN masters m ON m.id=? "
            "WHERE s.service_uid=? AND NOT EXISTS(SELECT 1 FROM service_masters sm WHERE sm.service_id=s.id)"));
        assignment.addBindValue(fields.value(QStringLiteral("assigned_to")).toString());
        assignment.addBindValue(serviceUid);
        if (!assignment.exec()) {
            if (errorText) *errorText = assignment.lastError().text();
            return false;
        }
    }
    return true;
}

bool upsertPartDirect(QSqlDatabase db, const QString& partUid, const QVariantMap& fields, QString* errorText)
{
    static const QSet<QString> writable = {
        QStringLiteral("order_uid"), QStringLiteral("order_number"), QStringLiteral("ordered_at"),
        QStringLiteral("delivery_date"), QStringLiteral("installed_at"), QStringLiteral("store"),
        QStringLiteral("part_number"), QStringLiteral("name"), QStringLiteral("manufacturer"),
        QStringLiteral("note"), QStringLiteral("qty"), QStringLiteral("quantity"), QStringLiteral("price"),
        QStringLiteral("amount"), QStringLiteral("service_id"), QStringLiteral("ordered_by_admin"),
        QStringLiteral("paid"), QStringLiteral("is_paid"), QStringLiteral("installed"), QStringLiteral("is_installed"),
        QStringLiteral("purchase_price"), QStringLiteral("purchase_amount"), QStringLiteral("markup_percent"),
        QStringLiteral("manual_markup_override"), QStringLiteral("markup_source"), QStringLiteral("pricing_calc_version"),
        QStringLiteral("status"), QStringLiteral("installed_by"), QStringLiteral("request_uid"),
        QStringLiteral("requested_at"), QStringLiteral("requested_by"), QStringLiteral("requested_by_uid"),
        QStringLiteral("ordered_by"), QStringLiteral("ordered_by_uid"), QStringLiteral("order_comment"),
        QStringLiteral("node_group"), QStringLiteral("component"), QStringLiteral("metadata_json"),
        QStringLiteral("created_at"), QStringLiteral("updated_at")
    };
    return upsertAllowedFieldsByUid(db, {QStringLiteral("work_order_parts"), QStringLiteral("part_uid"), writable}, partUid, fields, errorText);
}

} // namespace WorkOrderSyncApplyService
