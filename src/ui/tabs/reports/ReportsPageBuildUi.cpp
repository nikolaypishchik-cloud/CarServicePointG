// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

#include "ui/tabs/reports/ReportsPageModulePrelude.h"
#include <QComboBox>
#include "ui/tabs/reports/PurchaseDebtsPage.h"
#include "ui/tabs/reports/InvoicesPage.h"
#include "ui/tabs/reports/PartsRegisterView.h"
#include <QScrollArea>
#include <QTabBar>
#include <QStackedWidget>
#include <QPainter>
#include "ui/tabs/reports/ReportDashboard.h"

namespace {
class DebtTabBar final : public QTabBar {
public:
    explicit DebtTabBar(QWidget* parent) : QTabBar(parent) {}
protected:
    void paintEvent(QPaintEvent* event) override {
        QTabBar::paintEvent(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        for (int i=0;i<count();++i) {
            const QString kind=tabData(i).toString();
            if (kind!="debt" && kind!="analysis") continue;
            const bool selected=currentIndex()==i;
            const QRect rect=tabRect(i).adjusted(1,1,-1,-1);
            painter.save(); painter.setClipRect(tabRect(i));
            painter.setBrush(QColor(kind=="analysis" ? (selected?"#f59e0b":"#c76a08") : (selected?"#dc2626":"#b91c1c")));
            painter.setPen(QPen(selected?Qt::white:QColor("#c9cdd2"),selected?2:1));
            painter.drawRoundedRect(rect,8,8);
            auto textFont=font(); textFont.setBold(selected); painter.setFont(textFont);
            painter.setPen(kind=="analysis" && selected ? QColor("#111111") : Qt::white);
            painter.drawText(rect.adjusted(8,0,-8,0),Qt::AlignCenter,tabText(i));
            painter.restore();
        }
    }
};
class DebtReportTabs final : public QTabWidget {
public:
    explicit DebtReportTabs(QWidget* parent) : QTabWidget(parent) {
        setTabBar(new DebtTabBar(this));
        // The global QWidget gradient otherwise restarts behind the tab controls.
        if(auto* stack=findChild<QStackedWidget*>(QString(),Qt::FindDirectChildrenOnly)) {
            stack->setObjectName("reportsTabStack");
            stack->setAutoFillBackground(false);
            stack->setStyleSheet("#reportsTabStack {background:transparent;border:none;border-radius:0px;}");
        }
    }
};
}

void ReportsPage::buildUi()
{
    setObjectName(QStringLiteral("reportsPage"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QString::fromUtf8(R"(
        #reportsPage {
            background: transparent;
            border: none;
        }
    )"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* topHost = new QWidget(this);
    topHost->setObjectName(QStringLiteral("reportsTopHost"));
    topHost->setAttribute(Qt::WA_StyledBackground, true);
    topHost->setFixedHeight(qMax(AdaptiveUi::px(topHost,164),fontMetrics().lineSpacing()*8+24));
    topHost->setStyleSheet(QString::fromUtf8(R"(
        #reportsTopHost {
            background: transparent;
            border: none;
        }
    )"));
    auto* viewportLayout=new QVBoxLayout(topHost);
    viewportLayout->setContentsMargins(0,0,0,0);
    auto* headerScroll=new QScrollArea(topHost);
    headerScroll->setObjectName("reportsHeaderScroll");
    headerScroll->setFrameShape(QFrame::NoFrame);
    headerScroll->setWidgetResizable(true);
    headerScroll->setStyleSheet("QScrollArea#reportsHeaderScroll, QScrollArea#reportsHeaderScroll > QWidget {background:transparent;border:none;}");
    auto* headerContent=new QWidget;
    headerContent->setObjectName("reportsHeaderContent");
    headerContent->setStyleSheet("#reportsHeaderContent {background:transparent;border:none;}");
    headerScroll->setWidget(headerContent);
    viewportLayout->addWidget(headerScroll);
    auto* headerLayout=new QVBoxLayout(headerContent);
    headerLayout->setContentsMargins(0,0,0,0);
    headerLayout->setSpacing(0);
    headerLayout->setAlignment(Qt::AlignVCenter);
    auto* headerRow=new QWidget(topHost);
    headerRow->setObjectName("reportsHeaderRow");
    headerRow->setStyleSheet("#reportsHeaderRow {background:transparent;border:none;}");
    headerLayout->addWidget(headerRow);
    auto* top = new QHBoxLayout(headerRow);
    top->setContentsMargins(4, 4, 4, 6);
    top->setSpacing(4);
    top->setAlignment(Qt::AlignVCenter);

    auto* lblFrom = new QLabel(QString::fromUtf8("З:"), this);
    auto* lblTo = new QLabel(QString::fromUtf8("По:"), this);

    m_from = new QDateEdit(QDate::currentDate().addDays(-9), this);
    m_from->setDisplayFormat("dd.MM.yy");
    m_from->setCalendarPopup(true);
    m_from->setMinimumWidth(AdaptiveUi::px(m_from, 124));
    m_from->setMinimumHeight(AdaptiveUi::px(m_from, 34));
    m_from->setKeyboardTracking(false);
    m_from->setDateRange(QDate(1900, 1, 1), QDate(9999, 12, 31));
    m_to = new QDateEdit(QDate::currentDate(), this);
    m_to->setDisplayFormat("dd.MM.yy");
    m_to->setCalendarPopup(true);
    m_to->setMinimumWidth(AdaptiveUi::px(m_to, 124));
    m_to->setMinimumHeight(AdaptiveUi::px(m_to, 34));
    m_to->setKeyboardTracking(false);
    m_to->setDateRange(QDate(1900, 1, 1), QDate(9999, 12, 31));

    auto* filtersHost = new QWidget(topHost);
    filtersHost->setObjectName("reportsFiltersFrame");
    auto* filters = new QHBoxLayout(filtersHost);
    filters->setContentsMargins(4,6,4,6);
    filters->setSpacing(12);
    filters->setAlignment(Qt::AlignTop);
    filters->addWidget(lblFrom);
    filters->addWidget(m_from);
    filters->addWidget(lblTo);
    filters->addWidget(m_to);
    top->addWidget(filtersHost);
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName(QStringLiteral("reportsOrdersSearch"));
    m_searchEdit->setPlaceholderText(QString::fromUtf8("Пошук: № наряду, номер авто / 1212 / АА 1, марка авто, VIN…"));
    m_searchEdit->setClearButtonEnabled(true);
    AdaptiveUi::relaxLineEditWidth(m_searchEdit, 220, 620);
    m_searchEdit->hide();
    top->addWidget(m_searchEdit, 1);
    m_searchDebounce = new QTimer(this);
    m_searchDebounce->setSingleShot(true);
    m_searchDebounce->setInterval(200);
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this] { m_searchDebounce->start(); });
    connect(m_searchDebounce, &QTimer::timeout, this, [this] { scheduleRefresh(0); });
    connect(m_searchEdit, &QLineEdit::returnPressed, this, [this] { m_searchDebounce->stop(); scheduleRefresh(0); });

