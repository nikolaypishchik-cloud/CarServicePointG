#include "core/crm/OnlineBookingService.h"
#include "core/services/orders/ClientReceiptReader.h"
#include "core/cloudsync/EntityUidService.h"
#include "core/crm/CrmClientService.h"
#include "core/crm/CrmVehicleService.h"
#include "core/services/MutationExecutor.h"
#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/db/sqlite/DbThreadConnectionSupport.h"

#include <QDate>
#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QTimeZone>
#include "core/services/OrderIdentityService.h"

namespace {

bool execPrepared(QSqlQuery& q, QString* errorText)
{
    if (q.exec()) return true;
    if (errorText) *errorText = q.lastError().text();
    return false;
}

QString value(const QVariantMap& m, const QString& k)
{
    const auto result=m.value(k).toString().trimmed();
    return result.isNull()?QStringLiteral(""):result;
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

bool addInitialServiceFromBooking(QSqlDatabase db,
                                  const QString& orderUid,
                                  int orderNumber,
                                  const QString& serviceReason,
                                  const QString& masterId,
                                  QString* errorText)
{
    const QString title = serviceReason.trimmed();
    if (title.isEmpty() || !tableExists(db, QStringLiteral("work_order_services"))) return true;
    const QString serviceUid = EntityUidService::generateUid(QStringLiteral("svc"));
    const bool hasServiceUid = columnExists(db, QStringLiteral("work_order_services"), QStringLiteral("service_uid"));
    QSqlQuery q(db);
    if (hasServiceUid) {
        q.prepare(QStringLiteral("INSERT INTO work_order_services(order_uid,order_number,title,qty,price,amount,service_uid) VALUES(?,?,?,?,?,?,?)"));
    } else {
        q.prepare(QStringLiteral("INSERT INTO work_order_services(order_uid,order_number,title,qty,price,amount) VALUES(?,?,?,?,?,?)"));
    }
    q.addBindValue(orderUid);
    q.addBindValue(orderNumber);
    q.addBindValue(title);
    q.addBindValue(1.0);
    q.addBindValue(0.0);
    q.addBindValue(0.0);
    if (hasServiceUid) q.addBindValue(serviceUid);
    if (!execPrepared(q, errorText)) return false;

    const qint64 serviceId = q.lastInsertId().toLongLong();
    if (!masterId.trimmed().isEmpty() && serviceId > 0 && tableExists(db, QStringLiteral("service_masters"))) {
        QSqlQuery sm(db);
        sm.prepare(QStringLiteral("INSERT OR IGNORE INTO service_masters(service_id,master_id,share_percent,role) VALUES(?,?,100,'master')"));
        sm.addBindValue(serviceId);
        sm.addBindValue(masterId);
        if (!execPrepared(sm, errorText)) return false;
    }
    return true;
}

bool queueClientMessageDirect(QSqlDatabase db, const QString& appointmentUid, const QString& body, QString* errorText)
{
    QSqlQuery src(db);
    src.prepare(QStringLiteral("SELECT client_uid,vehicle_uid,client_name,phone,service_reason FROM online_booking_requests WHERE appointment_uid=?"));
    src.addBindValue(appointmentUid);
    if (!src.exec() || !src.next()) {
        if (errorText) *errorText = src.lastError().text().isEmpty() ? QStringLiteral("Заявку не знайдено") : src.lastError().text();
        return false;
    }

    const QString eventUid = EntityUidService::generateUid(QStringLiteral("event"));
    if (tableExists(db, QStringLiteral("crm_client_events"))) {
        QSqlQuery e(db);
        e.prepare(QStringLiteral(
            "INSERT INTO crm_client_events(event_uid,client_uid,vehicle_uid,order_uid,event_type,title,body,created_by) "
            "VALUES(?,?,?,?,?,?,?,?)"));
        e.addBindValue(eventUid);
        e.addBindValue(src.value(0).toString());
        e.addBindValue(src.value(1).toString());
        e.addBindValue(appointmentUid);
        e.addBindValue(QStringLiteral("booking_message"));
        e.addBindValue(QString::fromUtf8("Повідомлення клієнту по онлайн-заявці"));
        e.addBindValue(body.trimmed().isEmpty() ? QString::fromUtf8("Ваш запис підтверджено. Очікуємо вас у погоджений час.") : body.trimmed());
        e.addBindValue(QStringLiteral("desktop"));
        if (!execPrepared(e, errorText)) return false;
    }

    if (tableExists(db, QStringLiteral("online_booking_requests"))) {
        QSqlQuery u(db);
        u.prepare(QStringLiteral("UPDATE online_booking_requests SET comment=trim(COALESCE(comment,'') || char(10) || ?), updated_at=datetime('now') WHERE appointment_uid=?"));
        u.addBindValue(QString::fromUtf8("Повідомлення клієнту: %1").arg(body.trimmed()));
        u.addBindValue(appointmentUid);
        if (!execPrepared(u, errorText)) return false;
    }
    return true;
}

bool rescheduleDirect(QSqlDatabase db, const QString& appointmentUid, const QString& desiredStartAt, const QString& desiredEndAt, QString* errorText)
{
    const QString start = desiredStartAt.trimmed();
    if (start.isEmpty()) {
        if (errorText) *errorText = QString::fromUtf8("Новий час запису не вказано");
        return false;
    }
    QSqlQuery busy(db);
    busy.prepare(QStringLiteral("SELECT appointment_uid FROM online_booking_requests WHERE appointment_uid<>? AND desired_start_at=? AND status IN ('new','confirmed','rescheduled') LIMIT 1"));
    busy.addBindValue(appointmentUid);
    busy.addBindValue(start);
    if (!busy.exec()) {
        if (errorText) *errorText = busy.lastError().text();
        return false;
    }
    if (busy.next()) {
        if (errorText) *errorText = QString::fromUtf8("Обраний час уже зайнятий іншою онлайн-заявкою");
        return false;
    }

    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE online_booking_requests SET status='rescheduled', desired_start_at=?, desired_end_at=?, updated_at=datetime('now') WHERE appointment_uid=?"));
    q.addBindValue(start);
    q.addBindValue(desiredEndAt.trimmed());
    q.addBindValue(appointmentUid);
    return execPrepared(q, errorText);
}

bool updateStatusDirect(QSqlDatabase db, const QString& appointmentUid, const QString& status, QString* errorText)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE online_booking_requests SET status=?, updated_at=datetime('now') WHERE appointment_uid=?"));
    q.addBindValue(status);
    q.addBindValue(appointmentUid);
    return execPrepared(q, errorText);
}


bool ensureCrmLinksForBookingRequest(QSqlDatabase db,
                                     const QString& appointmentUid,
                                     const QString& clientName,
                                     const QString& phone,
                                     const QString& plate,
                                     const QString& carText,
                                     QString* clientUid,
                                     QString* vehicleUid,
                                     QString* errorText)
{
    QString resolvedClientUid = clientUid ? clientUid->trimmed() : QString();
    QString resolvedVehicleUid = vehicleUid ? vehicleUid->trimmed() : QString();

    if (resolvedClientUid.isEmpty() && tableExists(db, QStringLiteral("crm_clients"))) {
        if (!CrmClientService::findOrCreateFromOrder(db, clientName, phone, &resolvedClientUid, errorText)) {
            return false;
        }
    }

    if (resolvedVehicleUid.isEmpty() && tableExists(db, QStringLiteral("crm_vehicles"))) {
        if (!CrmVehicleService::findOrCreateFromOrder(db,
                                                       resolvedClientUid,
                                                       plate,
                                                       QString(),
                                                       carText,
                                                       0,
                                                       0,
                                                       QString(),
                                                       QString(),
                                                       QString(),
                                                       &resolvedVehicleUid,
                                                       errorText)) {
            return false;
        }
    }

    if (!resolvedClientUid.isEmpty() || !resolvedVehicleUid.isEmpty()) {
        QSqlQuery u(db);
        u.prepare(QStringLiteral("UPDATE online_booking_requests SET "
                                 "client_uid=CASE WHEN TRIM(COALESCE(client_uid,''))='' THEN ? ELSE client_uid END, "
                                 "vehicle_uid=CASE WHEN TRIM(COALESCE(vehicle_uid,''))='' THEN ? ELSE vehicle_uid END, "
                                 "updated_at=datetime('now') WHERE appointment_uid=?"));
        u.addBindValue(resolvedClientUid);
        u.addBindValue(resolvedVehicleUid);
        u.addBindValue(appointmentUid);
        if (!execPrepared(u, errorText)) return false;
    }

    if (clientUid) *clientUid = resolvedClientUid;
    if (vehicleUid) *vehicleUid = resolvedVehicleUid;
    return true;
}

bool createOrderFromRequestDirect(QSqlDatabase db, const QString& appointmentUid, QString* orderUid, int* internalNumber, bool* replayed, QString* errorText)
{
    QSqlQuery src(db);
    src.prepare(QStringLiteral("SELECT client_uid,vehicle_uid,client_name,phone,plate,car_text,service_reason,desired_start_at,status FROM online_booking_requests WHERE appointment_uid=?"));
    src.addBindValue(appointmentUid);
    if (!src.exec() || !src.next()) {
        if (errorText) *errorText = src.lastError().text().isEmpty() ? QStringLiteral("Заявку не знайдено") : src.lastError().text();
        return false;
    }
    QVariantList source;
    for (int i=0;i<9;++i) source.append(src.value(i));
    src.finish();
    const auto createdAt=QDateTime::currentDateTimeUtc();
    OrderIdentityService::Allocation identity;
    if (!OrderIdentityService::allocateAnnualIdentity(db,QStringLiteral("booking:")+appointmentUid,createdAt,0,&identity,errorText)) return false;
    const auto uid=identity.orderUid;
    const int nextNumber=identity.internalNumber;
    if (identity.replayed) {
        QSqlQuery original(db);
        original.prepare(QStringLiteral("SELECT number,order_number,order_year FROM work_orders WHERE order_uid=?")); original.addBindValue(uid);
        if (!original.exec()) {if(errorText)*errorText=original.lastError().text();return false;}
        if (!original.next() || original.value(0).toInt()!=nextNumber || original.value(1).toInt()!=identity.visibleNumber || original.value(2).toInt()!=identity.year) {
            if(errorText)*errorText=QStringLiteral("Original booking order is deleted or its identity conflicts; creation cannot be repeated"); return false;
        }
        original.finish();
        if (!updateStatusDirect(db,appointmentUid,QStringLiteral("order_created"),errorText)) return false;
        *orderUid=uid; *internalNumber=nextNumber; *replayed=true; return true;
    }
    if (source[8].toString()==QStringLiteral("order_created")) {
        if(errorText)*errorText=QStringLiteral("Historical booking conversion has no request mapping; resolve its original order before retry"); return false;
    }
    QString clientUid=source[0].toString().trimmed(),vehicleUid=source[1].toString().trimmed();
    if (!ensureCrmLinksForBookingRequest(db,appointmentUid,source[2].toString(),source[3].toString(),source[4].toString(),
                                       source[5].toString(),&clientUid,&vehicleUid,errorText)) return false;

    QString masterId;
    QSqlQuery masterQ(db);
    if (masterQ.exec(QStringLiteral("SELECT id FROM masters ORDER BY id LIMIT 1")) && masterQ.next()) {
        masterId = masterQ.value(0).toString();
    }

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO work_orders(number,order_uid,order_number,order_year,opened_at,status,plate,plate_norm,car,client,client_phone,master_id,works_sum,parts_sum,discount,client_uid,vehicle_uid) "
        "VALUES(?,?,?,?,?,'open',?,?,?,?,?,?,0,0,0,?,?)"));
    q.addBindValue(nextNumber);
    q.addBindValue(uid);
    q.addBindValue(identity.visibleNumber);
    q.addBindValue(identity.year);
    q.addBindValue(createdAt.toTimeZone(QTimeZone(QByteArrayLiteral("Europe/Kyiv"))).toString(Qt::ISODate));
    q.addBindValue(source[4].toString());
    q.addBindValue(source[4].toString().toUpper().remove(' '));
    q.addBindValue(source[5].toString());
    q.addBindValue(source[2].toString());
    q.addBindValue(source[3].toString());
    q.addBindValue(masterId);
    q.addBindValue(clientUid);
    q.addBindValue(vehicleUid);
    if (!execPrepared(q, errorText)) return false;

    if (!ClientReceiptReader::beginTrackingNewOrder(db,uid,errorText)) return false;
    if (!addInitialServiceFromBooking(db, uid, nextNumber, source[6].toString(), masterId, errorText)) return false;

    if (!updateStatusDirect(db, appointmentUid, QStringLiteral("order_created"), errorText)) return false;
    *orderUid=uid; *internalNumber=nextNumber; *replayed=false;
    return true;
}

} // namespace

