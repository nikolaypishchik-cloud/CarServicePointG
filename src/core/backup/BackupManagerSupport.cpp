#include "core/backup/BackupManagerSupport.h"

#include "core/backup/BackupPaths.h"
#include "core/io/SafeFileOps.h"
#include "runtime/AppRuntime.h"
#include "infrastructure/audit/BackupRecoveryAuditTrail.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace BackupManagerSupport {

QString pendingBackupJournalPath()
{
    return QDir(BackupPaths::configuredBackupsRoot()).filePath(QStringLiteral("pending_backup_journal.json"));
}

bool writePendingBackupJournal(const QQueue<SnapshotJobContext>& pending, const QString& reason, QString* errorText)
{
    if (pending.isEmpty()) {
        clearPendingBackupJournal();
        if (errorText) errorText->clear();
        return true;
    }

    QJsonArray events;
    for (const SnapshotJobContext& ctx : pending) {
        QJsonObject o = ctx.event.committedAt.isValid()
            ? QJsonObject{{QStringLiteral("event"), QJsonObject{
                    {QStringLiteral("eventId"), ctx.event.eventId},
                    {QStringLiteral("commitId"), ctx.event.commitId},
                    {QStringLiteral("committedAt"), ctx.event.committedAt.toString(Qt::ISODateWithMs)},
                    {QStringLiteral("module"), ctx.event.module},
                    {QStringLiteral("page"), ctx.event.page},
                    {QStringLiteral("entityType"), ctx.event.entityType},
                    {QStringLiteral("entityId"), ctx.event.entityId},
                    {QStringLiteral("orderUid"), ctx.event.orderUid},
                    {QStringLiteral("orderNumber"), ctx.event.orderNumber},
                    {QStringLiteral("operationType"), ctx.event.operationType},
                    {QStringLiteral("userVisibleDescription"), ctx.event.userVisibleDescription},
                    {QStringLiteral("schemaVersion"), ctx.event.schemaVersion},
                    {QStringLiteral("appVersion"), ctx.event.appVersion}
                }}}
            : QJsonObject{};
        QJsonArray changedTables;
        for (const QString& table : ctx.event.changedTables) changedTables.push_back(table);
        QJsonObject event = o.value(QStringLiteral("event")).toObject();
        event.insert(QStringLiteral("changedTables"), changedTables);
        o.insert(QStringLiteral("event"), event);
        o.insert(QStringLiteral("source"), ctx.source);
        o.insert(QStringLiteral("retryCount"), ctx.retryCount);
        events.push_back(o);
    }

    const SnapshotJobContext lastCtx = pending.back();
    const QString path = pendingBackupJournalPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorText) *errorText = file.errorString();
        return false;
    }
    const QJsonObject root{
        {QStringLiteral("version"), 2},
        {QStringLiteral("createdAt"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs)},
        {QStringLiteral("reason"), reason},
        {QStringLiteral("lastCommitId"), lastCtx.event.commitId},
        {QStringLiteral("appVersion"), lastCtx.event.appVersion},
        {QStringLiteral("schemaVersion"), lastCtx.event.schemaVersion},
        {QStringLiteral("dbPath"), AppRuntime::activeDbFilePath()},
        {QStringLiteral("shutdownWasCleanButBackupPending"), true},
        {QStringLiteral("events"), events}
    };
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (errorText) *errorText = file.errorString();
        return false;
    }
    if (errorText) errorText->clear();
    return true;
}

QQueue<SnapshotJobContext> readPendingBackupJournal(QString* errorText)
{
    QQueue<SnapshotJobContext> pending;
    QFile file(pendingBackupJournalPath());
    if (!file.exists()) {
        if (errorText) errorText->clear();
        return pending;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorText) *errorText = file.errorString();
        return pending;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    const QJsonArray events = doc.object().value(QStringLiteral("events")).toArray();
    for (const QJsonValue& value : events) {
        const QJsonObject o = value.toObject();
        const QJsonObject eventObject = o.value(QStringLiteral("event")).toObject();
        SnapshotJobContext ctx;
        ctx.event.eventId = eventObject.value(QStringLiteral("eventId")).toString();
        ctx.event.commitId = eventObject.value(QStringLiteral("commitId")).toString();
        ctx.event.committedAt = QDateTime::fromString(eventObject.value(QStringLiteral("committedAt")).toString(), Qt::ISODate);
        ctx.event.module = eventObject.value(QStringLiteral("module")).toString();
        ctx.event.page = eventObject.value(QStringLiteral("page")).toString();
        ctx.event.entityType = eventObject.value(QStringLiteral("entityType")).toString();
        ctx.event.entityId = eventObject.value(QStringLiteral("entityId")).toString();
        ctx.event.orderUid = eventObject.value(QStringLiteral("orderUid")).toString();
        ctx.event.orderNumber = eventObject.value(QStringLiteral("orderNumber")).toInt();
        ctx.event.operationType = eventObject.value(QStringLiteral("operationType")).toString(QStringLiteral("startup_pending_backup"));
        ctx.event.userVisibleDescription = eventObject.value(QStringLiteral("userVisibleDescription")).toString(QStringLiteral("Startup pending backup from previous shutdown"));
        ctx.event.schemaVersion = eventObject.value(QStringLiteral("schemaVersion")).toString();
        ctx.event.appVersion = eventObject.value(QStringLiteral("appVersion")).toString();
        const QJsonArray changedTables = eventObject.value(QStringLiteral("changedTables")).toArray();
        for (const QJsonValue& table : changedTables) ctx.event.changedTables.push_back(table.toString());
        ctx.source = QStringLiteral("startup-pending");
        ctx.retryCount = o.value(QStringLiteral("retryCount")).toInt();
        ctx.enqueuedAtMs = QDateTime::currentMSecsSinceEpoch();
        pending.enqueue(ctx);
    }
    if (errorText) errorText->clear();
    return pending;
}

void clearPendingBackupJournal()
{
    SafeFileOps::safeRemoveFile(pendingBackupJournalPath(), {BackupPaths::configuredBackupsRoot()}, {QStringLiteral("pending_backup_journal")}, {QStringLiteral(".json")});
}

void auditAndDiscardPendingShutdownBackups(QQueue<SnapshotJobContext>& pending, const QString& reason)
{
    QString journalError;
    if (!writePendingBackupJournal(pending, reason, &journalError) && !journalError.trimmed().isEmpty()) {
        BackupRecoveryAuditTrail::appendBackupEvent(QStringLiteral("failed"),
                                                    QStringLiteral("pending_backup_journal"),
                                                    QStringLiteral("shutdown_pending"),
                                                    QStringLiteral("BackupManagerSupport"),
                                                    QStringLiteral("database"),
                                                    QString(),
                                                    QStringLiteral("failed to persist pending backup journal"),
                                                    journalError);
    }
    while (!pending.isEmpty()) {
        const SnapshotJobContext ctx = pending.dequeue();
        BackupRecoveryAuditTrail::appendBackupEvent(QStringLiteral("deferred"),
                                                    ctx.event.operationType,
                                                    ctx.event.commitId,
                                                    ctx.event.module,
                                                    ctx.event.entityType,
                                                    ctx.event.entityId,
                                                    reason,
                                                    QString(),
                                                    ctx.event.changedTables);
    }
}

} // namespace BackupManagerSupport
