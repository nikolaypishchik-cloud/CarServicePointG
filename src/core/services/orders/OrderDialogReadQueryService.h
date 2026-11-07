#pragma once

#include "core/services/orders/OrderSnapshotService.h"

#include "core/crm/CrmFinancialService.h"
#include <QString>
#include <QList>
#include "core/services/orders/OrderFinancialService.h"

class OrderDialogReadQueryService
{
public:
    static CrmFinancialService::Snapshot settlement(int number, const QString& uid);
    static bool loadCurrentStatus(int orderNumber, QString* status, QString* errorText = nullptr);
    static bool loadVehicleHistory(const QString& plate, const QString& vin,
                                  QList<OrderSnapshotService::OrderSnapshot>* result,
                                  QString* errorText = nullptr);
    struct CrmVehiclePrefill {
        QString plate,vin,brand,model,engine,fuelType,color,clientName,phone;
        int year=0,mileage=0,previousOrder=0;
        double discountPercent=0;
    };
    static bool loadCrmVehiclePrefill(const QString& clientUid,const QString& vehicleUid,
                                      CrmVehiclePrefill* result,QString* errorText=nullptr);
    static bool loadSnapshotByNumber(int orderNumber,
                                     OrderSnapshotService::OrderSnapshot* outSnapshot,
                                     QString* errorText = nullptr);
    static bool loadLatestClosedVehicleSnapshotByPlate(const QString& plateText,
                                                       OrderSnapshotService::OrderSnapshot* outSnapshot,
                                                       QString* errorText = nullptr);
    static QString canonicalOrderUidForInternalNumber(int orderNumber);
    static OrderFinancialService::TotalsResult financialTotals(int orderNumber);
    static double servicesTotal(int orderNumber);
    static double partsTotal(int orderNumber);
};