    auto* bannersHost = new QWidget(topHost);
    bannersHost->setObjectName(QStringLiteral("reportsBannersHost"));
    bannersHost->setAttribute(Qt::WA_StyledBackground, true);
    bannersHost->setStyleSheet(QStringLiteral("#reportsBannersHost { background: transparent; border: none; }"));
    auto* banners = new QHBoxLayout(bannersHost);
    banners->setContentsMargins(8, 6, 8, 6);
    // Keep the right banner anchored and push the left auxiliary banner 0.8 cm farther left.
    // Base spacing was 5 mm; requested visual delta is +8 mm, so total spacing becomes 13 mm.
    banners->setSpacing(16);

    m_oilStatsLabel = new QLabel(bannersHost);
    m_oilStatsLabel->setObjectName(QStringLiteral("reportsOilStatsLabel"));
    m_oilStatsLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_oilStatsLabel->setMinimumWidth(AdaptiveUi::px(m_oilStatsLabel, 260));
    m_oilStatsLabel->setMaximumWidth(QWIDGETSIZE_MAX);
    m_oilStatsLabel->setMinimumHeight(AdaptiveUi::px(m_oilStatsLabel, 72));
    m_oilStatsLabel->setWordWrap(true);
    m_oilStatsLabel->setTextFormat(Qt::RichText);
    m_oilStatsLabel->setStyleSheet(QStringLiteral("#reportsOilStatsLabel { color: #ffffff; font-size: 11pt; font-weight: 500; }"));
    m_oilStatsLabel->hide();
    banners->addWidget(m_oilStatsLabel,1);

