#pragma once

#include <QObject>
#include <QString>
#include <functional>

class OpenDataBotClient
{
public:
    struct VehicleLookupResult {
        bool success = false;
        QString error;
        QString vin;
        QString plate;
        QString carDisplay;
        QString color;
        QString engineVolume;
        QString fuelType;
        int year = 0;
        bool demo = false;
    };

    static bool isConfigured();
    static QString configuredBaseUrl();
    static QString buildPlateLookupUrl(const QString& plate);
    static void lookupByPlate(QObject* context,
                              const QString& plate,
                              std::function<void(const VehicleLookupResult&)> callback);
};
