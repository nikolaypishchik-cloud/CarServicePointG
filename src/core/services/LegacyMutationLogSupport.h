#pragma once

#include <QSqlDatabase>
#include <QString>

namespace LegacyMutationLogSupport {

bool ensureSchema(QSqlDatabase database, QString* errorText = nullptr);
QString orderUidForNumber(QSqlDatabase database, int orderNumber);

QString snapshotOrder(QSqlDatabase database, int orderNumber);
QString snapshotService(QSqlDatabase database, int serviceId);
QString snapshotPart(QSqlDatabase database, int partId);
QString snapshotMaster(QSqlDatabase database, const QString& masterId);
QString snapshotPlanningDay(QSqlDatabase database, const QString& dayIso);

bool logMutation(QSqlDatabase database,
                 const QString& entityName,
                 const QString& action,
                 int orderNumber,
                 const QString& orderUid,
                 const QString& recordKey,
                 const QString& beforeJson,
                 const QString& afterJson,
                 QString* errorText = nullptr);

} // namespace LegacyMutationLogSupport
