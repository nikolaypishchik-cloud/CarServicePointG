// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

#include "ui/main/MainWindowModulePrelude.h"
#include "ui/main/ExternalDisplayController.h"
#include "runtime/AsyncThreadScope.h"
#include "core/services/AppRuntimeService.h"
#include <QMessageBox>

void MainWindow::applyStyle()
{
    // Styling is controlled by Theme (DB setting: ui.theme).
    ThemeManager::applySavedTheme();
}

void MainWindow::gotoPage(int idx)
{
    if (!m_stack || !canAccessPage(idx)) return;
    if (idx == 0) {
        if (auto* button = m_accessButtons.value(QStringLiteral("nav.planning")))
            button->setChecked(true);
    }

    QWidget* w = m_stack->widget(idx);
    if (!w) return;

    updateTopTitleForPage(idx);

    const bool samePage = (m_stack->currentIndex() == idx);
    if (samePage) {
        return;
    }

    // Do not mark pages dirty on ordinary navigation.
    // Page-level mutations already call requestRefreshAll()/markDirty() where needed,
    // and forcing dirty here causes unnecessary reloads, scroll resets and UI flicker.
    const bool anim = MainWindowMetricsService::loadAnimationsEnabled() && shouldUseFullPageFade(idx);
    const int dur = MainWindowMetricsService::loadAnimationDurationMs();
    const bool pageAlreadyMaterialized = isPageMaterialized(idx);

if (anim && pageAlreadyMaterialized && w) {
    // Fade-in animation for the target page.
    auto* eff = new QGraphicsOpacityEffect(w);
    w->setGraphicsEffect(eff);
    eff->setOpacity(0.0);

    m_stack->setCurrentIndex(idx);

    auto* a = new QPropertyAnimation(eff, "opacity", w);
    int duration = dur;
        if (duration < 80) duration = 80;
        if (duration > 1200) duration = 1200;
        a->setDuration(duration);
    a->setStartValue(0.0);
    a->setEndValue(1.0);
    connect(a, &QPropertyAnimation::finished, w, [w]() {
        // QWidget owns the graphics effect; setGraphicsEffect(nullptr) deletes it.
        w->setGraphicsEffect(nullptr);
    });
    a->start(QAbstractAnimation::DeleteWhenStopped);
} else {
    m_stack->setCurrentIndex(idx);
}

    applyFrozenPageMode(idx);

    const QString pageName = pageNameForIndex(idx);
    auto pageOpenTimer = std::make_shared<QElapsedTimer>();
    pageOpenTimer->start();
    auto scheduleStableLog = [this, idx, pageName, pageOpenTimer]() {
        QTimer::singleShot(180, this, [this, idx, pageName, pageOpenTimer]() {
            if (!m_stack || m_stack->currentIndex() != idx) return;
            const qint64 stableMs = pageOpenTimer->elapsed();
            if (RuntimeLoggingControl::deepDebugEnabled()) {
                qInfo().noquote() << QStringLiteral("PAGE_OPEN [%1] [stable] %2 ms")
                                         .arg(pageName, QString::number(stableMs));
            }
        });
    };

    if (m_pageActivation) {
        m_pageActivation->requestActivation(idx, pageName, [this, idx, scheduleStableLog]() {
            if (!m_stack || m_stack->currentIndex() != idx) return;
            ensurePage(idx);
            if (!m_stack || m_stack->currentIndex() != idx) return;
            applyFrozenPageMode(idx);
            refreshCurrentPage();
            scheduleStableLog();
        }, anim ? qBound(12, dur / 3, 90) : 0);
    } else {
        ensurePage(idx);
        applyFrozenPageMode(idx);
        refreshCurrentPage();
        scheduleStableLog();
    }
    // Navigation does not change debt. Mutations and the timer refresh the banner.

    // Keep page switches lightweight: do not fight the native window state on every
    // layout pass. A single delayed clamp is only needed after restoring to normal mode.
    if (!isMaximized() && !isFullScreen()) {
        const int postAnimDelay = anim ? qBound(90, dur + 24, 260) : 0;
        scheduleGeometryLock(postAnimDelay);
    }
}

