#pragma once
#include "core/services/ReportsPdfSupport.h"
#include "core/services/ActivityAuditService.h"
#include <QSqlQuery>
#include <QSqlError>
namespace FinancialPdfReadFixture {
inline bool run(QSqlDatabase db,QString* error) {
 auto fail=[&](const QString& message){*error=message;return false;};QSqlQuery q(db);
 auto sql=[&](const QString& text){if(q.exec(text))return true;*error=q.lastError().text();return false;};
 for(const auto& text:QStringList{
 "CREATE TABLE IF NOT EXISTS settings(key TEXT PRIMARY KEY,value TEXT)",
 "CREATE TABLE staff_accounts(id TEXT,role TEXT,active INTEGER)","CREATE TABLE access_roles(id TEXT,active INTEGER)",
 "INSERT INTO staff_accounts VALUES('pdf-owner','owner',1)","INSERT INTO access_roles VALUES('owner',1)",
 "CREATE TABLE router_work_orders(number INTEGER,order_number INTEGER)",
 "CREATE TABLE outsource_jobs(id INTEGER,company_name TEXT,sent_date TEXT,return_date TEXT,order_number INTEGER,part_name TEXT,company_price REAL,client_price REAL)",
 "CREATE TABLE IF NOT EXISTS expenses(id INTEGER PRIMARY KEY,period_from TEXT,period_to TEXT,elec_prev REAL,elec_curr REAL,elec_tariff REAL,elec_cost REAL,cleaning REAL,sw_opendatabot REAL,sw_tecalliance REAL,heating REAL,amortization REAL,rent REAL,other_amount REAL,cleaning_note TEXT,software_note TEXT,heating_note TEXT,amortization_note TEXT,rent_note TEXT,other_note TEXT)"})if(!sql(text))return false;
 ActivityAuditService::setCurrentActor({"pdf-owner","Synthetic","owner","test"});
 const QDate from(2026,1,1),to(2026,1,31);int count=9;double repair=9,client=9;
 auto read=[&]{return ReportsPdfSupport::collectOutsourceGroups(from.toString(Qt::ISODate),to.toString(Qt::ISODate),count,repair,client,error);};
 if(!read().isEmpty() || !error->isEmpty() || count || repair || client)return fail("Empty outsource rejected: "+*error);
 if(!ReportsPdfSupport::validateExpensesPdfSource(from,to,error))return false;
 if(!sql("INSERT INTO router_work_orders VALUES(11,7)"))return false;
 if(!sql("INSERT INTO outsource_jobs VALUES(1,'Shop','2026-01-02','',11,'A',12.25,20.75),(2,'Shop','2026-01-03','',11,'B',7.10,11.20),(3,'Other','2025-12-01','',11,'Outside',999,999)"))return false;
 auto groups=read();if(!error->isEmpty() || count!=2 || qRound64(repair*100)!=1935 || qRound64(client*100)!=3195 || groups["Shop"].first().orderNumber!="7")return fail("Wrong outsource totals/order: "+*error);
 for(const auto& table:QStringList{"outsource_jobs","router_work_orders"}) {
 if(!sql("ALTER TABLE "+table+" RENAME TO broken_pdf"))return false;
 if(!read().isEmpty() || error->isEmpty() || count || repair || client)return fail("Hidden SQL dependency: "+table);
 if(!sql("ALTER TABLE broken_pdf RENAME TO "+table))return false;
 read();if(!error->isEmpty() || count!=2)return fail("Outsource repair failed");
 }
 if(!sql("ALTER TABLE expenses RENAME COLUMN cleaning TO invalid_cleaning"))return false;
 if(ReportsPdfSupport::validateExpensesPdfSource(from,to,error) || error->isEmpty())return fail("Expense SQL failure hidden");
 if(!sql("ALTER TABLE expenses RENAME COLUMN invalid_cleaning TO cleaning"))return false;
 if(!ReportsPdfSupport::validateExpensesPdfSource(from,to,error))return false;
 if(!sql("DROP TABLE settings"))return false;
 if(ReportsPdfSupport::validateExpensesPdfSource(from,to,error) || error->isEmpty())return fail("Missing expense dependency hidden");
 if(!sql("CREATE TABLE settings(key TEXT PRIMARY KEY,value TEXT)"))return false;
 if(!ReportsPdfSupport::validateExpensesPdfSource(from,to,error))return false;
 if(!sql("UPDATE staff_accounts SET active=0"))return false;
 if(!read().isEmpty() || error->isEmpty() || count || repair || client)return fail("Revoked outsource access");
 if(ReportsPdfSupport::validateExpensesPdfSource(from,to,error) || error->isEmpty())return fail("Revoked expense access");
 if(!sql("UPDATE staff_accounts SET active=1"))return false;
 read();if(!error->isEmpty() || count!=2 || !ReportsPdfSupport::validateExpensesPdfSource(from,to,error))return fail("Permission repair failed");
 if(!sql("CREATE TABLE access_role_permissions(role_id TEXT,permission_key TEXT,granted INTEGER)"))return false;
 if(!sql("INSERT INTO access_roles VALUES('admin',1)"))return false;
 if(!sql("UPDATE staff_accounts SET role='admin'"))return false;
 ActivityAuditService::setCurrentActor({"pdf-owner","Synthetic","admin","test"});
 if(!read().isEmpty() || error->isEmpty() || ReportsPdfSupport::validateExpensesPdfSource(from,to,error))return fail("Missing report grant accepted");
 if(!sql("INSERT INTO access_role_permissions VALUES('admin','nav.reports',1)"))return false;
 read();if(!error->isEmpty() || count!=2 || !ReportsPdfSupport::validateExpensesPdfSource(from,to,error))return fail("Report grant retry failed");
 if(!sql("UPDATE access_role_permissions SET granted=0"))return false;
 if(!read().isEmpty() || error->isEmpty() || ReportsPdfSupport::validateExpensesPdfSource(from,to,error))return fail("Revoked report grant accepted");
 if(!sql("UPDATE staff_accounts SET role='owner'"))return false;
 ActivityAuditService::setCurrentActor({"pdf-owner","Synthetic","owner","test"});
 if(!ReportsPdfSupport::validateExpensesPdfSource(from,to,error))return false;
 return true;
}
}
