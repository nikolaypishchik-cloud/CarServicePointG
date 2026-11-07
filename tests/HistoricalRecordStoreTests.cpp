#include "core/services/legacyimport/MigrationArchiveWorkspace.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDebug>
int main(int argc,char** argv) {
 QCoreApplication app(argc,argv);QTemporaryDir dir;QString error;
 auto check=[&](bool ok){if(!ok)qFatal("Historical versions: %s",qPrintable(error));};
 auto db=QSqlDatabase::addDatabase("QSQLITE","setup");db.setDatabaseName(dir.filePath("old.db"));check(db.open());QSqlQuery q(db);
 auto sql=[&](const QString& text){if(!q.exec(text)){error=q.lastError().text();check(false);}};
 sql("CREATE TABLE work_orders(number INTEGER PRIMARY KEY,order_uid TEXT,order_number INTEGER,order_year INTEGER)");
 sql("INSERT INTO work_orders VALUES(40,'order-a',3,2023)");
 sql("CREATE TABLE work_order_services(id INTEGER PRIMARY KEY,order_number INTEGER,order_uid TEXT,service_uid TEXT,amount INTEGER,paid INTEGER)");
 sql("INSERT INTO work_order_services VALUES(50,40,'order-a','old-service',1250,1)");
 sql("CREATE TABLE work_order_parts(id INTEGER PRIMARY KEY,order_number INTEGER,order_uid TEXT,part_uid TEXT,service_id INTEGER,amount INTEGER,paid INTEGER,note BLOB)");
 sql("INSERT INTO work_order_parts VALUES(60,40,'order-a','old-part',50,3850,0,X'AB00FF')");
 sql("CREATE TABLE service_masters(service_id INTEGER,master_id TEXT)");
 sql("CREATE TABLE parts_catalog(id INTEGER PRIMARY KEY,part_id INTEGER,name TEXT,price REAL)");
 sql("INSERT INTO parts_catalog VALUES(1,60,'Historical catalogue',38.50)");
 sql("CREATE TABLE historical_adjustments(order_uid TEXT,cents INTEGER)");
 sql("INSERT INTO historical_adjustments VALUES('order-a',123),('order-a',123)");
 sql("ATTACH DATABASE '"+dir.filePath("current.db")+"' AS archive_rollover");
 for(const auto& table:QStringList{"work_orders","work_order_services","work_order_parts","service_masters","parts_catalog"}) {
  sql("SELECT sql FROM main.sqlite_master WHERE name='"+table+"'");check(q.next());const auto ddl=q.value(0).toString();q.finish();
  sql("CREATE TABLE archive_rollover."+table+ddl.mid(ddl.indexOf('(')));
  sql("INSERT INTO archive_rollover."+table+" SELECT * FROM main."+table);
 }
 sql("CREATE TABLE archive_rollover.workspace_order_state(order_number INTEGER PRIMARY KEY,order_uid TEXT,origin TEXT,source_db TEXT)");
 sql("CREATE TABLE archive_rollover.masters(id TEXT PRIMARY KEY,name TEXT)");
 sql("UPDATE archive_rollover.work_order_services SET service_uid='current-service',amount=1500");
 sql("UPDATE archive_rollover.work_order_parts SET part_uid='current-part',amount=4000,paid=1");
 sql("UPDATE archive_rollover.parts_catalog SET name='Current catalogue',price=40");
 const auto sourceDigest=HistoricalRecordStore::digestFile(dir.filePath("old.db"));
 for(int repeat=0;repeat<2;++repeat)check(MigrationArchiveWorkspace::merge(dir.filePath("old.db"),dir.filePath("current.db"),&error,nullptr,"source-A"));
 check(HistoricalRecordStore::digestFile(dir.filePath("old.db"))==sourceDigest);
 auto view=QSqlDatabase::addDatabase("QSQLITE","view");view.setDatabaseName(dir.filePath("current.db"));view.setConnectOptions("QSQLITE_OPEN_READONLY");check(view.open());
 QList<HistoricalRecordStore::Source> sources;check(HistoricalRecordStore::sources(view,"order-a",&sources,&error));check(sources.size()==1 && sources[0].origin=="source-A");const auto first=sources[0].id;
 QList<HistoricalRecordStore::Record> rows;
 auto field=[](const HistoricalRecordStore::Record& row,const QString& name){return row.values.value(row.columns.indexOf(name));};
 check(HistoricalRecordStore::read(view,first,"work_order_parts","order-a","old-part",0,100,&rows,&error));
 check(rows.size()==1 && field(rows[0],"amount").toInt()==3850 && field(rows[0],"paid").toInt()==0 && field(rows[0],"note").toByteArray()==QByteArray::fromHex("AB00FF"));
 check(HistoricalRecordStore::read(view,first,"parts_catalog",{}, {},0,100,&rows,&error));check(rows.size()==1 && field(rows[0],"name")=="Historical catalogue" && field(rows[0],"price").toDouble()==38.5 && rows[0].recordUid.isEmpty());
 check(HistoricalRecordStore::read(view,first,"historical_adjustments","order-a",{},0,100,&rows,&error));check(rows.size()==2 && rows[0].rowKey!=rows[1].rowKey);
 sql("SELECT part_uid,paid,SUM(amount) FROM archive_rollover.work_order_parts");check(q.next() && q.value(0)=="current-part" && q.value(1).toInt()==1 && q.value(2).toInt()==4000);q.finish();
 sql("SELECT name,price FROM archive_rollover.parts_catalog");check(q.next() && q.value(0)=="Current catalogue" && q.value(1).toDouble()==40);q.finish();
 // Equal data from a different origin is not a UID alias or the same source version.
 check(MigrationArchiveWorkspace::merge(dir.filePath("old.db"),dir.filePath("current.db"),&error,nullptr,"source-B"));
 check(HistoricalRecordStore::sources(view,"order-a",&sources,&error));check(sources.size()==2);
 // Preserve successive source versions without rewriting the earlier payment value.
 sql("UPDATE main.work_order_parts SET paid=1");
 check(MigrationArchiveWorkspace::merge(dir.filePath("old.db"),dir.filePath("current.db"),&error,nullptr,"source-A"));
 check(HistoricalRecordStore::sources(view,"order-a",&sources,&error));check(sources.size()==3);
 check(HistoricalRecordStore::read(view,first,"work_order_parts","order-a","old-part",0,100,&rows,&error));check(rows.size()==1 && field(rows[0],"paid").toInt()==0);
 // Validation failure rolls back the newly captured version as well as graph changes.
 sql("INSERT INTO main.work_order_parts VALUES(61,999,'missing-owner','orphan',50,100,0,NULL)");
 check(!MigrationArchiveWorkspace::merge(dir.filePath("old.db"),dir.filePath("current.db"),&error,nullptr,"source-C"));
 check(HistoricalRecordStore::sources(view,"order-a",&sources,&error));check(sources.size()==3);
 sql("DELETE FROM main.work_order_parts WHERE id=61");error.clear();
 // Ordinary reader restart retains both source-scoped child and catalogue data.
 view.close();check(view.open());
 check(HistoricalRecordStore::read(view,first,"work_order_parts","order-a","old-part",0,100,&rows,&error));check(rows.size()==1 && field(rows[0],"amount").toInt()==3850);
 // A subsequent migration inherits source versions, without re-keying old UIDs.
 const auto next=dir.filePath("next.db");
 {auto fresh=QSqlDatabase::addDatabase("QSQLITE","next");fresh.setDatabaseName(next);check(fresh.open());fresh.close();}
 check(HistoricalRecordStore::captureFile(dir.filePath("current.db"),next,"next-profile",&error));
 check(HistoricalRecordStore::captureFile(dir.filePath("current.db"),next,"next-profile",&error));
 {auto inherited=QSqlDatabase::database("next");check(inherited.open());
  check(HistoricalRecordStore::read(inherited,first,"work_order_parts","order-a","old-part",0,100,&rows,&error));
  check(rows.size()==1 && field(rows[0],"paid").toInt()==0 && field(rows[0],"amount").toInt()==3850);
  check(HistoricalRecordStore::sources(inherited,"order-a",&sources,&error));check(sources.size()==4);inherited.close();}
 // Invalid UTF-8 cannot silently become replacement characters in a version.
 sql("INSERT INTO main.historical_adjustments VALUES(CAST(X'FF' AS TEXT),4)");
 check(!HistoricalRecordStore::captureFile(dir.filePath("old.db"),dir.filePath("current.db"),"invalid-text",&error));
 check(error.contains("losslessly"));sql("DELETE FROM main.historical_adjustments WHERE cents=4");error.clear();
 check(HistoricalRecordStore::sources(view,"order-a",&sources,&error));check(sources.size()==3);
 // Corruption must not become a partial successful result or silent repair.
 sql("UPDATE archive_rollover.historical_records SET payload=X'00' WHERE source_id='"+first+"' AND table_name='work_order_parts'");
 check(!HistoricalRecordStore::read(view,first,"work_order_parts","order-a","old-part",0,100,&rows,&error));check(rows.isEmpty() && error.contains("fingerprint"));
 check(!HistoricalRecordStore::captureFile(dir.filePath("current.db"),next,"corrupt-profile",&error));
 {auto inherited=QSqlDatabase::database("next");check(inherited.open());check(HistoricalRecordStore::sources(inherited,"order-a",&sources,&error));check(sources.size()==4);inherited.close();}
 sql("SELECT COUNT(*),SUM(amount) FROM archive_rollover.work_order_parts");check(q.next() && q.value(0).toInt()==1 && q.value(1).toInt()==4000);q.finish();
 qInfo()<<"PASS: independent child/catalogue versions; source provenance; repeat; typed blobs and duplicate rows; rollback; reader restart; tamper refusal; current amounts unchanged";
}
