#pragma once

#include <QList>
#include <QString>

struct PayrollOrderRow {
    QString dateStart;
    QString dateEnd;
    QString orderNumber;
    int internalOrderNumber = 0;
    QString car;
    QString engine;
    QString works;
    double amount = 0.0;
    double masterAmount = 0.0;
    bool absent = false;
};

struct ElectricityPeriodRow {
    QString periodFrom;
    QString periodTo;
    double meterBefore = 0.0;
    double meterAfter = 0.0;
    double consumedKwh = 0.0;
    double tariff = 0.0;
    double amount = 0.0;
};

struct ExpensesSavePayload {
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
    double elecKwh = 0.0;
    double elecCost = 0.0;
};

namespace ReportsSyncSupport {

using ::ExpensesSavePayload;

QString payrollWorksTextForMaster(const QString& orderNumber, const QString& masterId, double payrollPercent);
QList<PayrollOrderRow> collectPayrollOrdersForMaster(const QString& masterId, const QString& df, const QString& dt);
bool setPayrollOrderAbsent(const QString& masterId, int orderNumber, bool absent, QString* errorMessage = nullptr);
bool setPayrollOrderDeleted(const QString& masterId, int orderNumber, bool deleted, QString* errorMessage = nullptr);
bool saveExpensesRecord(const QString& df, const QString& dt, const ExpensesSavePayload& payload, QString* errorMessage = nullptr);
QList<ElectricityPeriodRow> loadElectricityPeriods();

} // namespace ReportsSyncSupport
