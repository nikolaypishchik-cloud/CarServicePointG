#pragma once

#include <QSqlDatabase>
#include <QString>

namespace OrderIdentityResolver {

struct OrderIdentityRecord {
    int number = 0;
    QString orderUid;
    int visibleOrderNumber = 0;
    int orderYear = 0;
};

bool resolveByInternalNumber(QSqlDatabase database,
                             int orderNumber,
                             OrderIdentityRecord* outRecord = nullptr,
                             QString* errorText = nullptr);

bool resolveByAnyUid(QSqlDatabase database,
                     const QString& anyOrderUid,
                     OrderIdentityRecord* outRecord = nullptr,
                     QString* errorText = nullptr);

QString canonicalOrderUidForInternalNumber(QSqlDatabase database,
                                           int orderNumber,
                                           QString* errorText = nullptr);

QString canonicalOrderUidForAnyUid(QSqlDatabase database,
                                   const QString& anyOrderUid,
                                   QString* errorText = nullptr);

} // namespace OrderIdentityResolver
