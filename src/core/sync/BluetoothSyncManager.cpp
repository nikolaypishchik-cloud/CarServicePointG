#include "core/sync/BluetoothSyncManager.h"

#include "core/sync/SyncPayloadUtils.h"
#include "core/sync/BluetoothSyncSoundSupport.h"
#include "infrastructure/db/sqlite/Db.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimer>
#include <QVariant>
#include <utility>

#if defined(CS_HAS_BLUETOOTH) && CS_HAS_BLUETOOTH
#include <QBluetoothAddress>
#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothDeviceInfo>
#include <QBluetoothLocalDevice>
#include <QBluetoothServer>
#include <QBluetoothServiceInfo>
#include <QBluetoothSocket>
#include <QBluetoothUuid>
#include <QHash>
#include <QSet>
#endif

namespace {
static QString sanitizeForFileName(QString text)
{
    text = text.trimmed();
    if (text.isEmpty()) return QStringLiteral("payload");
    text.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_\\-]+")), QStringLiteral("_"));
    text = text.left(40);
    if (text.isEmpty()) text = QStringLiteral("payload");
    return text;
}

#if defined(CS_HAS_BLUETOOTH) && CS_HAS_BLUETOOTH
static QString pairingTextFor(QBluetoothLocalDevice::Pairing pairing)
{
    switch (pairing) {
    case QBluetoothLocalDevice::Unpaired:
        return QString::fromUtf8("не спарований");
    case QBluetoothLocalDevice::Paired:
        return QString::fromUtf8("спарований");
    case QBluetoothLocalDevice::AuthorizedPaired:
        return QString::fromUtf8("спарований + довірений");
    }
    return QString::fromUtf8("невідомо");
}
#endif
}

#if defined(CS_HAS_BLUETOOTH) && CS_HAS_BLUETOOTH
class BluetoothSyncManager::Private {
public:
    QBluetoothLocalDevice* localDevice = nullptr;
    QBluetoothDeviceDiscoveryAgent* discovery = nullptr;
    QBluetoothServer* server = nullptr;
    QBluetoothServiceInfo serviceInfo;
    QList<QBluetoothSocket*> sockets;
    QHash<QBluetoothSocket*, QByteArray> buffers;
    QSet<QBluetoothSocket*> completedSockets;
};
#endif

BluetoothSyncManager::BluetoothSyncManager(QObject* parent)
    : QObject(parent)
    , m_serviceUuidString(QStringLiteral("e8e10f95-1a70-4b27-9ccf-02010264e9c8"))
    , m_serverName(QString::fromUtf8("CarService Mobile Sync"))
{
#if defined(CS_HAS_BLUETOOTH) && CS_HAS_BLUETOOTH
    d = new Private();
    d->localDevice = new QBluetoothLocalDevice(this);
    d->discovery = new QBluetoothDeviceDiscoveryAgent(this);

    connect(d->discovery, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered,
            this,
            [this](const QBluetoothDeviceInfo& info) {
                DeviceItem item;
                item.name = info.name().trimmed().isEmpty() ? QString::fromUtf8("Bluetooth-пристрій") : info.name().trimmed();
                item.address = info.address().toString();
                if (d->localDevice && d->localDevice->isValid()) {
                    const auto pairing = d->localDevice->pairingStatus(info.address());
                    item.pairingText = pairingTextFor(pairing);
                    item.paired = pairing != QBluetoothLocalDevice::Unpaired;
                } else {
                    item.pairingText = QString::fromUtf8("невідомо");
                }
                item.preferred = !m_preferredDeviceAddress.trimmed().isEmpty()
                                 && item.address.compare(m_preferredDeviceAddress, Qt::CaseInsensitive) == 0;
                upsertDevice(item);
                emit devicesChanged();
            });
    connect(d->discovery, &QBluetoothDeviceDiscoveryAgent::finished,
            this, &BluetoothSyncManager::handleDiscoveryFinished);
#if QT_VERSION >= QT_VERSION_CHECK(6, 2, 0)
    connect(d->discovery, &QBluetoothDeviceDiscoveryAgent::errorOccurred,
            this,
            [this](QBluetoothDeviceDiscoveryAgent::Error) {
                BluetoothSyncSoundSupport::stopSearchLoop();
                setStatus(QString::fromUtf8("Помилка Bluetooth-сканування. Перевір Bluetooth-адаптер і права доступу Windows."));
            });
#endif

    if (d->localDevice) {
        connect(d->localDevice, &QBluetoothLocalDevice::hostModeStateChanged,
                this,
                [this](QBluetoothLocalDevice::HostMode) {
                    BluetoothSyncSoundSupport::stopSearchLoop();
                    rebuildDeviceStates();
                    emit devicesChanged();
                    emit statusChanged(statusText());
                });
        connect(d->localDevice, &QBluetoothLocalDevice::pairingFinished,
                this,
                [this](const QBluetoothAddress&, QBluetoothLocalDevice::Pairing) {
                    rebuildDeviceStates();
                    emit devicesChanged();
                    setStatus(QString::fromUtf8("Стан спарювання оновлено."));
                });
#if QT_VERSION >= QT_VERSION_CHECK(6, 2, 0)
        connect(d->localDevice, &QBluetoothLocalDevice::errorOccurred,
                this,
                [this](QBluetoothLocalDevice::Error) {
                    BluetoothSyncSoundSupport::stopSearchLoop();
                    setStatus(QString::fromUtf8("Bluetooth-адаптер повідомив про помилку. Перевір, чи ввімкнений Bluetooth у Windows."));
                });
#endif
    }
#else
    Q_UNUSED(parent);
    setStatus(QString::fromUtf8("Qt Bluetooth не підключений у цій збірці. Живий Bluetooth-канал недоступний."));
#endif
}

