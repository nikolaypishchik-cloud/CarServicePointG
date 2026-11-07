#pragma once
#include <QWidget>
#include <QVector>
#include <QPointer>
class QThread;
#include <functional>
class QTableView;
namespace ReportsTableSupport { class ReportsSnapshotTableModel; }
class QLabel;
class QTextBrowser;
class QComboBox;
class QPushButton;
class QLineEdit;
class QTabBar;
class SupplierDebtOverview;
#include "runtime/AsyncTaskHandle.h"
struct PartRowSnapshot;
#include "core/services/PurchasePaymentService.h"
#include "core/services/PartsQueryService.h"
#include "core/services/SupplierDebtReconciliationService.h"
#include "core/services/InvoiceEvidenceService.h"
struct PurchaseDebt;
namespace SupplierDebtReadService { struct ReconciliationInput; }
class PurchaseDebtsPage final : public QWidget {
public:
    explicit PurchaseDebtsPage(QWidget* parent = nullptr);
    void refresh();
    void reconcileDebts();
    static void showDetails(QWidget* parent);
    static QString summaryHtml(const QVector<PurchaseDebt>& debts, int gapPixels = 27,
        const QMap<QString,SupplierDebtReconciliation>& reconciliations = {},
        const QMap<QString,qint64>* verifiedTotals = nullptr);
    std::function<void()> paymentChanged;
    std::function<void(const QString&)> summaryChanged;
private:
    friend class PurchasePaymentTests;
    void showReconciliation(const SupplierDebtReadService::ReconciliationInput& input);
    bool m_reconciliationPending = false;
    void applyDebts(const QVector<PurchaseDebt>& debts, const QString& summary, const QString& error);
    QPointer<QThread> m_worker;
    std::shared_ptr<std::atomic_bool> m_cancel;
    bool m_refreshQueued = false;
    QTableView* m_table;
    ReportsTableSupport::ReportsSnapshotTableModel* m_model;
    QLabel* m_status;
    QPushButton* m_reconcile;
};

class PurchaseDebtAnalysisPage final : public QWidget {
public:
    explicit PurchaseDebtAnalysisPage(QWidget* parent = nullptr);
    ~PurchaseDebtAnalysisPage() override;
    void setExpensePeriod(const QDate& from, const QDate& to);
    void refresh();
    std::function<void()> reconciliationRequested;
    std::function<void()> reconciliationChanged;
    std::function<void(qint64)> inventoryRequested;
    std::function<void(const QString&)> summaryChanged;
    static QString analysisHtml(const QVector<PurchaseDebt>& debts, const QVector<PartRowSnapshot>& parts,
                                const QDate& today, const QString& supplierFilter = {}, int detailPage = 0,
                                const QMap<QString,SupplierDebtReconciliation>& reconciliations = {},
                                const QHash<QString,InvoiceEvidence>& evidence = {}, int view = -1);
private:
    QVector<PurchaseDebt> filteredDebts() const;
    void render();
    void exportPdf();
    QTextBrowser* m_report;
    SupplierDebtOverview* m_overview;
    QComboBox* m_supplier;
    QComboBox* m_source;
    QLineEdit* m_search;
    QTabBar* m_views;
    QLabel* m_expenses;
    QLabel* m_heading;
    QLabel* m_asOfLabel;
    QString m_expenseDetailsHtml;
    bool m_showExpenses = false;
    QPushButton* m_reconcile;
    QDate m_asOf;
    QDate m_expenseFrom, m_expenseTo;
    QMap<QString,QString> m_renderCache;
    std::shared_ptr<std::atomic_bool> m_expenseCancel;
    std::shared_ptr<std::atomic_bool> m_analysisCancel;
    quint64 m_generation = 0;
    QPushButton* m_previous;
    QPushButton* m_next;
    QLabel* m_pageStatus;
    QPushButton* m_pdf;
    QString m_loadedScope;
    bool m_loaded = false;
    int m_detailPage = 0;
    QVector<PurchaseDebt> m_debts;
    QVector<PartRowSnapshot> m_parts;
    QMap<QString,SupplierDebtReconciliation> m_reconciliations;
    QHash<QString,InvoiceEvidence> m_evidence;
};
