#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

namespace ReportsPdfExpenseSupport {

struct AdminSalaryTotals {
    double margin = 0.0;
    double clientTotal = 0.0;
    double purchaseTotal = 0.0;
    double partsQty = 0.0;
    int ordersCount = 0;
};

struct ExpenseBreakdown {
    double electricity = 0.0;
    double cleaning = 0.0;
    double software = 0.0;
    double heating = 0.0;
    double amortization = 0.0;
    double rent = 0.0;
    double other = 0.0;

    double total() const {
        return electricity + cleaning + software + heating + amortization + rent + other;
    }
};

struct ExpenseNotes {
    QString cleaning;
    QString software;
    QString heating;
    QString amortization;
    QString rent;
    QString other;
};

QMap<QString, QString> loadAdminSalarySources();
AdminSalaryTotals collectAdminSalaryTotals(const QString& df, const QString& dt);
double adminSalaryBaseForSource(const AdminSalaryTotals& totals, const QString& source);
QString expenseNoteJoined(const QString& base, const QString& note);
QString mergeExpenseNotes(const QStringList& notes);
double periodExpensesTotal(const QString& df, const QString& dt);
ExpenseBreakdown periodExpenseBreakdown(const QString& df, const QString& dt);
ExpenseNotes periodExpenseNotes(const QString& df, const QString& dt);

} // namespace ReportsPdfExpenseSupport
