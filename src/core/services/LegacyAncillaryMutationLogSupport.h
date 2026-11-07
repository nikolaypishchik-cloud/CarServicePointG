#pragma once

#include <QSqlDatabase>
#include <QString>

namespace LegacyAncillaryMutationLogSupport {

QString snapshotCatalogItem(QSqlDatabase database, int catalogId);
QString snapshotExpensePeriod(QSqlDatabase database, const QString& periodFrom, const QString& periodTo,
                             QString* errorText = nullptr);
QString snapshotOutsourceService(QSqlDatabase database, int serviceId);

bool logCatalogMutation(QSqlDatabase database,
                        const QString& action,
                        int catalogId,
                        const QString& beforeJson,
                        const QString& afterJson,
                        QString* errorText = nullptr);

bool logExpenseMutation(QSqlDatabase database,
                        const QString& action,
                        const QString& periodFrom,
                        const QString& periodTo,
                        const QString& beforeJson,
                        const QString& afterJson,
                        QString* errorText = nullptr);

bool logOutsourceMutation(QSqlDatabase database,
                          const QString& action,
                          int orderNumber,
                          int serviceId,
                          const QString& beforeJson,
                          const QString& afterJson,
                          QString* errorText = nullptr);

}
