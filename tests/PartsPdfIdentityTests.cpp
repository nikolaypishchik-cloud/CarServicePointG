#include "PartsPdfIdentityFixture.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDebug>
int main(int argc,char** argv){QCoreApplication app(argc,argv);QTemporaryDir dir;auto db=QSqlDatabase::addDatabase("QSQLITE","carservice_main");db.setDatabaseName(dir.filePath("synthetic.db"));if(!db.open())return 2;QString error;if(!PartsPdfIdentityFixture::run(db,&error)){qCritical()<<error;return 1;}qInfo()<<"PASS parts PDF identity: unique UID/numeric owners, invoice child UID, stock owner/child/operation, old schema, public fields/cents and SQL repair repeat";}
