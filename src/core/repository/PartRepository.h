#pragma once

#include <QList>
#include <QSqlDatabase>
#include <QString>

struct WorkOrderPartRow {
    int id = 0;
    int orderNumber = 0;
    QString orderedAt;
    QString deliveryDate;
    QString installedAt;
    QString store;
    QString partNumber;
    QString name;
    QString note;
    QString manufacturer;
    double qty = 1.0;
    double clientPrice = 0.0;
    double clientAmount = 0.0;
    double purchasePrice = 0.0;
    double purchaseAmount = 0.0;
    double markupPercent = 0.0;
    bool manualMarkupOverride = false;
    QString markupSource = QStringLiteral("policy");
    QString pricingUpdatedAt;
    int pricingCalcVersion = 1;
    int serviceId = 0;
};

class PartRepository
{
public:
    static QList<WorkOrderPartRow> listByOrder(QSqlDatabase database, int orderNumber);
    static QList<WorkOrderPartRow> listByOrderUid(QSqlDatabase database, const QString& orderUid);

    static bool insert(QSqlDatabase database,
                       int orderNumber,
                       const WorkOrderPartRow& row,
                       int* newPartId,
                       QString* errorText);

    static bool update(QSqlDatabase database,
                       int orderNumber,
                       int partId,
                       const WorkOrderPartRow& row,
                       QString* errorText);

    static bool remove(QSqlDatabase database,
                       int orderNumber,
                       int partId,
                       QString* errorText);

    static bool syncRows(QSqlDatabase database,
                         int orderNumber,
                         const QList<WorkOrderPartRow>& rows,
                         QString* errorText);
};