void MainWindow::requestOpenNewOrder(int sourceOrderNumber, const QString& clientUid, const QString& vehicleUid)
{
    if (!hasPermission(QStringLiteral("orders.create")) || m_orderDialogActive) return;
    m_orderDialogActive = true;

    QPointer<MainWindow> safeThis(this);
    BackupPerformanceGuard::hold(AppContext::instance().backupManager(), 2500);
    QTimer::singleShot(120, this, [safeThis, sourceOrderNumber, clientUid, vehicleUid]() {
        if (!safeThis) return;

        OrderDialog dlg(safeThis.data());
        dlg.setWindowTitle(QString::fromUtf8("Новий наряд"));
        if (sourceOrderNumber > 0) {
            dlg.prefillFromExistingOrder(sourceOrderNumber);
        }
        if (!vehicleUid.isEmpty() && !dlg.prefillFromCrmVehicle(clientUid, vehicleUid)) {
            safeThis->m_orderDialogActive = false;
            QMessageBox::warning(safeThis, QString::fromUtf8("CRM"),
                                 QString::fromUtf8("Не вдалося завантажити дані вибраного автомобіля."));
            return;
        }

        const int result = dlg.exec();
        if (!safeThis) return;
        safeThis->m_orderDialogActive = false;
        if (result == QDialog::Accepted) {
            safeThis->refreshAll();
        }
    });
}

void MainWindow::requestOpenNewOrderFromPlanning(int sourceOrderNumber, const QString& planningText, const QDate& plannedDate, const QTime& plannedTime)
{
    if (!hasPermission(QStringLiteral("orders.create")) || m_orderDialogActive) return;
    m_orderDialogActive = true;

    QPointer<MainWindow> safeThis(this);
    BackupPerformanceGuard::hold(AppContext::instance().backupManager(), 2500);
    QTimer::singleShot(120, this, [safeThis, sourceOrderNumber, planningText, plannedDate, plannedTime]() {
        if (!safeThis) return;

        OrderDialog dlg(safeThis.data());
        dlg.setWindowTitle(QString::fromUtf8("Новий наряд"));
        if (sourceOrderNumber > 0) {
            dlg.prefillFromPlanningSource(sourceOrderNumber, planningText, plannedDate, plannedTime);
        } else {
            dlg.prefillFromPlanningEntry(planningText, plannedDate, plannedTime);
        }

        const int result = dlg.exec();
        if (!safeThis) return;
        safeThis->m_orderDialogActive = false;
        if (result == QDialog::Accepted) {
            safeThis->refreshAll();
        }
    });
}

void MainWindow::requestOpenOnlineBookingFromPlanning()
{
    if (m_menu) {
        const bool wasExclusive = m_menu->exclusive();
        m_menu->setExclusive(false);
        for (auto* button : m_menu->buttons()) {
            if (button) button->setChecked(false);
        }
        m_menu->setExclusive(wasExclusive);
    }
    gotoPage(11);
}

void MainWindow::openNewOrder()
{
    requestOpenNewOrder(0);
}

void MainWindow::refreshAll()
{
    if (m_refreshAllPending) {
        m_refreshAllQueued = true;
        return;
    }
    m_refreshAllPending = true;

    QPointer<MainWindow> safeThis(this);
    QTimer::singleShot(30, this, [safeThis]() {
        if (!safeThis) return;
        safeThis->m_refreshAllPending = false;
        safeThis->performRefreshAll();
        if (safeThis->m_refreshAllQueued) {
            safeThis->m_refreshAllQueued = false;
            safeThis->refreshAll();
        }
    });
}

void MainWindow::performRefreshAll()
{
    if (m_externalDisplays) m_externalDisplays->refresh();
    // Mark data-driven pages dirty so they re-query after cross-page mutations.
    PartsQueryService::invalidateCache();
    if (m_pageCurrent) m_pageCurrent->markDirty();
    if (m_pageHistory) m_pageHistory->markDirty();
    if (m_pageParts) m_pageParts->markDirty();
    if (m_pageReports) m_pageReports->markDirty();
    if (m_pageStats) m_pageStats->markDirty();
    if (m_pageWorkshopInventory) m_pageWorkshopInventory->markDirty();
    if (m_pageOnlineBooking) m_pageOnlineBooking->setProperty("csForceRefreshAfterMutation", true);
    if (m_pageVehicleIntake) m_pageVehicleIntake->setProperty("csForceRefreshAfterMutation", true);

    // Current-page refresh stays lightweight, but keep stats/catalog/settings in sync
    // when the user is already on those pages.
    refreshCurrentPage();
    if (m_stack && m_stack->currentIndex() == 8) {
        ThemeManager::applySavedTheme();
        if (m_pageSettings) m_pageSettings->refreshThemeStyles();
    }
    updateUnpaidBanner();
}


