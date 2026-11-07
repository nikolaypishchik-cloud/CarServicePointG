#include "AppVersion.h"
#include "core/backup/BackupCommitPublisher.h"

#include "runtime/RuntimeServices.h"
#include "core/backup/BackupManager.h"
#include "infrastructure/db/sqlite/Db.h"

#include <QDateTime>
#include <QUuid>

DataCommitEvent BackupCommitPublisher::makeEvent(const QString& module,
                                                 const QString& page,
                                                 const QString& entityType,
                                                 const QString& entityId,
                                                 const QString& orderUid,
                                                 int orderNumber,
                                                 const QString& operationType,
                                                 const QString& description,
                                                 const QStringList& changedTables,
                                                 const QString& commitId)
{
    DataCommitEvent event;
    event.eventId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    event.commitId = commitId.trimmed().isEmpty() ? event.eventId : commitId.trimmed();
    event.committedAt = QDateTime::currentDateTime();
    event.module = module;
    event.page = page;
    event.entityType = entityType;
    event.entityId = entityId;
    event.orderUid = orderUid;
    event.orderNumber = orderNumber;
    event.operationType = operationType;
    event.userVisibleDescription = description;
    event.schemaVersion = Database::getSetting(QStringLiteral("app.schema.version"), QStringLiteral("runtime")).toString();
    event.appVersion = CarService::AppVersion::versionString();
    event.changedTables = changedTables;
    return event;
}

bool BackupCommitPublisher::publish(const DataCommitEvent& event, QString* errorText)
{
    auto* backup = RuntimeServices::backupManager();
    if (!backup) {
        if (errorText) *errorText = QStringLiteral("BackupManager недоступний");
        return false;
    }
    return backup->requestBackup(event, errorText);
}
