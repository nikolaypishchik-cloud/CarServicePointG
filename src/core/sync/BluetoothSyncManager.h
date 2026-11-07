#pragma once

#include <QObject>
#include <QString>
#include <QList>

class BluetoothSyncManager : public QObject
{
    Q_OBJECT
public:
    struct DeviceItem {
        QString name;
        QString address;
        QString pairingText;
        bool paired = false;
        bool connected = false;
        bool preferred = false;
    };

    explicit BluetoothSyncManager(QObject* parent = nullptr);
    ~BluetoothSyncManager() override;

    bool isBluetoothAvailable() const;
    QString adapterText() const;
    QString statusText() const;
    QList<DeviceItem> devices() const;
    bool serverRunning() const;
    QString serviceUuidString() const;
    QString preferredDeviceAddress() const;

    void setServiceUuidString(const QString& uuidText);
    void setServerName(const QString& name);
    void setInboxDir(const QString& path);
    void setPreferredDeviceAddress(const QString& address);

public slots:
    void refreshDevices();
    void startServer();
    void stopServer();
    void requestPairing(const QString& address);
    void removePairing(const QString& address);

signals:
    void devicesChanged();
    void statusChanged(const QString& text);
    void packetStored(const QString& filePath, const QString& summary);
    void serverStateChanged(bool running);

private:
    void setStatus(const QString& text);
    void rebuildDeviceStates();
    int findDeviceIndexByAddress(const QString& address) const;
    void upsertDevice(const DeviceItem& item);
    bool storeIncomingPayload(const QByteArray& rawJson, QString* storedPath, QString* summary);

private slots:
    void handleDiscoveryFinished();
    void handleNewConnection();
    void handleSocketReadyRead();
    void handleSocketDisconnected();

private:
    QString m_serviceUuidString;
    QString m_serverName;
    QString m_inboxDir;
    QString m_preferredDeviceAddress;
    QString m_status;
    QList<DeviceItem> m_devices;

#if defined(CS_HAS_BLUETOOTH) && CS_HAS_BLUETOOTH
    class Private;
    Private* d = nullptr;
#else
    void* d = nullptr;
#endif
};