    m_statsLabel = new QLabel(bannersHost);
    m_statsLabel->setObjectName(QStringLiteral("reportsStatsLabel"));
    m_statsLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_statsLabel->setMinimumWidth(AdaptiveUi::px(m_statsLabel, 240));
    m_statsLabel->setMaximumWidth(QWIDGETSIZE_MAX);
    m_statsLabel->setMinimumHeight(AdaptiveUi::px(m_statsLabel, 72));
    m_statsLabel->setWordWrap(true);
    m_statsLabel->setStyleSheet(QStringLiteral("#reportsStatsLabel { color: #ffffff; font-size: 13pt; font-weight: 500; }"));
    banners->addWidget(m_statsLabel,1);

    top->addWidget(bannersHost, 2);


    m_pdfButton = new QPushButton(QString::fromUtf8("PDF"), this);
    m_pdfButton->setObjectName("reportsPdfButton");
    m_pdfButton->setMinimumWidth(AdaptiveUi::px(m_pdfButton, 92));
    m_pdfButton->setMinimumHeight(AdaptiveUi::px(m_pdfButton, 34));

    root->addWidget(topHost);

    m_tabs = new DebtReportTabs(this);
    m_tabs->setDocumentMode(true);
    m_tabs->setContentsMargins(0, 0, 0, 0);

    auto makeTab = [this](QTableView*& table, ReportsTableSupport::ReportsSnapshotTableModel*& model) {
        auto* page = new QWidget(this);
        page->setObjectName(QStringLiteral("reportsTabPage"));
        page->setAttribute(Qt::WA_StyledBackground, true);
        page->setStyleSheet(QStringLiteral("#reportsTabPage { background: transparent; }"));
        auto* l = new QVBoxLayout(page);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(0);
        table = new QTableView(page);
        model = new ReportsTableSupport::ReportsSnapshotTableModel(table);
        table->setModel(model);
        setTableBaseStyle(table);
        l->addWidget(table, 1);
        return page;
    };

    const ExpensesWidgets expensesWidgets = ReportsExpensesSupport::buildExpensesPage(this);
    m_expensesPage = expensesWidgets.page;
    m_expenseValidation = expensesWidgets.validationLabel;
    m_repeatExpenses = expensesWidgets.repeatButton;
    connect(m_repeatExpenses, &QPushButton::clicked, this, [this] { refreshExpenses(true); });
    m_elecPrevEdit = expensesWidgets.elecPrevEdit;
    m_elecCurrEdit = expensesWidgets.elecCurrEdit;
    m_elecTariffEdit = expensesWidgets.elecTariffEdit;
    m_elecKwhEdit = expensesWidgets.elecKwhEdit;
    m_elecCostEdit = expensesWidgets.elecCostEdit;
    m_cleaningEdit = expensesWidgets.cleaningEdit;
    m_cleaningNoteEdit = expensesWidgets.cleaningNoteEdit;
    m_softwareEdit = expensesWidgets.softwareEdit;
    m_softwareNoteEdit = expensesWidgets.softwareNoteEdit;
    m_heatingEdit = expensesWidgets.heatingEdit;
    m_heatingNoteEdit = expensesWidgets.heatingNoteEdit;
    m_amortizationEdit = expensesWidgets.amortizationEdit;
    m_amortizationNoteEdit = expensesWidgets.amortizationNoteEdit;
    m_rentEdit = expensesWidgets.rentEdit;
    m_rentNoteEdit = expensesWidgets.rentNoteEdit;
    m_otherEdit = expensesWidgets.otherEdit;
    m_otherNoteEdit = expensesWidgets.otherNoteEdit;
    m_expensesTotalEdit = expensesWidgets.expensesTotalEdit;
    m_expensesSaveButton = expensesWidgets.expensesSaveButton;
    m_electricityPdfButton = expensesWidgets.electricityPdfButton;

