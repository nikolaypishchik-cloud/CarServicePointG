#pragma once

#include <QList>
#include <QSqlDatabase>
#include <QString>

struct WorkOrderServiceRow {
    int id = 0;
    int orderNumber = 0;
    QString title;
    double qty = 1.0;
    double price = 0.0;
    double amount = 0.0;
    QString masterId;
    QString masterName;
};

class ServiceRepository
{
public:
    static QList<WorkOrderServiceRow> listByOrder(QSqlDatabase database, int orderNumber);
    static QList<WorkOrderServiceRow> listByOrderUid(QSqlDatabase database, const QString& orderUid);

    static bool insert(QSqlDatabase database,
                       int orderNumber,
                       const WorkOrderServiceRow& row,
                       int* newServiceId,
                       QString* errorText);

    static bool update(QSqlDatabase database,
                       int orderNumber,
                       int serviceId,
                       const WorkOrderServiceRow& row,
                       QString* errorText);

    static bool remove(QSqlDatabase database,
                       int orderNumber,
                       int serviceId,
                       QString* errorText);

    static bool replaceMasterLink(QSqlDatabase database,
                                  int serviceId,
                                  const QString& masterId,
                                  const QString& masterName,
                                  QString* errorText);
};
