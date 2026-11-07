#include "core/cloudsync/CloudSyncPageService.h"

#include "core/cloudsync/CloudSyncInboxService.h"
#include "core/cloudsync/CloudSyncOutboxService.h"
#include "core/cloudsync/ConflictResolverService.h"
#include "core/cloudsync/DeviceIdentityService.h"
#include "infrastructure/db/sqlite/Db.h"

#include <QSqlError>
#include <QSqlQuery>

namespace CloudSyncPageService {

IdentitySnapshot ensureIdentity(QString* errorText)
{
    QSqlDatabase db = Database::db();
    DeviceIdentityService::ensureDeviceIdentity(db, errorText);
    return {DeviceIdentityService::deviceUid(db), DeviceIdentityService::deviceName(db)};
}

bool saveSettings(const CloudSyncSettings::Settings& settings, const QString& deviceName, QString* errorText)
{
    QSqlDatabase db = Database::db();
    if (!DeviceIdentityService::updateDeviceName(db, deviceName, errorText)) return false;
    if (!DeviceIdentityService::updateBranchUid(db, settings.branchUid, errorText)) return false;
    return CloudSyncSettings::save(settings, errorText);
}

bool regenerateDeviceUid(QString* newDeviceUid, QString* errorText)
{
    return DeviceIdentityService::regenerateDeviceUid(Database::db(), newDeviceUid, errorText);
}

StatusSnapshot status(QString* errorText)
{
    QSqlDatabase db = Database::db();
    QString localError;
    StatusSnapshot snapshot;
    const auto settings = CloudSyncSettings::load();
    snapshot.enabled = settings.enabled;
    snapshot.lastPushAt = settings.lastPushAt;
    snapshot.lastPullAt = settings.lastPullAt;
    snapshot.lastSuccessfulSyncAt = settings.lastSuccessfulSyncAt;
    snapshot.lastError = settings.lastError;
    snapshot.deviceUid = DeviceIdentityService::deviceUid(db);
    snapshot.deviceName = DeviceIdentityService::deviceName(db);
    snapshot.pendingOutbox = CloudSyncOutboxService::pendingCount(db, &localError);
    snapshot.dbError = localError;
    snapshot.pendingInbox = CloudSyncInboxService::pendingCount(db);
    snapshot.inboxConflicts = CloudSyncInboxService::conflictCount(db);
    snapshot.pendingConflicts = ConflictResolverService::pendingCount(db);
    if (errorText) *errorText = snapshot.dbError;
    return snapshot;
}

QList<ConflictRow> listConflicts(int limit, QString* errorText)
{
    QList<ConflictRow> rows;
    QSqlQuery q(Database::db());
    q.prepare(QStringLiteral(
        "SELECT conflict_uid,created_at,entity_type,entity_uid,conflict_type,local_payload_json,remote_payload_json,resolution "
        "FROM sync_conflicts "
        "ORDER BY CASE WHEN resolution='pending' THEN 0 ELSE 1 END, created_at DESC LIMIT ?"));
    q.addBindValue(qBound(1, limit, 500));
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return rows;
    }
    while (q.next()) {
        ConflictRow row;
        row.conflictUid = q.value(0).toString();
        row.createdAt = q.value(1).toString();
        row.entityType = q.value(2).toString();
        row.entityUid = q.value(3).toString();
        row.conflictType = q.value(4).toString();
        row.localPayloadJson = q.value(5).toString();
        row.remotePayloadJson = q.value(6).toString();
        row.resolution = q.value(7).toString();
        rows.append(row);
    }
    return rows;
}

bool resolveConflict(const QString& conflictUid, const QString& resolution, QString* errorText)
{
    return ConflictResolverService::resolveConflict(Database::db(), conflictUid, resolution, QStringLiteral("desktop_user"), errorText);
}

} // namespace CloudSyncPageService
