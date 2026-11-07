#include "core/services/PayrollIdentityRead.h"
#include "PreviousPayrollGraph.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDataStream>
#include <QIODevice>
#include <QCryptographicHash>
#include <QDebug>
#include <sys/resource.h>

static bool setup(QSqlDatabase db,int scale) {
    QSqlQuery q(db);auto sql=[&](const QString& s){if(q.exec(s))return true;qWarning()<<q.lastError().text();return false;};
    if(!db.transaction())return false;
    for(const auto& s:QStringList{
        "CREATE TABLE router_work_orders(number INT,closed_at TEXT,status TEXT,discount REAL,discount_mode TEXT,works_sum REAL,order_uid TEXT,order_number INT,opened_at TEXT,plate TEXT,car TEXT,engine_type TEXT,engine_volume TEXT,unused_payload TEXT)",
        "CREATE TABLE router_work_order_services(id INT,order_number INT,amount REAL,order_uid TEXT,service_uid TEXT,title TEXT,unused_payload TEXT)",
        "CREATE TABLE router_service_masters(service_id INT,master_id TEXT,share_percent REAL,service_uid TEXT,order_uid TEXT,unused_payload TEXT)",
        "CREATE TABLE payroll_order_adjustments(master_id TEXT,order_number INT,absent INT,deleted INT,order_uid TEXT,unused_payload TEXT)",
        "CREATE TABLE outsource_jobs(service_id INT,service_uid TEXT,order_uid TEXT,unused_payload TEXT)"})if(!sql(s))return false;
    for(int order=1;order<=200*scale;++order){
        if(!sql(QString("INSERT INTO router_work_orders VALUES(%1,'2026-01-15','closed',10,'percent',123.45,'o%1',%1,'2026-01-01','TEST','Synthetic','Diesel','2.0',zeroblob(4096))").arg(order)))return false;
        for(int child=0;child<4;++child){const int id=order*4+child;
            if(!sql(QString("INSERT INTO router_work_order_services VALUES(%1,%2,12.35,'o%2','s%1','Synthetic work',zeroblob(2048))").arg(id).arg(order)))return false;
            for(int split=0;split<2;++split)if(!sql(QString("INSERT INTO router_service_masters VALUES(%1,'m%2',50,'s%1','o%3',zeroblob(512))").arg(id).arg((id+split)%8).arg(order)))return false;
        }
    }
    return db.commit();
}
template<class Graph,class Loader,class Reader>
static int measure(QSqlDatabase db,Loader load,Reader read,const QString& mode) {
    QList<double> samples;QByteArray expected;int rowCount=0;qint64 pay=0,base=0;
    for(int iteration=-3;iteration<20;++iteration){
        QElapsedTimer timer;timer.start();QString error;if(!db.transaction())return 2;Graph graph;
        if(!load(db,&graph,&error)){qWarning()<<error;return 2;}
        QByteArray serialized;QDataStream stream(&serialized,QIODevice::WriteOnly);rowCount=0;pay=0;base=0;
        for(int master=0;master<8;++master){const auto rows=read(db,QString("m%1").arg(master),&graph,&error);if(!error.isEmpty()){qWarning()<<error;return 2;}
            for(const auto& r:rows){stream<<r.dateStart<<r.dateEnd<<r.orderNumber<<r.internalOrderNumber<<r.car<<r.engine<<r.works<<r.amount<<r.masterAmount<<r.absent;
                ++rowCount;pay+=qRound64(r.masterAmount*100);base+=qRound64(r.amount*100);}
        }
        if(!db.commit())return 2;const double ms=timer.nsecsElapsed()/1e6;
        const auto digest=QCryptographicHash::hash(serialized,QCryptographicHash::Sha256).toHex();
        if(expected.isEmpty())expected=digest;else if(expected!=digest){qWarning()<<"Nonrepeatable results";return 1;}
        if(iteration>=0)samples<<ms;
    }
    std::sort(samples.begin(),samples.end());rusage usage{};getrusage(RUSAGE_SELF,&usage);
    qInfo().noquote()<<QString("mode=%1 repeats=20 warmup=3 p50_ms=%2 p95_ms=%3 peak_rss_mib=%4 rows=%5 base_cents=%6 pay_cents=%7 digest=%8")
        .arg(mode).arg(samples[9],0,'f',3).arg(samples[18],0,'f',3).arg(usage.ru_maxrss/1024.0,0,'f',3).arg(rowCount).arg(base).arg(pay).arg(QString::fromLatin1(expected));
    return 0;
}
int main(int argc,char**argv){QCoreApplication app(argc,argv);const auto args=app.arguments();if(args.size()!=4)return 2;
 auto db=QSqlDatabase::addDatabase("QSQLITE");db.setDatabaseName(args[2]);if(args[1]!="setup")db.setConnectOptions("QSQLITE_OPEN_READONLY");if(!db.open())return 2;
 if(args[1]=="setup")return setup(db,args[3].toInt())?0:2;
 if(args[1]=="before")return measure<PreviousPayrollGraph::Graph>(db,PreviousPayrollGraph::loadGraph,[](QSqlDatabase d,const QString&m,const PreviousPayrollGraph::Graph*g,QString*e){return PreviousPayrollGraph::read(d,m,"2026-01-01","2026-01-31",50,e,{},g);},"before");
 return measure<PayrollIdentityRead::Graph>(db,PayrollIdentityRead::loadGraph,[](QSqlDatabase d,const QString&m,const PayrollIdentityRead::Graph*g,QString*e){return PayrollIdentityRead::read(d,m,"2026-01-01","2026-01-31",50,e,{},g);},"after");
}
