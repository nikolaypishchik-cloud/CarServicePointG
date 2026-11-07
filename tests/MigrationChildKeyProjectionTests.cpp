// Focused executable for the actual staging planner/projection on Linux and Windows.
#include "infrastructure/db/sqlite/MigrationOrderKeys.h"
#include "infrastructure/db/sqlite/DbCanonicalRewritePrimaryImportSupport.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDebug>

int main(int argc,char** argv) {
 QCoreApplication app(argc,argv); QTemporaryDir dir; QString error;
 auto db=QSqlDatabase::addDatabase("QSQLITE"); db.setDatabaseName(dir.filePath("target.db"));
 auto check=[&](bool ok){if(!ok) qFatal("Projection check failed: %s",qPrintable(error));};
 check(dir.isValid() && db.open()); QSqlQuery q(db);
 auto sql=[&](const QString& text){if(!q.exec(text)){error=q.lastError().text();check(false);}};
 sql("CREATE TABLE work_orders(number INTEGER PRIMARY KEY)");
 sql("INSERT INTO work_orders VALUES(7)");
 for(const auto& table:QStringList{"work_order_services","work_order_parts"}) {
  sql("CREATE TABLE "+table+"(id INTEGER PRIMARY KEY)"); sql("INSERT INTO "+table+" VALUES(20),(21)");
 }
 sql("ATTACH DATABASE '"+dir.filePath("source.db")+"' AS source");
 sql("CREATE TABLE source.work_orders(number INTEGER PRIMARY KEY,order_number INTEGER,order_uid TEXT)");
 sql("INSERT INTO source.work_orders VALUES(7,3,'source-order')");
 sql("CREATE TABLE source.work_order_services(id INTEGER PRIMARY KEY,order_number INTEGER,order_uid TEXT,service_uid TEXT,parent_id INTEGER,amount INTEGER)");
 sql("INSERT INTO source.work_order_services VALUES(20,7,'source-order','service-a',0,125),(21,7,'source-order','service-b',20,250)");
 sql("CREATE TABLE source.work_order_parts(id INTEGER PRIMARY KEY,order_number INTEGER,order_uid TEXT,part_uid TEXT,service_id INTEGER,amount INTEGER)");
 sql("INSERT INTO source.work_order_parts VALUES(21,7,'source-order','part-a',20,3850)");
 sql("CREATE TABLE source.service_masters(service_id INTEGER,master_id TEXT)");
 sql("INSERT INTO source.service_masters VALUES(20,'master-a')");
 for(const auto& table:QStringList{"work_orders","work_order_services","work_order_parts"}) {
  sql("DROP TABLE main."+table);sql("CREATE TABLE main."+table+" AS SELECT * FROM source."+table+" WHERE 0");
 }
 sql("INSERT INTO main.work_orders VALUES(7,9,'target-order')");
 sql("INSERT INTO main.work_order_services VALUES(20,7,'target-order','target-a',0,500),(21,7,'target-order','target-b',20,600)");
 sql("INSERT INTO main.work_order_parts VALUES(20,7,'target-order','target-part-a',20,700),(21,7,'target-order','target-part-b',21,800)");
 MigrationOrderKeys::Map keys,loaded;
 check(MigrationOrderKeys::plan(db,"source",&keys,&error));
 check(keys.children.value("work_order_services").size()==2 && keys.children.value("work_order_parts").size()==1);
 check(db.transaction());check(MigrationOrderKeys::store(db,"synthetic",keys,&error));check(db.commit());
 check(MigrationOrderKeys::load(db,"synthetic",&loaded,&error));
 check(loaded==keys && loaded.children==keys.children);
 sql("DETACH DATABASE source");
 for(int attempt=0;attempt<2;++attempt) {
  const auto output=dir.filePath(QString("projection%1.db").arg(attempt));
  check(MigrationOrderKeys::project(dir.filePath("source.db"),output,loaded,&error));
  sql("ATTACH DATABASE '"+output+"' AS projected");
  sql("SELECT p.order_uid,s.order_uid,p.part_uid,s.service_uid,p.amount,s.amount,p.order_number,s.order_number,p.id,s.id FROM projected.work_order_parts p JOIN projected.work_order_services s ON s.id=p.service_id");
  check(q.next());check(q.value(0)=="source-order" && q.value(1)=="source-order" && q.value(2)=="part-a" && q.value(3)=="service-a");
  check(q.value(4).toInt()==3850 && q.value(5).toInt()==125 && q.value(6).toInt()==8 && q.value(7).toInt()==8);
  check(q.value(8).toInt()==22 && q.value(9).toInt()==22);check(!q.next());q.finish();
  sql("SELECT COUNT(*) FROM projected.work_order_services child JOIN projected.work_order_services parent ON parent.id=child.parent_id JOIN projected.service_masters m ON m.service_id=parent.id WHERE child.service_uid='service-b' AND parent.service_uid='service-a'");check(q.next() && q.value(0).toInt()==1);q.finish();
  sql("DETACH DATABASE projected");
 }
 sql("ATTACH DATABASE '"+dir.filePath("projection0.db")+"' AS legacy_rewrite");
 CanonicalRewriteSourceSchema projected,original;
 check(loadCanonicalRewriteSourceSchema(db,"legacy_rewrite",&projected,&error));
 check(db.transaction());check(importCanonicalRewritePrimaryTables(db,projected,&error));check(db.commit());
 sql("ATTACH DATABASE '"+dir.filePath("source.db")+"' AS original");
 check(loadCanonicalRewriteSourceSchema(db,"original",&original,&error));
 original.serviceMasters.clear(); // Ancillary import is outside this focused executable.
 for(int repeat=0;repeat<2;++repeat) {
  check(db.transaction());check(recordCanonicalRewriteCorrespondence(db,"synthetic","original",original,&error,loaded));check(db.commit());
 }
 sql("SELECT COUNT(*) FROM migration_record_map");check(q.next() && q.value(0).toInt()==4);q.finish();
 sql("SELECT p.order_uid,s.service_uid,p.amount,s.amount FROM work_order_parts p JOIN work_order_services s ON s.id=p.service_id WHERE p.part_uid='part-a'");
 check(q.next() && q.value(0)=="source-order" && q.value(1)=="service-a" && q.value(2).toInt()==3850 && q.value(3).toInt()==125);check(!q.next());q.finish();
 sql("SELECT SUM(amount) FROM work_order_parts WHERE order_uid='target-order'");check(q.next() && q.value(0).toInt()==1500);q.finish();
 sql("SELECT COUNT(*) FROM work_order_parts WHERE id IN (20,21)");check(q.next() && q.value(0).toInt()==2);q.finish();
 qInfo()<<"PASS: staging child planner/projection, persisted map, owners/UIDs/amounts/links, repeat projection, destination unchanged; actual primary import and repeated correspondence PASS";
}
