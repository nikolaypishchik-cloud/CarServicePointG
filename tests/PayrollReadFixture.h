#pragma once
#include "core/services/ReportsDataLoader.h"
#include "core/cache/ReportsSnapshotCache.h"
#include <QCryptographicHash>
#include <QFile>
#include <QSqlQuery>
#include <QSqlError>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QUuid>
namespace PayrollReadFixture {
inline bool schema(QSqlDatabase db,QString* error) {
 const QStringList statements={
 "CREATE TABLE IF NOT EXISTS settings(key TEXT PRIMARY KEY,value TEXT)",
 "CREATE TABLE masters(id TEXT PRIMARY KEY,name TEXT,role TEXT,admin_percent REAL,active INTEGER,active_from TEXT,active_to TEXT)",
 "CREATE TABLE router_work_orders(number INTEGER,closed_at TEXT,status TEXT,discount REAL,discount_mode TEXT,works_sum REAL)",
 "CREATE TABLE router_work_order_parts(order_number INTEGER,purchase_amount REAL,purchase_price REAL,qty REAL,amount REAL)",
 "CREATE TABLE router_work_order_services(id INTEGER,order_number INTEGER,amount REAL)",
 "CREATE TABLE router_service_masters(service_id INTEGER,master_id TEXT,share_percent REAL)",
 "CREATE TABLE payroll_order_adjustments(master_id TEXT,order_number INTEGER,absent INTEGER,deleted INTEGER)",
 "CREATE TABLE outsource_jobs(service_id INTEGER,sent_date TEXT,company_price REAL,client_price REAL)",
 "CREATE TABLE IF NOT EXISTS expenses(period_from TEXT,period_to TEXT,elec_cost REAL,elec_curr REAL,elec_prev REAL,elec_tariff REAL,cleaning REAL,heating REAL,rent REAL,amortization REAL,sw_opendatabot REAL,sw_tecalliance REAL,other_amount REAL)"};
 QSqlQuery q(db);for(const auto& sql:statements)if(!q.exec(sql)){*error=q.lastError().text();return false;}return true;
}
inline bool populate(QSqlDatabase db,QString* error) {
 QSqlQuery q(db);for(const auto& sql:QStringList{
 "INSERT INTO masters VALUES('m','Master','master',0,1,'',''),('a','Admin','admin',10,1,'','')",
 "INSERT INTO settings VALUES('payroll_percents','{\"m\":50}')",
 "INSERT INTO router_work_orders VALUES(1,'2026-01-15','closed',0,'percent',100)",
 "INSERT INTO router_work_order_parts VALUES(1,30,30,1,50)",
 "INSERT INTO router_work_order_services VALUES(10,1,100)",
 "INSERT INTO router_service_masters VALUES(10,'m',100)",
 "INSERT INTO outsource_jobs VALUES(99,'2026-01-15',10,20)",
 "INSERT INTO expenses(period_from,period_to,cleaning) VALUES('2026-01-01','2026-01-31',10)"})if(!q.exec(sql)){*error=q.lastError().text();return false;}return true;
}
inline QByteArray bytes(const QString& path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};return f.readAll();}
inline bool run(QString* error) {
 const auto fail=[&](const QString& text){*error=text;return false;};
 const auto read=[](const QString& path){return ReportsDataLoader::buildPayrollSnapshot(path,"2026-01-01","2026-01-31");};
 auto names=[](){auto list=QSqlDatabase::connectionNames();list.sort();return list;};
 const auto connections=names();QTemporaryDir dir;
 const auto missing=dir.filePath("missing.db");const auto absent=read(missing);
 if(QFile::exists(missing) || absent.error.isEmpty() || !absent.rows.isEmpty())return fail("Missing database was created or read failure hidden");
 if(read(dir.path()).error.isEmpty())return fail("Directory treated as readable database");
 const auto corrupt=dir.filePath("corrupt.db");{QFile f(corrupt);if(!f.open(QIODevice::WriteOnly) || f.write("not sqlite")!=10)return fail("Fixture write failed");}
 const auto original=bytes(corrupt);if(read(corrupt).error.isEmpty() || bytes(corrupt)!=original)return fail("Invalid SQLite not reported or changed");
 const auto name=QUuid::createUuid().toString();auto db=QSqlDatabase::addDatabase("QSQLITE",name);
 const auto close=qScopeGuard([&]{db.close();db={};QSqlDatabase::removeDatabase(name);});db.setDatabaseName(dir.filePath("payroll.db"));
 if(!db.open() || !schema(db,error))return false;
 const auto empty=read(db.databaseName());if(!empty.error.isEmpty() || !empty.rows.isEmpty() || empty.stats.size()!=2)return fail("Valid empty payroll rejected: "+empty.error);
 {QSqlQuery brokenEmpty(db);if(!brokenEmpty.exec("ALTER TABLE router_work_order_parts RENAME COLUMN amount TO broken_amount"))return fail(brokenEmpty.lastError().text());
  const auto result=read(db.databaseName());if(result.error.isEmpty() || !result.rows.isEmpty())return fail("Empty selection hid invalid SQL schema");
  if(!brokenEmpty.exec("ALTER TABLE router_work_order_parts RENAME COLUMN broken_amount TO amount"))return fail(brokenEmpty.lastError().text());}
#ifndef Q_OS_WIN
 // POSIX permission denial; Windows ACL coverage belongs to its native runner.
 const auto mode=QFile::permissions(db.databaseName());
 if(!QFile::setPermissions(db.databaseName(),{}))return fail("Cannot create denied-read fixture");
 const auto denied=read(db.databaseName());
 if(!QFile::setPermissions(db.databaseName(),mode))return fail("Cannot restore fixture permissions");
 if(denied.error.isEmpty())return fail("Denied-read database treated as valid payroll");
#endif
 if(!populate(db,error))return false;
 const auto pristine=bytes(db.databaseName());
 auto valid=[&](){const auto result=read(db.databaseName());double master=-1,admin=-1;for(const auto& row:result.rows){if(row.masterId=="m")master=row.accruedValue;if(row.masterId=="a")admin=row.accruedValue;}return result.error.isEmpty() && result.rows.size()==2 && master==40 && admin==3;};
 if(!valid() || bytes(db.databaseName())!=pristine)return fail("Expected 40 master + 3 admin without database changes");
 QSqlQuery q(db);
 // Each public read must discard partial results and recover after schema repair.
 for(const auto& table:QStringList{"masters","settings","router_work_orders","router_work_order_parts","router_work_order_services","router_service_masters","payroll_order_adjustments","outsource_jobs","expenses"}) {
  if(!q.exec("ALTER TABLE "+table+" RENAME TO broken_table"))return fail(q.lastError().text());
  const auto broken=read(db.databaseName());
  if(broken.error.isEmpty() || !broken.rows.isEmpty() || broken.stats.size()!=1)return fail("Hidden SQL error or partial payroll: "+table);
  const auto cacheKey="payroll-failure-"+table;ReportsSnapshotCache::storePayroll(cacheKey,empty);ReportsSnapshotCache::storePayroll(cacheKey,broken);PayrollSnapshot cached;
  if(ReportsSnapshotCache::tryGetPayroll(cacheKey,&cached))return fail("Payroll failure cached: "+table);
  if(!q.exec("ALTER TABLE broken_table RENAME TO "+table) || !valid())return fail("Payroll did not recover: "+table);
 }
 if(!q.exec("INSERT INTO settings VALUES('settings.staff.employment.m','broken')"))return fail(q.lastError().text());
 if(read(db.databaseName()).error.isEmpty())return fail("Employment error hid employee");
 if(!q.exec("DELETE FROM settings WHERE key='settings.staff.employment.m'") || !valid())return fail("Employment repair failed");
 if(!q.exec("UPDATE settings SET value='broken' WHERE key='payroll_percents'"))return fail(q.lastError().text());
 if(read(db.databaseName()).error.isEmpty())return fail("Bad payroll policy became zero percentage");
 auto expected=connections;expected<<name;expected.sort();if(names()!=expected)return fail("Report connection leak");return true;
}
}
