#pragma once
#include "PayrollReadFixture.h"
#include "core/services/ReportsSyncSupport.h"
namespace PayrollIdentityFixture {
inline bool prepare(QSqlDatabase db,QString* error) {
 if(!PayrollReadFixture::schema(db,error))return false;
 QSqlQuery q(db);auto sql=[&](const QString& text){if(q.exec(text))return true;*error=q.lastError().text();return false;};
 for(const auto& column:QStringList{"order_number INTEGER","opened_at TEXT","plate TEXT","car TEXT","order_uid TEXT","engine_type TEXT","engine_volume TEXT"})if(!sql("ALTER TABLE router_work_orders ADD COLUMN "+column))return false;
 for(const auto& column:QStringList{"title TEXT","order_uid TEXT","service_uid TEXT"})if(!sql("ALTER TABLE router_work_order_services ADD COLUMN "+column))return false;
 for(const auto& text:QStringList{
 "INSERT INTO masters VALUES('m','Master','master',0,1,'',''),('n','Other','master',0,1,'','')",
 "INSERT INTO settings VALUES('payroll_percents','{\"m\":50,\"n\":50}')",
 "INSERT INTO router_work_orders(number,order_number,order_uid,closed_at,status,discount,discount_mode,works_sum) VALUES(1,7,'order-a','2026-01-15','closed',0,'percent',0.10)",
 "INSERT INTO router_work_order_services(id,order_number,order_uid,service_uid,amount,title) VALUES(10,1,'order-a','service-a',0.05,'A'),(11,1,'order-a','service-b',0.05,'B')",
 "INSERT INTO router_service_masters VALUES(10,'m',100),(11,'m',100)"})if(!sql(text))return false;
 return true;
}
inline bool run(QSqlDatabase db,QString* error) {
 if(!prepare(db,error))return false;
 auto fail=[&](const QString& text){*error=text;return false;};
 QSqlQuery q(db);auto sql=[&](const QString& text){if(q.exec(text))return true;*error=q.lastError().text();return false;};
 auto read=[&]{return ReportsSyncSupport::collectPayrollOrdersForMaster(db,"m","2026-01-01","2026-01-31",error);};
 auto rows=read();if(!error->isEmpty()||rows.size()!=1||qRound64(rows.first().masterAmount*100)!=6)return fail("Per-row half-up must produce 6 kopecks, not aggregate 5: "+*error);
 if(!sql("INSERT INTO router_work_orders(number,order_number,order_uid,closed_at,status,discount,discount_mode,works_sum) VALUES(1,8,'order-b','2026-01-15','closed',0,'percent',100)"))return false;
 rows=read();if(!error->isEmpty()||rows.size()!=1||rows.first().orderNumber!="7"||qRound64(rows.first().masterAmount*100)!=6)return fail("Header internal collision mixed UID owners: "+*error);
 if(!sql("INSERT INTO router_work_order_services(id,order_number,order_uid,service_uid,amount,title) VALUES(10,1,'order-b','service-c',100,'Foreign')"))return false;
 rows=read();if(error->isEmpty()||!rows.isEmpty())return fail("UID-less assignment with duplicate physical service ID was accepted");
 rows=ReportsSyncSupport::collectPayrollOrdersForMaster(db,"m","2027-01-01","2027-01-31",error);
 if(!error->isEmpty()||!rows.isEmpty())return fail("Unselected period must remain empty: "+*error);
 if(!sql("ALTER TABLE router_service_masters ADD COLUMN service_uid TEXT")||!sql("UPDATE router_service_masters SET service_uid=CASE service_id WHEN 10 THEN 'service-a' ELSE 'service-b' END"))return false;
 rows=read();if(!error->isEmpty()||rows.size()!=1||qRound64(rows.first().masterAmount*100)!=6)return fail("Explicit assignment UID repair failed: "+*error);
 if(!sql("INSERT INTO payroll_order_adjustments VALUES('m',1,1,0)"))return false;
 rows=read();if(error->isEmpty()||!rows.isEmpty())return fail("Ambiguous numeric adjustment borrowed an owner");
 if(!sql("ALTER TABLE payroll_order_adjustments ADD COLUMN order_uid TEXT")||!sql("UPDATE payroll_order_adjustments SET order_uid='order-b'"))return false;
 rows=read();if(!error->isEmpty()||rows.size()!=1||rows.first().absent)return fail("Foreign adjustment changed selected owner's pay: "+*error);
 if(!sql("UPDATE router_service_masters SET share_percent=50 WHERE service_id=10")||!sql("INSERT INTO router_service_masters(service_id,master_id,share_percent,service_uid) VALUES(10,'n',50,'service-a')"))return false;
 rows=read();if(!error->isEmpty()||rows.size()!=1||qRound64(rows.first().masterAmount*100)!=5)return fail("Legal split must produce 2+3 kopecks: "+*error);
 for(const auto& table:QStringList{"router_work_order_services","router_service_masters","payroll_order_adjustments","outsource_jobs"}){
 if(!sql("ALTER TABLE "+table+" RENAME TO broken_payroll"))return false;
 rows=read();if(error->isEmpty()||!rows.isEmpty())return fail("Hidden dependency failure: "+table);
 if(!sql("ALTER TABLE broken_payroll RENAME TO "+table))return false;
 rows=read();if(!error->isEmpty()||rows.size()!=1||qRound64(rows.first().masterAmount*100)!=5)return fail("Repair/repeat failed: "+*error);
 }
 if(!sql("UPDATE router_work_orders SET discount=20 WHERE order_uid='order-a'"))return false;
 rows=read();if(!error->isEmpty()||rows.size()!=1||qRound64(rows.first().amount*100)!=6||qRound64(rows.first().masterAmount*100)!=3)return fail("Per-row discount/split rounding differs: "+*error);
 if(!sql("UPDATE router_work_orders SET discount=0 WHERE order_uid='order-a'"))return false;
 if(!sql("UPDATE payroll_order_adjustments SET order_uid='order-a'"))return false;
 rows=read();if(!error->isEmpty()||rows.size()!=1||!rows.first().absent||rows.first().masterAmount!=0)return fail("Confirmed absent owner was not excluded: "+*error);
 if(!sql("UPDATE payroll_order_adjustments SET deleted=1,absent=0"))return false;
 rows=read();if(!error->isEmpty()||!rows.isEmpty())return fail("Confirmed deleted owner remained in payroll: "+*error);
 if(!sql("UPDATE payroll_order_adjustments SET order_uid='order-b',deleted=0,absent=1"))return false;
 if(!sql("INSERT INTO router_service_masters(service_id,master_id,share_percent,service_uid) VALUES(10,'m',100,'service-c')"))return false;
 rows=read();if(!error->isEmpty()||rows.size()!=2)return fail("Distinct UID owners were grouped together: "+*error);
 double sum=0;for(const auto& row:rows)sum+=row.masterAmount;
 // The other owner remains absent; its confirmed amount must not become earned pay.
 if(qRound64(sum*100)!=5)return fail("Foreign absent adjustment changed current earnings");
 if(!sql("DELETE FROM router_service_masters WHERE service_uid='service-c'"))return false;
 if(!sql("UPDATE router_service_masters SET service_uid='unknown' WHERE service_id=11"))return false;
 rows=read();if(error->isEmpty()||!rows.isEmpty())return fail("Unknown assignment UID borrowed physical service ID");
 if(!sql("UPDATE router_service_masters SET service_uid='service-b' WHERE service_id=11"))return false;
 rows=read();if(!error->isEmpty()||rows.size()!=1||qRound64(rows.first().masterAmount*100)!=5)return fail("Unknown UID repair failed: "+*error);
 return true;
}
}
