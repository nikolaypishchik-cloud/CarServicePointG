#pragma once
#include "infrastructure/db/sqlite/DbFacadeSupport.h"
#include "infrastructure/db/sqlite/DbThreadConnectionSupport.h"
#include <QSqlDatabase>
#include <QSqlError>
// A background report adopts an existing file, never initializes an application
// profile. Its standard DAO name lets existing readers share this one handle.
class ReportReadConnection {
public:
    explicit ReportReadConnection(const QString& path,QString* error):name(currentConnectionName()) {
        if(error)error->clear();
        if(QSqlDatabase::contains(name)){if(error)*error=QStringLiteral("Report worker already has a database connection");return;}
        owned=true;db=QSqlDatabase::addDatabase("QSQLITE",name);
        DbThreadConnectionSupport::registerConnectionName(name,path);
        db.setDatabaseName(path);db.setConnectOptions("QSQLITE_OPEN_READONLY");
        ready=db.open();if(!ready && error)*error=db.lastError().text();
    }
    ~ReportReadConnection(){if(owned){db.close();db={};QSqlDatabase::removeDatabase(name);DbThreadConnectionSupport::unregisterConnectionName(name);}}
    ReportReadConnection(const ReportReadConnection&)=delete;
    ReportReadConnection& operator=(const ReportReadConnection&)=delete;
    bool isOpen()const{return ready;}
private:
    QString name;QSqlDatabase db;bool owned=false,ready=false;
};
