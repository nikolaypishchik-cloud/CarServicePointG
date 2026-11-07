#include "core/services/PartsReportQuery.h"
#include "core/cache/ReportsSnapshotCache.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDebug>
int main(int argc,char** argv) {
 QCoreApplication app(argc,argv);QTemporaryDir dir;QString error;
 auto check=[&](bool ok){if(!ok)qFatal("Parts query: %s",qPrintable(error));};
 auto db=QSqlDatabase::addDatabase("QSQLITE","parts-read");
 const auto missing=dir.filePath("missing.db");check(!PartsReportQuery::openSnapshot(db,missing,&error));check(!QFile::exists(missing));db.close();
 check(!PartsReportQuery::openSnapshot(db,dir.path(),&error));db.close();
 const auto path=dir.filePath("parts.db");db.setConnectOptions({});db.setDatabaseName(path);check(db.open());QSqlQuery q(db);
 auto sql=[&](const QString& text){if(!q.exec(text)){error=q.lastError().text();check(false);}};
 sql("CREATE TABLE router_work_orders(number INTEGER,order_number INTEGER,closed_at TEXT,opened_at TEXT,plate TEXT,car TEXT,vin TEXT)");
 sql("CREATE TABLE router_work_order_parts(id INTEGER,ordered_at TEXT,order_number INTEGER,order_uid TEXT,store TEXT,name TEXT,part_number TEXT,purchase_amount REAL,purchase_price REAL,qty REAL,amount REAL,markup_percent REAL,manual_markup_override INTEGER,pricing_calc_version INTEGER)");
 sql("CREATE TABLE work_order_parts(id INTEGER,order_number INTEGER,order_uid TEXT)");
 // Actual query failure on an empty source: old buildPartsSnapshot ignored this error.
 check(!PartsReportQuery::execute(db,&q,&error));check(error.contains("work_order_part_attachments"));q.finish();
 sql("CREATE TABLE work_order_part_attachments(part_id INTEGER,invoice_number TEXT,file_path TEXT,invoice_date TEXT)");
 check(PartsReportQuery::execute(db,&q,&error));check(!q.next() && !q.lastError().isValid());q.finish();
 sql("INSERT INTO router_work_orders VALUES(1,7,'','2026-01-01','TEST','Synthetic','VIN')");
 sql("INSERT INTO router_work_order_parts VALUES(9,'2026-01-01',1,'uid','Shop','Part','A',30,30,1,45,50,1,1)");
 sql("INSERT INTO work_order_parts VALUES(9,1,'uid')");
 sql("INSERT INTO work_order_part_attachments VALUES(9,'INV','synthetic.pdf','2026-01-01')");
 q.finish();db.close();QFile file(path);check(file.open(QIODevice::ReadOnly));const auto original=file.readAll();file.close();
 for(int i=0;i<2;++i){check(PartsReportQuery::openSnapshot(db,path,&error));check(PartsReportQuery::execute(db,&q,&error));check(q.next());check(q.value(11).toDouble()==30 && q.value(13).toDouble()==45 && q.value(17).toString()=="INV");check(!q.next() && !q.lastError().isValid());q.finish();check(db.commit());db.close();}
 check(file.open(QIODevice::ReadOnly));check(file.readAll()==original);file.close();
 PartsSnapshot good,bad,cached;bad.error="synthetic failure";ReportsSnapshotCache::storeParts("parts-key",good);ReportsSnapshotCache::storeParts("parts-key",bad);check(!ReportsSnapshotCache::tryGetParts("parts-key",&cached));ReportsSnapshotCache::storeParts("parts-key",good);check(ReportsSnapshotCache::tryGetParts("parts-key",&cached));
 qInfo()<<"PASS: actual parts query error on empty source; repaired empty success; populated repeat with exact 30/45 and attachment; readonly no creation/writes; cache failure eviction/retry";
}
