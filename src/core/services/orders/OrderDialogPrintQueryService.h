#pragma once

#include <QString>
#include <QList>

struct OrderDialogPrintServiceRow {
    QString title;
    QString linkedMasters;
    QString outsourceCompany;
    double qty = 1.0;
    double price = 0.0;
    double payrollAmount = 0.0;
    bool hasPayrollMaster = false;
};

struct OrderDialogPrintPartRow {
    QString partNo;
    QString brand;
    QString name;
    double qty = 1.0;
    double price = 0.0;
    double amount = 0.0;
    QString store;
    QString purchaseDate;
    QString invoiceNumber;
    double purchasePrice = 0.0;
    double purchaseAmount = 0.0;
    bool stockMaterial = false;
    bool customerSupplied = false;
};

class OrderDialogPrintQueryService
{
public:
    static QList<OrderDialogPrintServiceRow> loadServiceRows(const QString& orderUid);
    static QList<OrderDialogPrintPartRow> loadPartRows(const QString& orderUid, bool useCurrentInventoryPrices = false);
};
