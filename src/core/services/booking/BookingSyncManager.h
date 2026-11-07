#pragma once

#include "core/services/booking/BookingRecord.h"

#include <QObject>
#include <QPointer>
#include <QDateTime>
#include <QTimer>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

class BookingSyncManager final : public QObject
{
public:
    using NotificationCallback = std::function<void(const BookingRecord&)>;

    explicit BookingSyncManager(QObject* parent = nullptr);
    ~BookingSyncManager() override;

    void setNotificationCallback(NotificationCallback callback);
    void start();
    void stop();
    void syncNow();
    void updateBookingStatus(const BookingRecord& booking, const QString& status, const QString& reason = QString());

private:
    void applySettings();
    void handleSyncReply(QNetworkReply* reply);
    void handleBookingFromServer(const BookingRecord& booking);
    void sendStatusPatch(const BookingRecord& booking, const QString& status, const QString& reason);
    void publishAvailability(bool force = false);

    QNetworkAccessManager* m_network = nullptr;
    QTimer m_timer;
    NotificationCallback m_notificationCallback;
    bool m_running = false;
    bool m_requestInFlight = false;
    bool m_availabilityRequestInFlight = false;
    QDateTime m_lastAvailabilityPublish;
    int m_syncCounter = 0;
    QString m_baseUrl;
    QString m_token;
};
