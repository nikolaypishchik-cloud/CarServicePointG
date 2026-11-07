#include "core/crm/CrmPageApplicationService.h"
#include "core/crm/CrmVehicleService.h"
#include "core/crm/CrmTimelineService.h"
#include "infrastructure/db/sqlite/Db.h"
#include <QSqlDriver>
#include <QSqlError>
namespace CrmPageApplicationService {
namespace {
bool prepare(QSqlDatabase db,QString* error) {
    if(!db.isValid() || !db.isOpen() || !db.driver()) {
        if(error)*error=db.lastError().isValid()?db.lastError().text():QString::fromUtf8("База даних CRM недоступна.");
        return false;
    }
    if(db.driver()->property("crmSchemaPrepared").toBool())return true;
    if(!CrmClientService::ensureLocalSchema(db,error))return false;
    db.driver()->setProperty("crmSchemaPrepared",true);return true;
}
}
bool backfill(CrmClientService::BackfillResult* result,QString* error) {
    auto db=Database::db();return prepare(db,error) && CrmClientService::backfillExistingData(db,result,error);
}
bool setPrimaryVehicle(const QString& client,const QString& vehicle,bool primary,QString* error) {
    auto db=Database::db();return prepare(db,error) && CrmVehicleService::setPrimaryStatus(db,client,vehicle,primary,error);
}
bool addNote(const QString& client,const QString& vehicle,const QString& text,QString* error) {
    auto db=Database::db();return prepare(db,error)
        && CrmTimelineService::addEvent(db,client,vehicle.isNull()?QStringLiteral(""):vehicle,
            QStringLiteral(""),"note",QString::fromUtf8("Нотатка"),text,"user",nullptr,error);
}
CrmFinancialService::Snapshot financial(const QString& client) {
    auto db=Database::db();CrmFinancialService::Snapshot result;
    if(!prepare(db,&result.error))return result;
    return CrmFinancialService::read(db,client);
}
}