BluetoothSyncManager::~BluetoothSyncManager()
{
    BluetoothSyncSoundSupport::stopSearchLoop();
    stopServer();
#if defined(CS_HAS_BLUETOOTH) && CS_HAS_BLUETOOTH
    delete d;
    d = nullptr;
#endif
}

bool BluetoothSyncManager::isBluetoothAvailable() const
{
#if defined(CS_HAS_BLUETOOTH) && CS_HAS_BLUETOOTH
    return d && d->localDevice && d->localDevice->isValid();
#else
    return false;
#endif
}

QString BluetoothSyncManager::adapterText() const
{
#if defined(CS_HAS_BLUETOOTH) && CS_HAS_BLUETOOTH
    if (!d || !d->localDevice || !d->localDevice->isValid()) {
        return QString::fromUtf8("Bluetooth-адаптер не знайдено або Qt Bluetooth недоступний.");
    }

    QString hostModeText;
    switch (d->localDevice->hostMode()) {
    case QBluetoothLocalDevice::HostPoweredOff:
        hostModeText = QString::fromUtf8("вимкнений");
        break;
    case QBluetoothLocalDevice::HostConnectable:
        hostModeText = QString::fromUtf8("увімкнений, доступний для з’єднань");
        break;
    case QBluetoothLocalDevice::HostDiscoverable:
        hostModeText = QString::fromUtf8("увімкнений, видимий для інших");
        break;
    case QBluetoothLocalDevice::HostDiscoverableLimitedInquiry:
        hostModeText = QString::fromUtf8("обмежена видимість");
        break;
    default:
        hostModeText = QString::fromUtf8("невідомий стан");
        break;
    }

    return QString::fromUtf8("Адаптер: %1\nАдреса: %2\nСтан: %3")
            .arg(d->localDevice->name().trimmed().isEmpty() ? QString::fromUtf8("Bluetooth-адаптер") : d->localDevice->name(),
                 d->localDevice->address().toString(),
                 hostModeText);
#else
    return QString::fromUtf8("Qt Bluetooth не підключений у цій збірці.");
#endif
}

QString BluetoothSyncManager::statusText() const
{
    return m_status;
}

QList<BluetoothSyncManager::DeviceItem> BluetoothSyncManager::devices() const
{
    return m_devices;
}

bool BluetoothSyncManager::serverRunning() const
{
#if defined(CS_HAS_BLUETOOTH) && CS_HAS_BLUETOOTH
    return d && d->server && d->server->isListening();
#else
    return false;
#endif
}

QString BluetoothSyncManager::serviceUuidString() const
{
    return m_serviceUuidString;
}

QString BluetoothSyncManager::preferredDeviceAddress() const
{
    return m_preferredDeviceAddress;
}

