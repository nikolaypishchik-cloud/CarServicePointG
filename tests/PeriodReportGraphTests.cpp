#include "core/services/PeriodReportRows.h"
#include <QCoreApplication>
#include <QSqlDriver>
#include <QLibrary>
#include <QDebug>
struct sqlite3;
using TraceFn=int(*)(sqlite3*,unsigned,int(*)(unsigned,void*,void*,void*),void*);
int main(int argc,char** argv) {
 QCoreApplication app(argc,argv);QString error;
 auto check=[&](bool ok){if(!ok)qFatal("Period graph: %s",qPrintable(error));};
 auto db=QSqlDatabase::addDatabase("QSQLITE","period-test");db.setDatabaseName(":memory:");check(db.open());QSqlQuery q(db);
 auto sql=[&](const QString& text){if(!q.exec(text)){error=q.lastError().text();check(false);}};
 sql("CREATE TABLE router_work_orders(number INTEGER,order_number INTEGER,order_uid TEXT,car TEXT,plate TEXT,opened_at TEXT,closed_at TEXT,works_sum REAL)");
 sql("CREATE TABLE router_work_order_services(id INTEGER,title TEXT,amount REAL,order_uid TEXT,service_uid TEXT,order_number INTEGER)");
 sql("CREATE TABLE router_work_order_parts(id INTEGER,name TEXT,store TEXT,qty REAL,amount REAL,purchase_amount REAL,purchase_price REAL,markup_percent REAL,manual_markup_override INTEGER,service_id INTEGER,pricing_calc_version INTEGER,order_uid TEXT,part_uid TEXT,order_number INTEGER)");
 auto handle=db.driver()->handle();check(QByteArray(handle.typeName())=="sqlite3*");auto* sqlite=*static_cast<sqlite3**>(handle.data());
 QLibrary library("libsqlite3.so.0");auto trace=reinterpret_cast<TraceFn>(library.resolve("sqlite3_trace_v2"));check(trace!=nullptr);
 int statements=0;auto count=[](unsigned,void* context,void*,void*){++*static_cast<int*>(context);return 0;};
 auto read=[&](PeriodReportGraph::Graph* graph){statements=0;check(trace(sqlite,1,count,&statements)==0);const bool ok=PeriodReportGraph::read(db,"2026-01-01","2026-01-31",graph,&error);check(trace(sqlite,0,nullptr,nullptr)==0);return ok;};
 PeriodReportGraph::Graph graph;check(read(&graph));check(graph.orders.isEmpty() && statements==1);
 for(const int size:{10,50}) {
  sql("DELETE FROM router_work_orders");sql("DELETE FROM router_work_order_services");sql("DELETE FROM router_work_order_parts");
  for(int i=1;i<=size;++i) {
   sql(QString("INSERT INTO router_work_orders VALUES(%1,7,'order-%1','Car %1','','2026-01-01','2026-01-02',100)").arg(i));
   // Physical keys deliberately collide with another owner's keys; UID scopes the details.
   sql(QString("INSERT INTO router_work_order_services VALUES(1,'Service',100,'order-%1','service-%1',777)").arg(i));
   sql(QString("INSERT INTO router_work_order_parts VALUES(1,'Part','Shop',1,45.25,30.10,30.10,0,0,1,3,'order-%1','part-%1',777)").arg(i));
  }
  sql("INSERT INTO router_work_orders VALUES(1,7,'outside','Outside','','2025-01-01','2025-01-02',999)");
  sql("INSERT INTO router_work_order_services VALUES(9,'Wrong owner',999,'outside','outside-service',1)");
  sql("SELECT title FROM router_work_order_services WHERE order_number=1 ORDER BY id ASC");check(q.next() && q.value(0).toString()=="Wrong owner");q.finish();
  check(read(&graph));check(statements==3 && graph.orders.size()==size);
  const auto rows=PeriodReportRows::build(graph,{});check(rows.rows.size()==size);check(qRound64(rows.parts*100)==size*4525 && qRound64(rows.works*100)==size*10000 && qRound64(rows.margin*100)==size*1515);
  for(int i=0;i<size;++i)check(rows.rows[i].car==QString("Car %1").arg(i+1) && rows.rows[i].workTitle=="Service" && rows.rows[i].partName=="Part" && rows.rows[i].orderNumber=="7" && qRound64(rows.rows[i].total*100)==14525);
  check(read(&graph));check(statements==3);qInfo()<<"PASS orders="<<size<<"actual SQLite statements="<<statements;
 }
 sql("ALTER TABLE router_work_order_parts RENAME COLUMN amount TO broken_amount");check(!read(&graph));check(graph.orders.isEmpty() && !error.isEmpty());
 sql("ALTER TABLE router_work_order_parts RENAME COLUMN broken_amount TO amount");check(read(&graph));check(graph.orders.size()==50);
 sql("UPDATE router_work_order_parts SET order_uid='' WHERE part_uid='part-1'");sql("UPDATE router_work_order_parts SET order_number=1 WHERE part_uid='part-1'");check(!read(&graph));check(graph.orders.isEmpty());
 sql("UPDATE router_work_order_parts SET order_uid='order-1' WHERE part_uid='part-1'");check(read(&graph));
 sql("UPDATE router_work_order_parts SET part_uid='part-1' WHERE part_uid='part-2'");check(!read(&graph));check(graph.orders.isEmpty());
 // Preserve service-first layout, first-part work charge, leftovers, and a header without children.
 sql("DELETE FROM router_work_orders");sql("DELETE FROM router_work_order_services");sql("DELETE FROM router_work_order_parts");
 sql("INSERT INTO router_work_orders VALUES(1,7,'layout','Layout','','2026-01-01','2026-01-02',30)");
 check(read(&graph));auto noChildren=PeriodReportRows::build(graph,{});check(noChildren.rows.size()==1 && noChildren.parts==0 && noChildren.works==0);
 sql("INSERT INTO router_work_order_services VALUES(20,'Second',20,'layout','s20',1),(10,'First',10,'layout','s10',1)");
 sql("INSERT INTO router_work_order_parts VALUES(2,'B','Shop',1,2.02,0,0,0,0,20,3,'layout','p2',1),(1,'A','Shop',1,1.01,0,0,0,0,20,3,'layout','p1',1),(3,'C','Shop',1,3.03,0,0,0,0,0,3,'layout','p3',1)");
 check(read(&graph));const auto layout=PeriodReportRows::build(graph,{});check(layout.rows.size()==4 && qRound64(layout.parts*100)==606 && qRound64(layout.works*100)==3000);
 check(layout.rows[0].workTitle=="First" && layout.rows[0].partName.isEmpty());
 check(layout.rows[1].workTitle=="Second" && layout.rows[1].partName=="A" && qRound64(layout.rows[1].total*100)==2101);
 check(layout.rows[2].workTitle.isEmpty() && layout.rows[2].partName=="B" && layout.rows[2].worksPrice==0);
 check(layout.rows[3].partName=="C" && layout.rows[3].workTitle.isEmpty());
 qInfo()<<"PASS: UID scope, exact ordering/cents, selected-only data, empty success, SQL failure, repair repeat, missing/duplicate UID refusal, constant actual SQL count";
}
