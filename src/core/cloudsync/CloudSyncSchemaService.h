#pragma once

#include <QSqlDatabase>
#include <QString>

namespace CloudSyncSchemaService {

bool ensureSchema(QSqlDatabase db, QString* errorText = nullptr);

} // namespace CloudSyncSchemaService
