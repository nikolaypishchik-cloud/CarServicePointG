#pragma once

#include <QWidget>
#include <functional>
#include <QHash>
#include <QtGlobal>

#include <atomic>
#include <memory>

#include "core/services/ReportsDataLoader.h"
#include "core/services/ReportsPdfRenderer.h"

class QDateEdit;
class QLabel;
class QScrollArea;
class QTabWidget;
class QTableView;
class QPushButton;
class QWidget;
class QLineEdit;
class QComboBox;
class QDoubleSpinBox;
class QTimer;
class QResizeEvent;
class QCloseEvent;
class PurchaseDebtsPage;
class PurchaseDebtAnalysisPage;
class InvoicesPage;
class ReportDashboard;

namespace ReportsTableSupport { class ReportsSnapshotTableModel; }


class ReportsPage : public QWidget
{
    Q_OBJECT
public:
    explicit ReportsPage(QWidget* parent=nullptr);

public slots:
    void refresh();
    void markDirty();
    void queueRefreshAll(int delayMs = 30);

signals:
    void requestInventoryPurchase(qint64 purchaseId);
    void requestRefreshAll();


private slots:
    void exportActivePdf();
    void recalcExpensesForm();
    void saveExpenses();
    void exportElectricityPdf();

private:
    enum class ReportTab {
        Period = 0,
        Orders = 1,
        Invoices = 2,
        Parts = 3,
        Outsource = 4,
        Expenses = 5,
        TotalExpenses = 6,
        Payroll = 7,
        Debts = 8,
        DebtAnalysis = 9
    };

    void buildUi();
    void updateDashboard();
    ReportDashboard* m_dashboard=nullptr;
    int m_orderStatusFilter=0;
    bool m_expensesFound=false;
    QStringList m_invoiceMetrics;
    QStringList m_totalExpenseMetrics;
    QStringList m_partsTileTitles,m_partsTileBodies;
    void resizeEvent(QResizeEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void updatePdfButtonPosition();
    void refreshActiveTab();
    void scheduleRefresh(int delayMs = 0);
    void refreshPeriod();
    void applyPeriodSnapshot(const PeriodSnapshot& snapshot);
    void refreshOrders();
    void applyOrdersSnapshot(const OrdersSnapshot& snapshot);
    void refreshParts();
    void applyPartsSnapshot(const PartsSnapshot& snapshot);
    void refreshOutsource();
    void applyOutsourceSnapshot(const OutsourceSnapshot& snapshot);
    void applyPayrollSnapshot(const PayrollSnapshot& snapshot);
    void refreshExpenses();
    void applyExpensesSnapshot(const ExpensesSnapshot& snapshot);
    void refreshTotalExpenses();
    void buildAdditionalExpensesForm(QWidget* page);
    void addAdditionalExpense();
    void deleteAdditionalExpense();
    void updateExpenseCategories();
    void applyTotalExpensesSnapshot(const TotalExpensesSnapshot& snapshot);
    void loadExpensesForm();
    void refreshPayroll();
    void openPayrollOrdersDialog(int row);

    void updateStats(const QStringList& lines);
    void setTabStats(ReportTab tab, const QStringList& lines);
    void setTabAuxStats(ReportTab tab, const QString& html);
    void setTabDescription(ReportTab tab, const QString& text);
    void applyCachedTabUi(ReportTab tab);
    void setTableBaseStyle(QTableView* table);
    void runPdfExportTask(const QString& progressText, std::function<ReportsPdfRenderer::ExportResult()> task);
    void exportPeriodPdf();
    void exportOrdersPdf();
    void previewOrderReport();
    void payReportOrder();
    void exportPartsPdf();
    void exportOutsourcePdf();
    void exportExpensesPdf();
    void exportPayrollPdf();

    ReportTab activeTab() const;

    QDateEdit* m_from = nullptr;
    QDateEdit* m_to = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QTimer* m_searchDebounce = nullptr;
    QTimer* m_refreshDebounce = nullptr;
    QTimer* m_refreshAllDebounce = nullptr;
    QLabel* m_oilStatsLabel = nullptr;
    QLabel* m_statsLabel = nullptr;
    QLabel* m_descLabel = nullptr;
    QTabWidget* m_tabs = nullptr;
    QPushButton* m_pdfButton = nullptr;

    QTableView* m_periodTable = nullptr;
    QTableView* m_ordersTable = nullptr;
    QTableView* m_partsTable = nullptr;
    QTableView* m_outsourceTable = nullptr;
    ReportsTableSupport::ReportsSnapshotTableModel* m_periodModel = nullptr;
    ReportsTableSupport::ReportsSnapshotTableModel* m_ordersModel = nullptr;
    ReportsTableSupport::ReportsSnapshotTableModel* m_partsModel = nullptr;
    ReportsTableSupport::ReportsSnapshotTableModel* m_outsourceModel = nullptr;
    QWidget* m_expensesPage = nullptr;
    PurchaseDebtsPage* m_debtsPage = nullptr;
    PurchaseDebtAnalysisPage* m_debtAnalysisPage = nullptr;
    InvoicesPage* m_invoicesPage = nullptr;
    QLineEdit* m_elecPrevEdit = nullptr;
    QLineEdit* m_elecCurrEdit = nullptr;
    QLineEdit* m_elecTariffEdit = nullptr;
    QLineEdit* m_elecKwhEdit = nullptr;
    QLineEdit* m_elecCostEdit = nullptr;
    QLineEdit* m_cleaningEdit = nullptr;
    QLineEdit* m_cleaningNoteEdit = nullptr;
    QLineEdit* m_softwareEdit = nullptr;
    QLineEdit* m_softwareNoteEdit = nullptr;
    QLineEdit* m_heatingEdit = nullptr;
    QLineEdit* m_heatingNoteEdit = nullptr;
    QLineEdit* m_amortizationEdit = nullptr;
    QLineEdit* m_amortizationNoteEdit = nullptr;
    QLineEdit* m_rentEdit = nullptr;
    QLineEdit* m_rentNoteEdit = nullptr;
    QLineEdit* m_otherEdit = nullptr;
    QLineEdit* m_otherNoteEdit = nullptr;
    QLineEdit* m_expensesTotalEdit = nullptr;
    QPushButton* m_expensesSaveButton = nullptr;
    QPushButton* m_electricityPdfButton = nullptr;
    QTableView* m_totalExpensesTable = nullptr;
    QDateEdit* m_expenseDate = nullptr;
    QComboBox* m_expenseCategory = nullptr;
    QLineEdit* m_expenseDescription = nullptr;
    QDoubleSpinBox* m_expenseAmount = nullptr;
    QPushButton* m_addExpenseButton = nullptr;
    QPushButton* m_deleteExpenseButton = nullptr;
    ReportsTableSupport::ReportsSnapshotTableModel* m_totalExpensesModel = nullptr;
    QTableView* m_payrollTable = nullptr;
    ReportsTableSupport::ReportsSnapshotTableModel* m_payrollModel = nullptr;
    QHash<int, QString> m_tabKeys;
    quint64 m_asyncRefreshGeneration = 0;
    std::shared_ptr<std::atomic_bool> m_asyncCancelToken;
    QHash<int, QStringList> m_tabStats;
    QHash<int, QString> m_tabAuxStats;
    QHash<int, QString> m_tabDescriptions;
    bool m_pdfExportInFlight = false;
};
