#include "core/crm/CrmPageReadService.h"
#include "core/crm/CrmClientService.h"
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
