#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

struct PeriodSnapshotRow {
    QString date;
    QString orderNumber;
    int internalOrderNumber = 0;
    QString car;
    QString master;
    QString works;
    QString parts;
    QString total;
};

struct PeriodSnapshot {
    QList<PeriodSnapshotRow> rows;
    QStringList stats;
};

struct OrdersSnapshotRow {
    qint64 clientReceivedCents = 0;
    bool closed=false;
    int internalOrderNumber = 0;
    QString date;
    QString orderNumber;
    QString car;
    QString client;
    QString master;
    QString purchaseTotal;
    QString saleTotal;
    QStringList invoiceKeys, invoiceNumbers;
    QString invoiceTotal, paymentDue, paymentStatus;
};

struct OrdersSnapshot {
    QString clientPaymentError;
    QList<OrdersSnapshotRow> rows;
    QStringList stats;
};

struct PartsSnapshotRow {
    bool isStoreHeader = false;
    QString store;
    QString date;
    QString orderNumber;
    int internalOrderNumber = 0;
    QString partName;
    QString invoiceNumber;
    QString invoicePath;
    QString invoiceDate;
    QString purchase;
    QString net;
    QString qty;
    QString client;
    QString tooltip;
};

struct PartsSnapshot {
    QList<PartsSnapshotRow> rows;
    QStringList stats;
    QString oilHtml;
    QStringList tileTitles;
    QStringList tileBodies;
};

struct OutsourceSnapshotRow {
    int serviceId = 0;
    int internalOrderNumber = 0;
    QString sentDate;
    QString returnDate;
    QString company;
    QString orderNumber;
    QString partName;
    QString repairCost;
    QString clientPrice;
};

struct OutsourceSnapshot {
    QList<OutsourceSnapshotRow> rows;
    QStringList stats;
    QString error;
};

struct PayrollSnapshotRow {
    QString masterId;
    QString masterName;
    QString ordersCount;
    QString baseSum;
    QString percent;
    QString accrued;
    double accruedValue = 0.0;
};

struct PayrollSnapshot {
    QList<PayrollSnapshotRow> rows;
    QStringList stats;
};

struct ExpensesSnapshot {
    QString error;
    bool foundRow = false;
    double elecPrev = 0.0;
    double elecCurr = 0.0;
    double elecTariff = 0.0;
    double cleaning = 0.0;
    QString cleaningNote;
    double software = 0.0;
    QString softwareNote;
    double heating = 0.0;
    QString heatingNote;
    double amortization = 0.0;
    QString amortizationNote;
    double rent = 0.0;
    QString rentNote;
    double other = 0.0;
    QString otherNote;
};

struct TotalExpensesSnapshotRow {
    bool isTotal = false;
    qint64 expenseId = 0;
    QString sortDate;
    QString period;
    QString category;
    QString description;
    QString source;
    QString total;
};

struct TotalExpensesSnapshot {
    QList<TotalExpensesSnapshotRow> rows;
    QStringList stats;
    QString error;
    qint64 additionalCents = 0;
    qint64 novaPoshtaCents = 0;
    qint64 periodicCents = 0;
};

namespace ReportsDataLoader {

QString currentDatabasePath();

PeriodSnapshot buildPeriodSnapshot(const QString& dbPath, const QString& df, const QString& dt);
OrdersSnapshot buildOrdersSnapshot(const QString& dbPath, const QString& df, const QString& dt, const QString& filter = QString());
PartsSnapshot buildPartsSnapshot(const QString& dbPath, const QString& df, const QString& dt,
    double defaultParts, double defaultOil, const QMap<QString,double>& partsMap, const QMap<QString,double>& oilMap);
OutsourceSnapshot buildOutsourceSnapshot(const QString& dbPath, const QString& df, const QString& dt);
PayrollSnapshot buildPayrollSnapshot(const QString& dbPath, const QString& df, const QString& dt);
ExpensesSnapshot buildExpensesSnapshot(const QString& dbPath, const QString& df, const QString& dt);
ExpensesSnapshot buildExpensesSnapshot(const QString& df, const QString& dt);
TotalExpensesSnapshot buildTotalExpensesSnapshot(const QString& dbPath, const QString& df, const QString& dt);

} // namespace ReportsDataLoader