namespace OnlineBookingService {

QList<BookingRequestRow> listRecentRequests(int limit, QString* errorText)
{
    QList<BookingRequestRow> rows;
    QSqlQuery q(Database::db());
    q.prepare(QStringLiteral(
        "SELECT appointment_uid,status,client_name,phone,car_text,service_reason,desired_start_at,comment "
        "FROM online_booking_requests "
        "ORDER BY CASE status WHEN 'new' THEN 0 WHEN 'confirmed' THEN 1 ELSE 2 END, created_at DESC LIMIT ?"));
    q.addBindValue(qBound(1, limit, 1000));
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return rows;
    }
    while (q.next()) {
        BookingRequestRow row;
        row.appointmentUid = q.value(0).toString();
        row.status = q.value(1).toString();
        row.clientName = q.value(2).toString();
        row.phone = q.value(3).toString();
        row.carText = q.value(4).toString();
        row.serviceReason = q.value(5).toString();
        row.desiredStartAt = q.value(6).toString();
        row.comment = q.value(7).toString();
        rows.append(row);
    }
    return rows;
}

bool upsertRequest(QSqlDatabase db, const QVariantMap& p, QString* errorText)
{
    const QString uid = value(p, QStringLiteral("appointment_uid")).isEmpty()
        ? EntityUidService::generateUid(QStringLiteral("appt"))
        : value(p, QStringLiteral("appointment_uid"));

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO online_booking_requests(appointment_uid,client_uid,vehicle_uid,client_name,phone,plate,car_text,service_reason,desired_start_at,desired_end_at,status,source,comment,updated_at) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,datetime('now')) "
        "ON CONFLICT(appointment_uid) DO UPDATE SET client_uid=COALESCE(NULLIF(excluded.client_uid,''),online_booking_requests.client_uid), vehicle_uid=COALESCE(NULLIF(excluded.vehicle_uid,''),online_booking_requests.vehicle_uid), client_name=excluded.client_name, phone=excluded.phone, plate=excluded.plate, car_text=excluded.car_text, service_reason=excluded.service_reason, desired_start_at=excluded.desired_start_at, desired_end_at=excluded.desired_end_at, status=excluded.status, source=excluded.source, comment=excluded.comment, updated_at=datetime('now')"));
    for (const QString& k : {QStringLiteral("appointment_uid"), QStringLiteral("client_uid"), QStringLiteral("vehicle_uid"), QStringLiteral("client_name"), QStringLiteral("phone"), QStringLiteral("plate"), QStringLiteral("car_text"), QStringLiteral("service_reason"), QStringLiteral("desired_start_at"), QStringLiteral("desired_end_at"), QStringLiteral("status"), QStringLiteral("source"), QStringLiteral("comment")}) {
        q.addBindValue(k == QStringLiteral("appointment_uid") ? uid : value(p, k));
    }
    return execPrepared(q, errorText);
}

