#include "FinancialPdfReadFixture.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDebug>
int main(int argc,char** argv){QCoreApplication app(argc,argv);QTemporaryDir dir;auto db=QSqlDatabase::addDatabase("QSQLITE","carservice_main");db.setDatabaseName(dir.filePath("synthetic.db"));if(!db.open())return 1;QString error;if(!FinancialPdfReadFixture::run(db,&error)){qCritical()<<error;return 1;}db.close();if(ReportsPdfSupport::validateFinancialPdfSource(QDate(2026,1,1),QDate(2026,1,31),&error) || error.isEmpty() || db.isOpen())return 2;qInfo()<<"PASS financial PDF readers: empty/populated, 19.35/31.95/margin12.60, SQL/dependency failures, repair and current account rights";}