QWidget* MainWindow::ensurePage(int idx)
{
    if (!m_stack) return nullptr;
    QWidget* existing = m_stack->widget(idx);
    auto replacePlaceholder = [&](QWidget* placeholder, QWidget* realPage) -> QWidget* {
        if (!realPage) return nullptr;
        realPage->setAutoFillBackground(false);
        if (realPage->styleSheet().trimmed().isEmpty()) {
            realPage->setAttribute(Qt::WA_StyledBackground, true);
            realPage->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
        }
        const bool wasCurrent = (m_stack->currentWidget() == placeholder);
        m_stack->removeWidget(placeholder);
        placeholder->deleteLater();
        m_stack->insertWidget(idx, realPage);
        connectPageSignals(realPage);
        if (wasCurrent) m_stack->setCurrentWidget(realPage);
        return realPage;
    };

    switch (idx) {
    case 0: if (!m_pagePlanning) m_pagePlanning = static_cast<PlanningPage*>(replacePlaceholder(m_placeholderPlanning, new PlanningPage(m_stack,true))); return m_pagePlanning ? m_pagePlanning : existing;
    case 1: if (!m_pageCurrent) m_pageCurrent = static_cast<CurrentOrdersPage*>(replacePlaceholder(m_placeholderCurrent, new CurrentOrdersPage(m_stack))); return m_pageCurrent ? m_pageCurrent : existing;
    case 2: if (!m_pageMasters) m_pageMasters = static_cast<MastersPage*>(replacePlaceholder(m_placeholderMasters, new MastersPage(m_stack))); return m_pageMasters ? m_pageMasters : existing;
    case 3: if (!m_pageHistory) m_pageHistory = static_cast<HistoryPage*>(replacePlaceholder(m_placeholderHistory, new HistoryPage(m_stack))); return m_pageHistory ? m_pageHistory : existing;
    case 4: if (!m_pageParts) m_pageParts = static_cast<PartsPage*>(replacePlaceholder(m_placeholderParts, new PartsPage(m_stack))); return m_pageParts ? m_pageParts : existing;
    case 5: if (!m_pageReports) m_pageReports = static_cast<ReportsPage*>(replacePlaceholder(m_placeholderReports, new ReportsPage(m_stack))); return m_pageReports ? m_pageReports : existing;
    case 6: if (!m_pageStats) m_pageStats = static_cast<StatsPage*>(replacePlaceholder(m_placeholderStats, new StatsPage(m_stack))); return m_pageStats ? m_pageStats : existing;
    case 7: if (!m_pageCatalog) m_pageCatalog = static_cast<CatalogPage*>(replacePlaceholder(m_placeholderCatalog, new CatalogPage(m_stack))); return m_pageCatalog ? m_pageCatalog : existing;
    case 8: if (!m_pageSettings) m_pageSettings = static_cast<SettingsPage*>(replacePlaceholder(m_placeholderSettings, new SettingsPage(m_stack))); return m_pageSettings ? m_pageSettings : existing;
    case 9: if (!m_pageCrm) m_pageCrm = static_cast<CrmPage*>(replacePlaceholder(m_placeholderCrm, new CrmPage(m_stack))); return m_pageCrm ? m_pageCrm : existing;
    case 10: if (!m_pageCloudSync) m_pageCloudSync = static_cast<CloudSyncPage*>(replacePlaceholder(m_placeholderCloudSync, new CloudSyncPage(m_stack))); return m_pageCloudSync ? m_pageCloudSync : existing;
    case 11: if (!m_pageOnlineBooking) m_pageOnlineBooking = static_cast<OnlineBookingPage*>(replacePlaceholder(m_placeholderOnlineBooking, new OnlineBookingPage(m_stack))); return m_pageOnlineBooking ? m_pageOnlineBooking : existing;
    case 12: if (!m_pageVehicleIntake) m_pageVehicleIntake = static_cast<VehicleIntakePage*>(replacePlaceholder(m_placeholderVehicleIntake, new VehicleIntakePage(m_stack))); return m_pageVehicleIntake ? m_pageVehicleIntake : existing;
    case 13: if (!m_pageWorkshopInventory) m_pageWorkshopInventory = static_cast<WorkshopInventoryPage*>(replacePlaceholder(m_placeholderWorkshopInventory, new WorkshopInventoryPage(m_stack))); return m_pageWorkshopInventory ? m_pageWorkshopInventory : existing;
    case 14: if (!m_pagePhoneSync) m_pagePhoneSync = replacePlaceholder(m_placeholderPhoneSync, PhoneSyncSettingsPage::build(m_stack)); return m_pagePhoneSync ? m_pagePhoneSync : existing;
    default: return existing;
    }
}

