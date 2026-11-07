#pragma once

#include <QSqlDatabase>
#include <QString>

bool ensureCanonicalRewriteSupportSchema(QSqlDatabase database, QString* errorText = nullptr);
bool upsertActiveOrdersIntoRegistryForWorkspace(QSqlDatabase workspaceDb, QString* errorText = nullptr);
bool upsertActiveOrdersIntoRegistry(QSqlDatabase workspaceDb, QSqlDatabase registryDb, QString* errorText = nullptr);
