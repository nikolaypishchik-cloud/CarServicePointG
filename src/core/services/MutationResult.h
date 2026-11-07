#pragma once

#include <QString>
#include <QStringList>

struct MutationResult {
    bool ok = false;
    QString errorText;
    QString operationId;
    QString sourceModule;
    QString entityType;
    QString entityId;
    QString operation;
    QString description;
    QStringList changedTables;
    // Business snapshots are used only for the human-readable audit journal.
    QString auditBeforeJson;
    QString auditAfterJson;
    bool systemMaintenance = false;
};