void MainWindow::openInventoryPurchase(qint64 purchaseId)
{
    if(!canAccessPage(13)) return;
    gotoPage(13);
    if(m_pageWorkshopInventory) m_pageWorkshopInventory->focusPurchase(purchaseId);
}

void MainWindow::connectPageSignals(QWidget* page)
{
    if (!page) return;
    const auto queuedUnique = static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection);
    if (auto* w = qobject_cast<CurrentOrdersPage*>(page)) connect(w, &CurrentOrdersPage::requestRefreshAll, this, &MainWindow::refreshAll, queuedUnique);
    if (auto* w = qobject_cast<MastersPage*>(page)) connect(w, &MastersPage::requestRefreshAll, this, &MainWindow::refreshAll, queuedUnique);
    if (auto* w = qobject_cast<HistoryPage*>(page)) connect(w, &HistoryPage::requestRefreshAll, this, &MainWindow::refreshAll, queuedUnique);
    if (auto* w = qobject_cast<PartsPage*>(page)) {
        connect(w, &PartsPage::requestRefreshAll, this, &MainWindow::refreshAll, queuedUnique);
        connect(w, &PartsPage::requestUnpaidBannerRefresh, this, &MainWindow::updateUnpaidBanner, queuedUnique);
    }
    if (auto* w = qobject_cast<CatalogPage*>(page)) connect(w, &CatalogPage::requestRefreshAll, this, &MainWindow::refreshAll, queuedUnique);
    if (auto* w = qobject_cast<ReportsPage*>(page)) {
        connect(w, &ReportsPage::requestRefreshAll, this, &MainWindow::refreshAll, queuedUnique);
        connect(w, &ReportsPage::requestInventoryPurchase, this, &MainWindow::openInventoryPurchase, queuedUnique);
    }
    if (auto* w = qobject_cast<SettingsPage*>(page)) connect(w, &SettingsPage::requestRefreshAll, this, &MainWindow::refreshAll, queuedUnique);
    if (auto* w = qobject_cast<OnlineBookingPage*>(page)) connect(w, &OnlineBookingPage::requestRefreshAll, this, &MainWindow::refreshAll, queuedUnique);
    if (auto* w = qobject_cast<VehicleIntakePage*>(page)) connect(w, &VehicleIntakePage::requestRefreshAll, this, &MainWindow::refreshAll, queuedUnique);
    if (auto* w = qobject_cast<WorkshopInventoryPage*>(page)) connect(w, &WorkshopInventoryPage::requestRefreshAll, this, &MainWindow::refreshAll, queuedUnique);
}

