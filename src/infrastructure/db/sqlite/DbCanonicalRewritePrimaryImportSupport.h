#pragma once

#include "infrastructure/db/sqlite/DbCanonicalRewriteSupport.h"

#include <QSqlDatabase>
#include <QString>

bool importCanonicalRewritePrimaryTables(QSqlDatabase database,
                                         const CanonicalRewriteSourceSchema& legacySchema,
                                         QString* errorText = nullptr);