    m_tabs->addTab(makeTab(m_periodTable, m_periodModel), QString::fromUtf8("Звіт за період"));
    m_tabs->addTab(makeTab(m_ordersTable, m_ordersModel), QString::fromUtf8("Наряди"));
    m_invoicesPage = new InvoicesPage(this);
    m_invoicesPage->periodExpanded=[this](QDate from,QDate to){m_from->setDate(from);m_to->setDate(to);};
    auto* invoiceSearch=m_invoicesPage->searchField();
    AdaptiveUi::relaxLineEditWidth(invoiceSearch,220,620);
    top->insertWidget(2,invoiceSearch,1);invoiceSearch->hide();
    auto* invoiceSummary=m_invoicesPage->findChild<QLabel*>("invoiceSummary");
    m_invoicesPage->controlsHost()->layout()->removeWidget(invoiceSummary);
    auto* summaryHost=new QWidget(headerContent);
    summaryHost->setObjectName("invoiceSummaryHost");
    summaryHost->setStyleSheet("#invoiceSummaryHost {background:transparent;border:none;}");
    auto* summaryLayout=new QVBoxLayout(summaryHost);
    summaryLayout->setContentsMargins(0,0,0,0);
    summaryLayout->addWidget(invoiceSummary);
    headerLayout->addWidget(summaryHost);summaryHost->hide();
    top->insertWidget(3,m_invoicesPage->controlsHost());
    m_invoicesPage->controlsHost()->hide();
    m_invoicesPage->paymentChanged = [this] { markDirty(); queueRefreshAll(); };
    m_invoicesPage->inventoryRequested = [this](qint64 id) { emit requestInventoryPurchase(id); };
    m_tabs->addTab(m_invoicesPage, QString::fromUtf8("Накладні"));
    m_dashboard=new ReportDashboard(topHost);
    m_dashboard->setInvoiceControls(m_invoicesPage->controlsHost());
    viewportLayout->addWidget(m_dashboard);
    m_dashboard->hide();
    m_dashboard->from->setDate(m_from->date());m_dashboard->to->setDate(m_to->date());
    connect(m_dashboard->from,&QDateEdit::dateChanged,m_from,&QDateEdit::setDate);
    connect(m_dashboard->to,&QDateEdit::dateChanged,m_to,&QDateEdit::setDate);
    connect(m_from,&QDateEdit::dateChanged,m_dashboard->from,&QDateEdit::setDate);
    connect(m_to,&QDateEdit::dateChanged,m_dashboard->to,&QDateEdit::setDate);
    connect(m_dashboard->search,&QLineEdit::textChanged,this,[this](const QString& text){
        if(activeTab()==ReportTab::Orders)m_searchEdit->setText(text);
        else if(activeTab()==ReportTab::Invoices)m_invoicesPage->searchField()->setText(text);
    });
    m_invoicesPage->metricsChanged=[this](int count,int paid,int unpaid,qint64 remaining){
        m_invoiceMetrics={QString::number(count),QString::number(paid),QString::number(unpaid),FormatUtils::money(remaining/100.0,2)+QString::fromUtf8(" грн")};
        if(activeTab()==ReportTab::Invoices)updateDashboard();
    };
    m_dashboard->action=[this](int group,int index){
        if(group==0){
            const auto today=QDate::currentDate();m_to->setDate(today);
            m_from->setDate(index==0?today:index==1?today.addDays(-6):QDate(today.year(),today.month(),1));
        }else if(group==1){m_orderStatusFilter=index;}
        else if(index==1){m_orderStatusFilter=0;m_searchEdit->clear();m_to->setDate(QDate::currentDate());m_from->setDate(QDate::currentDate().addDays(-9));}
        markDirty();scheduleRefresh(0);updateDashboard();
    };
    auto* partsPage = makeTab(m_partsTable, m_partsModel);
    auto* partsLayout = qobject_cast<QVBoxLayout*>(partsPage->layout());
    auto* groupActions = new QHBoxLayout;
    auto* collapse = new QPushButton(tr("Згорнути всі"), partsPage);
    auto* expand = new QPushButton(tr("Розгорнути всі"), partsPage);
    collapse->setObjectName("partsCollapseAll");
    expand->setObjectName("partsExpandAll");
    auto* search = new QLineEdit(partsPage);
    search->setObjectName("partsReportSearch");
    search->setAccessibleName(tr("Пошук у придбанні запчастин"));
    search->setPlaceholderText(tr("Запчастина, наряд або накладна"));
    search->setClearButtonEnabled(true);
    auto* mode = new QTabBar(partsPage);
    mode->setObjectName("partsReportMode");
    mode->setAccessibleName(tr("Режим придбання запчастин"));
    mode->setDrawBase(false);
    mode->setExpanding(false);
    mode->setUsesScrollButtons(false);
    mode->setElideMode(Qt::ElideNone);
    mode->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    mode->setFocusPolicy(Qt::StrongFocus);
    mode->addTab(tr("Звіт"));
    mode->addTab(tr("За нарядами"));
    mode->addTab(tr("Усі позиції"));
    mode->setStyleSheet("#partsReportMode::tab { padding: 4px 10px; margin: 1px; border: 1px solid #718591; border-radius: 4px; }"
                        "#partsReportMode::tab:selected { background: #146873; color: white; border-color: #55dae0; }");
    mode->ensurePolished();
    int tabsWidth = 0;
    for (int index = 0; index < mode->count(); ++index)
        tabsWidth += mode->tabRect(index).width();
    mode->setMinimumWidth(tabsWidth + 2 * mode->style()->pixelMetric(QStyle::PM_DefaultFrameWidth, nullptr, mode));
    groupActions->addWidget(search, 1);
    groupActions->addWidget(collapse);
    groupActions->addWidget(expand);
    groupActions->addWidget(mode);
    auto* noMatches = new QLabel(tr("За цим запитом позицій не знайдено"), partsPage);
    noMatches->setObjectName("partsSearchEmpty");
    noMatches->hide();
    partsLayout->insertWidget(0, noMatches);
    partsLayout->insertLayout(0, groupActions);
    const auto updateRows = [this, search, expand, noMatches, mode] {
        const QString query = search->text().trimmed();
        const bool collapsed = expand->isEnabledTo(expand->parentWidget());
        const int count = m_partsModel->rowCount();
        bool groupMatches = false;
        bool found = false;
        for (int row = count - 1; row >= 0; --row) {
            if (m_partsModel->rowSpansAllColumns(row)) {
                // A single spanning row also carries loading and empty-result messages.
                m_partsTable->setRowHidden(row, count > 1 && !groupMatches);
                groupMatches = false;
                continue;
            }
            const bool matches = query.isEmpty()
                || m_partsModel->cellText(row, 1).contains(query, Qt::CaseInsensitive)
                || m_partsModel->cellText(row, 2).contains(query, Qt::CaseInsensitive)
                || m_partsModel->cellText(row, 3).contains(query, Qt::CaseInsensitive);
            found |= matches;
            groupMatches |= matches;
            m_partsTable->setRowHidden(row, collapsed || !matches);
        }
        noMatches->setVisible(mode->currentIndex() == 0 && count > 1 && !found && !query.isEmpty());
    };
    const auto setCollapsed = [collapse, expand, updateRows](bool collapsed) {
        collapse->setEnabled(!collapsed);
        expand->setEnabled(collapsed);
        updateRows();
    };
    expand->setEnabled(false);
    connect(collapse, &QPushButton::clicked, this, [setCollapsed] { setCollapsed(true); });
    connect(expand, &QPushButton::clicked, this, [setCollapsed] { setCollapsed(false); });
    connect(search, &QLineEdit::textChanged, this, updateRows);
    connect(m_partsModel, &QAbstractItemModel::modelReset, this, [search, expand, noMatches, updateRows] {
        if (!search->text().trimmed().isEmpty() || expand->isEnabledTo(expand->parentWidget()))
            updateRows();
        else
            noMatches->hide();
    });
    m_partsRegister = new PartsRegisterView(partsPage);
    partsLayout->addWidget(m_partsRegister, 1);
    m_partsRegister->hide();
    QWidget::setTabOrder(search, collapse);
    QWidget::setTabOrder(collapse, expand);
    QWidget::setTabOrder(expand, mode);
    QWidget::setTabOrder(mode, m_partsTable);
    m_partsRegister->setTabOrderAfter(m_partsTable);
    m_partsRegister->storeChanged = [this] { updateDashboard(); };
    m_partsRegister->orderEdited = [this] { markDirty(); scheduleRefresh(0); queueRefreshAll(0); };
    connect(search, &QLineEdit::textChanged, m_partsRegister, &PartsRegisterView::setSearch);
    connect(mode, &QTabBar::currentChanged, this, [this, collapse, expand, search, updateRows](int index) {
        const bool flat = index != 0;
        m_partsTable->setVisible(!flat);
        m_partsRegister->setActive(flat, index == 1);
        collapse->setVisible(!flat);
        expand->setVisible(!flat);
        search->setPlaceholderText(flat ? tr("Запчастина, OEM, авто або наряд") : tr("Запчастина, наряд або накладна"));
        updateRows();
        updateDashboard();
    });
    m_tabs->addTab(partsPage, QString::fromUtf8("Придбання запчастин"));
    m_tabs->addTab(makeTab(m_outsourceTable, m_outsourceModel), QString::fromUtf8("Аутсорс"));
    m_tabs->addTab(m_expensesPage, QString::fromUtf8("Витрати"));
    auto* totalExpensesPage = makeTab(m_totalExpensesTable, m_totalExpensesModel);
    buildAdditionalExpensesForm(totalExpensesPage);
    auto* expenseControls=totalExpensesPage->findChild<QWidget*>("additionalExpensesFormScroll");
    m_dashboard->setExpenseControls(expenseControls);
    expenseControls->hide();
    m_tabs->addTab(totalExpensesPage, QString::fromUtf8("Сукупні витрати"));
    auto* payrollPage = makeTab(m_payrollTable, m_payrollModel);
    auto* payrollFilters = new QHBoxLayout;
    m_payrollSearch = new QLineEdit(payrollPage);
    m_payrollSearch->setObjectName("payrollSearch");
    m_payrollSearch->setPlaceholderText(tr("Пошук співробітника"));
    m_payrollEmployee = new QComboBox(payrollPage);
    m_payrollEmployee->setObjectName("payrollEmployee");
    m_payrollEmployee->setAccessibleName(tr("Співробітник"));
    payrollFilters->addWidget(m_payrollSearch, 1);
    payrollFilters->addWidget(m_payrollEmployee);
    m_payrollGrouping = new QComboBox(payrollPage);
    m_payrollGrouping->setObjectName("payrollGrouping");
    m_payrollGrouping->setAccessibleName(tr("Групування зарплати"));
    m_payrollGrouping->addItems({tr("Без помісячного розподілу"), tr("За співробітниками й місяцями"), tr("За місяцями й співробітниками")});
    payrollFilters->addWidget(m_payrollGrouping);
    connect(m_payrollGrouping, &QComboBox::currentIndexChanged, this, &ReportsPage::refreshPayroll);
    static_cast<QVBoxLayout*>(payrollPage->layout())->insertLayout(0, payrollFilters);
    connect(m_payrollSearch, &QLineEdit::textChanged, this, &ReportsPage::filterPayrollRows);
    connect(m_payrollEmployee, &QComboBox::currentIndexChanged, this, &ReportsPage::filterPayrollRows);
    m_tabs->addTab(payrollPage, tr("Зарплата"));
    m_debtsPage = new PurchaseDebtsPage(this);
    m_debtsPage->paymentChanged = [this]() { markDirty(); queueRefreshAll(); };
    m_tabs->addTab(m_debtsPage, QString::fromUtf8("Борги"));
    m_debtAnalysisPage = new PurchaseDebtAnalysisPage(this);
    m_debtAnalysisPage->inventoryRequested = [this](qint64 id) { emit requestInventoryPurchase(id); };
    m_debtAnalysisPage->reconciliationChanged = [this] { markDirty(); queueRefreshAll(); };
    m_debtAnalysisPage->reconciliationRequested = [this] { m_debtsPage->reconcileDebts(); m_debtAnalysisPage->refresh(); };
    m_tabs->addTab(m_debtAnalysisPage, QString::fromUtf8("Аналіз боргів"));
    m_tabs->tabBar()->setTabData(static_cast<int>(ReportTab::Debts),"debt");
    m_tabs->tabBar()->setTabData(static_cast<int>(ReportTab::DebtAnalysis),"analysis");

