#pragma once

#include <QDate>
#include <QTime>
#include <QString>

struct OrderDialogStateSnapshot
{
    int visibleNumber = 0;
    QDate openedDate;
    QTime openedTime;
    QString status;
    QString plate;
    QString vin;
    QString car;
    QString color;
    int carYear = 0;
    int odometer = 0;
    QString engineCode;
    QString engineVolume;
    QString fuelType;
    QString client;
    QString phone;
    QString masterId;
    double discount = 0.0;
    QString discountMode = QStringLiteral("legacy");
};

class OrderDialogStateSignature
{
public:
    static QString build(const OrderDialogStateSnapshot& snapshot);
};
