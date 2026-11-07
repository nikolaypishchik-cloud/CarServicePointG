// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

#include "ui/main/MainWindowModulePrelude.h"
#include "ui/tabs/reports/PurchaseDebtsPage.h"

void MainWindow::buildUi()
{
    m_root = new QWidget(this);
    auto* rootL = new QHBoxLayout(m_root);
    rootL->setContentsMargins(0,0,0,0);
    rootL->setSpacing(0);

    // Sidebar
    m_sidebar = new QFrame(m_root);
    m_sidebar->setObjectName("sidebar");
    // Narrow the left main-menu block by ~7 mm on the right side so the tab
    // content area gets the same extra width for wide tables.
    m_sidebar->setMinimumWidth(190);
    m_sidebar->setMaximumWidth(320);
    auto* sbL = new QVBoxLayout(m_sidebar);
    sbL->setContentsMargins(14,14,14,24);
    sbL->setSpacing(10);

    auto* topMenuScroll = new QScrollArea(m_sidebar);
    topMenuScroll->setObjectName(QStringLiteral("sidebar_top_scroll"));
    topMenuScroll->setFrameShape(QFrame::NoFrame);
    topMenuScroll->setWidgetResizable(true);
    topMenuScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    topMenuScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    topMenuScroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    topMenuScroll->setAttribute(Qt::WA_StyledBackground, false);
    topMenuScroll->setAutoFillBackground(false);
    topMenuScroll->viewport()->setObjectName(QStringLiteral("sidebar_top_scroll_viewport"));
    topMenuScroll->viewport()->setAutoFillBackground(false);
    topMenuScroll->viewport()->setAttribute(Qt::WA_StyledBackground, false);
    // Important: do not attach a generic stylesheet directly to the viewport.
    // A viewport-local rule like "background: transparent; border: none;" cascades to
    // every descendant button inside the scroll area and strips their normal sidebar
    // background/border styling, leaving only text visible. Scope the rule to the
    // viewport object itself and keep descendants on the global theme rules.
    topMenuScroll->viewport()->setStyleSheet(QStringLiteral("QWidget#sidebar_top_scroll_viewport { background: transparent; border: none; }"));
    topMenuScroll->setStyleSheet(QStringLiteral("QScrollArea#sidebar_top_scroll { background: transparent; border: none; }"));

    auto* topMenuContent = new QWidget(topMenuScroll);
    topMenuContent->setObjectName(QStringLiteral("sidebar_top_content"));
    topMenuContent->setAutoFillBackground(false);
    topMenuContent->setAttribute(Qt::WA_StyledBackground, false);
    auto* topMenuL = new QVBoxLayout(topMenuContent);
    topMenuL->setContentsMargins(0,0,0,0);
    topMenuL->setSpacing(8);

    auto* bottomMenu = new QWidget(m_sidebar);
    bottomMenu->setObjectName(QStringLiteral("sidebar_bottom_menu"));
    bottomMenu->setAutoFillBackground(false);
    bottomMenu->setAttribute(Qt::WA_StyledBackground, false);
    bottomMenu->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* bottomMenuL = new QVBoxLayout(bottomMenu);
    bottomMenuL->setContentsMargins(0,0,0,0);
    bottomMenuL->setSpacing(8);

    m_banner = new UnpaidBannerWidget(m_sidebar);
    m_banner->setObjectName(QStringLiteral("sidebar_debt_banner"));
    connect(m_banner, &UnpaidBannerWidget::clicked, this, [this] {
        if (!canAccessPage(5)) return;
        PurchaseDebtsPage::showDetails(this);
        refreshAll();
    });
    // Keep the banner outside the scrolling menu, aligned with its buttons.
    sbL->addWidget(m_banner);
    sbL->addSpacing(qMax(0, qRound(10.0 * logicalDpiY() / 25.4) - sbL->spacing()));

    // Menu
    m_menu = new QButtonGroup(this);
    m_menu->setExclusive(true);

    auto* btnPlanning = mkMenuButton(QString::fromUtf8("Планування"), m_menu, topMenuContent);
    auto* btnVehicleIntake = mkMenuButton(QString::fromUtf8("Приймання авто"), m_menu, topMenuContent);
    auto* btnNewOrder = new QPushButton(QString::fromUtf8("Новий наряд"), topMenuContent);
    btnNewOrder->setObjectName("sb_btn_uniform");
    btnNewOrder->setMinimumHeight(33);
    btnNewOrder->setMaximumHeight(33);
    btnNewOrder->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* btnCurrent = mkMenuButton(QString::fromUtf8("Наряди"), m_menu, topMenuContent, true);
    btnCurrent->setObjectName(QStringLiteral("sb_btn_orders"));
    btnCurrent->setFixedHeight(33 + qRound(3.0 * logicalDpiY() / 25.4));
    btnCurrent->setStyleSheet(QStringLiteral(
        "QPushButton#sb_btn_orders{color:black;font-weight:800;border:2px solid #fff3a0;border-radius:0px;"
        "background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #fffde0,stop:0.12 #ffe530,stop:0.48 #ffda00,stop:0.50 #ebb500,stop:1 #be8400);}"
        "QPushButton#sb_btn_orders:hover{border-color:#fffad2;"
        "background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #ffffef,stop:0.15 #fff25f,stop:0.48 #ffe530,stop:0.50 #f5bf00,stop:1 #d49b00);}"
        "QPushButton#sb_btn_orders:checked{border-color:#fff3a0;border-radius:6px;}"
        "QPushButton#sb_btn_orders:pressed{background:#ecb800;border-color:white;}"));
    auto* btnParts   = mkMenuButton(QString::fromUtf8("Замовлення запчастин"), m_menu, topMenuContent);
    auto* btnReports = mkMenuButton(QString::fromUtf8("Звіти"), m_menu, topMenuContent);
    auto* btnCrm     = mkMenuButton(QString::fromUtf8("Клієнти"), m_menu, topMenuContent);
    auto* btnWorkshopInventory = mkMenuButton(QString::fromUtf8("Склад СТО"), m_menu, bottomMenu);
    auto* btnStats   = mkMenuButton(QString::fromUtf8("Статистика"), m_menu, bottomMenu);
    auto* btnCatalog = mkMenuButton(QString::fromUtf8("Каталог"), m_menu, bottomMenu);
    auto* btnSettings= mkMenuButton(QString::fromUtf8("Налаштування"), m_menu, bottomMenu);
    auto* btnPhoneSync = mkMenuButton(QString::fromUtf8("Синхронізація телефону"), m_menu, bottomMenu);
    btnSettings->setObjectName(QStringLiteral("sb_btn_settings"));
    auto* btnChangeUser = new QPushButton(QString::fromUtf8("Зміна користувача"), bottomMenu);
    btnChangeUser->setObjectName(QStringLiteral("sb_btn_change_user"));
    btnChangeUser->setMinimumHeight(33);
    btnChangeUser->setMaximumHeight(33);
    btnChangeUser->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    btnChangeUser->setToolTip(QString::fromUtf8("Завершити поточний сеанс і увійти під іншим користувачем"));
    btnChangeUser->setStyleSheet(QStringLiteral(
        "QPushButton#sb_btn_change_user{"
        "background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #5bd37b,stop:0.20 #278f4c,stop:0.58 #146332,stop:1 #0a3a1f);"
        "color:#ffffff;border:1px solid #b6ebc4;border-radius:4px;padding:7px 12px;font-weight:800;}"
        "QPushButton#sb_btn_change_user:hover{"
        "background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #78e899,stop:0.24 #36a95c,stop:1 #126e35);}"
        "QPushButton#sb_btn_change_user:pressed{background:#0c4b27;border-color:#d0f5da;}"));

    topMenuL->addWidget(btnPlanning);
    topMenuL->addWidget(btnVehicleIntake);
    topMenuL->addWidget(btnNewOrder);
    topMenuL->addWidget(btnCurrent);
    topMenuL->addWidget(btnParts);
    topMenuL->addWidget(btnReports);
    topMenuL->addWidget(btnCrm);
    topMenuL->addStretch(1);

    bottomMenuL->addWidget(btnWorkshopInventory);
    bottomMenuL->addWidget(btnCatalog);
    bottomMenuL->addWidget(btnStats);
    bottomMenuL->addWidget(btnSettings);
    bottomMenuL->addWidget(btnPhoneSync);
    bottomMenuL->addWidget(btnChangeUser);

    topMenuScroll->setWidget(topMenuContent);
    sbL->addWidget(topMenuScroll, 1);
    sbL->addWidget(bottomMenu, 0);

    // Content
    m_content = new QWidget(m_root);
    m_content->setObjectName("content");
    auto* cL = new QVBoxLayout(m_content);
    cL->setContentsMargins(14,14,14,14);
    cL->setSpacing(12);

    // Top header pill
    auto* top = new QFrame(m_content);
    m_topPill = top;
    top->setObjectName("top_pill");
    top->setMinimumHeight(46);
    top->setMaximumHeight(78);
    auto* topL = new QHBoxLayout(top);
    topL->setContentsMargins(18,10,18,10);
    topL->setSpacing(10);

    m_topTitle = new QLabel(pageTitleForIndex(1), top);
    m_topTitle->setObjectName("top_title");
    m_topTitle->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_topTitle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    topL->addWidget(m_topTitle, 1);

    m_topRightSpacer = new QWidget(top);
    m_topRightSpacer->setMinimumWidth(180);
    m_topRightSpacer->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_topRightSpacer->setAttribute(Qt::WA_StyledBackground, false);
    m_topRightSpacer->setStyleSheet(QStringLiteral("background:transparent; border:none;"));
    topL->addWidget(m_topRightSpacer, 1);

    m_animatedLogo = new AnimatedMetalLogoWidget(top);
    m_animatedLogo->setObjectName("animated_logo");
    m_animatedLogo->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_animatedLogo->raise();
    cL->addWidget(top);

    m_stack = new QStackedWidget(m_content);
    m_stack->setObjectName("pages");
    connect(m_stack,&QStackedWidget::currentChanged,this,[this,cL](int index) {
        cL->setSpacing(index==5?qMax(1,qRound(0.5*logicalDpiY()/25.4)):
                       index==6?qMax(1,qRound(logicalDpiY()/25.4)):12);
    });
    m_stack->setAttribute(Qt::WA_StyledBackground, false);
    m_stack->setAutoFillBackground(false);
    m_stack->setStyleSheet(QStringLiteral("#pages { background: transparent; border: none; }"));
    m_stack->setFrameShape(QFrame::NoFrame);
    m_stack->setLineWidth(0);
    m_stack->setMidLineWidth(0);

    auto makePlaceholder = [this]() -> QWidget* {
        auto* w = new QWidget(m_stack);
        w->setAttribute(Qt::WA_StyledBackground, false);
        auto* l = new QVBoxLayout(w);
        l->setContentsMargins(18, 18, 18, 18);
        auto* lbl = new QLabel(QString::fromUtf8("Завантаження вкладки…"), w);
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setStyleSheet(QStringLiteral("background: transparent; color: rgba(255,255,255,0.88); font-size: 12pt; font-weight: 700;"));
        l->addStretch(1);
        l->addWidget(lbl);
        l->addStretch(1);
        return w;
    };

    m_placeholderPlanning = makePlaceholder();
    m_placeholderCurrent = makePlaceholder();
    m_placeholderMasters = makePlaceholder();
    m_placeholderHistory = makePlaceholder();
    m_placeholderParts = makePlaceholder();
    m_placeholderReports = makePlaceholder();
    m_placeholderStats = makePlaceholder();
    m_placeholderCatalog = makePlaceholder();
    m_placeholderSettings = makePlaceholder();
    m_placeholderCrm = makePlaceholder();
    m_placeholderCloudSync = makePlaceholder();
    m_placeholderOnlineBooking = makePlaceholder();
    m_placeholderVehicleIntake = makePlaceholder();
    m_placeholderPhoneSync = makePlaceholder();
    m_placeholderWorkshopInventory = new QWidget(m_stack);
    m_placeholderWorkshopInventory->setAttribute(Qt::WA_StyledBackground, false);
    m_placeholderWorkshopInventory->setAutoFillBackground(false);

    m_stack->addWidget(m_placeholderPlanning); // 0
    m_stack->addWidget(m_placeholderCurrent);  // 1
    m_stack->addWidget(m_placeholderMasters);  // 2
    m_stack->addWidget(m_placeholderHistory);  // 3
    m_stack->addWidget(m_placeholderParts);    // 4
    m_stack->addWidget(m_placeholderReports);  // 5
    m_stack->addWidget(m_placeholderStats);    // 6
    m_stack->addWidget(m_placeholderCatalog);  // 7
    m_stack->addWidget(m_placeholderSettings); // 8
    m_stack->addWidget(m_placeholderCrm);      // 9
    m_stack->addWidget(m_placeholderCloudSync); // 10
    m_stack->addWidget(m_placeholderOnlineBooking); // 11
    m_stack->addWidget(m_placeholderVehicleIntake); // 12
    m_stack->addWidget(m_placeholderWorkshopInventory); // 13
    m_stack->addWidget(m_placeholderPhoneSync); // 14

    m_ordersNavigation = new QWidget(m_content);
    m_ordersNavigation->setObjectName("ordersNavigation");
    m_ordersNavigation->setAutoFillBackground(false);
    m_ordersNavigation->setStyleSheet("QWidget#ordersNavigation { background: transparent; border: none; }");
    auto* ordersLayout = new QHBoxLayout(m_ordersNavigation);
    ordersLayout->setContentsMargins(0, 0, 0, 0);
    auto* ordersGroup = new QButtonGroup(m_ordersNavigation);
    for (const auto& entry : QList<QPair<int, QString>>{
             {1, QString::fromUtf8("Поточні наряди")},
             {3, QString::fromUtf8("Завершені наряди")}}) {
        auto* button = new SidebarMenuButton(entry.second, m_ordersNavigation);
        button->setCheckable(true);
        button->setFixedHeight(33);
        button->setMinimumWidth(160);
        button->setCursor(Qt::PointingHandCursor);
        ordersGroup->addButton(button, entry.first);
        ordersLayout->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, index = entry.first] { gotoPage(index); });
        connect(m_stack, &QStackedWidget::currentChanged, button, [button, index = entry.first](int active) {
            button->setChecked(active == index);
        });
        const QString permission = entry.first == 1 ? QStringLiteral("nav.current_orders")
            : entry.first == 3 ? QStringLiteral("nav.history") : QStringLiteral("nav.reports");
        m_accessButtons.insert(QStringLiteral("orders.tab.%1").arg(entry.first), button);
        button->setChecked(entry.first == 1);
        button->setProperty("ordersPermission", permission);
    }
    auto* settlement = new QLabel(tr("Оберіть наряд для розрахунку"), m_ordersNavigation);
    settlement->setObjectName("currentOrderSettlement");
    settlement->setTextFormat(Qt::PlainText);
    settlement->setAlignment(Qt::AlignCenter);
    settlement->setFixedHeight(33);
    settlement->setStyleSheet("QLabel#currentOrderSettlement {background:#284957;color:#ffffff;"
                              "border:1px solid #698491;border-radius:8px;font-family:'Times New Roman';font-weight:bold;}");
    ordersLayout->addWidget(settlement, 1);
    connect(m_stack, &QStackedWidget::currentChanged, settlement, [settlement](int index) {
        settlement->setVisible(index == 1);
    });
    cL->addWidget(m_ordersNavigation);

    // Materialize the initial Current Orders page eagerly so the main window
    // opens on the real first tab instead of a placeholder flash. Heavy page
    // prewarm of secondary tabs is scheduled later, after the shell is visible.
    ensurePage(1);
    connect(m_pageCurrent, &CurrentOrdersPage::settlementChanged, settlement, &QLabel::setText);
    m_stack->setCurrentIndex(1);
    settlement->show();
    cL->addWidget(m_stack, 1);

    rootL->addWidget(m_sidebar);
    rootL->addWidget(m_content, 1);

    applyAdaptiveShellMetrics();
    positionAnimatedLogo();

    setCentralWidget(m_root);
    applyAdaptiveShellMetrics();

    // Local graphite shimmer overlays: behind child widgets, so white tables are not affected.
    const QList<QWidget*> shimmerHosts = {
        m_sidebar,
        m_content,
        top
    };
    for (QWidget* host : shimmerHosts) {
        if (!host) continue;
        auto* shimmer = new GraphiteShimmerOverlay(host);
        shimmer->hide();
        shimmer->lower();
    }

    const QList<QWidget*> shimmerButtons = {
        
    };
    for (QWidget* host : shimmerButtons) {
        if (!host) continue;
        auto* shimmer = new GraphiteShimmerOverlay(host);
        shimmer->hide();
        shimmer->raise();
    }

    // Navigation
    connect(btnPlanning, &QPushButton::clicked, this, [this]() { gotoPage(0); });
    connect(btnCurrent, &QPushButton::clicked, this, [this]() {
        for (int index : {1, 3}) if (canAccessPage(index)) { gotoPage(index); break; }
    });
    connect(m_stack, &QStackedWidget::currentChanged, this, [this, btnCurrent](int index) {
        const bool orders = index == 1 || index == 3;
        m_ordersNavigation->setVisible(orders);
        if (orders) btnCurrent->setChecked(true);
    });
    connect(btnParts,    &QPushButton::clicked, this, [this]() { gotoPage(4); });
    connect(btnReports,  &QPushButton::clicked, this, [this]() { gotoPage(5); });
    connect(btnCrm,      &QPushButton::clicked, this, [this]() { gotoPage(9); });
    connect(btnVehicleIntake, &QPushButton::clicked, this, [this]() { gotoPage(12); });
    connect(btnWorkshopInventory, &QPushButton::clicked, this, [this]() { gotoPage(13); });
    connect(btnStats,    &QPushButton::clicked, this, [this]() { gotoPage(6); });
    connect(btnCatalog,  &QPushButton::clicked, this, [this]() { gotoPage(7); });
    connect(btnSettings, &QPushButton::clicked, this, [this]() { gotoPage(8); });
    connect(btnPhoneSync, &QPushButton::clicked, this, [this]() { gotoPage(14); });
    connect(btnChangeUser, &QPushButton::clicked, this, &MainWindow::switchUser);

    connect(btnNewOrder, &QPushButton::clicked, this, &MainWindow::openNewOrder);

    const auto ordersButtons = m_accessButtons;
    m_accessButtons = {
        {QStringLiteral("nav.planning"), btnPlanning},
        {QStringLiteral("nav.intake"), btnVehicleIntake},
        {QStringLiteral("nav.new_order"), btnNewOrder},
        {QStringLiteral("nav.current_orders"), btnCurrent},
        {QStringLiteral("nav.parts"), btnParts},
        {QStringLiteral("nav.reports"), btnReports},
        {QStringLiteral("nav.clients"), btnCrm},
        {QStringLiteral("nav.inventory"), btnWorkshopInventory},
        {QStringLiteral("nav.catalog"), btnCatalog},
        {QStringLiteral("nav.statistics"), btnStats},
        {QStringLiteral("nav.settings"), btnSettings},
        {QStringLiteral("mobile.pair"), btnPhoneSync}
    };
    for (auto it = ordersButtons.cbegin(); it != ordersButtons.cend(); ++it) m_accessButtons.insert(it.key(), it.value());
    applyAccessControl();
}
