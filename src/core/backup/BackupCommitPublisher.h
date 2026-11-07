#pragma once

#include "core/backup/BackupDtos.h"

#include <QString>
#include <QStringList>

class BackupCommitPublisher
{
public:
    static DataCommitEvent makeEvent(const QString& module,
                                     const QString& page,
                                     const QString& entityType,
                                     const QString& entityId,
                                     const QString& orderUid,
                                     int orderNumber,
                                     const QString& operationType,
                                     const QString& description,
                                     const QStringList& changedTables,
                                     const QString& commitId = QString());

    static bool publish(const DataCommitEvent& event, QString* errorText = nullptr);
};
