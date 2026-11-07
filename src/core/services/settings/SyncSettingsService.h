#pragma once

#include <QString>
#include <QList>

namespace SyncSettingsService {

struct WindowsWifiNetworkItem {
    QString ssid;
    bool connected = false;
    bool saved = false;
};

struct SyncSettingsPayload {
    bool enabled = false;
    QString desktopName;
    QString desktopCode;
    QString wifiSsid;
    QString wifiPassword;
    QString transport;
    QString inboxDir;
    bool autoPlate = true;
    bool autoParts = true;
    bool acceptPhotos = true;
    QString btUuid;
    QString btPreferredDevice;
};

SyncSettingsPayload load();

bool save(const SyncSettingsPayload& payload,
          bool* changed = nullptr,
          QString* effectiveInboxDir = nullptr);

QList<WindowsWifiNetworkItem> queryWindowsWifiNetworks();

} // namespace SyncSettingsService
