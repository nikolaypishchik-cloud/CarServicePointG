#pragma once
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
namespace PartsReportQuery {
inline bool openSnapshot(QSqlDatabase& db,const QString& path,QString* error) {
    db.setDatabaseName(path);db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    if(!db.open() || !db.transaction()){if(error)*error=db.lastError().text();return false;}
    return true;
}
inline bool execute(QSqlDatabase db,QSqlQuery* query,QString* error,bool withIssuedStock = false) {
    *query=QSqlQuery(db);
    const auto uidColumn = [&](const QString& table, const QString& alias) {
        return db.record(table).contains("part_uid") ? "COALESCE(" + alias + ".part_uid,'')" : QStringLiteral("''");
    };
    if(!query->prepare(QStringLiteral(
                "WITH %3headers AS MATERIALIZED (SELECT number,order_uid,order_number,closed_at,opened_at,plate,car,vin FROM router_work_orders), "
                "keys AS (SELECT 'u:'||order_uid AS lookup,number,order_uid FROM headers WHERE COALESCE(order_uid,'')<>'' "
                "UNION ALL SELECT 'n:'||number,number,order_uid FROM headers), "
                "unique_owners AS (SELECT lookup,MIN(number) AS number,MIN(order_uid) AS order_uid FROM keys GROUP BY lookup HAVING COUNT(*)=1) "
                "SELECT p.id, p.ordered_at, substr(COALESCE(NULLIF(wo.closed_at,''), wo.opened_at),1,10), COALESCE(wo.number,0), COALESCE(wo.order_number,wo.number), "
                "COALESCE(p.store,''), COALESCE(p.name,''), COALESCE(p.part_number,''), "
                "COALESCE(wo.plate,''), COALESCE(wo.car,''), COALESCE(wo.vin,''), "
                "COALESCE(p.purchase_amount, CASE WHEN COALESCE(p.purchase_price,0)>0 THEN COALESCE(p.purchase_price,0)*COALESCE(p.qty,1) ELSE COALESCE(p.amount,0) END), "
                "COALESCE(p.qty,1), COALESCE(p.amount,0), COALESCE(p.purchase_price,0), COALESCE(p.markup_percent,0), COALESCE(p.manual_markup_override,0), COALESCE(a.invoice_number,''), COALESCE(a.file_path,''), COALESCE(a.invoice_date,''), COALESCE(p.pricing_calc_version,1), wo.order_uid%4 "
                "FROM router_work_order_parts p "
                "LEFT JOIN unique_owners owner ON owner.lookup=CASE WHEN COALESCE(p.order_uid,'')<>'' THEN 'u:'||p.order_uid ELSE 'n:'||p.order_number END "
                "LEFT JOIN headers wo ON wo.number=owner.number AND wo.order_uid IS owner.order_uid "
                "LEFT JOIN work_order_parts wp ON wp.id = p.id AND COALESCE(wp.order_number,'') = COALESCE(p.order_number,'') AND COALESCE(wp.order_uid,'') = COALESCE(p.order_uid,'') AND %1=%2 "
                "LEFT JOIN work_order_part_attachments a ON a.part_id = wp.id%5")
                .arg(uidColumn("work_order_parts","wp"), uidColumn("router_work_order_parts","p"),
                     withIssuedStock ? QStringLiteral("issued_stock AS MATERIALIZED (SELECT DISTINCT part_id,order_number FROM workshop_spare_movements WHERE operation='issue'), ") : QString(),
                     withIssuedStock ? QStringLiteral(", issued.part_id IS NOT NULL") : QString(),
                     withIssuedStock ? QStringLiteral(" LEFT JOIN issued_stock issued ON issued.part_id=wp.id AND issued.order_number=wp.order_number") : QString())
                + (withIssuedStock ? QStringLiteral(" ORDER BY COALESCE(NULLIF(TRIM(p.store),''), '(без магазину)') ASC,p.order_number ASC,p.id ASC") : QString()))){if(error)*error=query->lastError().text();return false;}
    if(!query->exec()){if(error)*error=query->lastError().text();return false;}
    return true;
}
}
