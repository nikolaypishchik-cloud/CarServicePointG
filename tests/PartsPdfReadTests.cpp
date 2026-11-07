#include "FinancialPdfReadFixture.h"
#include "core/services/ReportsPdfExpenseSupport.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDebug>
int main(int argc,char** argv){QCoreApplication app(argc,argv);QTemporaryDir dir;auto db=QSqlDatabase::addDatabase("QSQLITE","carservice_main");db.setDatabaseName(dir.filePath("synthetic.db"));if(!db.open())return 1;QString error;auto check=[&](bool ok){if(!ok)qFatal("Parts PDF: %s",qPrintable(error));};check(FinancialPdfReadFixture::run(db,&error));QSqlQuery q(db);auto sql=[&](const QString& text){if(!q.exec(text)){error=q.lastError().text();check(false);}};
 for(const auto& text:QStringList{
 "ALTER TABLE router_work_orders ADD COLUMN status TEXT", "ALTER TABLE router_work_orders ADD COLUMN order_uid TEXT","ALTER TABLE router_work_orders ADD COLUMN opened_at TEXT","ALTER TABLE router_work_orders ADD COLUMN closed_at TEXT",
 "UPDATE router_work_orders SET status='closed',order_uid='u',opened_at='2026-01-01',closed_at='2026-01-02'",
 "CREATE TABLE router_work_order_parts(id INTEGER,order_number INTEGER,order_uid TEXT,ordered_at TEXT,name TEXT,store TEXT,part_number TEXT,qty REAL,amount REAL,purchase_amount REAL,purchase_price REAL,markup_percent REAL,manual_markup_override INTEGER,pricing_calc_version INTEGER)",
 "CREATE TABLE work_order_parts(id INTEGER,order_number INTEGER,order_uid TEXT)","CREATE TABLE work_order_part_attachments(part_id INTEGER,invoice_number TEXT)",
 "CREATE TABLE workshop_spare_movements(part_id INTEGER)","CREATE TABLE workshop_inventory_purchases(id INTEGER,material_key TEXT,material_name TEXT,unit TEXT,stock_kind TEXT,purchased_on TEXT,client_price REAL,unit_cost REAL)"})sql(text);
 auto read=[&]{return ReportsPdfSupport::collectPartsPdfGroups("2026-01-01","2026-01-31",&error);};check(read().isEmpty() && error.isEmpty());
 sql("INSERT INTO router_work_order_parts VALUES(21,11,'u','2026-01-01','Synthetic part','Shop','',1,45.25,30.10,30.10,0,0,3)");
 auto rows=read();check(error.isEmpty() && rows.size()==1);auto row=rows.first().first();check(qRound64(row.purchase*100)==3010 && qRound64(row.clientPrice*100)==4525 && qRound64(row.added*100)==1515);
 for(const auto& table:QStringList{"work_order_part_attachments","workshop_spare_movements","workshop_inventory_purchases","settings","router_work_orders"}){sql("ALTER TABLE "+table+" RENAME TO broken_pdf");check(read().isEmpty() && !error.isEmpty());sql("ALTER TABLE broken_pdf RENAME TO "+table);rows=read();check(error.isEmpty() && rows.size()==1);}
 auto admin=[&]{return ReportsPdfExpenseSupport::collectAdminSalaryTotals("2026-01-01","2026-01-31",&error);};
 auto total=admin();check(error.isEmpty() && total.ordersCount==1 && qRound64(total.purchaseTotal*100)==4945 && qRound64(total.clientTotal*100)==7720 && qRound64(total.margin*100)==2775);
 sql("ALTER TABLE outsource_jobs RENAME TO broken_outsource");total=admin();check(!error.isEmpty() && total.purchaseTotal==0 && total.ordersCount==0);sql("ALTER TABLE broken_outsource RENAME TO outsource_jobs");check(qRound64(admin().margin*100)==2775 && error.isEmpty());
 sql("INSERT INTO access_roles VALUES('master',1)");sql("UPDATE staff_accounts SET role='master'");sql("INSERT INTO access_role_permissions VALUES('master','nav.reports',1)");ActivityAuditService::setCurrentActor({"pdf-owner","Synthetic","master","test"});check(read().isEmpty() && !error.isEmpty());
 sql("UPDATE staff_accounts SET role='owner'");ActivityAuditService::setCurrentActor({"pdf-owner","Synthetic","owner","test"});check(!read().isEmpty() && error.isEmpty());
 db.close();PartMarkupPolicyService::loadPolicySnapshot(db,&error);check(!error.isEmpty() && !db.isOpen());
 qInfo()<<"PASS production parts PDF reader: empty, exact30.10/45.25/15.15, five missing dependencies, repeat, restricted scope refused; closed pricing handle stays closed";
}
