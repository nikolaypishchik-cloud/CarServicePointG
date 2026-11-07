#include "core/services/orders/WorkPartsPersistenceRepositoryFacade.h"

bool WorkPartsPersistenceRepositoryFacade::insertPart(QSqlDatabase database,
                                                      int orderNumber,
                                                      const WorkOrderPartRow& row,
                                                      int* newPartId,
                                                      QString* errorText)
{
    return PartRepository::insert(database, orderNumber, row, newPartId, errorText);
}

bool WorkPartsPersistenceRepositoryFacade::updatePart(QSqlDatabase database,
                                                      int orderNumber,
                                                      int partId,
                                                      const WorkOrderPartRow& row,
                                                      QString* errorText)
{
    return PartRepository::update(database, orderNumber, partId, row, errorText);
}

bool WorkPartsPersistenceRepositoryFacade::removePart(QSqlDatabase database,
                                                      int orderNumber,
                                                      int partId,
                                                      QString* errorText)
{
    return PartRepository::remove(database, orderNumber, partId, errorText);
}

bool WorkPartsPersistenceRepositoryFacade::insertService(QSqlDatabase database,
                                                         int orderNumber,
                                                         const WorkOrderServiceRow& row,
                                                         int* newServiceId,
                                                         QString* errorText)
{
    return ServiceRepository::insert(database, orderNumber, row, newServiceId, errorText);
}

bool WorkPartsPersistenceRepositoryFacade::updateService(QSqlDatabase database,
                                                         int orderNumber,
                                                         int serviceId,
                                                         const WorkOrderServiceRow& row,
                                                         QString* errorText)
{
    return ServiceRepository::update(database, orderNumber, serviceId, row, errorText);
}

bool WorkPartsPersistenceRepositoryFacade::removeService(QSqlDatabase database,
                                                         int orderNumber,
                                                         int serviceId,
                                                         QString* errorText)
{
    return ServiceRepository::remove(database, orderNumber, serviceId, errorText);
}

bool WorkPartsPersistenceRepositoryFacade::replaceServiceMasterLink(QSqlDatabase database,
                                                                    int serviceId,
                                                                    const QString& masterId,
                                                                    const QString& masterName,
                                                                    QString* errorText)
{
    return ServiceRepository::replaceMasterLink(database, serviceId, masterId, masterName, errorText);
}
