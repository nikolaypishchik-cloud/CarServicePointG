#pragma once
#include <QList>
#include <QSqlDatabase>
#include <QString>
#include <QVariantMap>

namespace OnlineBookingService {
struct BookingRequestRow {
    QString appointmentUid;
    QString status;
    QString clientName;
    QString phone;
    QString carText;
    QString serviceReason;
    QString desiredStartAt;
    QString comment;
};

QList<BookingRequestRow> listRecentRequests(int limit = 300, QString* errorText = nullptr);
bool upsertRequest(QSqlDatabase db, const QVariantMap& payload, QString* errorText = nullptr);
bool updateStatus(const QString& appointmentUid, const QString& status, QString* errorText = nullptr);
bool updateStatus(QSqlDatabase db, const QString& appointmentUid, const QString& status, QString* errorText = nullptr);
bool rescheduleRequest(const QString& appointmentUid, const QString& desiredStartAt, const QString& desiredEndAt, QString* errorText = nullptr);
bool rescheduleRequest(QSqlDatabase db, const QString& appointmentUid, const QString& desiredStartAt, const QString& desiredEndAt, QString* errorText = nullptr);
bool createOrderFromRequest(const QString& appointmentUid, QString* orderUid, QString* errorText = nullptr);
bool createOrderFromRequest(QSqlDatabase db, const QString& appointmentUid, QString* orderUid, QString* errorText = nullptr);
bool queueClientMessage(const QString& appointmentUid, const QString& body, QString* errorText = nullptr);
bool queueClientMessage(QSqlDatabase db, const QString& appointmentUid, const QString& body, QString* errorText = nullptr);
}
