#include "StatisticsReadFixture.h"
#include <QCoreApplication>
#include <QDebug>
int main(int argc,char** argv){QCoreApplication app(argc,argv);QString error;if(!StatisticsReadFixture::run(&error)){qCritical()<<error;return 1;}qInfo()<<"PASS statistics public reader: absent/directory/empty, seven SQL failures, malformed optional table, repair/repeat, totals 67.50; expenses 1000/250 cents empty/error/retry; readonly scoped worker; unchanged bytes/no leaks";}