    m_tabs->setStyleSheet(QString::fromUtf8(R"(
        QTabWidget { background: transparent; }
        QTabWidget::pane { margin-top: 0px; background: transparent; border: none; border-radius: 0px; }
        QTabWidget::right-corner { background: transparent; }
        QTabBar { background: transparent; }
        QTabBar::tab { margin-bottom: 0px; }
        QTabBar::tab:selected {
            color: #111111;
            font-weight: 700;
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 rgba(255,255,255,0.82),
                                        stop:0.16 rgba(245,250,255,0.70),
                                        stop:0.17 rgba(160,174,190,0.90),
                                        stop:0.46 rgba(119,132,147,0.92),
                                        stop:0.76 rgba(82,92,105,0.94),
                                        stop:1 rgba(61,69,80,0.96));
            border: 1px solid rgba(255,255,255,0.48);
        }
        QTabBar::tab:!selected {
            color: #f5f7fa;
        }
    )"));

    m_descLabel = new QLabel(this);
    m_descLabel->setWordWrap(true);
    m_descLabel->setContentsMargins(0, 0, 0, 0);
    m_descLabel->setAttribute(Qt::WA_StyledBackground, true);
    m_descLabel->setStyleSheet(QStringLiteral("background: transparent; color: rgba(245,247,250,0.92);"));
    m_descLabel->hide();
    if (m_tabs->tabBar()) {
        m_tabs->tabBar()->setFixedHeight(40);
        m_tabs->tabBar()->setDrawBase(false);
    }
    m_tabs->setCornerWidget(m_pdfButton, Qt::TopRightCorner);

