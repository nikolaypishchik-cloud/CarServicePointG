#!/bin/bash
# Expected exit 1: the unmodified resolver from the task's starting commit.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build-single-instance-audit/invoice-import-owners
python - <<'PY'
import subprocess
from pathlib import Path
source=subprocess.check_output(['git','show','7db1399:src/core/services/orders/CurrentOrdersInvoiceImportService.cpp'],text=True)
a=source.index('static int resolveInternalOrderNumberForInvoiceImport(')
b=source.index('\nQString attachmentBaseDir',a)
headers='''#include <QCoreApplication>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include "core/diagnostics/RuntimeLoggingControl.h"
'''
main='''
int main(int argc,char**argv){
 QCoreApplication app(argc,argv);auto db=QSqlDatabase::addDatabase("QSQLITE");db.setDatabaseName(":memory:");if(!db.open())return 2;
 QSqlQuery q(db);if(!q.exec("CREATE TABLE work_orders(number INT,order_number INT,order_year INT,status TEXT)"))return 2;
 if(!q.exec("INSERT INTO work_orders VALUES(90,7,2024,'closed'),(91,7,2026,'in_progress'),(7,8,2026,'in_progress')"))return 2;
 QString error;int failures=0;
 const int ambiguous=resolveInternalOrderNumberForInvoiceImport(db,7,2026,&error);
 const int missing=resolveInternalOrderNumberForInvoiceImport(db,999,2026,&error);
 const int internalOnly=resolveInternalOrderNumberForInvoiceImport(db,90,2026,&error);
 if(ambiguous!=0){qWarning()<<"FAIL ambiguity auto-selected"<<ambiguous;++failures;}
 if(missing!=0){qWarning()<<"FAIL missing owner returned draft candidate"<<missing;++failures;}
 if(internalOnly!=0){qWarning()<<"FAIL internal number treated as visible"<<internalOnly;++failures;}
 qInfo()<<"Previous production resolver failures:"<<failures;return failures?1:0;
}
'''
Path('build-single-instance-audit/invoice-import-owners/previous.cpp').write_text(headers+source[a:b]+main)
PY
export LD_LIBRARY_PATH=/workspace/qt-sdk/root/usr/lib/x86_64-linux-gnu
export QT_PLUGIN_PATH=$LD_LIBRARY_PATH/qt6/plugins
qt=/workspace/qt-sdk/root/usr/include/x86_64-linux-gnu/qt6
g++ -std=c++17 -fPIC -Isrc -I"$qt" -I"$qt/QtCore" -I"$qt/QtSql" build-single-instance-audit/invoice-import-owners/previous.cpp src/core/diagnostics/RuntimeLoggingControl.cpp -L"$LD_LIBRARY_PATH" -lQt6Sql -lQt6Core -o build-single-instance-audit/invoice-import-owners/previous
build-single-instance-audit/invoice-import-owners/previous
