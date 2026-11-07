#include "core/cloudsync/CloudSyncSettings.h"
#include "core/cloudsync/CloudSyncCredentials.h"

#include "infrastructure/db/sqlite/Db.h"
#include "core/services/orders/LocalWorkshopBackend.h"
#include <QSqlQuery>

#include <QDateTime>
#include <QVariant>
#include <QtGlobal>

namespace {

const QString kDefaultOwnerServerUrl = QStringLiteral("https://api.carservicepointg.com.ua");
const QString kDefaultOwnerEmail = QStringLiteral("");
const QString kDefaultOrganizationUid = QStringLiteral("org-default");
const QString kDefaultBranchUid = QStringLiteral("branch-default");
constexpr int kDefaultSyncIntervalSeconds = 60;

bool setSettingSafe(const QString& key, const QVariant& value, QString* errorText)
{
    if (Database::setSetting(key, value)) return true;
    if (errorText) *errorText = QString::fromUtf8("Не вдалося зберегти налаштування: %1").arg(key);
    return false;
}

QString normalizedServerUrl(QString value)
{
    value = value.trimmed();
    while (value.endsWith(QLatin1Char('/'))) value.chop(1);
    const QString apiSuffix = QStringLiteral("/api/v1");
    if (value.endsWith(apiSuffix, Qt::CaseInsensitive)) {
        value.chop(apiSuffix.size());
        while (value.endsWith(QLatin1Char('/'))) value.chop(1);
    }
    return value;
}

} // namespace

