#include "core/backup/BackupDtos.h"

#include <QJsonArray>

QJsonObject SnapshotMetadata::toJson() const
{
    QJsonArray changedTablesArray;
    for (const QString& table : changedTables) changedTablesArray.push_back(table);

    return {
        {QStringLiteral("snapshotId"), snapshotId},
        {QStringLiteral("createdAt"), createdAt.toString(Qt::ISODateWithMs)},
        {QStringLiteral("operationType"), operationType},
        {QStringLiteral("module"), module},
        {QStringLiteral("page"), page},
        {QStringLiteral("entityType"), entityType},
        {QStringLiteral("entityId"), entityId},
        {QStringLiteral("orderUid"), orderUid},
        {QStringLiteral("orderNumber"), orderNumber},
        {QStringLiteral("description"), description},
        {QStringLiteral("appVersion"), appVersion},
        {QStringLiteral("schemaVersion"), schemaVersion},
        {QStringLiteral("dbPath"), dbPath},
        {QStringLiteral("snapshotPath"), snapshotPath},
        {QStringLiteral("snapshotSizeBytes"), QString::number(snapshotSizeBytes)},
        {QStringLiteral("dbFileSizeBytes"), QString::number(dbFileSizeBytes)},
        {QStringLiteral("pageCount"), QString::number(pageCount)},
        {QStringLiteral("validationStatus"), validationStatus},
        {QStringLiteral("commitId"), commitId},
        {QStringLiteral("eventId"), eventId},
        {QStringLiteral("source"), source},
        {QStringLiteral("engineUsed"), engineUsed},
        {QStringLiteral("backupRoot"), backupRoot},
        {QStringLiteral("hashSha256"), hashSha256},
        {QStringLiteral("warning"), warning},
        {QStringLiteral("degradedReason"), degradedReason},
        {QStringLiteral("changedTables"), changedTablesArray},
        {QStringLiteral("snapshotDurationMs"), QString::number(snapshotDurationMs)},
        {QStringLiteral("validationDurationMs"), QString::number(validationDurationMs)}
    };
}

SnapshotMetadata SnapshotMetadata::fromJson(const QJsonObject& o)
{
    SnapshotMetadata m;
    m.snapshotId = o.value(QStringLiteral("snapshotId")).toString();
    m.createdAt = QDateTime::fromString(o.value(QStringLiteral("createdAt")).toString(), Qt::ISODate);
    m.operationType = o.value(QStringLiteral("operationType")).toString();
    m.module = o.value(QStringLiteral("module")).toString();
    m.page = o.value(QStringLiteral("page")).toString();
    m.entityType = o.value(QStringLiteral("entityType")).toString();
    m.entityId = o.value(QStringLiteral("entityId")).toString();
    m.orderUid = o.value(QStringLiteral("orderUid")).toString();
    m.orderNumber = o.value(QStringLiteral("orderNumber")).toInt();
    m.description = o.value(QStringLiteral("description")).toString();
    m.appVersion = o.value(QStringLiteral("appVersion")).toString();
    m.schemaVersion = o.value(QStringLiteral("schemaVersion")).toString();
    m.dbPath = o.value(QStringLiteral("dbPath")).toString();
    m.snapshotPath = o.value(QStringLiteral("snapshotPath")).toString();
    m.snapshotSizeBytes = o.value(QStringLiteral("snapshotSizeBytes")).toString().toLongLong();
    m.validationStatus = o.value(QStringLiteral("validationStatus")).toString();
    m.commitId = o.value(QStringLiteral("commitId")).toString();
    m.eventId = o.value(QStringLiteral("eventId")).toString();
    m.source = o.value(QStringLiteral("source")).toString();
    m.engineUsed = o.value(QStringLiteral("engineUsed")).toString();
    m.backupRoot = o.value(QStringLiteral("backupRoot")).toString();
    m.hashSha256 = o.value(QStringLiteral("hashSha256")).toString();
    m.warning = o.value(QStringLiteral("warning")).toString();
    m.degradedReason = o.value(QStringLiteral("degradedReason")).toString();
    const QJsonArray changedTablesArray = o.value(QStringLiteral("changedTables")).toArray();
    for (const QJsonValue& value : changedTablesArray) m.changedTables.push_back(value.toString());
    m.dbFileSizeBytes = o.value(QStringLiteral("dbFileSizeBytes")).toString().toLongLong();
    m.pageCount = o.value(QStringLiteral("pageCount")).toString().toLongLong();
    m.snapshotDurationMs = o.value(QStringLiteral("snapshotDurationMs")).toString().toLongLong();
    m.validationDurationMs = o.value(QStringLiteral("validationDurationMs")).toString().toLongLong();
    return m;
}

