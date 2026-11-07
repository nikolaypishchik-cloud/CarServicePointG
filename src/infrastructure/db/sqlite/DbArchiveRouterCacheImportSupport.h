#pragma once

#include "infrastructure/db/sqlite/DbArchiveSourceSchemaSupport.h"

#include <QtSql/QSqlDatabase>

class QString;

bool importArchiveSourceIntoRouterCache(QSqlDatabase database,
                                       const ArchiveRouterSourceSchema& sourceSchema,
                                       QString* errorText = nullptr);

bool backfillRouterCacheFromActiveIfNeeded(QSqlDatabase database,
                                           QString* errorText = nullptr);

bool rebuildRouterCacheFromActiveWorkspaceData(QSqlDatabase database,
                                               QString* errorText = nullptr);

// False includes unknown or invalidated content; callers must not hydrate it.
bool routerCacheContentReady(QSqlDatabase database, QString* errorText = nullptr);

bool validateRouterCacheReconstructionSources(QSqlDatabase database, bool archiveAttached, QString* errorText);
bool mergeActiveWorkspaceIntoRouterCache(QSqlDatabase database, QString* errorText);