bool updateStatus(QSqlDatabase db, const QString& appointmentUid, const QString& status, QString* errorText)
{
    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("OnlineBookingService");
    request.entityType = QStringLiteral("appointment");
    request.entityId = appointmentUid;
    request.operation = QStringLiteral("update");
    request.description = QString::fromUtf8("Зміна статусу онлайн-заявки");
    request.changedTables = {QStringLiteral("online_booking_requests")};
    request.beginErrorFallback = QString::fromUtf8("Не вдалося почати зміну статусу онлайн-заявки.");
    request.commitErrorFallback = QString::fromUtf8("Не вдалося зберегти статус онлайн-заявки.");

    const auto result = MutationExecutor::execute(db, request, [&](QSqlDatabase& txDb, QString* txError) {
        return updateStatusDirect(txDb, appointmentUid, status, txError);
    });
    if (!result.ok && errorText) *errorText = result.errorText;
    return result.ok;
}

bool updateStatus(const QString& appointmentUid, const QString& status, QString* errorText)
{
    return updateStatus(Database::db(), appointmentUid, status, errorText);
}


bool rescheduleRequest(QSqlDatabase db, const QString& appointmentUid, const QString& desiredStartAt, const QString& desiredEndAt, QString* errorText)
{
    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("OnlineBookingService");
    request.entityType = QStringLiteral("appointment");
    request.entityId = appointmentUid;
    request.operation = QStringLiteral("update");
    request.description = QString::fromUtf8("Перенесення онлайн-заявки на інший час");
    request.changedTables = {QStringLiteral("online_booking_requests")};
    request.beginErrorFallback = QString::fromUtf8("Не вдалося почати перенесення онлайн-заявки.");
    request.commitErrorFallback = QString::fromUtf8("Не вдалося зберегти перенесення онлайн-заявки.");
    const auto result = MutationExecutor::execute(db, request, [&](QSqlDatabase& txDb, QString* txError) {
        return rescheduleDirect(txDb, appointmentUid, desiredStartAt, desiredEndAt, txError);
    });
    if (!result.ok && errorText) *errorText = result.errorText;
    return result.ok;
}