    root->addWidget(m_tabs, 1);
    QPointer<ReportsPage> self(this);
    QTimer::singleShot(0, this, [self]() { if (self) self->updatePdfButtonPosition(); });

    ReportsTableSupport::initializePeriodSnapshot(m_periodModel);
    ReportsTableSupport::initializeOrdersSnapshot(m_ordersModel);
    ReportsTableSupport::initializePartsSnapshot(m_partsModel);
    ReportsTableSupport::initializeOutsourceSnapshot(m_outsourceModel);
    ReportsTableSupport::initializePayrollSnapshot(m_payrollModel);
    ReportsTableSupport::initializeTotalExpensesSnapshot(m_totalExpensesModel);

    m_periodTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_periodTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_periodTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_periodTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_periodTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_periodTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_periodTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);

    m_ordersTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_ordersTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_ordersTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_ordersTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_ordersTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_ordersTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_ordersTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);

    m_partsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_partsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_partsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_partsTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_partsTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_partsTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_partsTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    m_partsTable->horizontalHeader()->setSectionResizeMode(7, QHeaderView::ResizeToContents);

    m_outsourceTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_outsourceTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_outsourceTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_outsourceTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_outsourceTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_outsourceTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_outsourceTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);

    m_payrollTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_payrollTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_payrollTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_payrollTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_payrollTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);

    m_totalExpensesTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    for (int column = 1; column <= 4; ++column) {
        m_totalExpensesTable->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    }

    AdaptiveUi::installAdaptiveTable(m_periodTable, {
        {0, 88, 100, 1, 130, false},
        {1, 92, 110, 1, 140, false},
        {2, 220, 320, 6, 620, false},
        {3, 120, 150, 2, 240, false},
        {4, 96, 110, 1, 150, false},
        {5, 96, 110, 1, 150, false},
        {6, 96, 110, 1, 150, false}
    }, QStringLiteral("ui.reports.period.adaptive"));
    AdaptiveUi::installAdaptiveTable(m_ordersTable, {
        {0, 90, 100, 1, 140, false},
        {1, 80, 90, 1, 120, false},
        {2, 220, 300, 5, 600, false},
        {3, 120, 150, 2, 260, false},
        {4, 140, 170, 2, 280, false},
        {5, 105, 120, 1, 160, false},
        {6, 92, 104, 1, 140, false}
    }, QStringLiteral("ui.reports.orders.list.adaptive"));
    m_partsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    AdaptiveUi::installAdaptiveTable(m_partsTable, {
        {0, 88, 100, 1, 130, false},
        {1, 92, 110, 1, 140, false},
        {2, 220, 320, 6, 620, false},
        {3, 120, 140, 2, 220, false},
        {4, 96, 110, 1, 150, false},
        {5, 96, 110, 1, 150, false},
        {6, 88, 96, 1, 130, false},
        {7, 88, 96, 1, 130, false}
    }, QStringLiteral("ui.reports.parts.invoices.adaptive"));
    m_partsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    AdaptiveUi::installAdaptiveTable(m_outsourceTable, {
        {0, 110, 130, 1, 170, false},
        {1, 110, 130, 1, 170, false},
        {2, 110, 140, 1, 200, false},
        {3, 92, 110, 1, 150, false},
        {4, 220, 320, 6, 620, false},
        {5, 96, 110, 1, 150, false},
        {6, 96, 110, 1, 150, false}
    }, QStringLiteral("ui.reports.outsource.adaptive"));
    AdaptiveUi::installAdaptiveTable(m_payrollTable, {
        {0, 220, 320, 6, 520, false},
        {1, 92, 110, 1, 140, false},
        {2, 96, 110, 1, 150, false},
        {3, 72, 88, 1, 120, false},
        {4, 96, 110, 1, 150, false}
    }, QStringLiteral("ui.reports.payroll.adaptive"));
    m_totalExpensesTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_totalExpensesTable->horizontalHeader()->setMinimumSectionSize(110);
    m_totalExpensesTable->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_totalExpensesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    ReportsInteractionSupport::configurePayrollTableContextMenu(
        this,
        m_payrollTable,
        [this]() { refreshPayroll(); },
        [this]() { queueRefreshAll(0); },
        [this]() {
            const QDate fromDate = m_from ? m_from->date() : QDate::currentDate();
            const QDate effective = fromDate.isValid() ? fromDate.addDays(-1) : QDate::currentDate().addDays(-1);
            return effective.toString(QStringLiteral("yyyy-MM-dd"));
        });

    m_refreshDebounce = new QTimer(this);
    m_refreshDebounce->setSingleShot(true);
    connect(m_refreshDebounce, &QTimer::timeout, this, &ReportsPage::refreshActiveTab);

    m_refreshAllDebounce = new QTimer(this);
    m_refreshAllDebounce->setSingleShot(true);
    connect(m_refreshAllDebounce, &QTimer::timeout, this, [this]() { emit requestRefreshAll(); });

    connect(m_from, &QDateEdit::dateChanged, this, [this](const QDate&) {
        markDirty();
        scheduleRefresh(90);
    });
    connect(m_to, &QDateEdit::dateChanged, this, [this](const QDate&) {
        markDirty();
        scheduleRefresh(90);
    });
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index){
        applyCachedTabUi(static_cast<ReportTab>(index));
        scheduleRefresh(0);
    });
    connect(m_pdfButton, &QPushButton::clicked, this, &ReportsPage::exportActivePdf);
    for (QLineEdit* e : {m_elecPrevEdit, m_elecCurrEdit, m_elecTariffEdit, m_cleaningEdit, m_softwareEdit, m_heatingEdit, m_amortizationEdit, m_rentEdit, m_otherEdit}) {
        connect(e, &QLineEdit::textChanged, this, &ReportsPage::recalcExpensesForm);
    }
    connect(m_expensesSaveButton, &QPushButton::clicked, this, &ReportsPage::saveExpenses);
    connect(m_electricityPdfButton, &QPushButton::clicked, this, &ReportsPage::exportElectricityPdf);

    connect(m_periodTable, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
        const int row = index.row();
        ReportsInteractionSupport::openOrderFromReportTable(this, m_periodTable, 1, row, [this]() {
            markDirty();
            scheduleRefresh(0);
            queueRefreshAll(0);
        });
    });
    connect(m_ordersTable, &QTableView::clicked, this, [this](const QModelIndex& index) {
        if(!index.isValid() || (index.column()!=3 && index.column()!=6))return;
        const auto keys=m_ordersModel->cellUserValue(index.row(),3).toStringList();
        if(index.column()==6) {m_ordersTable->setCurrentIndex(index);payReportOrder();return;}
        if(keys.isEmpty())return;
        const auto names=m_ordersModel->cellText(index.row(),3).split('\n');
        QString key=keys.first();
        if(keys.size()>1) {
            QStringList labels;
            for (int i=0; i<keys.size(); ++i)
                labels.append(names.value(i).isEmpty() ? keys.at(i) : names.at(i));
            const int choice = ReportsDialogSupport::chooseLinkedRecord(m_ordersTable, index,
                QStringLiteral("Накладні наряду"), labels, true);
            if (choice < 0) return;
            key = keys.at(choice);
        }
        m_tabs->setCurrentIndex(static_cast<int>(ReportTab::Invoices));m_invoicesPage->selectInvoice(key);
    });
    connect(m_ordersTable, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
        if (!index.isValid()) return;
        if(index.column()==3 || index.column()==6)return;
        m_ordersTable->setCurrentIndex(index);
        previewOrderReport();
    });
    connect(m_partsTable, &QTableView::clicked, this, [this](const QModelIndex& index) {
        ReportsInteractionSupport::openInvoiceFromReport(this, index);
    });
    m_partsTable->setMouseTracking(true);
    connect(m_partsTable, &QTableView::entered, this, [this](const QModelIndex& index) {
        m_partsTable->viewport()->setCursor(index.column() == 3 && !index.data().toString().trimmed().isEmpty()
            ? Qt::PointingHandCursor : Qt::ArrowCursor);
    });
    connect(m_partsTable, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
        if (index.column() == 3) return;
        const int row = index.row();
        ReportsInteractionSupport::openOrderFromReportTable(this, m_partsTable, 1, row, [this]() {
            markDirty();
            scheduleRefresh(0);
            queueRefreshAll(0);
        });
    });
    connect(m_outsourceTable, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
        const int row = index.row();
        ReportsInteractionSupport::openOutsourceOrOrderFromReportTable(this, m_outsourceTable, row, 3, [this]() {
            markDirty();
            scheduleRefresh(0);
            queueRefreshAll(0);
        });
    });
    connect(m_payrollTable, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
        openPayrollOrdersDialog(index.row());
    });
}
