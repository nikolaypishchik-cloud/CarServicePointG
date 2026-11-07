#pragma once

#include <QString>

namespace CloudSyncSettings {

struct Settings {
    bool enabled = false;
    bool offlineMode = false;
    QString serverUrl;
    QString organizationUid;
    QString branchUid;
    QString apiToken;
    int intervalSeconds = 60;
    QString lastPushAt;
    QString lastPullAt;
    QString lastSuccessfulSyncAt;
    QString lastAuditPullAt;
    QString lastError;
};

Settings load();
// Backend authority is fixed for the lifetime of the process. Changing it needs restart.
bool localBackendEnabled();
bool serverRequestsAllowed();
bool save(const Settings& settings, QString* errorText = nullptr);
bool setLastPushAt(const QString& isoUtc, QString* errorText = nullptr);
bool setLastPullAt(const QString& isoUtc, QString* errorText = nullptr);
bool setLastSuccessfulSyncAt(const QString& isoUtc, QString* errorText = nullptr);
bool setLastAuditPullAt(const QString& isoUtc, QString* errorText = nullptr);
bool setLastError(const QString& error, QString* errorText = nullptr);

// Central server settings are product constants, not per-workstation input.
// The only per-device credentials are the device token and, optionally, the
// owner password saved with Windows DPAPI after a successful sign-in.
const QString& fixedServerUrl();
const QString& fixedOwnerEmail();
const QString& fixedOrganizationUid();
const QString& fixedBranchUid();
int fixedSyncIntervalSeconds();

} // namespace CloudSyncSettings
