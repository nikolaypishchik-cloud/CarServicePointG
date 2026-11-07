#pragma once

// P2 architecture module boundary.
// Helper declarations shared by the separately compiled ReportsPdfSupport slices.
// The helper implementations live in ReportsPdfSupportLocalSupport.cpp.

#include "core/services/ReportsPdfSupport.h"
#include "infrastructure/db/sqlite/Db.h"

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

namespace ReportsPdfSupport::LocalSupport {

struct PayrollPercentResolver {
    QMap<QString, double> byId;
    QMap<QString, double> activeByName;
};

struct AdminSalaryTotals {
    double margin = 0.0;
    double clientTotal = 0.0;
    double purchaseTotal = 0.0;
    double partsQty = 0.0;
    int ordersCount = 0;
};

struct PayrollHistoryIssuedTotals {
    double total = 0.0;
    QMap<QString, double> byMasterId;
    QMap<QString, double> byMasterName;
};

QString payrollMasterNameKey(QString value);
QString formatPlateForUi(const QString& value);
QString composePlateCarForUi(const QString& plate, const QString& car);
QList<MasterRow> orderedPersonnelForReports(const QList<MasterRow>& personnel);
PayrollPercentResolver makePayrollPercentResolver();
double resolvePayrollPercent(const MasterRow& master, const PayrollPercentResolver& resolver);
QString adminSalarySourceLabel(const QString& key);
QMap<QString, QString> loadAdminSalarySources();
AdminSalaryTotals collectAdminSalaryTotals(const QString& df, const QString& dt, QString* error = nullptr);
double adminSalaryBaseForSource(const AdminSalaryTotals& totals, const QString& source);
double periodExpensesTotal(const QString& df, const QString& dt, QString* error = nullptr);
QString resolveStoreForReports(const QString& rawStore, const QString& rawInvoice);
QString canonicalizeOilGradesForReport(QString text);
QString normalizeOilGradeForReport(QString grade);
QString detectOilGradeForReport(const QString& text);
bool isMotorOilNameImpl(QString name);
QMap<QString,double> loadMarkupMapForReportsImpl(const QString& key);
QString motorOilStoreName();
QString normalizeInvoiceNumberForReports(QString raw);
PayrollHistoryIssuedTotals collectIssuedPayrollTotals(const QString& df, const QString& dt, QString* error = nullptr);
double issuedPayrollForMaster(const PayrollHistoryIssuedTotals& totals, const MasterRow& master);
int partServiceMatchScore(const QString& serviceTitle, const QString& partName);

} // namespace ReportsPdfSupport::LocalSupport
