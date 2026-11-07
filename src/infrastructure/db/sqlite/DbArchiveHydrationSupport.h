#pragma once

#include <QtSql/QSqlDatabase>

class QString;

bool hydrateCachedOrderToActive(QSqlDatabase database,
                                const QString& requestedCanonicalUid,
                                int orderNumber,
                                QString* errorText);

// Compatibility wrapper for archive routing modules split out from the former monolith.
bool copyCachedOrderToActive(QSqlDatabase database,
                             const QString& requestedCanonicalUid,
                             int orderNumber,
                             QString* errorText);