QJsonObject RecoverySessionState::toJson() const
{
    return {
        {QStringLiteral("sessionId"), sessionId},
        {QStringLiteral("appStartTime"), appStartTime.toString(Qt::ISODateWithMs)},
        {QStringLiteral("lastHeartbeat"), lastHeartbeat.toString(Qt::ISODateWithMs)},
        {QStringLiteral("lastDraftWrite"), lastDraftWrite.toString(Qt::ISODateWithMs)},
        {QStringLiteral("cleanShutdown"), cleanShutdown},
        {QStringLiteral("dirtyState"), dirtyState},
        {QStringLiteral("activeModule"), activeModule},
        {QStringLiteral("draftCount"), draftCount},
        {QStringLiteral("appVersion"), appVersion},
        {QStringLiteral("schemaVersion"), schemaVersion},
        {QStringLiteral("sessionDir"), sessionDir}
    };
}

RecoverySessionState RecoverySessionState::fromJson(const QJsonObject& o)
{
    RecoverySessionState s;
    s.sessionId = o.value(QStringLiteral("sessionId")).toString();
    s.appStartTime = QDateTime::fromString(o.value(QStringLiteral("appStartTime")).toString(), Qt::ISODate);
    s.lastHeartbeat = QDateTime::fromString(o.value(QStringLiteral("lastHeartbeat")).toString(), Qt::ISODate);
    s.lastDraftWrite = QDateTime::fromString(o.value(QStringLiteral("lastDraftWrite")).toString(), Qt::ISODate);
    s.cleanShutdown = o.value(QStringLiteral("cleanShutdown")).toBool();
    s.dirtyState = o.value(QStringLiteral("dirtyState")).toBool();
    s.activeModule = o.value(QStringLiteral("activeModule")).toString();
    s.draftCount = o.value(QStringLiteral("draftCount")).toInt();
    s.appVersion = o.value(QStringLiteral("appVersion")).toString();
    s.schemaVersion = o.value(QStringLiteral("schemaVersion")).toString();
    s.sessionDir = o.value(QStringLiteral("sessionDir")).toString();
    return s;
}

QJsonObject DraftPayload::toJson() const
{
    return {
        {QStringLiteral("recoverySessionId"), recoverySessionId},
        {QStringLiteral("draftId"), draftId},
        {QStringLiteral("module"), module},
        {QStringLiteral("formType"), formType},
        {QStringLiteral("entityType"), entityType},
        {QStringLiteral("entityId"), entityId},
        {QStringLiteral("orderUid"), orderUid},
        {QStringLiteral("createdAt"), createdAt.toString(Qt::ISODateWithMs)},
        {QStringLiteral("updatedAt"), updatedAt.toString(Qt::ISODateWithMs)},
        {QStringLiteral("committedBaseVersion"), committedBaseVersion},
        {QStringLiteral("payloadSchemaVersion"), payloadSchemaVersion},
        {QStringLiteral("appVersion"), appVersion},
        {QStringLiteral("schemaVersion"), schemaVersion},
        {QStringLiteral("baseEntityVersion"), baseEntityVersion},
        {QStringLiteral("baseUpdatedAt"), baseUpdatedAt.toString(Qt::ISODateWithMs)},
        {QStringLiteral("payloadHash"), payloadHash},
        {QStringLiteral("payloadData"), payloadData},
        {QStringLiteral("validationState"), validationState},
        {QStringLiteral("conflictState"), conflictState},
        {QStringLiteral("filePath"), filePath},
        {QStringLiteral("fileSizeBytes"), QString::number(fileSizeBytes)}
    };
}

DraftPayload DraftPayload::fromJson(const QJsonObject& o)
{
    DraftPayload p;
    p.recoverySessionId = o.value(QStringLiteral("recoverySessionId")).toString();
    p.draftId = o.value(QStringLiteral("draftId")).toString();
    p.module = o.value(QStringLiteral("module")).toString();
    p.formType = o.value(QStringLiteral("formType")).toString();
    p.entityType = o.value(QStringLiteral("entityType")).toString();
    p.entityId = o.value(QStringLiteral("entityId")).toString();
    p.orderUid = o.value(QStringLiteral("orderUid")).toString();
    p.createdAt = QDateTime::fromString(o.value(QStringLiteral("createdAt")).toString(), Qt::ISODate);
    p.updatedAt = QDateTime::fromString(o.value(QStringLiteral("updatedAt")).toString(), Qt::ISODate);
    p.committedBaseVersion = o.value(QStringLiteral("committedBaseVersion")).toString();
    p.payloadSchemaVersion = o.value(QStringLiteral("payloadSchemaVersion")).toString();
    p.appVersion = o.value(QStringLiteral("appVersion")).toString();
    p.schemaVersion = o.value(QStringLiteral("schemaVersion")).toString();
    p.baseEntityVersion = o.value(QStringLiteral("baseEntityVersion")).toString(p.committedBaseVersion);
    p.baseUpdatedAt = QDateTime::fromString(o.value(QStringLiteral("baseUpdatedAt")).toString(), Qt::ISODate);
    p.payloadHash = o.value(QStringLiteral("payloadHash")).toString();
    p.payloadData = o.value(QStringLiteral("payloadData")).toObject();
    p.validationState = o.value(QStringLiteral("validationState")).toString();
    p.conflictState = o.value(QStringLiteral("conflictState")).toString();
    p.filePath = o.value(QStringLiteral("filePath")).toString();
    p.fileSizeBytes = o.value(QStringLiteral("fileSizeBytes")).toString().toLongLong();
    return p;
}
