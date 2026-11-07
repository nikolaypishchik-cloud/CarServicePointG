#pragma once
#include "core/services/OrderIdentityService.h"

class OrderNumberService {
public:
    static int nextVisibleOrderNumber();
    static bool allocate(QSqlDatabase db, const QString& requestUid, const QDateTime& createdAt,
                         int preferredInternalNumber, OrderIdentityService::Allocation* result, QString* error);
};