void MainWindow::refreshCurrentPage()
{
    if (!m_stack) return;
    switch (m_stack->currentIndex()) {
    case 0: if (m_pagePlanning) m_pagePlanning->refresh(); break;
    case 1: if (m_pageCurrent) m_pageCurrent->refresh(); break;
    case 2: if (m_pageMasters) m_pageMasters->refresh(); break;
    case 3: if (m_pageHistory) m_pageHistory->refresh(); break;
    case 4: if (m_pageParts) m_pageParts->refresh(); break;
    case 5: if (m_pageReports) m_pageReports->refresh(); break;
    case 6: if (m_pageStats) m_pageStats->refresh(); break;
    case 7: if (m_pageCatalog) m_pageCatalog->refresh(); break;
    case 8: break;
    case 9: if (m_pageCrm) m_pageCrm->refresh(); break;
    case 10: if (m_pageCloudSync) m_pageCloudSync->refresh(); break;
    case 11: if (m_pageOnlineBooking) m_pageOnlineBooking->refresh(); break;
    case 12: if (m_pageVehicleIntake) m_pageVehicleIntake->refresh(); break;
    case 13: if (m_pageWorkshopInventory) m_pageWorkshopInventory->refresh(); break;
    case 14: break;
    default: break;
    }
}


QString MainWindow::pageNameForIndex(int idx) const
{
    switch (idx) {
    case 0: return QStringLiteral("planning");
    case 1: return QStringLiteral("current_orders");
    case 2: return QStringLiteral("masters");
    case 3: return QStringLiteral("history");
    case 4: return QStringLiteral("parts");
    case 5: return QStringLiteral("reports");
    case 6: return QStringLiteral("stats");
    case 7: return QStringLiteral("catalog");
    case 8: return QStringLiteral("settings");
    case 9: return QStringLiteral("crm_clients");
    case 10: return QStringLiteral("cloud_sync");
    case 11: return QStringLiteral("online_booking");
    case 12: return QStringLiteral("vehicle_intake");
    case 13: return QStringLiteral("workshop_inventory");
    case 14: return QStringLiteral("phone_sync");
    default: return QStringLiteral("unknown");
    }
}

QString MainWindow::pageTitleForIndex(int idx) const
{
    switch (idx) {
    case 0: return QString::fromUtf8("Планування");
    case 1: return QString::fromUtf8("Поточні наряди");
    case 2: return QString::fromUtf8("Майстри");
    case 3: return QString::fromUtf8("Завершені наряди");
    case 4: return QString::fromUtf8("Замовлення запчастин");
    case 5: return QString::fromUtf8("Звіти");
    case 6: return QString::fromUtf8("Статистика");
    case 7: return QString::fromUtf8("Каталог");
    case 8: return QString::fromUtf8("Налаштування");
    case 9: return QString::fromUtf8("Клієнти");
    case 10: return QString::fromUtf8("Синхронізація");
    case 11: return QString::fromUtf8("Онлайн-заявки");
    case 12: return QString::fromUtf8("Приймання авто");
    case 13: return QString::fromUtf8("Склад СТО");
    case 14: return QString::fromUtf8("Синхронізація телефону");
    default: return QString();
    }
}

void MainWindow::updateTopTitleForPage(int idx)
{
    if (!m_topTitle) return;
    const QString title = pageTitleForIndex(idx);
    if (!title.isEmpty() && m_topTitle->text() != title) {
        m_topTitle->setText(title);
    }
}

void MainWindow::applyFrozenPageMode(int activeIndex)
{
    PageFreezeController::apply(m_stack, activeIndex);
}

void MainWindow::schedulePagePrewarm(int idx, int delayMs)
{
    if (delayMs < 0) delayMs = 0;
    QTimer::singleShot(delayMs, this, [this, idx]() {
        if (!m_stack) return;
        if (!isVisible() || isMinimized()) return;
        if (isPageMaterialized(idx)) return;
        const int activeIndex = m_stack->currentIndex();
        const QString pageName = pageNameForIndex(idx);
        if (RuntimeLoggingControl::deepDebugEnabled()) {
            qInfo().noquote() << QStringLiteral("PAGE_PREWARM [%1] [begin]").arg(pageName);
        }
        // Startup responsiveness has priority over background warming.
        // Only materialize light page shells here. Data refresh for heavy tabs
        // (catalog/reports/stats) is deferred until the user actually opens
        // them, otherwise the UI thread stalls right after the main window
        // becomes visible.
        try {
            ensurePage(idx);
            applyFrozenPageMode(activeIndex);
        } catch (const std::bad_alloc& e) {
            qCritical().noquote() << QStringLiteral("PAGE_PREWARM [%1] [bad_alloc] %2")
                                         .arg(pageName, QString::fromUtf8(e.what()));
            return;
        } catch (const std::exception& e) {
            qCritical().noquote() << QStringLiteral("PAGE_PREWARM [%1] [exception] %2")
                                         .arg(pageName, QString::fromUtf8(e.what()));
            return;
        } catch (...) {
            qCritical().noquote() << QStringLiteral("PAGE_PREWARM [%1] [unknown_exception]").arg(pageName);
            return;
        }
        if (RuntimeLoggingControl::deepDebugEnabled()) {
            qInfo().noquote() << QStringLiteral("PAGE_PREWARM [%1] [done]").arg(pageName);
        }
    });
}


