#pragma once

#include <QJsonObject>
#include <QSqlDatabase>
#include <QString>

namespace ConflictResolverService {

bool ensureSchema(QSqlDatabase db = QSqlDatabase(), QString* errorText = nullptr);
bool recordConflict(QSqlDatabase db,
                    const QString& entityType,
                    const QString& entityUid,
                    const QJsonObject& localPayload,
                    const QJsonObject& remotePayload,
                    const QString& conflictType,
                    QString* conflictUid = nullptr,
                    QString* errorText = nullptr);
int pendingCount(QSqlDatabase db = QSqlDatabase(), QString* errorText = nullptr);
bool resolveConflict(QSqlDatabase db,
                     const QString& conflictUid,
                     const QString& resolution,
                     const QString& resolvedBy = QString(),
                     QString* errorText = nullptr);

} // namespace ConflictResolverService
