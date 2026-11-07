#pragma once
#include <QWidget>
#include <functional>
#include "core/services/InvoiceRegistryService.h"
#include "runtime/AsyncTaskHandle.h"
class QTableView;
class QLineEdit;
class QComboBox;
class QLabel;
namespace ReportsTableSupport { class ReportsSnapshotTableModel; }

class InvoicesPage final : public QWidget {
public:
    std::function<void(int,int,int,qint64)> metricsChanged;
    explicit InvoicesPage(QWidget* parent = nullptr);
    ~InvoicesPage() override;
    void refresh();
    void setPeriod(const QDate& from, const QDate& to);
    std::function<void(QDate, QDate)> periodExpanded;
    void selectInvoice(const QString& key);
    void payInvoiceByKey(const QString& key);
    QLineEdit* searchField() const { return m_search; }
    QWidget* controlsHost() const { return m_controls; }
    std::function<void()> paymentChanged;
    std::function<void(qint64)> inventoryRequested;
private:
    void render();
    void activate(int row, int column);
    void showInvoice(const RegisteredInvoice& invoice, bool invoiceOnly = false, const QString& pdfPath = {});
    bool eventFilter(QObject* watched, QEvent* event) override;
    void fitColumns();
    QTableView* m_table;
    QWidget* m_controls;
    ReportsTableSupport::ReportsSnapshotTableModel* m_model;
    QLineEdit* m_search;
    QComboBox* m_statusFilter;
    QComboBox* m_sort;
    QLabel* m_status;
    QVector<RegisteredInvoice> m_invoices, m_visible;
    std::shared_ptr<std::atomic_bool> m_cancel;
    quint64 m_generation = 0;
    QString m_loadedScope;
    QString m_loadedAccount;
    QString m_selectedInvoice;
    QDate m_from, m_to;
};