void BluetoothSyncManager::setServiceUuidString(const QString& uuidText)
{
    const QString trimmed = uuidText.trimmed();
    if (!trimmed.isEmpty()) m_serviceUuidString = trimmed;
}

void BluetoothSyncManager::setServerName(const QString& name)
{
    const QString trimmed = name.trimmed();
    if (!trimmed.isEmpty()) m_serverName = trimmed;
}

void BluetoothSyncManager::setInboxDir(const QString& path)
{
    m_inboxDir = path.trimmed();
}

void BluetoothSyncManager::setPreferredDeviceAddress(const QString& address)
{
    m_preferredDeviceAddress = address.trimmed();
    rebuildDeviceStates();
    emit devicesChanged();
}

void BluetoothSyncManager::refreshDevices()
{
#if defined(CS_HAS_BLUETOOTH) && CS_HAS_BLUETOOTH
    if (!d || !d->localDevice || !d->localDevice->isValid()) {
        BluetoothSyncSoundSupport::stopSearchLoop();
        setStatus(QString::fromUtf8("Bluetooth-адаптер не знайдено. Живе сканування недоступне."));
        return;
    }

    if (d->localDevice->hostMode() == QBluetoothLocalDevice::HostPoweredOff) {
        d->localDevice->powerOn();
    }

    m_devices.clear();
    emit devicesChanged();
    BluetoothSyncSoundSupport::stopSearchLoop();
    setStatus(QString::fromUtf8("Запущено Bluetooth-сканування. Шукаю мобільні пристрої поруч..."));
    BluetoothSyncSoundSupport::playSearchLoop();
    d->discovery->start();
#else
    BluetoothSyncSoundSupport::stopSearchLoop();
    setStatus(QString::fromUtf8("Qt Bluetooth не підключений у цій збірці. Сканування недоступне."));
#endif
}

void BluetoothSyncManager::startServer()
{
#if defined(CS_HAS_BLUETOOTH) && CS_HAS_BLUETOOTH
    if (!d || !d->localDevice || !d->localDevice->isValid()) {
        setStatus(QString::fromUtf8("Не вдалося запустити Bluetooth-канал: адаптер не знайдено."));
        return;
    }

    if (d->localDevice->hostMode() == QBluetoothLocalDevice::HostPoweredOff) {
        d->localDevice->powerOn();
    }

    stopServer();

    d->server = new QBluetoothServer(QBluetoothServiceInfo::RfcommProtocol, this);
    connect(d->server, &QBluetoothServer::newConnection,
            this, &BluetoothSyncManager::handleNewConnection);
#if QT_VERSION >= QT_VERSION_CHECK(6, 2, 0)
    connect(d->server, &QBluetoothServer::errorOccurred,
            this,
            [this](QBluetoothServer::Error) {
                setStatus(QString::fromUtf8("Помилка запуску Bluetooth RFCOMM-сервера."));
                emit serverStateChanged(false);
            });
#endif

    const QBluetoothUuid serviceUuid(m_serviceUuidString);
    d->serviceInfo = d->server->listen(serviceUuid, m_serverName);
    if (!d->serviceInfo.isValid()) {
        d->server->deleteLater();
        d->server = nullptr;
        setStatus(QString::fromUtf8("Не вдалося зареєструвати Bluetooth-сервіс. Перевір UUID каналу і чи не зайнятий адаптер."));
        emit serverStateChanged(false);
        return;
    }

    setStatus(QString::fromUtf8("Bluetooth-канал запущено. UUID: %1\nТелефон можна спарувати і підключити з мобільного додатку.")
              .arg(m_serviceUuidString));
    emit serverStateChanged(true);
#else
    setStatus(QString::fromUtf8("Qt Bluetooth не підключений у цій збірці. Живий канал недоступний."));
#endif
}

void BluetoothSyncManager::stopServer()
{
#if defined(CS_HAS_BLUETOOTH) && CS_HAS_BLUETOOTH
    if (!d) return;

    for (QBluetoothSocket* socket : std::as_const(d->sockets)) {
        if (!socket) continue;
        QObject::disconnect(socket, nullptr, this, nullptr);
        socket->close();
        socket->deleteLater();
    }
    d->sockets.clear();
    d->buffers.clear();
    d->completedSockets.clear();

    if (d->serviceInfo.isValid()) {
        d->serviceInfo.unregisterService();
        d->serviceInfo = QBluetoothServiceInfo();
    }

    if (d->server) {
        d->server->close();
        d->server->deleteLater();
        d->server = nullptr;
        emit serverStateChanged(false);
    }
#endif
}

