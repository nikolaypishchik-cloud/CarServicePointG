#pragma once

#include <QSqlDatabase>
#include <QString>

class OrderSnapshotService
{
public:
    struct OrderSnapshot {
        int number = 0;
        int visibleNumber = 0;
        QString openedAt;
        QString closedAt;
        QString status;
        QString plate;
        QString plateNorm;
        QString vin;
        QString car;
        QString carBrand;
        QString carModel;
        QString color;
        int carYear = 0;
        int odometer = 0;
        QString engineType;
        QString engineVolume;
        QString fuelType;
        QString client;
        QString ownerName;
        QString clientPhone;
        QString masterId;
        QString masterNameClosed;
        double discount = 0.0;
    QString discountMode = QStringLiteral("legacy");
    };

    static bool loadByNumber(QSqlDatabase database,
                             int orderNumber,
                             OrderSnapshot* outSnapshot,
                             QString* errorText = nullptr);
};
