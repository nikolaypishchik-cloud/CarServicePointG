#pragma once
#include "PayrollReadFixture.h"
#include "core/services/ReportsSyncSupport.h"
namespace AdminPayrollIdentityFixture {
inline bool run(QString* error) {
    const auto fail=[&](const QString& text){*error=text;return false;};
    QTemporaryDir dir;const auto name=QUuid::createUuid().toString();
    auto db=QSqlDatabase::addDatabase("QSQLITE",name);
    const auto close=qScopeGuard([&]{db.close();db={};QSqlDatabase::removeDatabase(name);});
    db.setDatabaseName(dir.filePath("synthetic-admin.db"));
    if(!db.open()||!PayrollReadFixture::schema(db,error)||!PayrollReadFixture::populate(db,error))return false;
    auto sql=[&](const QString& text){QSqlQuery q(db);if(q.exec(text))return true;*error=q.lastError().text();return false;};
    for(const auto& ddl:QStringList{"ALTER TABLE router_work_orders ADD COLUMN order_number INTEGER","ALTER TABLE router_work_orders ADD COLUMN opened_at TEXT","ALTER TABLE router_work_orders ADD COLUMN plate TEXT","ALTER TABLE router_work_orders ADD COLUMN car TEXT","ALTER TABLE router_work_order_services ADD COLUMN title TEXT","UPDATE router_work_order_services SET title='owned'","UPDATE router_work_orders SET order_number=7,works_sum=100.05"})if(!sql(ddl))return false;
    auto read=[&](const QString& start=QStringLiteral("2026-01-01")){return ReportsSyncSupport::collectPayrollOrdersForMaster(db,"a",start,"2026-01-31",error);};
    auto good=[&](const QString& title){const auto rows=read();return error->isEmpty()&&rows.size()==1&&rows[0].works==title&&rows[0].orderNumber=="7"&&rows[0].amount==100.05&&rows[0].masterAmount==10.01;};
    if(!good("owned"))return fail("Legal numeric-only history or money changed: "+*error);
    for(const auto& ddl:QStringList{"ALTER TABLE router_work_orders ADD COLUMN order_uid TEXT","ALTER TABLE router_work_order_services ADD COLUMN order_uid TEXT","UPDATE router_work_orders SET order_uid='A'","UPDATE router_work_order_services SET order_uid='A'","INSERT INTO router_work_orders(number,order_number,order_uid,status) VALUES(2,7,'B','open')","INSERT INTO router_work_order_services(id,order_number,amount,title,order_uid) VALUES(11,1,999,'foreign','B')"})if(!sql(ddl))return false;
    // Known UID is authoritative even when historical internal numbers collide.
    if(!good("owned"))return fail("Foreign UID leaked into admin detail: "+*error);
    if(!sql("INSERT INTO router_work_order_services(id,order_number,amount,title,order_uid) VALUES(12,999,10,'historical','A'),(13,1,1,'legacy','')"))return false;
    if(!good("owned, historical, legacy"))return fail("Legitimate UID/numeric history lost: "+*error);
    if(!sql("UPDATE router_work_order_services SET order_uid='unknown' WHERE id=11"))return false;
    if(!read().isEmpty()||error->isEmpty())return fail("Unknown UID silently matched numeric parent");
    if(!sql("UPDATE router_work_order_services SET order_uid='B' WHERE id=11"))return false;
    if(!good("owned, historical, legacy"))return fail("Repair/repeat failed");
    if(!sql("INSERT INTO router_work_orders(number,order_uid,status) VALUES(1,'C','open')"))return false;
    if(!read().isEmpty()||error->isEmpty())return fail("Ambiguous numeric-only child accepted");
    if(!sql("DELETE FROM router_work_orders WHERE order_uid='C'"))return false;
    if(!sql("INSERT INTO router_work_orders(number,order_uid,status) VALUES(3,'A','open')"))return false;
    if(!read().isEmpty()||error->isEmpty())return fail("Duplicate parent UID accepted");
    if(!sql("DELETE FROM router_work_orders WHERE number=3"))return false;
    if(!sql("ALTER TABLE router_work_order_services RENAME COLUMN title TO broken_title"))return false;
    if(!read().isEmpty()||error->isEmpty())return fail("SQL dependency failure hidden");
    if(!sql("ALTER TABLE router_work_order_services RENAME COLUMN broken_title TO title"))return false;
    const auto before=PayrollReadFixture::bytes(db.databaseName());
    if(!good("owned, historical, legacy")||!good("owned, historical, legacy"))return fail("Repaired repeat failed");
    if(!read("2027-01-01").isEmpty()||!error->isEmpty())return fail("Empty selection is not success");
    if(before!=PayrollReadFixture::bytes(db.databaseName()))return fail("Reader changed database");
    return true;
}
}