void BluetoothSyncManager::requestPairing(const QString& address)
{
#if defined(CS_HAS_BLUETOOTH) && CS_HAS_BLUETOOTH
    if (!d || !d->localDevice || !d->localDevice->isValid()) {
        setStatus(QString::fromUtf8("Немає Bluetooth-адаптера для спарювання."));
        return;
    }
    const QString trimmed = address.trimmed();
    if (trimmed.isEmpty()) {
        setStatus(QString::fromUtf8("Спочатку вибери пристрій у списку Bluetooth."));
        return;
    }
    d->localDevice->requestPairing(QBluetoothAddress(trimmed), QBluetoothLocalDevice::Paired);
    setStatus(QString::fromUtf8("Надіслано запит на спарювання з пристроєм %1.").arg(trimmed));
#else
    Q_UNUSED(address);
#endif
}

void BluetoothSyncManager::removePairing(const QString& address)
{
#if defined(CS_HAS_BLUETOOTH) && CS_HAS_BLUETOOTH
    if (!d || !d->localDevice || !d->localDevice->isValid()) {
        setStatus(QString::fromUtf8("Немає Bluetooth-адаптера для керування спарюванням."));
        return;
    }
    const QString trimmed = address.trimmed();
    if (trimmed.isEmpty()) {
        setStatus(QString::fromUtf8("Спочатку вибери пристрій у списку Bluetooth."));
        return;
    }
    d->localDevice->requestPairing(QBluetoothAddress(trimmed), QBluetoothLocalDevice::Unpaired);
    setStatus(QString::fromUtf8("Надіслано запит на скасування спарювання для %1.").arg(trimmed));
#else
    Q_UNUSED(address);
#endif
}

void BluetoothSyncManager::setStatus(const QString& text)
{
    m_status = text;
    emit statusChanged(m_status);
}

void BluetoothSyncManager::rebuildDeviceStates()
{
#if defined(CS_HAS_BLUETOOTH) && CS_HAS_BLUETOOTH
    if (!d || !d->localDevice || !d->localDevice->isValid()) return;
    const auto connected = d->localDevice->connectedDevices();
    for (DeviceItem& item : m_devices) {
        const auto pairing = d->localDevice->pairingStatus(QBluetoothAddress(item.address));
        item.pairingText = pairingTextFor(pairing);
        item.paired = pairing != QBluetoothLocalDevice::Unpaired;
        item.connected = false;
        for (const QBluetoothAddress& addr : connected) {
            if (addr.toString().compare(item.address, Qt::CaseInsensitive) == 0) {
                item.connected = true;
                break;
            }
        }
        item.preferred = !m_preferredDeviceAddress.trimmed().isEmpty()
                         && item.address.compare(m_preferredDeviceAddress, Qt::CaseInsensitive) == 0;
    }
#endif
}

int BluetoothSyncManager::findDeviceIndexByAddress(const QString& address) const
{
    for (int i = 0; i < m_devices.size(); ++i) {
        if (m_devices.at(i).address.compare(address, Qt::CaseInsensitive) == 0) return i;
    }
    return -1;
}

void BluetoothSyncManager::upsertDevice(const DeviceItem& item)
{
    const int index = findDeviceIndexByAddress(item.address);
    if (index >= 0) {
        auto merged = m_devices.at(index);
        if (!item.name.trimmed().isEmpty()) merged.name = item.name;
        merged.pairingText = item.pairingText;
        merged.paired = item.paired;
        merged.connected = item.connected;
        merged.preferred = item.preferred;
        m_devices[index] = merged;
    } else {
        m_devices.push_back(item);
    }
}

