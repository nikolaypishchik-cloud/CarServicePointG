#pragma once

#include <QSet>
#include <QString>
#include <QSqlDatabase>

struct ArchiveRouterSourceSchema
{
    bool useActiveFallbackOnly = false;
    bool legacyAttached = false;
    QSet<QString> srcMasters;
    QSet<QString> srcOrders;
    QSet<QString> srcServices;
    QSet<QString> srcServiceMasters;
    QSet<QString> srcParts;
    QSet<QString> srcOutsource;
};

bool prepareArchiveRouterSourceSchema(QSqlDatabase database,
                                      const QString& archivePath,
                                      const QString& legacyPath,
                                      ArchiveRouterSourceSchema* schema,
                                      QString* errorText = nullptr);

void detachArchiveRouterSource(QSqlDatabase database);
