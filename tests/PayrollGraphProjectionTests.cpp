#include "PayrollGraphProjectionFixture.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDebug>
int main(int argc,char**argv){QCoreApplication app(argc,argv);QTemporaryDir dir;auto db=QSqlDatabase::addDatabase("QSQLITE");db.setDatabaseName(dir.filePath("fixture.db"));QString error;
 if(!db.open()||!PayrollGraphProjectionFixture::run(db,&error)){qWarning()<<"FAIL"<<error;return 1;}qInfo()<<"PASS projection, every result field, split cents, repeat, transaction, old schema, SQL errors and index refresh";}
