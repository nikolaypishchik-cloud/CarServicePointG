#pragma once

#include "core/services/orders/PartMarkupPolicyService.h"

#include <QList>
#include <QMap>
#include <QString>

#include "core/services/ReportsSyncSupport.h"

namespace ReportsPdfSupport {

struct PeriodPdfRow {
    QString date;
    QString orderNumber;
    QString car;
    QString plate;
    QString workTitle;
    QString partName;
    QString qty;
    double partsPrice = 0.0;
    double added = 0.0;
    double worksPrice = 0.0;
    double total = 0.0;
};

struct PartPdfRow {
    QString store;
    QString date;
    QString orderNumber;
    QString name;
    QString oem;
    QString invoiceNumber;
    QString oilGrade;
    double purchase = 0.0;
    double purchaseUnitPrice = 0.0;
    double added = 0.0;
    QString qty;
    double qtyValue = 0.0;
    double clientPrice = 0.0;
};

struct OutsourcePdfRow {
    QString company;
    QString date;
    QString returnDate;
    QString orderNumber;
    QString partName;
    double repairCost = 0.0;
    double clientPrice = 0.0;
};

struct PayrollMasterPdf {
    QString masterId;
    QString masterName;
    double percent = 0.0;
    QList<PayrollOrderRow> orders;
    int ordersCount = 0;
    double worksTotal = 0.0;
    double accrued = 0.0;
    double issued = 0.0;
    double toIssue = 0.0;
    double sharedExpenses = 0.0;
};

struct ReportMarkupSnapshot {
    double defaultParts = 0.0;
    double defaultOil = 0.0;
    QMap<QString,double> partsMap;
    QMap<QString,double> oilMap;
    PartMarkupPolicyService::PolicySnapshot policy;
};

struct PartFinancials {
    double qty = 0.0;
    double purchasePrice = 0.0;
    double purchaseAmount = 0.0;
    double clientPrice = 0.0;
    double clientAmount = 0.0;
    double added = 0.0;
    double markupPercent = 0.0;
};

struct PartPeriodTotals {
    double purchaseTotal = 0.0;
    double clientTotal = 0.0;
    double marginTotal = 0.0;
    double qtyTotal = 0.0;
};

ReportMarkupSnapshot loadReportMarkupSnapshot();
PartFinancials resolvePartFinancialsForReports(const ReportMarkupSnapshot& snapshot,
                                               const QString& partName,
                                               const QString& store,
                                               double qty,
                                               double storedClientAmount,
                                               double storedPurchaseAmount,
                                               double storedPurchasePrice,
                                               double storedMarkupPercent,
                                               bool manualMarkupOverride);
PartFinancials resolvePartFinancialsForReports(const ReportMarkupSnapshot& snapshot,
                                               const QString& partName,
                                               const QString& store,
                                               double qty,
                                               double storedClientAmount,
                                               double storedPurchaseAmount,
                                               double storedPurchasePrice,
                                               double storedMarkupPercent,
                                               bool manualMarkupOverride, int pricingCalcVersion);
PartPeriodTotals collectClosedPartFinancialTotalsForReports(const QString& df, const QString& dt, QString* error = nullptr);

QList<PeriodPdfRow> collectPeriodPdfRows(const QString& df, const QString& dt,
                                         double& partsTotal, double& worksTotal, double& partsMargin,
                                         double& outsourceMargin, double& salaryTotal, double& expensesTotal, QString* error = nullptr);
QMap<QString, QList<PartPdfRow>> collectPartsPdfGroups(const QString& df, const QString& dt);
QMap<QString, QList<OutsourcePdfRow>> collectOutsourceGroups(const QString& df, const QString& dt,
                                                             int& count, double& repairTotal, double& clientTotal, QString* error = nullptr);
bool validateFinancialPdfSource(const QDate& from, const QDate& to, QString* error);
bool validateExpensesPdfSource(const QDate& from, const QDate& to, QString* error);
QList<PayrollMasterPdf> collectPayrollDetails(const QString& df, const QString& dt, QString* error = nullptr, bool includeWorkText = true);
QMap<QString,double> loadMarkupMapForReports(const QString& key);
bool isMotorOilName(QString name);

} // namespace ReportsPdfSupport
