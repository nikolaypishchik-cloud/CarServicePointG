#pragma once
#include "core/crm/CrmClientService.h"
#include "core/crm/CrmFinancialService.h"
namespace CrmPageApplicationService {
bool backfill(CrmClientService::BackfillResult* result,QString* error);
bool setPrimaryVehicle(const QString& client,const QString& vehicle,bool primary,QString* error);
bool addNote(const QString& client,const QString& vehicle,const QString& text,QString* error);
CrmFinancialService::Snapshot financial(const QString& client);
}
