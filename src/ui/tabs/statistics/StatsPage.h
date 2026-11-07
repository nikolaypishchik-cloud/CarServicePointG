#pragma once

#include <QWidget>
#include "core/services/statistics/StatisticsReadService.h"
class QThread;
class QCloseEvent;

class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QTabWidget;
class QTableView;
class QTimer;
class QTextBrowser;
class TabularSnapshotTableModel;

class StatsPage : public QWidget
{
    Q_OBJECT
public:
    enum class View { Performance, WorkLog };
    explicit StatsPage(QWidget* parent = nullptr, View view = View::Performance);

    ~StatsPage() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;

public slots:
    void refresh();
    void markDirty();

private slots:
    void setToday();
    void setWeek();
    void setMonth();
    void applyFilters();

private:
    void buildUi();
    void renderPerformance();
    void scheduleRefresh(int delayMs = 0);
    void reloadData();
    void populateActors();
    void populateTables();
    void updateSummary(int visibleEvents, int activeStaff, int waitingParts, int agingParts);
    void showEventDetails(int sourceRow);
    void showTeamMemberDetails(int sourceRow);
    void openPartOrder(int sourceRow);
    void showEventsMetricDetails();
    void showTeamMetricDetails();
    void showWaitingPartsMetricDetails();
    void showAgingPartsMetricDetails();

    QDateEdit* m_from = nullptr;
    QDateEdit* m_to = nullptr;
    QComboBox* m_actor = nullptr;
    QComboBox* m_category = nullptr;
    QLineEdit* m_search = nullptr;
    QLabel* m_summary = nullptr;
    QLabel* m_eventsMetric = nullptr;
    QLabel* m_staffMetric = nullptr;
    QLabel* m_waitingMetric = nullptr;
    QLabel* m_agingMetric = nullptr;
    QLabel* m_alertText = nullptr;
    QTabWidget* m_tabs = nullptr;
    QTextBrowser* m_overview = nullptr;
    QTextBrowser* m_masterReport = nullptr;
    QComboBox* m_master = nullptr;
    QTableView* m_dashboardTable = nullptr;
    QTableView* m_activityTable = nullptr;
    QTableView* m_teamTable = nullptr;
    QTableView* m_partsTable = nullptr;
    TabularSnapshotTableModel* m_dashboardModel = nullptr;
    TabularSnapshotTableModel* m_activityModel = nullptr;
    TabularSnapshotTableModel* m_teamModel = nullptr;
    TabularSnapshotTableModel* m_partsModel = nullptr;
    QTimer* m_refreshTimer = nullptr;
    std::shared_ptr<std::atomic_bool> m_refreshCancelToken;
    QThread* m_refreshWorker = nullptr;
    bool m_refreshQueued = false;
    bool m_closed = false;
    QVector<StatisticsReadService::PartTraceRow> m_loadedParts;
    bool m_dirty = true;
    View m_view = View::Performance;
};