bool BluetoothSyncManager::storeIncomingPayload(const QByteArray& rawJson, QString* storedPath, QString* summary)
{
    if (storedPath) storedPath->clear();
    if (summary) summary->clear();

    const QByteArray trimmed = rawJson.trimmed();
    if (trimmed.isEmpty()) return false;

    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(trimmed, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        if (summary) *summary = QString::fromUtf8("Отримано дані, але JSON пошкоджений: %1").arg(error.errorString());
        return false;
    }

    const QJsonObject root = doc.object();
    QString module = sanitizeForFileName(root.value(QStringLiteral("module")).toString());
    if (module.isEmpty()) module = sanitizeForFileName(root.value(QStringLiteral("type")).toString());
    const QString baseDir = m_inboxDir.trimmed().isEmpty() ? SyncPayloadUtils::syncInboxDir() : m_inboxDir.trimmed();
    QDir dir(QDir::fromNativeSeparators(baseDir));
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        if (summary) *summary = QString::fromUtf8("Не вдалося створити inbox для Bluetooth sync.");
        return false;
    }

    if (module.isEmpty()) module = QStringLiteral("payload");
    const QString fileName = QStringLiteral("bt_%1_%2.json")
        .arg(module,
             QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz")));
    const QString filePath = dir.filePath(fileName);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (summary) *summary = QString::fromUtf8("Не вдалося записати Bluetooth-пакет у inbox.");
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();

    const auto payload = SyncPayloadUtils::loadPayloadFromFile(filePath);
    if (storedPath) *storedPath = filePath;
    if (summary) {
        *summary = payload.isValid()
            ? SyncPayloadUtils::payloadDisplayLabel(payload)
            : QString::fromUtf8("Bluetooth-пакет записано в inbox.");
    }
    return true;
}

void BluetoothSyncManager::handleDiscoveryFinished()
{
    BluetoothSyncSoundSupport::stopSearchLoop();
    rebuildDeviceStates();
    emit devicesChanged();
    if (m_devices.isEmpty()) {
        setStatus(QString::fromUtf8("Bluetooth-сканування завершено. Пристроїв не знайдено."));
    } else {
        setStatus(QString::fromUtf8("Bluetooth-сканування завершено. Знайдено пристроїв: %1").arg(m_devices.size()));
    }
}

void BluetoothSyncManager::handleNewConnection()
{
#if defined(CS_HAS_BLUETOOTH) && CS_HAS_BLUETOOTH
    if (!d || !d->server) return;
    while (d->server->hasPendingConnections()) {
        QBluetoothSocket* socket = d->server->nextPendingConnection();
        if (!socket) break;
        d->sockets.push_back(socket);
        d->buffers.insert(socket, QByteArray());

        DeviceItem item;
        item.name = socket->peerName().trimmed().isEmpty() ? QString::fromUtf8("Мобільний пристрій") : socket->peerName().trimmed();
        item.address = socket->peerAddress().toString();
        item.connected = true;
        item.preferred = !m_preferredDeviceAddress.trimmed().isEmpty()
                         && item.address.compare(m_preferredDeviceAddress, Qt::CaseInsensitive) == 0;
        if (d->localDevice && d->localDevice->isValid()) {
            const auto pairing = d->localDevice->pairingStatus(socket->peerAddress());
            item.pairingText = pairingTextFor(pairing);
            item.paired = pairing != QBluetoothLocalDevice::Unpaired;
        } else {
            item.pairingText = QString::fromUtf8("невідомо");
        }
        upsertDevice(item);
        emit devicesChanged();

        connect(socket, &QBluetoothSocket::readyRead,
                this, &BluetoothSyncManager::handleSocketReadyRead);
        connect(socket, &QBluetoothSocket::disconnected,
                this, &BluetoothSyncManager::handleSocketDisconnected);
#if QT_VERSION >= QT_VERSION_CHECK(6, 2, 0)
        connect(socket, &QBluetoothSocket::errorOccurred,
                this,
                [this](QBluetoothSocket::SocketError) {
                    setStatus(QString::fromUtf8("Помилка Bluetooth-сокета під час обміну даними."));
                });
#endif

        const QString deviceLabel = socket->peerName().trimmed().isEmpty() ? socket->peerAddress().toString() : socket->peerName();
        setStatus(QString::fromUtf8("Bluetooth-пристрій підключився: %1\nОчікую JSON-пакет...").arg(deviceLabel));
        BluetoothSyncSoundSupport::playConnectSync();
    }
#endif
}