bool MainWindow::isPageMaterialized(int idx) const
{
    switch (idx) {
    case 0: return m_pagePlanning != nullptr;
    case 1: return m_pageCurrent != nullptr;
    case 2: return m_pageMasters != nullptr;
    case 3: return m_pageHistory != nullptr;
    case 4: return m_pageParts != nullptr;
    case 5: return m_pageReports != nullptr;
    case 6: return m_pageStats != nullptr;
    case 7: return m_pageCatalog != nullptr;
    case 8: return m_pageSettings != nullptr;
    case 9: return m_pageCrm != nullptr;
    case 10: return m_pageCloudSync != nullptr;
    case 11: return m_pageOnlineBooking != nullptr;
    case 12: return m_pageVehicleIntake != nullptr;
    case 13: return m_pageWorkshopInventory != nullptr;
    case 14: return m_pagePhoneSync != nullptr;
    default: return false;
    }
}

void MainWindow::beginSplashPrewarm()
{
    // Keep the first seconds after showing the main window completely responsive.
    // Only light secondary shells are warmed, and only after a noticeable delay.
    // Heavy data pages are left for on-demand activation to avoid visible hangs.
    schedulePagePrewarm(8, 2500);   // settings shell
    schedulePagePrewarm(3, 5000);   // history shell
    schedulePagePrewarm(4, 8000);   // parts shell
    schedulePagePrewarm(0, 11000);  // planning shell
}

void MainWindow::updateUnpaidBanner()
{
    if (!m_banner || RuntimeJob::RuntimeJobDispatcher::instance().isShutdownRequested()) return;
    const auto scopeKey = AccessControlService::currentScopeKey();
    if (m_banner->property("debtScopeKey").toString() != scopeKey) {
        m_banner->setDebt(false, QString(), QString());
        m_banner->setProperty("debtScopeKey", scopeKey);
    }
    if (m_bannerRefreshRunning) {
        m_bannerRefreshPending = true;
        return;
    }
    m_bannerRefreshRunning = true;
    const auto scope = AccessControlService::currentOrderScope();
    struct Result { int count = 0; double sum = 0; bool ok = false; bool unresolved = false; };
    const auto result = std::make_shared<Result>();
    const auto cancel = std::make_shared<std::atomic_bool>(false);
    auto* worker = AsyncThreadScope::startWorker(this, QStringLiteral("unpaid_banner"), cancel, [result, scope]() {
        // Release all query handles before removing the worker's DB connection.
        struct ConnectionCleanup { ~ConnectionCleanup() { AppRuntimeService::closeThreadConnection(); } } cleanup;
        result->ok = MainWindowMetricsService::loadUnpaidPurchasesSummary(result->count, result->sum, scope, &result->unresolved);
    });
    connect(worker, &QThread::finished, this, [this, result, cancel, scopeKey]() {
        m_bannerRefreshRunning = false;
        const bool sameScope = scopeKey == AccessControlService::currentScopeKey();
        if (result->ok && !cancel->load() && sameScope && !m_bannerRefreshPending) {
            const bool has = result->sum > 0.0001;
            m_banner->setDebt(has, has ? QString::number(result->count) : QString(),
                              has ? FormatUtils::money(result->sum, 2) : QString());
            m_banner->setToolTip(result->unresolved
                ? QString::fromUtf8("Є незвірені накладні Еліт. Показано підтверджений облік; сума ще не остаточна. Відкрийте Аналіз боргів.")
                : QString::fromUtf8("Відкрити деталізацію боргів"));
        }
        if (m_bannerRefreshPending || !sameScope) {
            m_bannerRefreshPending = false;
            QTimer::singleShot(0, this, &MainWindow::updateUnpaidBanner);
        }
    });
    worker->start();
}


