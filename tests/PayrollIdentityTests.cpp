#include "PayrollIdentityFixture.h"
#include <QCoreApplication>
#include "core/util/FormatUtils.h"
#include <QDebug>
int main(int argc,char**argv){
 QCoreApplication app(argc,argv);QTemporaryDir dir;auto db=QSqlDatabase::addDatabase("QSQLITE","carservice_main");db.setDatabaseName(dir.filePath("synthetic.db"));QString error;
 if(!db.open()||!PayrollIdentityFixture::run(db,&error)){qWarning()<<"FAIL"<<error;return 1;}
 const auto ambiguous=ReportsSyncSupport::payrollWorksTextForMaster("1","m",50,&error);
 if(!ambiguous.isEmpty()||error.isEmpty()){qWarning()<<"FAIL public text accepted ambiguous numeric order";return 1;}
 for(int i=0;i<2;++i){const auto snapshot=ReportsDataLoader::buildPayrollSnapshot(db.databaseName(),"2026-01-01","2026-01-31");
  double m=-1,n=-1;for(const auto& row:snapshot.rows){if(row.masterId=="m"){m=row.accruedValue;if(row.accrued!=FormatUtils::money(m,2)){qWarning()<<"FAIL payroll table hides kopecks"<<row.accrued;return 1;}}if(row.masterId=="n")n=row.accruedValue;}
  if(!snapshot.error.isEmpty()||qRound64(m*100)!=5||qRound64(n*100)!=2){qWarning()<<"FAIL snapshot"<<snapshot.error<<m<<n;return 1;}
 }
 qInfo()<<"PASS payroll public detail/text/snapshot UID ownership, split, per-row cents, dependency failure and repeat";
}
