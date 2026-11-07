#pragma once

#include <QDateTime>
#include <QString>

struct BackupEngineState
{
    QString preferredMode = QStringLiteral("sqlite3_backup_api");
    QString activeMode = QStringLiteral("sqlite3_backup_api");
    bool degraded = false;
    QString degradedReason;
    QString lastEngineError;
    QString lastFallbackReason;
    QDateTime updatedAt;
};
