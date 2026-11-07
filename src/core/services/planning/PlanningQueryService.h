#pragma once

#include <QString>

class PlanningQueryService
{
public:
    static int findLatestVehicleOrderNumber(const QString& plateNorm);
    static bool vehicleWorkInProgress(const QString& plateNorm);
};
