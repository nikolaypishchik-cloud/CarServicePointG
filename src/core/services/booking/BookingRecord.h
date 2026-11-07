#pragma once

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QTime>

struct BookingRecord
{
    int id = 0;
    QString bookingUid;
    QString clientName;
    QString clientPhone;
    QString clientEmail;
    QString carMakeModel;
    QString carMake;
    QString carModel;
    QString carYear;
    QString fuelType;
    QString engineVolumeL;
    QString odometerKm;
    QString carNumber;
    QString serviceType;
    QString comment;
    QDate date;
    QTime timeFrom;
    QTime timeTo;
    QString status = QStringLiteral("pending");
    QString source = QStringLiteral("online");
    QDateTime createdAt;
    QDateTime updatedAt;
};

QString bookingStatusNormalized(const QString& status);
bool bookingStatusBlocksPlanning(const QString& status);
QString bookingDisplayTitle(const BookingRecord& booking);
QString bookingPlanningText(const BookingRecord& booking);
