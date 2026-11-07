#include "core/services/settings/SyncSettingsService.h"

#include "infrastructure/db/sqlite/Db.h"
#include "core/sync/SyncPayloadUtils.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QThread>

#include <QRegularExpression>
#include <QSet>

namespace SyncSettingsService {

namespace {

// CS_INTERNAL_BLOCKING_SYNC_ALLOWED: settings-only Wi-Fi profile discovery.
// This is not a UI-layer path and it is bounded to avoid resurrecting old
// unbounded synchronous process fallbacks while the network-settings backend is
// kept as an internal implementation detail.
bool isUiThread_local()
{
    return QCoreApplication::instance()
        && QThread::currentThread() == QCoreApplication::instance()->thread();
}

QString runProcessAndReadStdout_local(const QString& program, const QStringList& args)
{
    Q_UNUSED(program);
    Q_UNUSED(args);
    // TZ-100 runtime acceptance: synchronous external probes are disabled.
    // Wi-Fi auto-detection must not spin a nested event loop or block startup/settings UI.
    qWarning() << "SyncSettingsService: external process probe disabled; use async sync backend"
               << "uiThread=" << isUiThread_local();
    return QString();
}

QString currentWindowsWifiSsid_local()
{
#ifdef Q_OS_WIN
    const QString output = runProcessAndReadStdout_local(QStringLiteral("netsh"), {QStringLiteral("wlan"), QStringLiteral("show"), QStringLiteral("interfaces")});
    const QStringList lines = output.split(QRegularExpression(QStringLiteral("\r?\n")), Qt::SkipEmptyParts);
    const QRegularExpression rx(QStringLiteral(R"(^\s*SSID\s*:\s*(.+?)\s*$)"));
    for (const QString& line : lines) {
        if (line.contains(QStringLiteral("BSSID"), Qt::CaseInsensitive)) continue;
        const auto m = rx.match(line);
        if (m.hasMatch()) {
            const QString ssid = m.captured(1).trimmed();
            if (!ssid.isEmpty()) return ssid;
        }
    }
#endif
    return QString();
}

QSet<QString> savedWindowsWifiProfiles_local()
{
    QSet<QString> result;
#ifdef Q_OS_WIN
    const QString output = runProcessAndReadStdout_local(QStringLiteral("netsh"), {QStringLiteral("wlan"), QStringLiteral("show"), QStringLiteral("profiles")});
    const QStringList lines = output.split(QRegularExpression(QStringLiteral("\r?\n")), Qt::SkipEmptyParts);
    const QStringList markers = {
        QString::fromUtf8("All User Profile"),
        QString::fromUtf8("Усі профілі користувачів"),
        QString::fromUtf8("Все профили пользователей")
    };
    for (const QString& line : lines) {
        for (const QString& markerText : markers) {
            const int idx = line.indexOf(markerText, 0, Qt::CaseInsensitive);
            if (idx >= 0) {
                const int colon = line.indexOf(QLatin1Char(':'), idx);
                if (colon >= 0) {
                    const QString value = line.mid(colon + 1).trimmed();
                    if (!value.isEmpty()) result.insert(value);
                }
            }
        }
    }
#endif
    return result;
}

} // namespace


SyncSettingsPayload load()
{
    SyncSettingsPayload payload;
    const QString defaultInboxRoot = QDir::toNativeSeparators(SyncPayloadUtils::defaultSyncInboxDir());
    payload.enabled = Database::getSetting(QStringLiteral("settings.int.sync.enabled"), false).toBool();
    payload.desktopName = Database::getSetting(QStringLiteral("settings.int.sync.desktop_name"), QString::fromUtf8("CarService Desktop")).toString();
    payload.desktopCode = Database::getSetting(QStringLiteral("settings.int.sync.desktop_code"), QStringLiteral("CS-DESKTOP-01")).toString();
    payload.wifiSsid = Database::getSetting(QStringLiteral("settings.int.sync.wifi_ssid"), QString::fromUtf8("CarService-Desktop")).toString();
    payload.wifiPassword = Database::getSetting(QStringLiteral("settings.int.sync.wifi_password"), QString::fromUtf8("12345678")).toString();
    payload.transport = Database::getSetting(QStringLiteral("settings.int.sync.transport"), QStringLiteral("local_json")).toString();
    payload.inboxDir = Database::getSetting(QStringLiteral("settings.int.sync.inbox_dir"), defaultInboxRoot).toString();
    payload.autoPlate = Database::getSetting(QStringLiteral("settings.int.sync.auto_plate"), true).toBool();
    payload.autoParts = Database::getSetting(QStringLiteral("settings.int.sync.auto_parts"), true).toBool();
    payload.acceptPhotos = Database::getSetting(QStringLiteral("settings.int.sync.accept_photos"), true).toBool();
    payload.btUuid = Database::getSetting(QStringLiteral("settings.int.sync.bt_uuid"), QStringLiteral("e8e10f95-1a70-4b27-9ccf-02010264e9c8")).toString();
    payload.btPreferredDevice = Database::getSetting(QStringLiteral("settings.int.sync.bt_preferred_device"), QString()).toString();
    return payload;
}

bool save(const SyncSettingsPayload& payload,
          bool* changed,
          QString* effectiveInboxDir)
{
    const QString defaultInboxRoot = QDir::toNativeSeparators(SyncPayloadUtils::defaultSyncInboxDir());
    const QString trimmedInputPath = payload.inboxDir.trimmed();
    QDir dir(QDir::fromNativeSeparators(trimmedInputPath.isEmpty() ? defaultInboxRoot : trimmedInputPath));
    if (!dir.exists()) dir.mkpath(QStringLiteral("."));
    if (!dir.exists(QStringLiteral("processed"))) dir.mkpath(QStringLiteral("processed"));
    const QString normalizedInboxDir = QDir::toNativeSeparators(dir.absolutePath());
    if (effectiveInboxDir) *effectiveInboxDir = normalizedInboxDir;

    const bool hasChanges =
        Database::getSetting(QStringLiteral("settings.int.sync.enabled"), false).toBool() != payload.enabled ||
        Database::getSetting(QStringLiteral("settings.int.sync.desktop_name"), QString::fromUtf8("CarService Desktop")).toString().trimmed() != payload.desktopName.trimmed() ||
        Database::getSetting(QStringLiteral("settings.int.sync.desktop_code"), QStringLiteral("CS-DESKTOP-01")).toString().trimmed() != payload.desktopCode.trimmed() ||
        Database::getSetting(QStringLiteral("settings.int.sync.wifi_ssid"), QString::fromUtf8("CarService-Desktop")).toString().trimmed() != payload.wifiSsid.trimmed() ||
        Database::getSetting(QStringLiteral("settings.int.sync.wifi_password"), QString::fromUtf8("12345678")).toString().trimmed() != payload.wifiPassword.trimmed() ||
        Database::getSetting(QStringLiteral("settings.int.sync.transport"), QStringLiteral("local_json")).toString().trimmed() != payload.transport.trimmed() ||
        Database::getSetting(QStringLiteral("settings.int.sync.inbox_dir"), defaultInboxRoot).toString().trimmed() != normalizedInboxDir ||
        Database::getSetting(QStringLiteral("settings.int.sync.auto_plate"), true).toBool() != payload.autoPlate ||
        Database::getSetting(QStringLiteral("settings.int.sync.auto_parts"), true).toBool() != payload.autoParts ||
        Database::getSetting(QStringLiteral("settings.int.sync.accept_photos"), true).toBool() != payload.acceptPhotos ||
        Database::getSetting(QStringLiteral("settings.int.sync.bt_uuid"), QStringLiteral("e8e10f95-1a70-4b27-9ccf-02010264e9c8")).toString().trimmed() != payload.btUuid.trimmed() ||
        Database::getSetting(QStringLiteral("settings.int.sync.bt_preferred_device"), QString()).toString().trimmed() != payload.btPreferredDevice.trimmed();
    if (changed) *changed = hasChanges;

    Database::setSetting(QStringLiteral("settings.int.sync.enabled"), payload.enabled);
    Database::setSetting(QStringLiteral("settings.int.sync.desktop_name"), payload.desktopName.trimmed());
    Database::setSetting(QStringLiteral("settings.int.sync.desktop_code"), payload.desktopCode.trimmed());
    Database::setSetting(QStringLiteral("settings.int.sync.wifi_ssid"), payload.wifiSsid.trimmed());
    Database::setSetting(QStringLiteral("settings.int.sync.wifi_password"), payload.wifiPassword.trimmed());
    Database::setSetting(QStringLiteral("settings.int.sync.transport"), payload.transport.trimmed());
    Database::setSetting(QStringLiteral("settings.int.sync.inbox_dir"), normalizedInboxDir);
    Database::setSetting(QStringLiteral("settings.int.sync.auto_plate"), payload.autoPlate);
    Database::setSetting(QStringLiteral("settings.int.sync.auto_parts"), payload.autoParts);
    Database::setSetting(QStringLiteral("settings.int.sync.accept_photos"), payload.acceptPhotos);
    Database::setSetting(QStringLiteral("settings.int.sync.bt_uuid"), payload.btUuid.trimmed());
    Database::setSetting(QStringLiteral("settings.int.sync.bt_preferred_device"), payload.btPreferredDevice.trimmed());
    Database::setSetting(QStringLiteral("settings.int.sync.last_saved_at"), QDateTime::currentDateTime().toString(Qt::ISODate));
    return true;
}

QList<WindowsWifiNetworkItem> queryWindowsWifiNetworks()
{
    QList<WindowsWifiNetworkItem> result;
#ifdef Q_OS_WIN
    const QString currentSsid = currentWindowsWifiSsid_local();
    const QSet<QString> savedProfiles = savedWindowsWifiProfiles_local();
    const QString output = runProcessAndReadStdout_local(QStringLiteral("netsh"), {QStringLiteral("wlan"), QStringLiteral("show"), QStringLiteral("networks"), QStringLiteral("mode=Bssid")});
    const QStringList lines = output.split(QRegularExpression(QStringLiteral("\r?\n")), Qt::SkipEmptyParts);
    const QRegularExpression rx(QStringLiteral(R"(^\s*SSID\s+\d+\s*:\s*(.*)$)"));
    QSet<QString> seen;
    for (const QString& line : lines) {
        const auto m = rx.match(line);
        if (!m.hasMatch()) continue;
        const QString ssid = m.captured(1).trimmed();
        if (ssid.isEmpty() || seen.contains(ssid)) continue;
        seen.insert(ssid);
        WindowsWifiNetworkItem item;
        item.ssid = ssid;
        item.connected = (!currentSsid.isEmpty() && ssid.compare(currentSsid, Qt::CaseInsensitive) == 0);
        item.saved = savedProfiles.contains(ssid);
        result.push_back(item);
    }
    if (!currentSsid.isEmpty() && !seen.contains(currentSsid)) {
        WindowsWifiNetworkItem item;
        item.ssid = currentSsid;
        item.connected = true;
        item.saved = savedProfiles.contains(currentSsid);
        result.push_front(item);
    }
#endif
    return result;
}

} // namespace SyncSettingsService