void BluetoothSyncManager::handleSocketReadyRead()
{
#if defined(CS_HAS_BLUETOOTH) && CS_HAS_BLUETOOTH
    auto* socket = qobject_cast<QBluetoothSocket*>(sender());
    if (!socket || !d || !d->buffers.contains(socket)) return;

    d->buffers[socket].append(socket->readAll());
    const QByteArray payloadBytes = d->buffers.value(socket).trimmed();
    if (payloadBytes.isEmpty()) return;

    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(payloadBytes, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return;
    }

    const QString address = socket->peerAddress().toString();
    const QString name = socket->peerName().trimmed();
    const bool unexpectedDevice = !m_preferredDeviceAddress.trimmed().isEmpty()
                                  && address.compare(m_preferredDeviceAddress, Qt::CaseInsensitive) != 0;

    QString filePath;
    QString summary;
    if (storeIncomingPayload(payloadBytes, &filePath, &summary)) {
        d->completedSockets.insert(socket);
        d->buffers[socket].clear();

        const QString label = name.isEmpty() ? address : name;
        const QString ackText = unexpectedDevice
            ? QString::fromUtf8("ACK STORED_FROM_UNEXPECTED_DEVICE\n")
            : QString::fromUtf8("ACK STORED\n");
        socket->write(ackText.toUtf8());

        if (unexpectedDevice) {
            setStatus(QString::fromUtf8("Отримано Bluetooth-пакет від %1 (%2), хоча вибраний інший телефон %3. Пакет однаково збережено.\n%4")
                      .arg(label, address, m_preferredDeviceAddress, summary));
        } else {
            setStatus(QString::fromUtf8("Отримано Bluetooth-пакет від %1\n%2").arg(label, summary));
        }
        emit packetStored(filePath, summary);
        BluetoothSyncSoundSupport::playConnectSync();

        QTimer::singleShot(150, socket, [socket]() {
            if (socket->state() == QBluetoothSocket::SocketState::ConnectedState) {
                socket->disconnectFromService();
            }
        });
    }
#endif
}

void BluetoothSyncManager::handleSocketDisconnected()
{
#if defined(CS_HAS_BLUETOOTH) && CS_HAS_BLUETOOTH
    auto* socket = qobject_cast<QBluetoothSocket*>(sender());
    if (!socket || !d) return;

    BluetoothSyncSoundSupport::playDisconnectLoss();

    const bool alreadyProcessed = d->completedSockets.contains(socket);
    const QByteArray payloadBytes = d->buffers.take(socket).trimmed();
    const QString address = socket->peerAddress().toString();
    const QString name = socket->peerName().trimmed();
    d->completedSockets.remove(socket);

    QString filePath;
    QString summary;
    if (!alreadyProcessed && !payloadBytes.isEmpty()) {
        const bool unexpectedDevice = !m_preferredDeviceAddress.trimmed().isEmpty()
                                      && address.compare(m_preferredDeviceAddress, Qt::CaseInsensitive) != 0;
        if (storeIncomingPayload(payloadBytes, &filePath, &summary)) {
            const QString label = name.isEmpty() ? address : name;
            if (unexpectedDevice) {
                setStatus(QString::fromUtf8("Отримано Bluetooth-пакет від %1 (%2), хоча вибраний інший телефон %3. Пакет однаково збережено.\n%4")
                          .arg(label, address, m_preferredDeviceAddress, summary));
            } else {
                setStatus(QString::fromUtf8("Отримано Bluetooth-пакет від %1\n%2").arg(label, summary));
            }
            emit packetStored(filePath, summary);
            BluetoothSyncSoundSupport::playConnectSync();
        } else {
            setStatus(summary.trimmed().isEmpty()
                      ? QString::fromUtf8("Bluetooth-пакет не вдалося зберегти.")
                      : summary);
        }
    }

    d->sockets.removeAll(socket);
    socket->deleteLater();

    if (!alreadyProcessed && payloadBytes.isEmpty()) {
        const QString label = name.isEmpty() ? address : name;
        setStatus(QString::fromUtf8("Bluetooth-зʼєднання розірвано: %1").arg(label.isEmpty() ? QString::fromUtf8("невідомий пристрій") : label));
    }

    const int index = findDeviceIndexByAddress(address);
    if (index >= 0) {
        m_devices[index].connected = false;
        emit devicesChanged();
    }
#endif
}