namespace CloudSyncSettings {

bool localBackendEnabled() {
    static const bool local=Database::getSetting("settings.backend.offline",false).toBool();
    return local;
}
bool serverRequestsAllowed() {
    return !localBackendEnabled() && !Database::getSetting("settings.backend.offline",false).toBool();
}

Settings load()
{
    Settings s;
    s.offlineMode=Database::getSetting("settings.backend.offline",false).toBool();
    // Keep both setting keys supported. Older stage-1 builds and the ТЗ
    // name the global switch as `cloudsync.enabled`, while the detailed sync
    // settings page stores `settings.cloudsync.enabled`. Prefer the detailed
    // key when present, but fall back to the global key so an existing enabled
    // installation does not silently stop capturing outbox events after update.
    const QVariant detailedEnabled = Database::getSetting(QStringLiteral("settings.cloudsync.enabled"), QVariant());
    s.serverUrl = fixedServerUrl();
    s.organizationUid = fixedOrganizationUid();
    s.branchUid = fixedBranchUid();
    const QString storedToken = Database::getSetting(QStringLiteral("settings.cloudsync.api_token"), QString()).toString().trimmed();
    QString tokenError;
    if (storedToken.startsWith(QStringLiteral("dpapi:v1:"))) {
        s.apiToken = CloudSyncCredentials::unprotectToken(storedToken, &tokenError);
    } else if (!storedToken.isEmpty()) {
        // Migrate only after successful encryption and persistence.
        const QString protectedToken = CloudSyncCredentials::protectToken(storedToken, &tokenError);
        if (!protectedToken.isEmpty() && setSettingSafe(QStringLiteral("settings.cloudsync.api_token"), protectedToken, &tokenError))
            s.apiToken = storedToken;
    }
    s.enabled = detailedEnabled.isValid()
        ? detailedEnabled.toBool()
        : Database::getSetting(QStringLiteral("cloudsync.enabled"), false).toBool();
    // A desktop that has already authenticated against the fixed server must
    // continue to capture and publish changes after restart. Previously the
    // token could exist while an old disabled flag silently dropped mutations.
    if (!s.apiToken.isEmpty()) s.enabled = true;
    if(!serverRequestsAllowed())s.enabled=false;
    s.intervalSeconds = fixedSyncIntervalSeconds();
    s.lastPushAt = Database::getSetting(QStringLiteral("settings.cloudsync.last_push_at"), QString()).toString().trimmed();
    s.lastPullAt = Database::getSetting(QStringLiteral("settings.cloudsync.last_pull_at"), QString()).toString().trimmed();
    s.lastSuccessfulSyncAt = Database::getSetting(QStringLiteral("settings.cloudsync.last_successful_sync_at"), QString()).toString().trimmed();
    s.lastAuditPullAt = Database::getSetting(QStringLiteral("settings.cloudsync.last_audit_pull_at"), QString()).toString().trimmed();
    s.lastError = Database::getSetting(QStringLiteral("settings.cloudsync.last_error"), QString()).toString();
    if (!tokenError.isEmpty()) s.lastError = tokenError;
    return s;
}

bool save(const Settings& settings, QString* errorText)
{
    const bool wasLocal=localBackendEnabled();
    if(!settings.offlineMode && wasLocal && LocalWorkshopBackend::hasChanges(Database::db())) {
        if(errorText)*errorText=QString::fromUtf8("Є автономні операції з постами або оплатами. Перед поверненням до сервера потрібне перенесення й звірка цих даних; автоматичне змішування двох журналів заблоковано. Автономна робота доступна.");
        return false;
    }
    if(settings.offlineMode && !wasLocal && Database::db().tables().contains("order_closure_commands")) {
        QSqlQuery q(Database::db());
        if(!q.exec("SELECT 1 FROM main.order_closure_commands WHERE state='pending' LIMIT 1") || q.next()) {
            if(errorText)*errorText=QString::fromUtf8("Спочатку завершіть відновлення раніше надісланого закриття наряду. Сервер міг уже прийняти оплату; автономний повтор не повинен створити її вдруге.");
            return false;
        }
    }
    const QString protectedToken = CloudSyncCredentials::protectToken(settings.apiToken.trimmed(), errorText);
    if (!settings.apiToken.trimmed().isEmpty() && protectedToken.isEmpty()) return false;
    const bool enabled = !settings.offlineMode && (settings.enabled || !settings.apiToken.trimmed().isEmpty());
    if(!setSettingSafe("settings.backend.offline",settings.offlineMode,errorText))return false;
    if (!setSettingSafe(QStringLiteral("settings.cloudsync.enabled"), enabled, errorText)) return false;
    if (!setSettingSafe(QStringLiteral("cloudsync.enabled"), enabled, errorText)) return false;
    if (!setSettingSafe(QStringLiteral("settings.cloudsync.server_url"), normalizedServerUrl(fixedServerUrl()), errorText)) return false;
    if (!setSettingSafe(QStringLiteral("settings.cloudsync.organization_uid"), fixedOrganizationUid(), errorText)) return false;
    if (!setSettingSafe(QStringLiteral("settings.cloudsync.branch_uid"), fixedBranchUid(), errorText)) return false;
    if (!setSettingSafe(QStringLiteral("settings.cloudsync.api_token"), protectedToken, errorText)) return false;
    if (!setSettingSafe(QStringLiteral("settings.cloudsync.interval_seconds"), fixedSyncIntervalSeconds(), errorText)) return false;
    return true;
}

bool setLastPushAt(const QString& isoUtc, QString* errorText)
{
    return setSettingSafe(QStringLiteral("settings.cloudsync.last_push_at"), isoUtc.trimmed(), errorText);
}

bool setLastPullAt(const QString& isoUtc, QString* errorText)
{
    return setSettingSafe(QStringLiteral("settings.cloudsync.last_pull_at"), isoUtc.trimmed(), errorText);
}

bool setLastAuditPullAt(const QString& isoUtc, QString* errorText)
{
    return setSettingSafe(QStringLiteral("settings.cloudsync.last_audit_pull_at"), isoUtc.trimmed(), errorText);
}
bool setLastSuccessfulSyncAt(const QString& isoUtc, QString* errorText)
{
    return setSettingSafe(QStringLiteral("settings.cloudsync.last_successful_sync_at"), isoUtc.trimmed(), errorText);
}
bool setLastError(const QString& error, QString* errorText)
{
    return setSettingSafe(QStringLiteral("settings.cloudsync.last_error"), error.trimmed(), errorText);
}

const QString& fixedServerUrl()
{
    return kDefaultOwnerServerUrl;
}

const QString& fixedOwnerEmail()
{
    return kDefaultOwnerEmail;
}

const QString& fixedOrganizationUid()
{
    return kDefaultOrganizationUid;
}

const QString& fixedBranchUid()
{
    return kDefaultBranchUid;
}

int fixedSyncIntervalSeconds()
{
    return kDefaultSyncIntervalSeconds;
}

} // namespace CloudSyncSettings
