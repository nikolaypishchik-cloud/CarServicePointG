#pragma once

#include <QMainWindow>
#include <QButtonGroup>
#include <QStackedWidget>
#include <QTimer>
#include <QDate>
#include <QTime>
#include <QHash>
#include <QQueue>
#include <QJsonObject>

class UnpaidBannerWidget;
class PlanningPage;
class CurrentOrdersPage;
class HistoryPage;
class PartsPage;
class WorkshopInventoryPage;
class MastersPage;
class ReportsPage;
class StatsPage;
class CatalogPage;
class SettingsPage;
class CrmPage;
class CloudSyncPage;
class OnlineBookingPage;
class VehicleIntakePage;
class GraphiteShimmerOverlay;
class QLabel;
class QAbstractButton;
class QResizeEvent;
class QShowEvent;
class QMoveEvent;
class QEvent;
class AnimatedMetalLogoWidget;
class PageActivationCoordinator;
class BookingSyncManager;
class EmailSyncManager;
class ExternalDisplayController;
struct BookingRecord;
struct EmailNotification;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent=nullptr);
    void lockToAvailableScreenGeometry();
    void requestOpenNewOrder(int sourceOrderNumber = 0, const QString& clientUid = {}, const QString& vehicleUid = {});
    void requestOpenNewOrderFromPlanning(int sourceOrderNumber, const QString& planningText, const QDate& plannedDate = QDate(), const QTime& plannedTime = QTime());
    void requestOpenOnlineBookingFromPlanning();
    void beginSplashPrewarm();
    void refreshAccessControl();
    QWidget* externalDisplaySettings(QWidget* parent);

private slots:
    void openInventoryPurchase(qint64 purchaseId);
    void gotoPage(int idx);
    void openNewOrder();
    void refreshAll();
    void updateUnpaidBanner();
    void switchUser();
    void openPartRequests();

protected:
    void showEvent(QShowEvent* event) override;
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void moveEvent(QMoveEvent* event) override;

private:
    void buildUi();
    void positionAnimatedLogo();
    void applyStyle();
    void applyAdaptiveShellMetrics();
    void scheduleGeometryLock(int delayMs = 0);
    QWidget* ensurePage(int idx);
    void connectPageSignals(QWidget* page);
    void refreshCurrentPage();
    QString pageNameForIndex(int idx) const;
    QString pageTitleForIndex(int idx) const;
    void updateTopTitleForPage(int idx);
    void performRefreshAll();
    void applyFrozenPageMode(int activeIndex);
    void schedulePagePrewarm(int idx, int delayMs);
    bool isPageMaterialized(int idx) const;
    void showOnlineBookingModal(const BookingRecord& booking);
    void showEmailEnvelopeModal(const EmailNotification& notification);
    void checkPartRequestNotifications();
    void showNextPartRequestNotification();
    void applyAccessControl();
    bool canAccessPage(int idx) const;
    bool hasPermission(const QString& permissionKey) const;

    QWidget* m_root = nullptr;
    ExternalDisplayController* m_externalDisplays = nullptr;
    QWidget* m_sidebar = nullptr;
    QWidget* m_content = nullptr;
    QWidget* m_topPill = nullptr;
    QLabel* m_topTitle = nullptr;
    AnimatedMetalLogoWidget* m_animatedLogo = nullptr;
    QWidget* m_topRightSpacer = nullptr;

    QStackedWidget* m_stack = nullptr;

    QButtonGroup* m_menu = nullptr;
    QHash<QString, QAbstractButton*> m_accessButtons;

    UnpaidBannerWidget* m_banner = nullptr;

    PlanningPage* m_pagePlanning = nullptr;
    CurrentOrdersPage* m_pageCurrent = nullptr;
    HistoryPage* m_pageHistory = nullptr;
    QWidget* m_ordersNavigation = nullptr;
    PartsPage* m_pageParts = nullptr;
    WorkshopInventoryPage* m_pageWorkshopInventory = nullptr;
    MastersPage* m_pageMasters = nullptr;
    ReportsPage* m_pageReports = nullptr;
    StatsPage* m_pageStats = nullptr;
    CatalogPage* m_pageCatalog = nullptr;
    SettingsPage* m_pageSettings = nullptr;
    CrmPage* m_pageCrm = nullptr;
    CloudSyncPage* m_pageCloudSync = nullptr;
    QWidget* m_pagePhoneSync = nullptr;
    OnlineBookingPage* m_pageOnlineBooking = nullptr;
    VehicleIntakePage* m_pageVehicleIntake = nullptr;

    QWidget* m_placeholderPlanning = nullptr;
    QWidget* m_placeholderCurrent = nullptr;
    QWidget* m_placeholderMasters = nullptr;
    QWidget* m_placeholderHistory = nullptr;
    QWidget* m_placeholderParts = nullptr;
    QWidget* m_placeholderReports = nullptr;
    QWidget* m_placeholderStats = nullptr;
    QWidget* m_placeholderCatalog = nullptr;
    QWidget* m_placeholderSettings = nullptr;
    QWidget* m_placeholderCrm = nullptr;
    QWidget* m_placeholderCloudSync = nullptr;
    QWidget* m_placeholderPhoneSync = nullptr;
    QWidget* m_placeholderOnlineBooking = nullptr;
    QWidget* m_placeholderVehicleIntake = nullptr;
    QWidget* m_placeholderWorkshopInventory = nullptr;

    QTimer m_bannerTimer;
    bool m_bannerRefreshRunning = false;
    bool m_bannerRefreshPending = false;
    PageActivationCoordinator* m_pageActivation = nullptr;
    BookingSyncManager* m_bookingSync = nullptr;
    EmailSyncManager* m_emailSync = nullptr;
    GraphiteShimmerOverlay* m_graphiteShimmer = nullptr;
    bool m_orderDialogActive = false;
    bool m_geometryLockPending = false;
    bool m_geometryLockReentry = false;
    bool m_screenChangedHooked = false;
    bool m_refreshAllPending = false;
    bool m_refreshAllQueued = false;
    bool m_runtimeWarmupScheduled = false;
    bool m_updatePromptScheduled = false;
    bool m_updateStatusChecked = false;
    bool m_emailModalActive = false;
    bool m_partRequestCheckInProgress = false;
    bool m_partRequestDialogActive = false;
    QQueue<QJsonObject> m_partRequestNotificationQueue;
};
