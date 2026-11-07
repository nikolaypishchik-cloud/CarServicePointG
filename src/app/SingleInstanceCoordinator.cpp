#include "app/SingleInstanceCoordinator.h"

#include <QAbstractSocket>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSharedMemory>
#include <QStandardPaths>

#include <chrono>
#include <thread>
#include <utility>

namespace {

constexpr auto kActivationMessage = "activate";
constexpr int kActivationTimeoutMs = 1500;
constexpr int kConnectAttemptTimeoutMs = 150;
constexpr int kRetryDelayMs = 25;
constexpr int kMaximumMessageSize = 64;

} // namespace

namespace CarService {

SingleInstanceCoordinator::SingleInstanceCoordinator(QString serverName, QObject* parent)
    : QObject(parent)
    , m_serverName(std::move(serverName))
    , m_instanceLock(new QSharedMemory(m_serverName + QStringLiteral(".lock"), this))
    , m_server(new QLocalServer(this))
{
    connect(m_server, &QLocalServer::newConnection,
            this, &SingleInstanceCoordinator::processPendingConnections);
}

SingleInstanceCoordinator::~SingleInstanceCoordinator()
{
    if (m_server && m_server->isListening()) {
        m_server->close();
        QLocalServer::removeServer(m_serverName);
    }
    if (m_instanceLock && m_instanceLock->isAttached()) {
        m_instanceLock->detach();
    }
}

QString SingleInstanceCoordinator::defaultServerName()
{
    const QString appDataLocation = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QByteArray userScope = QCryptographicHash::hash(appDataLocation.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QStringLiteral("CarServicePointG.SingleInstance.%1").arg(QString::fromLatin1(userScope.left(16)));
}

SingleInstanceCoordinator::StartResult SingleInstanceCoordinator::acquireOrActivate(QString* errorText)
{
    if (m_instanceLock->isAttached()) return StartResult::PrimaryInstance;

    if (m_instanceLock->create(1)) {
        if (startServer()) return StartResult::PrimaryInstance;

        const QString serverError = m_server->errorString();
        if (m_server->serverError() == QAbstractSocket::AddressInUseError) {
            // The lock proves there is no live primary instance, so this can
            // only remove a stale local-server endpoint after an unclean exit.
            QLocalServer::removeServer(m_serverName);
            if (startServer()) return StartResult::PrimaryInstance;
        }
        m_instanceLock->detach();
        if (errorText) {
            *errorText = serverError.trimmed().isEmpty()
                ? QStringLiteral("Не вдалося створити канал активації застосунку.")
                : serverError;
        }
        return StartResult::Unavailable;
    }

    if (m_instanceLock->error() == QSharedMemory::AlreadyExists && requestPrimaryActivation()) {
        return StartResult::SecondaryInstanceActivated;
    }

    if (errorText) {
        if (m_instanceLock->error() == QSharedMemory::AlreadyExists) {
            *errorText = QStringLiteral("Вже відкритий екземпляр програми не відповідає на запит активації.");
        } else {
            *errorText = m_instanceLock->errorString();
        }
    }
    return StartResult::Unavailable;
}

bool SingleInstanceCoordinator::startServer()
{
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    return m_server->listen(m_serverName);
}

bool SingleInstanceCoordinator::requestPrimaryActivation() const
{
    QElapsedTimer elapsed;
    elapsed.start();
    const QByteArray message = QByteArray(kActivationMessage) + '\n';

    while (elapsed.elapsed() < kActivationTimeoutMs) {
        QLocalSocket socket;
        socket.connectToServer(m_serverName, QIODevice::ReadWrite);
        const int remainingMs = kActivationTimeoutMs - static_cast<int>(elapsed.elapsed());
        if (socket.waitForConnected(qMin(kConnectAttemptTimeoutMs, qMax(1, remainingMs)))) {
            if (socket.write(message) == message.size()) {
                socket.flush();
                if (socket.waitForReadyRead(qMax(1, remainingMs))) {
                    return socket.readAll().trimmed() == "ok";
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kRetryDelayMs));
    }
    return false;
}

void SingleInstanceCoordinator::processPendingConnections()
{
    while (QLocalSocket* socket = m_server->nextPendingConnection()) {
        connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
            processSocketData(socket);
        });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket]() {
            m_socketBuffers.remove(socket);
            socket->deleteLater();
        });
        processSocketData(socket);
    }
}

void SingleInstanceCoordinator::processSocketData(QLocalSocket* socket)
{
    if (!socket) return;

    QByteArray& buffer = m_socketBuffers[socket];
    buffer += socket->readAll();
    if (!buffer.contains('\n') && buffer.size() < kMaximumMessageSize) return;

    const QByteArray command = buffer.left(kMaximumMessageSize).trimmed();
    m_socketBuffers.remove(socket);

    if (command == kActivationMessage) {
        emit activationRequested();
        socket->write("ok\n");
        socket->flush();
    }
    socket->disconnectFromServer();
}

} // namespace CarService