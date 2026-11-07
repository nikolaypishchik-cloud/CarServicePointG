#pragma once

#include <QWidget>
#include <QList>
#include <QDate>
#include <QTimer>
#include <QHash>
#include <QPointer>
#include <QVector>
#include <QCloseEvent>

#include <atomic>
#include <memory>

#include "core/services/PartsQueryService.h"
#include "ui/tabs/parts/PartsPageTableModelAdapter.h"

class QTableView;
class QLineEdit;
class QPushButton;
class QLabel;
class QDateEdit;
class QThread;
class PartsSupplierDashboard;
namespace PartsPageTableModelAdapter { class PartsArchiveTableModel; }

class PartsPage : public QWidget
{
    Q_OBJECT
public:
    explicit PartsPage(QWidget* parent=nullptr);
    ~PartsPage() override;

    void refresh();
    void markDirty();

signals:
    void requestRefreshAll();
    void requestUnpaidBannerRefresh();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    friend class PurchasePaymentTests;
    void queueRefreshAll(int delayMs = 30);

    int selectedPartId() const;
    int selectedOrderNumber() const;
    int visibleRowForPartId(int partId) const;
    int firstVisibleRowForOrderUid(const QString& orderUid) const;
    int firstVisibleRowForOrderNumber(int orderNumber) const;
    QString selectedOrderUid() const;
    QList<int> selectedPartIds() const;

    void buildUi();
    void loadRows();
    void startAsyncRefresh();
    void cancelActiveRefreshForOwnerShutdown(const char* reason);
    void applyAsyncSnapshot(const QVector<PartRowSnapshot>& rows,
                            const PartsQueryService::PartArchiveDateBounds& bounds,
                            const QString& errorText,
                            int selectedPartBefore,
                            int selectedOrderBefore,
                            const QString& selectedOrderUidBefore,
                            int vScrollBefore,
                            int hScrollBefore);
    void populateRows(const QVector<PartRowSnapshot>& rows);
    void populateTableRow(int visualRow, const PartsPageTableModelAdapter::TableRow& rowData);
    bool applyIncrementalPaidToggle(int partId, bool newPaidState);
    void applyFilter();
    void togglePaidForPart(int partId, bool newPaidState);
    void resortRowsAfterPaidToggle(int focusPartId);
    void openSelectedOrder();
    void exportSelectedOrderPdf();
    void syncDateRangeFromStorage();
    void restoreDateRangeState();
    void saveDateRangeState() const;
    void updateRowHeights();
    void scheduleRowHeightsUpdate(int delayMs = 0);
    void restoreTableHeaderState();
    void saveTableHeaderState();
    void clearSearchStateForReturn();
    void updateDebtBanner();
    void scheduleDebtBannerUpdate(int delayMs = 120);

    QTableView* m_table = nullptr;
    PartsPageTableModelAdapter::PartsArchiveTableModel* m_partsModel = nullptr;
    QLineEdit* m_search = nullptr;
    QPushButton* m_findBtn = nullptr;
    QLabel* m_debtsBanner = nullptr;
    QWidget* m_debtsBannerHost = nullptr;
    PartsSupplierDashboard* m_supplierDashboard = nullptr;
    QDateEdit* m_dateFrom = nullptr;
    QDateEdit* m_dateTo = nullptr;
    QDate m_lastAppliedFrom;
    QDate m_lastAppliedTo;
    bool m_dateRangeInitialized = false;
    bool m_loaded = false;
    bool m_dirty = true;
    bool m_refreshInProgress = false;
    bool m_refreshQueued = false;
    bool m_paymentActionsEnabled = true;
    QString m_actorScopeKey;
    QTimer* m_searchDebounce = nullptr;
    QTimer* m_refreshAllDebounce = nullptr;
    QTimer* m_rowHeightsDebounce = nullptr;
    QTimer* m_debtBannerDebounce = nullptr;
    QPointer<QThread> m_refreshWorker;
    QPointer<QThread> m_debtWorker;
    std::shared_ptr<std::atomic_bool> m_debtCancel;
    bool m_debtRefreshQueued = false;
    int m_refreshGeneration = 0;
    std::shared_ptr<std::atomic_bool> m_refreshCancelToken;
    QHash<int, int> m_rowHeightCache;
    QHash<int, PartRowSnapshot> m_rowsSnapshotById;
    // P1/pass9: visible row indexes now live inside PartsArchiveTableModel.
    QVector<PartRowSnapshot> m_rowsSnapshot;
};
