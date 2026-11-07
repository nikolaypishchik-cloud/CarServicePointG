#pragma once

#include "core/services/MutationResult.h"

#include <QString>
#include <QList>
#include <QMap>

struct WorkOrderServiceRow;

class OrderMutationFacade
{
public:
    static QMap<QString,int> workshopPosts();
    static bool setWorkshopPost(int orderNumber, int post, QString* errorText);
    static MutationResult insertServiceWithOrderMasterDetailed(int orderNumber,
                                                               WorkOrderServiceRow row,
                                                               int* newServiceId = nullptr);

    static MutationResult updatePrimaryMasterDetailed(int orderNumber,
                                                      const QString& masterId,
                                                      const QString& masterName);

    static MutationResult setServiceMasterDetailed(int orderNumber,
                                                   int serviceId,
                                                   const QString& masterId,
                                                   const QString& masterName);

    static MutationResult setServicePriceDetailed(int orderNumber,
                                                  int serviceId,
                                                  double price);

    static MutationResult deleteServicesDetailed(int orderNumber,
                                                 const QList<int>& serviceIds);

    static MutationResult deletePartDetailed(int orderNumber,
                                             int partId);
    static bool insertServiceWithOrderMaster(int orderNumber,
                                             WorkOrderServiceRow row,
                                             int* newServiceId,
                                             QString* errorText);

    static bool updatePrimaryMaster(int orderNumber,
                                    const QString& masterId,
                                    const QString& masterName,
                                    QString* errorText);

    static bool setServiceMaster(int orderNumber,
                                 int serviceId,
                                 const QString& masterId,
                                 const QString& masterName,
                                 QString* errorText);

    static bool clearServiceMaster(int orderNumber,
                                   int serviceId,
                                   QString* errorText);

    static bool setServicePrice(int orderNumber,
                                int serviceId,
                                double price,
                                QString* errorText);

    static bool deleteService(int orderNumber,
                              int serviceId,
                              QString* errorText);

    static bool deleteServices(int orderNumber,
                               const QList<int>& serviceIds,
                               QString* errorText);

    static bool deletePart(int orderNumber,
                           int partId,
                           QString* errorText);
};