void MainWindow::applyAdaptiveShellMetrics()
{
    const QRect avail = AdaptiveUi::availableRect(this);
    const qreal scale = AdaptiveUi::dpiScale(this);

    if (m_sidebar) {
        const int target = qBound(190, qRound(qMin<qreal>(qMax<qreal>(avail.width() * 0.145, 205.0), 248.0 * scale)), 300);
        m_sidebar->setFixedWidth(target);
    }
    if (m_topPill) {
        const int h = qBound(46, AdaptiveUi::px(this, 52), 76);
        m_topPill->setMinimumHeight(h);
        m_topPill->setMaximumHeight(h);
    }
    if (m_topRightSpacer) {
        const int w = qBound(180, qRound(avail.width() * 0.19), AdaptiveUi::px(this, 360));
        m_topRightSpacer->setMinimumWidth(w);
        m_topRightSpacer->setMaximumWidth(w);
    }
}

void MainWindow::positionAnimatedLogo()
{
    if (!m_topPill || !m_animatedLogo) return;
    const QSize hint = m_animatedLogo->sizeHint();
    const int liftPx = 4;         // ~1 mm
    const int x = qMax(0, (m_topPill->width() - hint.width()) / 2);
    const int y = qMax(0, ((m_topPill->height() - hint.height()) / 2) - liftPx);
    m_animatedLogo->setGeometry(x, y, hint.width(), hint.height());
    m_animatedLogo->raise();
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);

    if (!m_updateStatusChecked) {
        m_updateStatusChecked = true;
        QPointer<MainWindow> statusSelf(this);
        QTimer::singleShot(450, this, [statusSelf]() {
            if (!statusSelf || !statusSelf->isVisible()) return;
            UpdateService::checkPreviousUpdateResult(statusSelf);
        });
    }

    if (!m_runtimeWarmupScheduled) {
        m_runtimeWarmupScheduled = true;
        applyFrozenPageMode(1);
        if (m_pageActivation) {
            m_pageActivation->requestActivation(1, QStringLiteral("current_orders_initial"), [this]() {
                if (!m_stack || m_stack->currentIndex() != 1) return;
                refreshCurrentPage();
            }, 0);
        } else {
            refreshCurrentPage();
        }
        QPointer<MainWindow> self(this);
        QTimer::singleShot(1200, this, [self]() {
            if (!self) return;
            self->updateUnpaidBanner();
            if (!self->m_bannerTimer.isActive()) self->m_bannerTimer.start();
        });
        if (!m_updatePromptScheduled && GeneralSettingsService::loadUpdatesAuto()) {
            m_updatePromptScheduled = true;
            QPointer<MainWindow> self(this);
            QTimer::singleShot(9000, this, [self]() {
                if (!self || !self->isVisible()) return;
                UpdateService::promptAndRunUpdate(self, true);
            });
        }
    }

    if (windowHandle() && !m_screenChangedHooked) {
        m_screenChangedHooked = true;
        connect(windowHandle(), &QWindow::screenChanged, this, [this](QScreen*) {
            applyAdaptiveShellMetrics();
            scheduleGeometryLock(0);
            QPointer<MainWindow> self(this);
            QTimer::singleShot(120, this, [self]() {
                if (!self || !self->isVisible()) return;
                self->lockToAvailableScreenGeometry();
            });
        });
    }

    applyAdaptiveShellMetrics();
    scheduleGeometryLock(0);
    QPointer<MainWindow> self(this);
    QTimer::singleShot(120, this, [self]() {
        if (!self || !self->isVisible()) return;
        self->lockToAvailableScreenGeometry();
    });
}

void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    if (!event) return;

    switch (event->type()) {
    case QEvent::WindowStateChange:
    case QEvent::ScreenChangeInternal:
        applyAdaptiveShellMetrics();
        scheduleGeometryLock(0);
        break;
    default:
        break;
    }
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    applyAdaptiveShellMetrics();
    positionAnimatedLogo();
}

void MainWindow::moveEvent(QMoveEvent* event)
{
    QMainWindow::moveEvent(event);
    applyAdaptiveShellMetrics();
}
