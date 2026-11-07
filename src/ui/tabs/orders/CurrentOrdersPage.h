#pragma once

#include <QWidget>
#include <QList>
#include <QTimer>
#include <QRect>
#include <QPointer>
#include <atomic>
#include <memory>

class QSortFilterProxyModel;
class TabularSnapshotTableModel;

class QTableView;
class QLineEdit;
class QModelIndex;
class QToolButton;
class QShowEvent;
class QCloseEvent;
class QThread;
struct CurrentOrdersQueryResult;
struct TabularReadModel;

class CurrentOrdersPage : public QWidget
{
    Q_OBJECT
public:
    explicit CurrentOrdersPage(QWidget* parent=nullptr);
    ~CurrentOrdersPage() override;

    void refresh();
    void markDirty();
    QRect partsCellRectForOrder(int orderNumber = -1) const;

protected:
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

signals:
    void requestRefreshAll();
    void settlementChanged(const QString& text);

private:
    void buildUi();
    void publishSettlement();
    void queueRefreshAll(int delayMs = 30);
    void startAsyncRefresh();
    void cancelActiveRefreshForOwnerShutdown(const char* reason);
    void applyAsyncSnapshot(const CurrentOrdersQueryResult& queryResult,
                            const TabularReadModel& readModel,
                            int selectedBefore,
                            int vScrollBefore,
                            int hScrollBefore);

    int selectedOrderNumber() const;
    QString selectedOrderUid() const;
    QList<int> selectedOrderNumbers() const;
    void restoreHeaderState();
    void saveHeaderState() const;
    QString selectedOrderStatus() const;
    void openSelectedOrder();
    void openSelectedVehicleAsNewOrder();
    bool setSelectedOrdersClosed(bool closed);
    bool deleteSelectedOrders();
    void exportSelectedOrderPdf();
    void importInvoiceFromExcel();
    void clearSearchStateForReturn();

    TabularSnapshotTableModel* m_model = nullptr;
    QSortFilterProxyModel* m_proxy = nullptr;
    QTableView* m_table = nullptr;
    QLineEdit* m_search = nullptr;
    QToolButton* m_btnImportInvoice = nullptr;
    QToolButton* m_btnPdf = nullptr;
    bool m_loaded = false;
    QString m_actorScopeKey;
    bool m_dirty = true;
    bool m_refreshInProgress = false;
    bool m_refreshQueued = false;
    QPointer<QThread> m_refreshWorker;
    int m_refreshGeneration = 0;
    std::shared_ptr<std::atomic_bool> m_refreshCancelToken;
    QTimer* m_searchDebounce = nullptr;
    QTimer* m_refreshAllDebounce = nullptr;
};
