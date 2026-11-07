#pragma once

#include <QSqlDatabase>
#include <QString>

#include <functional>

bool workspaceOrderStateLookup(QSqlDatabase database, int orderNumber, QString* origin = nullptr);
bool workspaceOrderStateLookupByUid(QSqlDatabase database, const QString& orderUid, QString* origin = nullptr);
QString workspaceOrderSourceDbLookup(QSqlDatabase database, int orderNumber);
QString workspaceOrderSourceDbLookupByUid(QSqlDatabase database, const QString& orderUid);
bool routerCacheHasOrderEntry(QSqlDatabase database, int orderNumber);
bool upsertWorkspaceOrderStateEntry(QSqlDatabase database,
                                    int orderNumber,
                                    const QString& origin,
                                    const QString& sourceDb,
                                    const QString& orderUid,
                                    const std::function<QString(int)>& fallbackOrderUidResolver,
                                    bool prepareSchema = true);
void removeWorkspaceOrderStateEntry(QSqlDatabase database, int orderNumber);

// Caller encloses this write in its transaction or savepoint. Header finalizers
// publish explicitly; child mutations can coalesce a registered after-COMMIT drain.
bool queueOrderRegistryPublication(QSqlDatabase database,int orderNumber,QString* errorText,bool deferPublication = true);

// Read-only identity gate; missing projections can be rebuilt, recorded conflicts cannot.
bool validateStartupRegistryIdentity(QSqlDatabase database,const QString& registryPath,QString* errorText);

// Validate an existing canonical archive without repairing historical identities.
bool validateArchiveShardIdentity(QSqlDatabase database,QString* errorText);
