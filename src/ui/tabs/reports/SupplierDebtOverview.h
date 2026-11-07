#pragma once
#include <QWidget>
#include "core/services/SupplierInvoiceLedger.h"
#include <atomic>
#include <memory>
class QGridLayout;
class QPushButton;
class QTableView;
namespace ReportsTableSupport { class ReportsSnapshotTableModel; }
class QLabel;

class SupplierDebtOverview final : public QWidget {
public:
    explicit SupplierDebtOverview(QWidget* parent=nullptr);
    ~SupplierDebtOverview() override;
    void refresh();
    void setFilters(const QString& supplier,const QString& source,const QString& search);
    QString reportHtml() const;
    std::function<void(const QStringList&)> suppliersChanged;
    std::function<void(qint64)> inventoryRequested;
    std::function<void()> associationsChanged;
private:
    bool authorized() const;
    void render();
    void details(int row);
    void openLocation(int row);
    bool reconcileLinks(const SupplierInvoiceLedger::Entry& entry, QWidget* parent);
    bool accepts(const SupplierInvoiceLedger::Entry&,int filter) const;
    QStringList columns(const SupplierInvoiceLedger::Entry&) const;
    SupplierInvoiceLedger::Ledger m_ledger;
    SupplierInvoiceLedger::Summary m_summary;
    QVector<int> m_visible;
    QVector<QPushButton*> m_tiles;
    QGridLayout* m_grid;
    QTableView* m_table;
    ReportsTableSupport::ReportsSnapshotTableModel* m_model;
    QLabel* m_status;
    QPushButton* m_previous;
    QPushButton* m_next;
    int m_page=0;
    QString m_scope,m_account,m_supplier,m_source,m_search;
    int m_filter=-1;
    bool m_loaded=false;
    bool m_loading=false;
    bool m_refreshPending=false;
    bool m_remoteBalances=false;
    quint64 m_generation=0;
    std::shared_ptr<std::atomic_bool> m_cancel;
};