bool rescheduleRequest(const QString& appointmentUid, const QString& desiredStartAt, const QString& desiredEndAt, QString* errorText)
{
    return rescheduleRequest(Database::db(), appointmentUid, desiredStartAt, desiredEndAt, errorText);
}

bool createOrderFromRequest(QSqlDatabase db, const QString& appointmentUid, QString* orderUid, QString* errorText)
{
    QString createdOrderUid; int createdInternalNumber=0; bool replayed=false;
    const bool runtime=DbThreadConnectionSupport::isRuntimeConnection(db.connectionName());
    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("OnlineBookingService");
    request.entityType = QStringLiteral("work_order");
    request.entityId = appointmentUid;
    request.operation = QStringLiteral("create");
    request.description = QString::fromUtf8("Створення наряду з онлайн-заявки");
    request.changedTables = {QStringLiteral("work_orders"), QStringLiteral("work_order_services"), QStringLiteral("service_masters"), QStringLiteral("online_booking_requests"), QStringLiteral("crm_clients"), QStringLiteral("crm_vehicles"), QStringLiteral("annual_order_counters"), QStringLiteral("order_creation_reservations")};
    if (runtime) request.changedTables.append({QStringLiteral("workspace_order_state"),QStringLiteral("order_registry")});
    request.beginErrorFallback = QString::fromUtf8("Не вдалося почати створення наряду із заявки.");
    request.commitErrorFallback = QString::fromUtf8("Не вдалося зберегти наряд із заявки.");

    const auto result = MutationExecutor::execute(db, request, [&](QSqlDatabase& txDb, QString* txError) {
        if (!createOrderFromRequestDirect(txDb,appointmentUid,&createdOrderUid,&createdInternalNumber,&replayed,txError)) return false;
        if (runtime && !Database::finalizeOrderWorkspaceInTransaction(txDb,createdInternalNumber,txError)) return false;
        request.entityId=QString::number(createdInternalNumber);
        request.operation=replayed?QStringLiteral("replay_create"):QStringLiteral("create");
        return true;
    });
    if (!result.ok) {
        if (errorText) *errorText = result.errorText;
        return false;
    }
    if (runtime) Database::publishFinalizedOrderWorkspace(createdInternalNumber);
    if (orderUid) *orderUid = createdOrderUid;
    return true;
}

