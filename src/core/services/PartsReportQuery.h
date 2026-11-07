#pragma once
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
namespace PartsReportQuery {
inline bool openSnapshot(QSqlDatabase& db,const QString& path,QString* error) {
    db.setDatabaseName(path);db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    if(!db.open() || !db.transaction()){if(error)*error=db.lastError().text();return false;}
    return true;
}
inline bool execute(QSqlDatabase db,QSqlQuery* query,QString* error) {
    *query=QSqlQuery(db);
    if(!query->prepare(
                "WITH headers AS MATERIALIZED (SELECT number,order_uid,order_number,closed_at,opened_at,plate,car,vin FROM router_work_orders), "
                "keys AS (SELECT 'u:'||order_uid AS lookup,number,order_uid FROM headers WHERE COALESCE(order_uid,'')<>'' "
                "UNION ALL SELECT 'n:'||number,number,order_uid FROM headers), "
                "unique_owners AS (SELECT lookup,MIN(number) AS number,MIN(order_uid) AS order_uid FROM keys GROUP BY lookup HAVING COUNT(*)=1) "
                "SELECT p.id, p.ordered_at, substr(COALESCE(NULLIF(wo.closed_at,''), wo.opened_at),1,10), COALESCE(wo.number,0), COALESCE(wo.order_number,wo.number), "
                "COALESCE(p.store,''), COALESCE(p.name,''), COALESCE(p.part_number,''), "
                "COALESCE(wo.plate,''), COALESCE(wo.car,''), COALESCE(wo.vin,''), "
                "COALESCE(p.purchase_amount, CASE WHEN COALESCE(p.purchase_price,0)>0 THEN COALESCE(p.purchase_price,0)*COALESCE(p.qty,1) ELSE COALESCE(p.amount,0) END), "
                "COALESCE(p.qty,1), COALESCE(p.amount,0), COALESCE(p.purchase_price,0), COALESCE(p.markup_percent,0), COALESCE(p.manual_markup_override,0), COALESCE(a.invoice_number,''), COALESCE(a.file_path,''), COALESCE(a.invoice_date,''), COALESCE(p.pricing_calc_version,1), wo.order_uid "
                "FROM router_work_order_parts p "
                "LEFT JOIN unique_owners owner ON owner.lookup=CASE WHEN COALESCE(p.order_uid,'')<>'' THEN 'u:'||p.order_uid ELSE 'n:'||p.order_number END "
                "LEFT JOIN headers wo ON wo.number=owner.number AND wo.order_uid IS owner.order_uid "
                "LEFT JOIN work_order_parts wp ON wp.id = p.id AND COALESCE(wp.order_number,'') = COALESCE(p.order_number,'') AND COALESCE(wp.order_uid,'') = COALESCE(p.order_uid,'') "
                "LEFT JOIN work_order_part_attachments a ON a.part_id = wp.id")){if(error)*error=query->lastError().text();return false;}
    if(!query->exec()){if(error)*error=query->lastError().text();return false;}
    return true;
}
}
