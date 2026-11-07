#include "core/services/legacyimport/MigrationArchiveWorkspace.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDebug>
#include <QFile>
int main(int argc,char** argv) {
 QCoreApplication app(argc,argv);QTemporaryDir dir;QString error;
 auto check=[&](bool ok){if(!ok)qFatal("Archive workspace: %s",qPrintable(error));};
 auto db=QSqlDatabase::addDatabase("QSQLITE");db.setDatabaseName(dir.filePath("old.db"));check(db.open());QSqlQuery q(db);
 auto sql=[&](const QString& s){if(!q.exec(s)){error=q.lastError().text();check(false);}};
 sql("CREATE TABLE work_orders(number INTEGER PRIMARY KEY,order_uid TEXT,order_number INTEGER,order_year INTEGER)");
 sql("INSERT INTO work_orders VALUES(40,'old-order',3,2023)");
 sql("CREATE TABLE work_order_services(id INTEGER PRIMARY KEY,order_number INTEGER,order_uid TEXT,service_uid TEXT,amount INTEGER)");
 sql("INSERT INTO work_order_services VALUES(50,40,'old-order','old-service',1250)");
 sql("CREATE TABLE work_order_parts(id INTEGER PRIMARY KEY,order_number INTEGER,order_uid TEXT,part_uid TEXT,service_id INTEGER,amount INTEGER)");
 sql("INSERT INTO work_order_parts VALUES(60,40,'old-order','old-part',50,3850)");
 sql("CREATE TABLE service_masters(service_id INTEGER,master_id TEXT)");
 sql("INSERT INTO service_masters VALUES(50,'m1')");
 sql("CREATE TABLE work_order_part_attachments(part_id INTEGER,payload BLOB)");
 sql("INSERT INTO work_order_part_attachments VALUES(60,X'0001FF')");
 sql("ATTACH DATABASE '"+dir.filePath("workspace.db")+"' AS archive_rollover");
 for(const auto& table:QStringList{"work_orders","work_order_services","work_order_parts","service_masters","work_order_part_attachments"}) {
  sql("SELECT sql FROM main.sqlite_master WHERE name='"+table+"'");check(q.next());const auto ddl=q.value(0).toString();q.finish();
  sql("CREATE TABLE archive_rollover."+table+ddl.mid(ddl.indexOf('(')));
 }
 sql("CREATE TABLE archive_rollover.workspace_order_state(order_number INTEGER PRIMARY KEY,order_uid TEXT,origin TEXT,source_db TEXT)");
 sql("INSERT INTO archive_rollover.work_orders VALUES(7,'current-order',3,2024)");
 sql("CREATE TABLE archive_rollover.masters(id TEXT PRIMARY KEY,name TEXT)");
 sql("INSERT INTO archive_rollover.masters VALUES('m1','Existing master')");
 auto bytes=[](const QString& path){QFile file(path);if(!file.open(QIODevice::ReadOnly))return QByteArray();return file.readAll();};
 const auto original=bytes(dir.filePath("old.db"));check(!original.isEmpty());
 check(MigrationArchiveWorkspace::merge(dir.filePath("old.db"),dir.filePath("workspace.db"),&error));
 check(bytes(dir.filePath("old.db"))==original);
 sql("SELECT p.order_uid,p.part_uid,s.service_uid,p.amount,s.amount,hex(a.payload) FROM archive_rollover.work_order_parts p JOIN archive_rollover.work_order_services s ON s.id=p.service_id JOIN archive_rollover.work_order_part_attachments a ON a.part_id=p.id");
 check(q.next() && q.value(0)=="old-order" && q.value(1)=="old-part" && q.value(2)=="old-service" && q.value(3).toInt()==3850 && q.value(4).toInt()==1250 && q.value(5)=="0001FF");q.finish();
 // A normal subsequent staging/reopen must preserve newer hydrated edits.
 sql("UPDATE archive_rollover.work_order_parts SET amount=4000");
 check(MigrationArchiveWorkspace::merge(dir.filePath("old.db"),dir.filePath("workspace.db"),&error));
 sql("SELECT COUNT(*),SUM(amount) FROM archive_rollover.work_order_parts");check(q.next() && q.value(0).toInt()==1 && q.value(1).toInt()==4000);q.finish();
 // Conflicting historical copies may not silently choose a winner by path order.
 QSet<QString> imported{"old-order"};
 check(!MigrationArchiveWorkspace::merge(dir.filePath("old.db"),dir.filePath("workspace.db"),&error,&imported));
 check(error.contains("differ"));error.clear();
 // A later order fails on a child key: the earlier header insert must roll back.
 sql("INSERT INTO work_orders VALUES(41,'second-old',4,2023)");
 sql("INSERT INTO work_order_services VALUES(51,41,'second-old','second-service',100)");
 sql("INSERT INTO archive_rollover.work_order_services VALUES(51,7,'current-order','occupied-service',200)");
 check(!MigrationArchiveWorkspace::merge(dir.filePath("old.db"),dir.filePath("workspace.db"),&error));
 sql("SELECT COUNT(*) FROM archive_rollover.work_orders WHERE number=41");check(q.next() && q.value(0).toInt()==0);q.finish();
 sql("DELETE FROM work_order_services WHERE id=51");sql("DELETE FROM work_orders WHERE number=41");
 sql("UPDATE archive_rollover.work_orders SET order_uid='wrong-owner' WHERE number=40");
 check(!MigrationArchiveWorkspace::merge(dir.filePath("old.db"),dir.filePath("workspace.db"),&error));check(error.contains("identity conflicts"));
 qInfo()<<"PASS: exact archived graph materialization, child UID/cents/blob, repeat retains newer edits, identity conflict refused";
}
