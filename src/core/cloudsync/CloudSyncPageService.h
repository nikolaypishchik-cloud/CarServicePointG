#pragma once

#include "core/cloudsync/CloudSyncSettings.h"

#include <QList>
#include <QString>

namespace CloudSyncPageService {

struct IdentitySnapshot {
    QString deviceUid;
    QString deviceName;
};

struct StatusSnapshot {
    bool enabled = false;
    QString deviceUid;
    QString deviceName;
    int pendingOutbox = 0;
    int pendingInbox = 0;
    int inboxConflicts = 0;
    int pendingConflicts = 0;
    QString lastPushAt;
    QString lastPullAt;
    QString lastSuccessfulSyncAt;
    QString lastError;
    QString dbError;
};

struct ConflictRow {
    QString conflictUid;
    QString createdAt;
    QString entityType;
    QString entityUid;
    QString conflictType;
    QString localPayloadJson;
    QString remotePayloadJson;
    QString resolution;
};

IdentitySnapshot ensureIdentity(QString* errorText = nullptr);
bool saveSettings(const CloudSyncSettings::Settings& settings, const QString& deviceName, QString* errorText = nullptr);
bool regenerateDeviceUid(QString* newDeviceUid, QString* errorText = nullptr);
StatusSnapshot status(QString* errorText = nullptr);
QList<ConflictRow> listConflicts(int limit = 50, QString* errorText = nullptr);
bool resolveConflict(const QString& conflictUid, const QString& resolution, QString* errorText = nullptr);

} // namespace CloudSyncPageService
