#pragma once
#include "core/crm/CrmFinancialService.h"
#include <QVariantList>
#include <QVector>
namespace CrmPageReadService {
// Rows are immutable query projections in the documented SELECT column order;
// no SQL handle, cursor, statement or connection is exposed to the UI.
using Rows=QVector<QVariantList>;
struct RefreshResult {
        bool backfillOk = true;
        bool backfillRan = false;
        int clientsCount = 0;
        int vehiclesCount = 0;
        int tasksCount = 0;
        QString backfillStatus;
        QString backfillError;
        QString dbError;
    };


struct ClientList {Rows clients;QString error;};
struct ClientDetails {
    QVariantList client;
    Rows vehicles,history,events,tasks;
    CrmFinancialService::Snapshot financial;
    QString error;
};
RefreshResult refresh(bool runBackfillNow);
ClientList clients(const QString& term);
ClientDetails details(const QString& uid);
}
