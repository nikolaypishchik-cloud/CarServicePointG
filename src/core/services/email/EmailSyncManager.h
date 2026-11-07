#pragma once

#include "core/services/email/EmailNotification.h"

#include <QByteArray>
#include <QDateTime>
#include <QObject>
#include <QTimer>
#include <functional>

class QSslSocket;

class EmailSyncManager final : public QObject
{
public:
    using NotificationCallback = std::function<void(const EmailNotification&)>;

    explicit EmailSyncManager(QObject* parent = nullptr);
    ~EmailSyncManager() override;

    void setNotificationCallback(NotificationCallback callback);
    void start();
    void stop();
    void syncNow();
    void markViewed(quint64 uid);
    void postpone(quint64 uid, int minutes = 15);

private:
    enum class Stage {
        Idle,
        WaitingGreeting,
        Login,
        Status,
        Select,
        Search,
        Logout
    };

    void loadSettings();
    void resetSocket();
    void openSocket();
    void processIncomingData();
    void processLine(const QString& line);
    void processTaggedResponse(const QString& line);
    void sendCommand(const QString& command, Stage nextStage);
    void requestLogout();
    void completeCycle();
    void failCycle(const QString& reason);
    void applyUidNext();
    void publishNotification(quint64 uid);
    void storeLastViewedUid(quint64 uid);
    QString quoteImapValue(const QString& value) const;

    QSslSocket* m_socket = nullptr;
    QTimer m_timer;
    NotificationCallback m_notificationCallback;
    QByteArray m_rx;
    Stage m_stage = Stage::Idle;
    QString m_account;
    QString m_password;
    QString m_currentTag;
    int m_tagCounter = 0;
    int m_pollIntervalMs = 60000;
    quint64 m_lastViewedUid = 0;
    quint64 m_uidNext = 0;
    quint64 m_candidateUid = 0;
    quint64 m_postponedUid = 0;
    QDateTime m_postponedUntil;
    bool m_enabled = false;
    bool m_requestInFlight = false;
};
