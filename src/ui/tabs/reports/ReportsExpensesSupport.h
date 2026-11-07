#pragma once

#include <QStringList>

#include "core/services/ReportsDataLoader.h"
#include "core/services/ReportsPdfRenderer.h"
#include "core/services/ReportsSyncSupport.h"

class QWidget;
class QLineEdit;
class QPushButton;
class QGridLayout;
class QLabel;

namespace ReportsExpensesSupport {

struct ExpensesWidgets {
    QWidget* page = nullptr;
    QLineEdit* elecPrevEdit = nullptr;
    QLineEdit* elecCurrEdit = nullptr;
    QLineEdit* elecTariffEdit = nullptr;
    QLineEdit* elecKwhEdit = nullptr;
    QLineEdit* elecCostEdit = nullptr;
    QLineEdit* cleaningEdit = nullptr;
    QLineEdit* cleaningNoteEdit = nullptr;
    QLineEdit* softwareEdit = nullptr;
    QLineEdit* softwareNoteEdit = nullptr;
    QLineEdit* heatingEdit = nullptr;
    QLineEdit* heatingNoteEdit = nullptr;
    QLineEdit* amortizationEdit = nullptr;
    QLineEdit* amortizationNoteEdit = nullptr;
    QLineEdit* rentEdit = nullptr;
    QLineEdit* rentNoteEdit = nullptr;
    QLineEdit* otherEdit = nullptr;
    QLineEdit* otherNoteEdit = nullptr;
    QLineEdit* expensesTotalEdit = nullptr;
    QPushButton* expensesSaveButton = nullptr;
    QPushButton* electricityPdfButton = nullptr;
};

struct ExpensesRecalcResult {
    double kwh = 0.0;
    double electricityCost = 0.0;
    double total = 0.0;
    QStringList stats;
};

ExpensesWidgets buildExpensesPage(QWidget* parent);
void applySnapshot(const ExpensesSnapshot& snapshot, const ExpensesWidgets& widgets);
ExpensesRecalcResult recalcForm(const ExpensesWidgets& widgets, bool persistTariffSetting);
ReportsSyncSupport::ExpensesSavePayload buildSavePayload(const ExpensesWidgets& widgets);
ReportsPdfRenderer::ExpensesFormData buildPdfData(const ExpensesWidgets& widgets);

} // namespace ReportsExpensesSupport
