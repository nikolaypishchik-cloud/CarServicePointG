#pragma once

#include <QWidget>
#include <QList>
#include <QTimer>
#include <QDate>
#include <QPointer>
#include <atomic>
#include <memory>

class TabularSnapshotTableModel;
class QTableView;
class QLineEdit;
class QDateEdit;
class QShowEvent;
class QCloseEvent;
class QThread;
class QSortFilterProxyModel;
class QPushButton;
class QLabel;
struct HistoryQueryResult;
struct TabularReadModel;

class HistoryPage : public QWidget
{
    Q_OBJECT
public:
    explicit HistoryPage(QWidget* parent=nullptr);
    ~HistoryPage() override;

    void refresh();
    void markDirty();

protected:
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    void requestRefreshAll();

private:
    void queueRefreshAll(int delayMs = 30);
    void startAsyncRefresh();
    void cancelActiveRefreshForOwnerShutdown(const char* reason);
    void applyAsyncSnapshot(const HistoryQueryResult& queryResult,
                            const TabularReadModel& readModel,
                            int selectedBefore,
                            int vScrollBefore,
                            int hScrollBefore);

    void restoreHeaderState();
    void saveHeaderState() const;
    int selectedOrderNumber() const;
    QString selectedOrderUid() const;
    QList<int> selectedOrderNumbers() const;
    bool deleteSelectedOrders();
    void openSelectedVehicleAsNewOrder();
    void clearSearchStateForReturn();

    TabularSnapshotTableModel* m_model = nullptr;
    QSortFilterProxyModel* m_proxy = nullptr;
    QTableView* m_table = nullptr;
    QLineEdit* m_search = nullptr;
    QDateEdit* m_dateFrom = nullptr;
    QDateEdit* m_dateTo = nullptr;
    QPushButton* m_previousPage = nullptr;
    QPushButton* m_nextPage = nullptr;
    QLabel* m_pageLabel = nullptr;
    int m_pageOffset = 0;
    QString m_pageFilterKey;
    bool m_loaded = false;
    bool m_dirty = true;
    bool m_refreshInProgress = false;
    QString m_actorScopeKey;
    bool m_refreshQueued = false;
    QPointer<QThread> m_refreshWorker;
    int m_refreshGeneration = 0;
    std::shared_ptr<std::atomic_bool> m_refreshCancelToken;
    QTimer* m_searchDebounce = nullptr;
    QTimer* m_refreshAllDebounce = nullptr;
};
