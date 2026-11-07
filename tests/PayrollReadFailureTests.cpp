#include "PayrollReadFixture.h"
#include <QCoreApplication>
#include <QDebug>
int main(int argc,char** argv) {
 QCoreApplication app(argc,argv);QString error;
 if(!PayrollReadFixture::run(&error)){qWarning()<<"FAIL:"<<error;return 1;}
 qInfo()<<"PASS: payroll missing/unusable/corrupt database; empty result; nine SQL failures and retry; exact amounts; no writes; policy errors; no cached failures or leaked connections";
}