bool createOrderFromRequest(const QString& appointmentUid, QString* orderUid, QString* errorText)
{
    return createOrderFromRequest(Database::db(), appointmentUid, orderUid, errorText);
}


bool queueClientMessage(QSqlDatabase db, const QString& appointmentUid, const QString& body, QString* errorText)
{
    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("OnlineBookingService");
    request.entityType = QStringLiteral("appointment");
    request.entityId = appointmentUid;
    request.operation = QStringLiteral("update");
    request.description = QString::fromUtf8("Повідомлення клієнту по онлайн-заявці");
    request.changedTables = {QStringLiteral("online_booking_requests"), QStringLiteral("crm_client_events")};
    request.beginErrorFallback = QString::fromUtf8("Не вдалося почати створення повідомлення клієнту.");
    request.commitErrorFallback = QString::fromUtf8("Не вдалося зберегти повідомлення клієнту.");

    const auto result = MutationExecutor::execute(db, request, [&](QSqlDatabase& txDb, QString* txError) {
        return queueClientMessageDirect(txDb, appointmentUid, body, txError);
    });
    if (!result.ok && errorText) *errorText = result.errorText;
    return result.ok;
}

bool queueClientMessage(const QString& appointmentUid, const QString& body, QString* errorText)
{
    return queueClientMessage(Database::db(), appointmentUid, body, errorText);
}

} // namespace OnlineBookingService
