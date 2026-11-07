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
 sql("CREATE TABLE historical_adjustments(id INTEGER PRIMARY KEY,order_uid TEXT,cents INTEGER,note BLOB)");
 sql("INSERT INTO historical_adjustments VALUES(1,'old-order',7250,X'0002FF')");
 sql("ATTACH DATABASE '"+dir.filePath("workspace.db")+"' AS archive_rollover");
 for(const auto& table:QStringList{"work_orders","work_order_services","work_order_parts","service_masters","work_order_part_attachments"}) {
  sql("SELECT sql FROM main.sqlite_master WHERE name='"+table+"'");check(q.next());const auto ddl=q.value(0).toString();q.finish();
  sql("CREATE TABLE archive_rollover."+table+ddl.mid(ddl.indexOf('(')));
 }
 sql("CREATE TABLE archive_rollover.workspace_order_state(order_number INTEGER PRIMARY KEY,order_uid TEXT,origin TEXT,source_db TEXT)");
 sql("INSERT INTO archive_rollover.work_orders VALUES(7,'current-order',3,2024)");
 sql("CREATE TABLE archive_rollover.masters(id TEXT PRIMARY KEY,name TEXT)");
 sql("INSERT INTO archive_rollover.masters VALUES('m1','Existing master')");
 sql("ALTER TABLE work_order_parts ADD COLUMN historic_note BLOB");
 sql("UPDATE work_order_parts SET historic_note=X'AB00CD'");
 auto bytes=[](const QString& path){QFile file(path);if(!file.open(QIODevice::ReadOnly))return QByteArray();return file.readAll();};
 const auto original=bytes(dir.filePath("old.db"));check(!original.isEmpty());
 check(MigrationArchiveWorkspace::merge(dir.filePath("old.db"),dir.filePath("workspace.db"),&error));
 check(bytes(dir.filePath("old.db"))==original);
 sql("SELECT p.order_uid,p.part_uid,s.service_uid,p.amount,s.amount,hex(a.payload) FROM archive_rollover.work_order_parts p JOIN archive_rollover.work_order_services s ON s.id=p.service_id JOIN archive_rollover.work_order_part_attachments a ON a.part_id=p.id");
 check(q.next() && q.value(0)=="old-order" && q.value(1)=="old-part" && q.value(2)=="old-service" && q.value(3).toInt()==3850 && q.value(4).toInt()==1250 && q.value(5)=="0001FF");q.finish();
 sql("SELECT cents,hex(note) FROM archive_rollover.historical_adjustments WHERE order_uid='old-order'");check(q.next() && q.value(0).toInt()==7250 && q.value(1)=="0002FF");q.finish();
 sql("SELECT hex(historic_note) FROM archive_rollover.work_order_parts WHERE part_uid='old-part'");check(q.next() && q.value(0)=="AB00CD");q.finish();
 // Same parent and same physical child key/fields are NOT evidence of a UID alias.
 sql("INSERT INTO main.work_orders VALUES(3,'earlier-history',4,2022)");
 const auto ambiguousOriginal=bytes(dir.filePath("old.db"));
 sql("UPDATE archive_rollover.work_order_parts SET part_uid='current-part' WHERE id=60");
 for(int retry=0;retry<2;++retry) {
  check(!MigrationArchiveWorkspace::merge(dir.filePath("old.db"),dir.filePath("workspace.db"),&error));
  check(error.contains("Historical child ambiguity"));
 }
 sql("SELECT part_uid,amount FROM archive_rollover.work_order_parts WHERE id=60");check(q.next() && q.value(0)=="current-part" && q.value(1).toInt()==3850);q.finish();
 check(bytes(dir.filePath("old.db"))==ambiguousOriginal);
 sql("SELECT COUNT(*) FROM archive_rollover.work_orders WHERE number=3");check(q.next() && q.value(0).toInt()==0);q.finish();
 sql("SELECT COUNT(*) FROM archive_rollover.workspace_order_state WHERE order_number=3");check(q.next() && q.value(0).toInt()==0);q.finish();
 sql("DELETE FROM main.work_orders WHERE number=3");
 sql("UPDATE archive_rollover.work_order_parts SET part_uid='old-part' WHERE id=60");error.clear();
 // A known stable UID with conflicting payment evidence also needs a decision.
 sql("ALTER TABLE main.work_order_parts ADD COLUMN paid INTEGER DEFAULT 0");
 sql("ALTER TABLE archive_rollover.work_order_parts ADD COLUMN paid INTEGER DEFAULT 0");
 sql("UPDATE archive_rollover.work_order_parts SET paid=1 WHERE id=60");
 check(!MigrationArchiveWorkspace::merge(dir.filePath("old.db"),dir.filePath("workspace.db"),&error));check(error.contains("paid value differs"));
 sql("SELECT paid,amount FROM archive_rollover.work_order_parts WHERE id=60");check(q.next() && q.value(0).toInt()==1 && q.value(1).toInt()==3850);q.finish();
 sql("UPDATE archive_rollover.work_order_parts SET paid=0 WHERE id=60");error.clear();
 // Different physical service keys with the SAME stable UID are valid.
 sql("UPDATE archive_rollover.work_order_services SET id=150 WHERE id=50");
 sql("UPDATE archive_rollover.work_order_parts SET service_id=150 WHERE id=60");
 sql("UPDATE archive_rollover.service_masters SET service_id=150 WHERE service_id=50");
 check(MigrationArchiveWorkspace::merge(dir.filePath("old.db"),dir.filePath("workspace.db"),&error));
 sql("UPDATE archive_rollover.work_order_services SET service_uid='unrelated-service' WHERE id=150");
 check(!MigrationArchiveWorkspace::merge(dir.filePath("old.db"),dir.filePath("workspace.db"),&error));check(error.contains("Historical child ambiguity"));
 sql("UPDATE archive_rollover.work_order_services SET service_uid='old-service' WHERE id=150");
 sql("INSERT INTO archive_rollover.work_order_services VALUES(151,40,'old-order','second-current-service',0)");
 sql("UPDATE archive_rollover.work_order_parts SET service_id=151 WHERE id=60");
 check(!MigrationArchiveWorkspace::merge(dir.filePath("old.db"),dir.filePath("workspace.db"),&error));check(error.contains("stable service UID"));
 sql("UPDATE archive_rollover.work_order_parts SET service_id=50 WHERE id=60");
 sql("DELETE FROM archive_rollover.work_order_services WHERE id=151");
 sql("UPDATE archive_rollover.work_order_services SET id=50 WHERE id=150");
 sql("UPDATE archive_rollover.service_masters SET service_id=50 WHERE service_id=150");error.clear();
 sql("UPDATE archive_rollover.historical_adjustments SET cents=9999");
 check(!MigrationArchiveWorkspace::merge(dir.filePath("old.db"),dir.filePath("workspace.db"),&error));check(error.contains("supplemental history"));
 sql("SELECT cents FROM archive_rollover.historical_adjustments");check(q.next() && q.value(0).toInt()==9999);q.finish();
 sql("UPDATE archive_rollover.historical_adjustments SET cents=7250");error.clear();
 sql("INSERT INTO main.work_order_parts VALUES(61,999,'missing-order','orphan',NULL,100,NULL,0)");
 check(!MigrationArchiveWorkspace::merge(dir.filePath("old.db"),dir.filePath("workspace.db"),&error));check(error.contains("Orphan"));
 sql("DELETE FROM main.work_order_parts WHERE id=61");error.clear();
 sql("ALTER TABLE main.work_order_parts ADD COLUMN not_in_workspace TEXT");
 check(!MigrationArchiveWorkspace::merge(dir.filePath("old.db"),dir.filePath("workspace.db"),&error));check(error.contains("Unrepresented historical field"));
 sql("ALTER TABLE main.work_order_parts DROP COLUMN not_in_workspace");error.clear();
 sql("UPDATE historical_adjustments SET order_uid='missing-owner'");
 check(!MigrationArchiveWorkspace::merge(dir.filePath("old.db"),dir.filePath("workspace.db"),&error));check(error.contains("unresolved owner"));
 sql("UPDATE historical_adjustments SET order_uid='old-order'");error.clear();
 sql("UPDATE main.work_order_parts SET service_id=999 WHERE id=60");
 check(!MigrationArchiveWorkspace::merge(dir.filePath("old.db"),dir.filePath("workspace.db"),&error));check(error.contains("Conflicting service owner"));
 sql("UPDATE main.work_order_parts SET service_id=50 WHERE id=60");error.clear();
 sql("CREATE TABLE main.order_registry(order_number INTEGER,order_uid TEXT,visible_order_number INTEGER,order_year INTEGER)");
 sql("INSERT INTO main.order_registry VALUES(40,'wrong-uid',3,2023)");
 check(!MigrationArchiveWorkspace::merge(dir.filePath("old.db"),dir.filePath("workspace.db"),&error));check(error.contains("registry identity conflicts"));
 sql("UPDATE main.order_registry SET order_uid='old-order'");error.clear();
 // Bounded schema-review boundary: refuse a partial copy that would omit
 // an explicit index, then prove the caller-owned source can be reconciled
 // after the fixture's reviewed schema change. No production gate is relaxed.
 sql("CREATE TABLE main.historical_indexed(order_uid TEXT,cents INTEGER)");
 sql("INSERT INTO main.historical_indexed VALUES('old-order',1234)");
 sql("CREATE INDEX main.historical_lookup ON historical_indexed(order_uid)");
 check(!MigrationArchiveWorkspace::merge(dir.filePath("old.db"),dir.filePath("workspace.db"),&error));check(error.contains("schema requiring review"));
 sql("SELECT COUNT(*) FROM archive_rollover.sqlite_master WHERE name='historical_indexed'");check(q.next() && q.value(0).toInt()==0);q.finish();
 sql("SELECT cents FROM main.historical_indexed");check(q.next() && q.value(0).toInt()==1234);q.finish();
 sql("DROP INDEX main.historical_lookup");error.clear();
 check(MigrationArchiveWorkspace::merge(dir.filePath("old.db"),dir.filePath("workspace.db"),&error));
 sql("SELECT cents FROM archive_rollover.historical_indexed WHERE order_uid='old-order'");check(q.next() && q.value(0).toInt()==1234);q.finish();
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
 qInfo()<<"PASS: exact archived graph materialization, child UID/cents/blob, repeat retains newer edits, identity conflict refused; supplemental history/unknown fields preserved, conflicting history and orphans refused";
}
