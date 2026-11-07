#pragma once

#include "core/services/orders/PartMarkupPolicyService.h"

// P2 architecture module boundary.
// Helper declarations shared by the separately compiled ReportsDataLoader slices.
// The helper implementations live in ReportsDataLoaderLocalSupport.cpp.

#include "core/services/ReportsDataLoader.h"
#include "infrastructure/db/sqlite/Db.h"

#include <QList>
#include <QMap>
#include <QSqlDatabase>
#include <QString>
#include <QVariant>

namespace ReportsDataLoader::LocalSupport {

QString parsePlateForUi(const QString& value);
QString money0(double value);
QString normalizeInvoiceNumberForReports(QString raw);
QString resolveStoreForReports(const QString& rawStore, const QString& rawInvoice);
QString canonicalizeOilGradesForReport(QString text);
QString detectOilGradeForReport(const QString& text);
QString normalizeOilGradeForReport(QString text);
bool oilGradeLessThan(const QString& a, const QString& b);
bool isMotorOilName(QString name);
QVariant reportsWorkerSetting(QSqlDatabase& db, const QString& key, const QVariant& def = QVariant(), QString* error = nullptr);
QList<MasterRow> loadMastersForPeriodForReports(QSqlDatabase& db, const QString& fromDateIso, const QString& toDateIso, QString* error);
QString makeReportsWorkerConnectionName(const QString& suffix);
QString payrollMasterNameKey(QString value);

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

QList<MasterRow> orderedPersonnelForReports(const QList<MasterRow>& personnel);
double adminSalaryBaseForSource(const AdminSalaryTotals& totals, const QString& source);
PayrollPercentResolver makePayrollPercentResolverForReports(QSqlDatabase& db, QString* error);
QMap<QString, QString> loadAdminSalarySourcesForReports(QSqlDatabase& db, QString* error);
double periodExpensesTotalForReports(QSqlDatabase& db, const QString& df, const QString& dt, QString* error);
AdminSalaryTotals collectAdminSalaryTotalsForReports(QSqlDatabase& db, const QString& df, const QString& dt, QString* error);

} // namespace ReportsDataLoader::LocalSupport
