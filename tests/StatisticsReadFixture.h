#pragma once
#include "core/services/StatisticsSnapshotService.h"
#include "core/services/ReportsDataLoader.h"
#include "infrastructure/db/sqlite/ScopedConnectionManager.h"
#include <QTemporaryDir>
#include <QFile>
#include <QSqlQuery>
#include <QSqlError>
#include <QUuid>
#include <QScopeGuard>
namespace StatisticsReadFixture {
inline bool run(QString* error) {
 auto fail=[&](const QString& message){*error=message;return false;};QTemporaryDir dir;
 auto read=[&](const QString& path){return StatisticsSnapshotService::collectFromPath(path,QDate(2026,1,1),QDate(2026,1,31));};
 const auto before=QSqlDatabase::connectionNames();const auto missing=dir.filePath("missing.db");
 if(read(missing).error.isEmpty() || QFile::exists(missing))return fail("Missing source created or not reported");
 if(read(dir.path()).error.isEmpty())return fail("Directory accepted");
 {QString openError;ScopedSqliteConnection absent(missing,"stats-test",&openError,true);if(absent.isOpen() || openError.isEmpty() || QFile::exists(missing))return fail("Readonly worker open created source");}
 const auto name=QUuid::createUuid().toString();auto db=QSqlDatabase::addDatabase("QSQLITE",name);db.setDatabaseName(dir.filePath("fixture.db"));
 auto close=qScopeGuard([&]{db.close();db={};QSqlDatabase::removeDatabase(name);});if(!db.open())return fail(db.lastError().text());
 QSqlQuery q(db);auto sql=[&](const QString& s){if(q.exec(s))return true;*error=q.lastError().text();return false;};
 for(const auto& s:QStringList{
 "CREATE TABLE router_work_orders(number INTEGER,order_uid TEXT,opened_at TEXT,closed_at TEXT,status TEXT,client TEXT,works_sum REAL,parts_sum REAL,discount REAL,discount_mode TEXT)",
 "CREATE TABLE router_work_order_services(id INTEGER,order_number INTEGER,amount REAL)",
 "CREATE TABLE router_work_order_parts(order_number INTEGER,store TEXT,name TEXT,qty REAL,purchase_amount REAL,amount REAL,paid INTEGER)",
 "CREATE TABLE router_service_masters(service_id INTEGER,master_id TEXT,share_percent REAL)",
 "CREATE TABLE masters(id TEXT,name TEXT,admin_percent REAL)",
 "CREATE TABLE router_outsource_repairs(sent_at TEXT,client_price REAL,repair_cost REAL)",
 "CREATE TABLE settings(key TEXT PRIMARY KEY,value TEXT)",
 "CREATE TABLE expenses(id INTEGER PRIMARY KEY,period_from TEXT,period_to TEXT,elec_curr REAL,elec_prev REAL,elec_tariff REAL,cleaning_note TEXT,software_note TEXT,heating_note TEXT,amortization_note TEXT,rent_note TEXT,other_note TEXT,elec_cost REAL,cleaning REAL,heating REAL,rent REAL,amortization REAL,sw_opendatabot REAL,sw_tecalliance REAL,other_amount REAL)",
 "CREATE TABLE additional_expenses(id INTEGER PRIMARY KEY,spent_on TEXT,category TEXT,description TEXT,amount_cents INTEGER)"})if(!sql(s))return false;
 auto empty=read(db.databaseName());if(!empty.error.isEmpty() || empty.closedOrders || empty.expenses)return fail("Empty success failed: "+empty.error);
 for(const auto& s:QStringList{
 "INSERT INTO router_work_orders VALUES(1,'uid','2026-01-01','2026-01-02','closed','Synthetic',100,50,0,'percent')",
 "INSERT INTO router_work_order_services VALUES(10,1,100)","INSERT INTO router_service_masters VALUES(10,'m',100)","INSERT INTO masters VALUES('m','Master',50)",
 "INSERT INTO router_work_order_parts VALUES(1,'Shop','Part',1,30,50,1)","INSERT INTO router_outsource_repairs VALUES('2026-01-02',20,10)",
 "INSERT INTO expenses(period_from,period_to,cleaning) VALUES('2026-01-01','2026-01-31',10)","INSERT INTO additional_expenses(spent_on,amount_cents) VALUES('2026-01-02',250)"})if(!sql(s))return false;
 const auto expenses=[&]{return ReportsDataLoader::buildExpensesSnapshot(db.databaseName(),"2026-01-01","2026-01-31");};
 const auto totals=[&]{return ReportsDataLoader::buildTotalExpensesSnapshot(db.databaseName(),"2026-01-01","2026-01-31");};
 if(ReportsDataLoader::buildExpensesSnapshot(missing,"2026-01-01","2026-01-31").error.isEmpty() || QFile::exists(missing))return fail("Expense reader created source");
 if(!expenses().error.isEmpty() || expenses().cleaning!=10 || !totals().error.isEmpty() || totals().periodicCents!=1000 || totals().additionalCents!=250)return fail("Expense public readers totals failed");
 if(!sql("ALTER TABLE expenses RENAME COLUMN cleaning TO invalid_cleaning"))return false;
 if(expenses().error.isEmpty() || totals().error.isEmpty() || !totals().rows.isEmpty() || totals().periodicCents)return fail("Expense failure hidden/partial");
 if(!sql("ALTER TABLE expenses RENAME COLUMN invalid_cleaning TO cleaning"))return false;
 if(!expenses().error.isEmpty() || expenses().cleaning!=10 || !totals().error.isEmpty() || totals().additionalCents!=250)return fail("Expense repair retry failed");
 auto emptyExpense=ReportsDataLoader::buildExpensesSnapshot(db.databaseName(),"2027-01-01","2027-01-31");
 auto emptyTotals=ReportsDataLoader::buildTotalExpensesSnapshot(db.databaseName(),"2027-01-01","2027-01-31");
 if(!emptyExpense.error.isEmpty() || emptyExpense.foundRow || !emptyTotals.error.isEmpty() || !emptyTotals.rows.isEmpty())return fail("Expense empty success failed");
 auto good=read(db.databaseName());if(!good.error.isEmpty() || good.closedOrders!=1 || good.payroll!=50 || good.expenses!=12.5 || good.netProfit!=67.5)return fail("Independent totals failed: "+good.error);
 for(const auto& table:QStringList{"router_work_orders","router_work_order_services","router_work_order_parts","router_service_masters","masters","router_outsource_repairs","expenses"}) {
 if(!sql("ALTER TABLE "+table+" RENAME TO broken"))return false;
 const auto failed=read(db.databaseName());if(failed.error.isEmpty() || failed.closedOrders || failed.revenueLabor || !failed.dailyRevenue.isEmpty())return fail("Partial/hidden SQL failure: "+table);
 if(!sql("ALTER TABLE broken RENAME TO "+table))return false;
 const auto retry=read(db.databaseName());if(!retry.error.isEmpty() || retry.netProfit!=67.5)return fail("Repair retry: "+retry.error);
 }
 if(!sql("ALTER TABLE additional_expenses RENAME COLUMN amount_cents TO broken"))return false;
 if(read(db.databaseName()).error.isEmpty())return fail("Optional existing malformed table ignored");
 if(!sql("ALTER TABLE additional_expenses RENAME COLUMN broken TO amount_cents"))return false;
 {QString openError;ScopedSqliteConnection source(db.databaseName(),"stats-test",&openError,true);if(!source.isOpen())return fail(openError);QSqlQuery write(source.db());if(write.exec("INSERT INTO masters VALUES('forbidden','Forbidden',0)"))return fail("Readonly worker allowed write");}
 QFile file(db.databaseName());if(!file.open(QIODevice::ReadOnly))return fail("Cannot hash fixture");const auto bytes=file.readAll();file.close();
 if(!read(db.databaseName()).error.isEmpty())return fail("Final repeat failed");if(!file.open(QIODevice::ReadOnly) || file.readAll()!=bytes)return fail("Read changed source");
 auto after=QSqlDatabase::connectionNames();after.removeAll(name);if(after!=before)return fail("Leaked connection");return true;
}
}
