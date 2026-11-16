#include "core/crm/CrmPageReadService.h"
#include "core/crm/CrmClientService.h"
#include "core/crm/BusinessExtensionsService.h"
#include "infrastructure/db/sqlite/Db.h"
#include <QSqlQuery>
#include <QSqlRecord>
#include <QSqlError>
#include <QScopeGuard>
namespace CrmPageReadService {
namespace {
bool collect(QSqlQuery& query,Rows* rows,QString* error) {
    if(!query.exec()){*error=query.lastError().text();return false;}
    while(query.next()) {
        QVariantList row;for(int i=0;i<query.record().count();++i)row.append(query.value(i));
        rows->append(row);
    }
    if(query.lastError().isValid()){*error=query.lastError().text();rows->clear();return false;}return true;
}
int count(QSqlDatabase db,const QString& table,QString* error) {
    if(!error->isEmpty())return 0;
    QSqlQuery q(db);
    if(!q.exec("SELECT COUNT(*) FROM "+table) || !q.next()){*error=q.lastError().text();return 0;}
    return q.value(0).toInt();
}
bool tableOrViewExists(QSqlDatabase db, const QString& name)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type IN ('table','view') AND name=? LIMIT 1"));
    q.addBindValue(name);
    return q.exec() && q.next();
}

bool columnExists(QSqlDatabase db, const QString& table, const QString& column)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table))) return false;
    while (q.next()) {
        if (q.value(1).toString().compare(column, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

QString crmOrderHistorySource(QSqlDatabase db)
{
    return tableOrViewExists(db, QStringLiteral("router_work_orders"))
               && columnExists(db, QStringLiteral("router_work_orders"), QStringLiteral("client_uid"))
        ? QStringLiteral("router_work_orders")
        : QStringLiteral("work_orders");
}


}
RefreshResult refresh(bool runBackfillNow) {
        RefreshResult result;
        QSqlDatabase db = Database::db();
        if (!db.isOpen()) {
            result.backfillOk = false;
            result.dbError = db.lastError().text();
        } else {
            if (runBackfillNow) {
                result.backfillRan = true;
                CrmClientService::BackfillResult backfill;
                result.backfillOk = CrmClientService::backfillExistingData(db, &backfill, &result.backfillError);
                if (result.backfillOk) {
                    result.backfillStatus = QString::fromUtf8("CRM оновлено: клієнтів %1/%2, авто %3/%4, нарядів %5")
                                                .arg(backfill.clientsCreated)
                                                .arg(backfill.clientsUpdated)
                                                .arg(backfill.vehiclesCreated)
                                                .arg(backfill.vehiclesUpdated)
                                                .arg(backfill.ordersLinked);
                }
            }
            result.clientsCount = count(db, "crm_clients", &result.dbError);
            result.vehiclesCount = count(db, "crm_vehicles", &result.dbError);
            result.tasksCount = count(db, "crm_client_tasks", &result.dbError);
        }



    if(!result.dbError.isEmpty())result.backfillOk=false;
    return result;
}
ClientList clients(const QString& term) {
    auto db=Database::db();ClientList result;
    const auto like="%"+term+"%";
            QSqlQuery q(db);
            if (term.isEmpty()) {
                q.prepare(QStringLiteral(
                    "SELECT c.client_uid, c.full_name, c.phone, "
                    "       (SELECT COUNT(*) FROM crm_vehicles v WHERE v.client_uid=c.client_uid), "
                    "       COALESCE((SELECT TRIM(vv.brand || ' ' || vv.model) FROM crm_vehicles vv "
                    "                 WHERE vv.client_uid=c.client_uid "
                    "                 ORDER BY COALESCE(vv.is_primary,0) DESC, vv.updated_at DESC, vv.id DESC LIMIT 1),'') "
                    "FROM crm_clients c ORDER BY c.updated_at DESC, c.id DESC LIMIT 400"));
            } else {
                q.prepare(QStringLiteral(
                    "SELECT c.client_uid, c.full_name, c.phone, "
                    "       (SELECT COUNT(*) FROM crm_vehicles vcount WHERE vcount.client_uid=c.client_uid), "
                    "       COALESCE((SELECT TRIM(vv.brand || ' ' || vv.model) FROM crm_vehicles vv "
                    "                 WHERE vv.client_uid=c.client_uid "
                    "                 ORDER BY COALESCE(vv.is_primary,0) DESC, vv.updated_at DESC, vv.id DESC LIMIT 1),'') "
                    "FROM crm_clients c "
                    "WHERE c.full_name LIKE ? OR c.phone LIKE ? OR c.email LIKE ? "
                    "   OR EXISTS (SELECT 1 FROM crm_vehicles v WHERE v.client_uid=c.client_uid "
                    "              AND (v.plate LIKE ? OR v.vin LIKE ? OR (v.brand || ' ' || v.model) LIKE ?)) "
                    "ORDER BY c.updated_at DESC, c.id DESC LIMIT 400"));
                for (int i = 0; i < 6; ++i) q.addBindValue(like);
            }

    collect(q,&result.clients,&result.error);
    if(!result.error.isEmpty())result.clients.clear();return result;
}
QVariantMap statementPayment(const QString& clientUid, const QString& orderUid, const QString& paymentUid, QString* error) {
    auto db=Database::db();
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT p.payment_uid,p.order_uid,p.paid_on,p.amount_cents,p.method,p.actor,"
                                 "typeof(p.amount_cents) FROM main.client_payments p JOIN %1 o ON o.order_uid=p.order_uid "
                                 "WHERE p.payment_uid=? AND p.order_uid=? AND o.client_uid=? "
                                 "AND (SELECT count(*) FROM %1 x WHERE x.order_uid=o.order_uid)=1 LIMIT 2").arg(crmOrderHistorySource(db)));
    query.addBindValue(paymentUid); query.addBindValue(orderUid); query.addBindValue(clientUid);
    if (!query.exec()) { *error=query.lastError().text(); return {}; }
    if (!query.next() || query.value(6).toString()!="integer" || query.value(3).toLongLong()<=0) {
        *error=query.lastError().isValid()?query.lastError().text():QStringLiteral("Платіж цього клієнта і наряду не підтверджено.");
        return {};
    }
    QVariantMap record;
    for (int c=0;c<6;++c) record.insert(query.record().fieldName(c),query.value(c));
    record.insert("amount",CrmFinancialService::money(query.value(3).toLongLong()));
    if (query.next()) { *error=QStringLiteral("UID платежу неоднозначний."); return {}; }
    if (query.lastError().isValid()) { *error=query.lastError().text(); return {}; }
    return record;
}
QVariantMap statementOrder(const QString& clientUid, const QString& orderUid, QString* error) {
    if (clientUid.trimmed().isEmpty() || orderUid.trimmed().isEmpty()) {
        *error = QStringLiteral("У погодженні немає підтвердженого зв’язку з нарядом.");
        return {};
    }
    auto db = Database::db();
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT client_uid,order_uid,COALESCE(NULLIF(order_number,0),number) AS display_number,"
                                 "opened_at,closed_at,car,plate,status FROM %1 WHERE order_uid=? LIMIT 2")
                  .arg(crmOrderHistorySource(db)));
    query.addBindValue(orderUid);
    if (!query.exec()) { *error = query.lastError().text(); return {}; }
    if (!query.next() || query.value(0).toString()!=clientUid) {
        *error = query.lastError().isValid() ? query.lastError().text() : QStringLiteral("Пов’язаний наряд цього клієнта недоступний.");
        return {};
    }
    QVariantMap row;
    const auto fields = query.record();
    for (int i=0; i<fields.count(); ++i) row.insert(fields.fieldName(i),query.value(i));
    if (query.next()) { *error = QStringLiteral("UID відповідає кільком нарядам."); return {}; }
    if (query.lastError().isValid()) { *error = query.lastError().text(); return {}; }
    return row;
}
Rows statementPartHistory(const QString& clientUid, const QString& vehicleUid, QDate from, QDate through, QString* error) {
    if (clientUid.trimmed().isEmpty() || !from.isValid() || !through.isValid() || from>through) {
        *error = QStringLiteral("Некоректний клієнт або період руху запчастин.");
        return {};
    }
    auto db = Database::db();
    const auto orders = crmOrderHistorySource(db);
    const QString parts = orders=="router_work_orders" ? QStringLiteral("router_work_order_parts") : QStringLiteral("work_order_parts");
    QSqlQuery query(db);
    QString sql = QStringLiteral(
        "WITH positions AS (SELECT o.order_uid,p.part_uid,p.id,p.order_number,p.name,p.installed_at,o.car,"
        "COALESCE(NULLIF(o.order_number,0),o.number) AS visible FROM %1 o JOIN %2 p "
        "ON p.order_number=o.number AND p.order_uid=o.order_uid WHERE o.client_uid=? "
        "AND trim(o.order_uid)<>'' AND trim(p.part_uid)<>'' "
        "AND (SELECT count(*) FROM %1 x WHERE x.order_uid=o.order_uid)=1 "
        "AND (SELECT count(*) FROM %2 x WHERE x.order_number=p.order_number AND x.id=p.id)=1 "
        "AND (SELECT count(*) FROM %2 x WHERE x.part_uid=p.part_uid)=1 ").arg(orders,parts);
    if (!vehicleUid.isEmpty()) sql += QStringLiteral("AND o.vehicle_uid=? ");
    sql += QStringLiteral("), events(event_date,name,quantity,actor,order_uid,part_uid,source_id,car,visible,kind,issue_id) AS ("
        "SELECT installed_at,name,NULL,NULL,order_uid,part_uid,id,car,visible,'installed',NULL FROM positions "
        "WHERE trim(installed_at)<>'' ");
    if (tableOrViewExists(db,QStringLiteral("workshop_spare_movements"))) {
        sql += QStringLiteral("UNION ALL SELECT m.moved_on,p.name,m.quantity,m.actor,p.order_uid,p.part_uid,"
            "m.id,p.car,p.visible,'issue',m.id FROM workshop_spare_movements m JOIN positions p "
            "ON p.id=m.part_id AND p.order_number=m.order_number WHERE m.operation='issue' ");
        if (tableOrViewExists(db,QStringLiteral("workshop_spare_orders"))) {
            const QString links=QStringLiteral(
                " FROM positions p JOIN (SELECT part_id,order_number,spare_id,min(id) AS issue_id "
                "FROM workshop_spare_movements WHERE operation='issue' GROUP BY part_id,order_number,spare_id) link "
                "ON link.part_id=p.id AND link.order_number=p.order_number JOIN workshop_spare_orders s ON s.id=link.spare_id ");
            sql += QStringLiteral("UNION ALL SELECT s.ordered_on,p.name,NULL,NULL,p.order_uid,p.part_uid,"
                "s.id,p.car,p.visible,'batch_order',link.issue_id") + links + QStringLiteral("WHERE trim(s.ordered_on)<>'' ");
            sql += QStringLiteral("UNION ALL SELECT r.moved_on,p.name,NULL,r.actor,p.order_uid,p.part_uid,"
                "r.id,p.car,p.visible,'batch_receipt',link.issue_id") + links +
                QStringLiteral("JOIN workshop_spare_movements r ON r.spare_id=link.spare_id AND r.operation='receipt' ");
        }
    }
    sql += QStringLiteral(") SELECT * FROM events WHERE substr(event_date,1,10) BETWEEN ? AND ? "
                          "ORDER BY event_date,kind DESC,source_id,part_uid");
    query.prepare(sql);
    query.addBindValue(clientUid);
    if (!vehicleUid.isEmpty()) query.addBindValue(vehicleUid);
    query.addBindValue(from.toString(Qt::ISODate));
    query.addBindValue(through.toString(Qt::ISODate));
    Rows events;
    if (!collect(query,&events,error)) events.clear();
    return events;
}
IssueSource statementIssueSource(const QString& clientUid, const QString& orderUid, const QString& partUid, qint64 issueId) {
    IssueSource result;
    auto db = Database::db();
    const auto orders = crmOrderHistorySource(db);
    const QString parts = orders=="router_work_orders" ? QStringLiteral("router_work_order_parts") : QStringLiteral("work_order_parts");
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT m.id AS issue_id,m.moved_on,m.quantity AS issued,m.actor,s.id AS reserve_id,s.name,s.ordered_on,s.expected_on,"
        "r.id,r.moved_on,r.quantity,r.actor FROM workshop_spare_movements m "
        "JOIN %1 o ON o.number=m.order_number JOIN %2 p ON p.id=m.part_id AND p.order_number=o.number AND p.order_uid=o.order_uid "
        "JOIN workshop_spare_orders s ON s.id=m.spare_id LEFT JOIN workshop_spare_movements r ON r.spare_id=s.id AND r.operation='receipt' "
        "WHERE m.id=? AND m.operation='issue' AND o.client_uid=? AND o.order_uid=? AND p.part_uid=? "
        "AND (SELECT count(*) FROM %1 x WHERE x.order_uid=o.order_uid)=1 "
        "AND (SELECT count(*) FROM %2 x WHERE x.part_uid=p.part_uid)=1 "
        "AND (SELECT count(*) FROM %2 x WHERE x.order_number=m.order_number AND x.id=m.part_id)=1 "
        "ORDER BY r.moved_on,r.id").arg(orders,parts));
    query.addBindValue(issueId);
    query.addBindValue(clientUid);
    query.addBindValue(orderUid);
    query.addBindValue(partUid);
    if (!query.exec()) { result.error=query.lastError().text(); return result; }
    while (query.next()) {
        if (result.issue.isEmpty()) {
            const auto fields=query.record();
            for (int i=0;i<8;++i) result.issue.insert(fields.fieldName(i),query.value(i));
        }
        if (!query.value(8).isNull())
            result.receipts.append({query.value(8),query.value(9),query.value(10),query.value(11)});
    }
    if (query.lastError().isValid()) result.error=query.lastError().text();
    else if (result.issue.isEmpty()) result.error=QStringLiteral("Первинну видачу або її зв’язок із позицією не підтверджено.");
    return result;
}
CrmFinancialService::ClientJournal statementFinances(const QString& clientUid, const QString& vehicleUid, QDate from, QDate through) {
    return CrmFinancialService::readClientJournal(Database::db(),clientUid,vehicleUid,from,through);
}
Rows statementVehicles(const QString& clientUid, QString* error) {
    QSqlQuery query(Database::db());
    query.prepare("SELECT vehicle_uid, TRIM(brand || ' ' || model),plate FROM crm_vehicles WHERE client_uid=? ORDER BY brand,model,plate,vehicle_uid");
    query.addBindValue(clientUid);
    Rows vehicles;
    if (!collect(query, &vehicles, error)) vehicles.clear();
    return vehicles;
}
bool statementDecisions(const QString& clientUid, const QString& vehicleUid, QDate from, QDate through, QVariantList& rows, QString* error) {
    return BusinessExtensionsService::readEstimateDecisions(Database::db(), clientUid, vehicleUid, from, through, rows, error);
}
ClientDetails details(const QString& uid) {
    ClientDetails result;if(uid.isEmpty())return result;
    auto db=Database::db();
    if(!db.transaction()){result.error=db.lastError().text();return result;}
    const auto rollback=qScopeGuard([&db]{db.rollback();});
    const auto orderHistorySource=crmOrderHistorySource(db);
    QSqlQuery client(db);
    client.prepare(QStringLiteral("SELECT full_name, first_name, last_name, phone, phone_alt, loyalty_level, discount_percent, notes, status, source "
                                  "FROM crm_clients WHERE client_uid=? LIMIT 1"));
    client.addBindValue(uid);

    Rows clients;
    if(!collect(client,&clients,&result.error))return result;
    if(clients.isEmpty()){result.error=QString::fromUtf8("Клієнта не знайдено.");return result;}
    result.client=clients.first();
    result.financial=CrmFinancialService::read(db,uid);
    QSqlQuery vehicles(db);
    vehicles.prepare(QStringLiteral(
        "SELECT v.vehicle_uid, v.brand, v.model, TRIM(v.brand || ' ' || v.model), v.year, "
        "CASE "
        "  WHEN TRIM(COALESCE(v.engine,''))<>'' AND LOWER(TRIM(COALESCE(v.engine,'')))<>LOWER(TRIM(COALESCE(v.fuel_type,''))) THEN v.engine "
        "  ELSE COALESCE((SELECT NULLIF(TRIM(wo.engine_volume),'') FROM work_orders wo "
        "                 WHERE wo.client_uid=v.client_uid AND (wo.vehicle_uid=v.vehicle_uid OR TRIM(COALESCE(wo.vehicle_uid,''))='') "
        "                   AND NULLIF(TRIM(wo.engine_volume),'') IS NOT NULL "
        "                 ORDER BY (CASE WHEN substr(trim(wo.opened_at),3,1)='.' AND substr(trim(wo.opened_at),6,1)='.' THEN CASE WHEN substr(trim(wo.opened_at),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(wo.opened_at),7,4) ELSE '20'||substr(trim(wo.opened_at),7,2) END||'-'||substr(trim(wo.opened_at),4,2)||'-'||substr(trim(wo.opened_at),1,2) ELSE wo.opened_at END) DESC, wo.number DESC LIMIT 1), v.engine) "
        "END, "
        "v.fuel_type, v.plate, v.mileage, v.color, COALESCE(v.is_primary,0) "
        "FROM crm_vehicles v WHERE v.client_uid=? ORDER BY COALESCE(v.is_primary,0) DESC, v.updated_at DESC, v.id DESC"));
    vehicles.addBindValue(uid);
    if(result.error.isEmpty())collect(vehicles,&result.vehicles,&result.error);
    QSqlQuery history(db);
    history.prepare(QStringLiteral("SELECT COALESCE(order_number, number), substr(COALESCE(opened_at,''),1,10), COALESCE(car,''), COALESCE(status,''), "
                                   "NULL, COALESCE(order_uid,'') "
                                   "FROM %1 WHERE client_uid=? ORDER BY (CASE WHEN substr(trim(opened_at),3,1)='.' AND substr(trim(opened_at),6,1)='.' THEN CASE WHEN substr(trim(opened_at),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(opened_at),7,4) ELSE '20'||substr(trim(opened_at),7,2) END||'-'||substr(trim(opened_at),4,2)||'-'||substr(trim(opened_at),1,2) ELSE opened_at END) DESC, number DESC LIMIT 300").arg(orderHistorySource));
    history.addBindValue(uid);
    if(result.error.isEmpty())collect(history,&result.history,&result.error);
    QSqlQuery events(db);
    events.prepare(QStringLiteral("SELECT substr(created_at,1,19), title, body FROM crm_client_events WHERE client_uid=? ORDER BY created_at DESC, id DESC LIMIT 200"));
    events.addBindValue(uid);
    if(result.error.isEmpty())collect(events,&result.events,&result.error);
    QSqlQuery tasks(db);
    tasks.prepare(QStringLiteral("SELECT task_uid, title, status, due_at FROM crm_client_tasks WHERE client_uid=? "
                                 "ORDER BY CASE status WHEN 'open' THEN 0 ELSE 1 END, (CASE WHEN substr(trim(due_at),3,1)='.' AND substr(trim(due_at),6,1)='.' THEN CASE WHEN substr(trim(due_at),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(due_at),7,4) ELSE '20'||substr(trim(due_at),7,2) END||'-'||substr(trim(due_at),4,2)||'-'||substr(trim(due_at),1,2) ELSE due_at END) ASC, id DESC LIMIT 200"));
    tasks.addBindValue(uid);
    if(result.error.isEmpty())collect(tasks,&result.tasks,&result.error);
    if(!result.error.isEmpty()){const auto error=result.error;result={};result.error=error;}
    return result;
}
}
