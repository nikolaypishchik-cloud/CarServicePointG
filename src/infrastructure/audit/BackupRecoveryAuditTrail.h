#pragma once

#include <QString>
#include <QStringList>

namespace BackupRecoveryAuditTrail {

void appendBackupEvent(const QString& status,
                       const QString& operation,
                       const QString& operationId,
                       const QString& sourceModule,
                       const QString& entityType,
                       const QString& entityId,
                       const QString& message,
                       const QString& errorText = QString(),
                       const QStringList& changedTables = {});

void appendRecoveryEvent(const QString& status,
                         const QString& operation,
                         const QString& operationId,
                         const QString& sourceModule,
                         const QString& entityType,
                         const QString& entityId,
                         const QString& message,
                         const QString& errorText = QString());

} // namespace BackupRecoveryAuditTrail
