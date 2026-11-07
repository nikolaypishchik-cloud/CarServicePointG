#pragma once
#include "core/services/PayrollIdentityRead.h"
namespace PayrollGraphProjectionFixture {
inline bool run(QSqlDatabase db,QString* error) {
    auto fail=[&](const QString& text){*error=text;return false;};
    QSqlQuery q(db);auto sql=[&](const QString& text){if(q.exec(text))return true;*error=q.lastError().text();return false;};
    for(const auto& statement:QStringList{
        "CREATE TABLE router_work_orders(number INT,closed_at TEXT,status TEXT,discount REAL,discount_mode TEXT,works_sum REAL,order_uid TEXT,order_number INT,opened_at TEXT,plate TEXT,car TEXT,engine_type TEXT,engine_volume TEXT,unread TEXT)",
        "CREATE TABLE router_work_order_services(id INT,order_number INT,amount REAL,order_uid TEXT,service_uid TEXT,title TEXT,unread TEXT)",
        "CREATE TABLE router_service_masters(service_id INT,master_id TEXT,share_percent REAL,service_uid TEXT,order_uid TEXT,unread TEXT)",
        "CREATE TABLE payroll_order_adjustments(master_id TEXT,order_number INT,absent INT,deleted INT,order_uid TEXT,unread TEXT)",
        "CREATE TABLE outsource_jobs(service_id INT,service_uid TEXT,order_uid TEXT,unread TEXT)",
        "INSERT INTO router_work_orders VALUES(11,'2026-01-15','closed',0,'percent',0.10,'order-a',7,'2026-01-01','AA 1234 AA','Synthetic car','Diesel','2.0',zeroblob(1048576))",
        "INSERT INTO router_work_order_services VALUES(21,11,0.05,'order-a','service-a','Tiny A',zeroblob(1048576)),(22,11,0.05,'order-a','service-b','Tiny B',zeroblob(1048576))",
        "INSERT INTO router_service_masters VALUES(21,'m',50,'service-a','order-a',zeroblob(1048576)),(21,'n',50,'service-a','order-a',zeroblob(1048576)),(22,'m',100,'service-b','order-a',zeroblob(1048576))"})if(!sql(statement))return false;
    PayrollIdentityRead::Graph graph;
    auto load=[&]{error->clear();return PayrollIdentityRead::loadGraph(db,&graph,error);};
    auto read=[&](const QString& employee){return PayrollIdentityRead::read(db,employee,"2026-01-01","2026-01-31",50,error,{},&graph);};
    if(!load())return false;
    for(const auto& rows:{graph.headers,graph.services,graph.assignments,graph.adjustments,graph.outsources})
        for(const auto& row:rows)if(row.contains("unread"))return fail("Unused payload materialized");
    if(graph.headers.first().size()!=13||graph.services.first().size()!=6||graph.assignments.first().size()!=5)return fail("Projection dropped or added a result/identity column");
    const auto owners=graph.orderIndex.uids,services=graph.serviceIndex.uids;
    for(int i=0;i<20;++i){auto rows=read("m");if(!error->isEmpty()||rows.size()!=1)return fail("Master repeat failed: "+*error);
        const auto& r=rows.first();
        if(r.dateStart!="01.01.26"||r.dateEnd!="15.01.26"||r.orderNumber!="7"||r.internalOrderNumber!=11
           ||r.car!=QString::fromUtf8("АА 1234 АА | Synthetic car")||r.engine!="Diesel\n2.0"||r.absent
           ||r.works!=QString::fromUtf8("Tiny A — 0.02 грн\nTiny B — 0.03 грн")
           ||qRound64(r.amount*100)!=8||qRound64(r.masterAmount*100)!=5)return fail("Result fields or cents changed");
        rows=read("n");if(!error->isEmpty()||rows.size()!=1||qRound64(rows.first().masterAmount*100)!=2)return fail("Split worker mismatch");
    }
    if(graph.orderIndex.uids!=owners||graph.serviceIndex.uids!=services)return fail("Prepared indexes mutated during read");
    if(!sql("UPDATE router_work_orders SET number=12")||!load())return false;
    const auto changedRows=read("m");
    if(!error->isEmpty()||changedRows.size()!=1||graph.orderIndex.numbers.contains("11")||graph.orderIndex.numbers.value("12").size()!=1||changedRows.first().internalOrderNumber!=12)return fail("Reload reused previous numeric owner index");
    if(!sql("UPDATE router_work_orders SET number=11")||!load())return false;
    if(!db.transaction()||!sql("UPDATE router_work_order_services SET amount=100 WHERE id=21")||!load())return fail("Transaction fixture failed");
    if(!db.rollback()||!load()||qRound64(read("m").first().masterAmount*100)!=5)return fail("Read committed or lost caller transaction");
    for(const auto& table:QStringList{"router_work_orders","router_work_order_services","router_service_masters","payroll_order_adjustments","outsource_jobs"}){
        if(!sql("ALTER TABLE "+table+" RENAME TO unavailable"))return false;
        if(load()||error->isEmpty()||!graph.headers.isEmpty()||!graph.orderIndex.uids.isEmpty()||!graph.serviceIndex.numbers.isEmpty())return fail("Failed load published stale graph/indexes");
        if(!sql("ALTER TABLE unavailable RENAME TO "+table)||!load())return false;
    }
    if(!sql("ALTER TABLE router_work_order_services RENAME TO readable_services")
       ||!sql("CREATE VIEW router_work_order_services AS SELECT id,order_number,CASE WHEN id=22 THEN abs(-9223372036854775808) ELSE amount END AS amount,order_uid,service_uid,title FROM readable_services"))return false;
    if(load()||!error->contains("integer overflow")||!graph.headers.isEmpty())return fail("Row evaluation error was hidden");
    if(!sql("DROP VIEW router_work_order_services")||!sql("ALTER TABLE readable_services RENAME TO router_work_order_services")||!load())return false;
    // Old schema: optional identity and display columns may all be absent.
    for(const auto& table:QStringList{"router_work_orders","router_work_order_services","router_service_masters","payroll_order_adjustments","outsource_jobs"}){
        QSqlQuery columns(db);if(!columns.exec("SELECT * FROM "+table+" LIMIT 0"))return fail(columns.lastError().text());const auto record=columns.record();columns.finish();
        for(const auto& optional:QStringList{"order_uid","service_uid","unread","order_number","opened_at","plate","car","engine_type","engine_volume","title"}){
            if(optional=="order_number"&&table!="router_work_orders")continue;
            if(record.indexOf(optional)>=0&&!sql("ALTER TABLE "+table+" DROP COLUMN "+optional))return false;
        }
    }
    if(!load())return false;auto rows=read("m");if(!error->isEmpty()||rows.size()!=1||rows.first().orderNumber!="11"||qRound64(rows.first().masterAmount*100)!=5)return fail("Old schema changed");
    if(!sql("ALTER TABLE router_work_order_services RENAME COLUMN amount TO missing_amount"))return false;
    if(load()||*error!="Payroll column unavailable: router_work_order_services.amount")return fail("Required-column diagnostic changed");
    if(!sql("ALTER TABLE router_work_order_services RENAME COLUMN missing_amount TO amount")||!load())return false;
    return true;
}
}
