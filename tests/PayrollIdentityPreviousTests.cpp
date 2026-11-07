#include "PayrollIdentityFixture.h"
#include <QCoreApplication>
#include <QDebug>
#include "core/util/FormatUtils.h"
int main(int argc,char**argv){QCoreApplication app(argc,argv);QTemporaryDir dir;auto db=QSqlDatabase::addDatabase("QSQLITE","payroll-before");db.setDatabaseName(dir.filePath("synthetic.db"));QString error;if(!db.open()||!PayrollIdentityFixture::prepare(db,&error))return 2;
 int failures=0;auto read=[&]{return ReportsSyncSupport::collectPayrollOrdersForMaster(db,"m","2026-01-01","2026-01-31",&error);};
 auto rows=read();if(rows.size()!=1||qRound64(rows.first().masterAmount*100)!=6){qWarning()<<"FAIL per-row half-up";++failures;}
 const auto snapshot=ReportsDataLoader::buildPayrollSnapshot(db.databaseName(),"2026-01-01","2026-01-31");
 if(!snapshot.error.isEmpty())return 2;
 for(const auto& row:snapshot.rows)if(row.masterId=="m"&&row.accrued!=FormatUtils::money(row.accruedValue,2)){qWarning()<<"FAIL payroll table hides kopecks";++failures;}
 QSqlQuery q(db);if(!q.exec("INSERT INTO router_work_orders(number,order_number,order_uid,closed_at,status,discount,discount_mode,works_sum) VALUES(1,8,'foreign','2026-01-15','closed',0,'percent',100)"))return 2;
 rows=read();if(rows.size()!=1){qWarning()<<"FAIL header UID collision: rows"<<rows.size();++failures;}
 if(!q.exec("INSERT INTO router_work_order_services(id,order_number,order_uid,service_uid,amount,title) VALUES(10,1,'foreign','foreign-service',100,'Foreign')"))return 2;
 rows=read();if(error.isEmpty()||!rows.isEmpty()){qWarning()<<"FAIL ambiguous service assignment accepted";++failures;}
 qInfo()<<"Previous payroll failures"<<failures;return failures?1:0;
}
