#pragma once

#include <QObject>
#include <QByteArray>
#include <QHash>
#include <QString>

class QLocalServer;
class QLocalSocket;
class QSharedMemory;

namespace CarService {

class SingleInstanceCoordinator final : public QObject
{
    Q_OBJECT

public:
    enum class StartResult {
        PrimaryInstance,
        SecondaryInstanceActivated,
        Unavailable
    };

    explicit SingleInstanceCoordinator(QString serverName = defaultServerName(), QObject* parent = nullptr);
    ~SingleInstanceCoordinator() override;

    StartResult acquireOrActivate(QString* errorText = nullptr);
    static QString defaultServerName();

signals:
    void activationRequested();

private:
    bool startServer();
    bool requestPrimaryActivation() const;
    void processPendingConnections();
    void processSocketData(QLocalSocket* socket);

    QString m_serverName;
    QSharedMemory* m_instanceLock = nullptr;
    QLocalServer* m_server = nullptr;
    QHash<QLocalSocket*, QByteArray> m_socketBuffers;
};

} // namespace CarService