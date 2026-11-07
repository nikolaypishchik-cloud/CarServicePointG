#include "PayrollReadFixture.h"
#include "core/services/ReportsSyncSupport.h"
#include "core/services/ReportReadConnection.h"
#include <thread>
#include <QCoreApplication>
#include <QDebug>
int main(int argc,char** argv) {
 QCoreApplication app(argc,argv);QTemporaryDir dir;QString error;
 auto check=[&](bool ok){if(!ok)qFatal("Detail read: %s",qPrintable(error));};
 auto db=QSqlDatabase::addDatabase("QSQLITE","detail-test");db.setDatabaseName(dir.filePath("detail.db"));
 check(ReportsSyncSupport::loadElectricityPeriods(db,&error).isEmpty() && !error.isEmpty());check(!QFile::exists(db.databaseName()));
 check(ReportsSyncSupport::collectPayrollOrdersForMaster(db,"m","2026-01-01","2026-01-31",&error).isEmpty() && !error.isEmpty());
 check(db.open());check(PayrollReadFixture::schema(db,&error));check(PayrollReadFixture::populate(db,&error));QSqlQuery q(db);
 auto sql=[&](const QString& text){if(!q.exec(text)){error=q.lastError().text();check(false);}};
 for(const auto& column:QStringList{"order_number INTEGER","opened_at TEXT","plate TEXT","car TEXT"})sql("ALTER TABLE router_work_orders ADD COLUMN "+column);
 sql("ALTER TABLE router_work_order_services ADD COLUMN title TEXT");sql("UPDATE router_work_order_services SET title='Synthetic work'");
 auto read=[&](QString from="2026-01-01",QString to="2026-01-31"){return ReportsSyncSupport::collectPayrollOrdersForMaster(db,"m",from,to,&error);};
 auto rows=read("2027-01-01","2027-01-31");check(error.isEmpty() && rows.isEmpty());
 rows=read();check(error.isEmpty() && rows.size()==1 && rows[0].amount==100 && rows[0].masterAmount==50 && rows[0].works.contains("Synthetic work"));
 sql("ALTER TABLE router_work_order_services RENAME TO broken_services");rows=read();check(!error.isEmpty() && rows.isEmpty());
 sql("ALTER TABLE broken_services RENAME TO router_work_order_services");rows=read();check(error.isEmpty() && rows.size()==1 && rows[0].masterAmount==50);
 check(ReportsSyncSupport::loadElectricityPeriods(db,&error).isEmpty() && !error.isEmpty());
 sql("CREATE TABLE electricity_periods(period_from TEXT,period_to TEXT,meter_before REAL,meter_after REAL,consumed_kwh REAL,tariff REAL,amount REAL)");
 check(ReportsSyncSupport::loadElectricityPeriods(db,&error).isEmpty() && error.isEmpty());
 sql("INSERT INTO electricity_periods VALUES('2026-01-01','2026-01-31',100,120,20,4.25,85)");
 const auto original=PayrollReadFixture::bytes(db.databaseName());
 for(int i=0;i<2;++i){auto periods=ReportsSyncSupport::loadElectricityPeriods(db,&error);check(error.isEmpty() && periods.size()==1 && periods[0].amount==85 && periods[0].consumedKwh==20);}
 check(PayrollReadFixture::bytes(db.databaseName())==original);
 sql("ALTER TABLE electricity_periods RENAME COLUMN amount TO broken_amount");check(ReportsSyncSupport::loadElectricityPeriods(db,&error).isEmpty() && !error.isEmpty());
 sql("ALTER TABLE electricity_periods RENAME COLUMN broken_amount TO amount");check(ReportsSyncSupport::loadElectricityPeriods(db,&error).size()==1 && error.isEmpty());
 bool workerOk=false;QString workerError;
 const auto path=db.databaseName();std::thread worker([&]{
  {ReportReadConnection missing(dir.filePath("worker-missing.db"),&workerError);if(missing.isOpen() || workerError.isEmpty())return;}
  {ReportReadConnection source(path,&workerError);if(!source.isOpen())return;const auto periods=ReportsSyncSupport::loadElectricityPeriods(&workerError);if(!workerError.isEmpty() || periods.size()!=1 || periods[0].amount!=85)return;}
  workerOk=!QSqlDatabase::contains(currentConnectionName());
 });worker.join();error=workerError;check(workerOk);check(!QFile::exists(dir.filePath("worker-missing.db")));
 qInfo()<<"PASS: actual payroll detail and electricity readers; closed database no creation; SQL failure; empty success; repair/repeat; exact 100/50 and 20*4.25=85; no read writes";
}
