#pragma once

#include <QSqlDatabase>
#include <QString>

namespace DbSchemaAuditSupport {

bool ensureContractMeta(QSqlDatabase db, QString* errorText = nullptr);
bool applySupplementalIndexes(QSqlDatabase db, QString* errorText = nullptr);
bool runActiveSchemaAudit(QSqlDatabase db, QString* errorText = nullptr);
bool runRegistrySchemaAudit(QSqlDatabase db, QString* errorText = nullptr);

} // namespace DbSchemaAuditSupport
