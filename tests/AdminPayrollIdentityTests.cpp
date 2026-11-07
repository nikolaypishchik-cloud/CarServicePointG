#include "AdminPayrollIdentityFixture.h"
#include <QCoreApplication>
#include <QDebug>
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);QString error;
    if(!AdminPayrollIdentityFixture::run(&error)){qCritical().noquote()<<error;return 1;}
    qInfo()<<"PASS admin payroll UID ownership, historical rows, cents, SQL failure, repair/repeat and read-only source";
}
