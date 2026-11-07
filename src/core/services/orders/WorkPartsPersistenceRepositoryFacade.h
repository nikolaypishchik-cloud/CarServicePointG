#pragma once

#include "core/repository/PartRepository.h"
#include "core/repository/ServiceRepository.h"

#include <QSqlDatabase>
#include <QString>

// P2 architecture boundary:
// UI modules must not call repository classes directly. This application-service
// facade keeps current persistence behavior intact while moving repository
// dependency ownership to the core/services layer.
class WorkPartsPersistenceRepositoryFacade
{
public:
    static bool insertPart(QSqlDatabase database,
                           int orderNumber,
                           const WorkOrderPartRow& row,
                           int* newPartId,
                           QString* errorText);

    static bool updatePart(QSqlDatabase database,
                           int orderNumber,
                           int partId,
                           const WorkOrderPartRow& row,
                           QString* errorText);

    static bool removePart(QSqlDatabase database,
                           int orderNumber,
                           int partId,
                           QString* errorText);

    static bool insertService(QSqlDatabase database,
                              int orderNumber,
                              const WorkOrderServiceRow& row,
                              int* newServiceId,
                              QString* errorText);

    static bool updateService(QSqlDatabase database,
                              int orderNumber,
                              int serviceId,
                              const WorkOrderServiceRow& row,
                              QString* errorText);

    static bool removeService(QSqlDatabase database,
                              int orderNumber,
                              int serviceId,
                              QString* errorText);

    static bool replaceServiceMasterLink(QSqlDatabase database,
                                         int serviceId,
                                         const QString& masterId,
                                         const QString& masterName,
                                         QString* errorText);
};
