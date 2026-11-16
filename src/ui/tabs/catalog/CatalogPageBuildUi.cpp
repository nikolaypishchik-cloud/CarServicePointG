// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

#include "ui/tabs/catalog/CatalogPageModulePrelude.h"
#include "ui/common/dialogs/HistoricalVersionsDialog.h"
#include <QSplitter>
#include <QScrollArea>
#include <QClipboard>
#include <QApplication>

void CatalogPage::buildUi()
{
    setObjectName(QStringLiteral("catalogPage"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(12);

    auto* titleRow = new QHBoxLayout();
    titleRow->setSpacing(10);

    const bool lightUi = ThemeManager::currentTheme() == QStringLiteral("light");
    auto* title = new QLabel(QString::fromUtf8("Каталог запчастин"), this);
    title->setStyleSheet(QString::fromUtf8("font-size:28px; font-weight:700; color:%1;")
                             .arg(lightUi ? QStringLiteral("#0b0d10") : QStringLiteral("#f4efe7")));
    titleRow->addWidget(title);

    auto applyTopButtonSize = [](QPushButton* btn) {
        if (!btn) return;
        btn->setMinimumSize(AdaptiveUi::px(btn, 106), AdaptiveUi::px(btn, 34));
        btn->setMaximumHeight(AdaptiveUi::px(btn, 40));
        btn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        QFont f = btn->font();
        if (f.pointSizeF() > 0.0) f.setPointSizeF(std::max(10.0, f.pointSizeF() - 1.5));
        else f.setPointSize(10);
        btn->setFont(f);
    };

    m_filtersToggleBtn = new GlossyButton(QString::fromUtf8("Фільтри"), this);
    applyCatalogMetalButtonStyle(m_filtersToggleBtn);
    applyTopButtonSize(m_filtersToggleBtn);
    titleRow->addWidget(m_filtersToggleBtn);
    titleRow->addStretch(1);

    m_addBtn = new GlossyButton(QString::fromUtf8("Новий"), this);
    m_saveBtn = new GlossyButton(QString::fromUtf8("Редагувати"), this);
    m_deleteBtn = new GlossyButton(QString::fromUtf8("Видалити"), this);
    m_importBtn = new GlossyButton(QString::fromUtf8("Із замовлень"), this);
    for (auto* btn : {m_addBtn, m_saveBtn, m_deleteBtn, m_importBtn}) {
        applyCatalogMetalButtonStyle(btn);
        applyTopButtonSize(btn);
    }
    titleRow->addWidget(m_addBtn);
    titleRow->addWidget(m_saveBtn);
    titleRow->addWidget(m_deleteBtn);
    titleRow->addWidget(m_importBtn);
    auto* versions=new GlossyButton(QString::fromUtf8("Історичні версії"),this);versions->setObjectName("catalogHistoricalVersions");
    applyCatalogMetalButtonStyle(versions);applyTopButtonSize(versions);
    versions->setVisible(!AccessControlService::currentActorIsMaster() && AccessControlService::currentUserHasPermission("nav.catalog"));
    connect(versions,&QPushButton::clicked,this,[this](){HistoricalVersionsDialog::open(this,QString());});titleRow->addWidget(versions);

    root->addLayout(titleRow);

    m_filtersPanel = new QWidget(this);
    m_filtersPanel->setObjectName(QStringLiteral("catalogFiltersPanel"));
    auto* filtersRoot = new QVBoxLayout(m_filtersPanel);
    filtersRoot->setContentsMargins(18, 16, 18, 16);
    filtersRoot->setSpacing(12);

    m_brandFilter = makeCombo(m_filtersPanel, false);
    m_modelFilter = makeCombo(m_filtersPanel, false);
    m_fuelFilter = makeCombo(m_filtersPanel, false);
    m_groupFilter = makeCombo(m_filtersPanel, false);
    m_yearFromFilter = makeYearCombo(m_filtersPanel, QString::fromUtf8("Будь-який"));
    m_yearToFilter = makeYearCombo(m_filtersPanel, QString::fromUtf8("Будь-який"));
    m_searchEdit = makeEdit(m_filtersPanel, QString::fromUtf8("Пошук по OEM, назві, бренду, постачальнику"));

    m_manufacturer = makeCombo(m_filtersPanel, false);
    m_manufacturer->setObjectName("catalogManufacturer");
    filtersRoot->addWidget(new QLabel(tr("Уточнити пошук"), m_filtersPanel));
    auto* filterForm = new QFormLayout;
    filterForm->setRowWrapPolicy(QFormLayout::WrapAllRows);
    filterForm->addRow(tr("Марка"), m_brandFilter);
    filterForm->addRow(tr("Модель"), m_modelFilter);
    filterForm->addRow(tr("Рік від"), m_yearFromFilter);
    filterForm->addRow(tr("Рік до"), m_yearToFilter);
    filterForm->addRow(tr("Паливо"), m_fuelFilter);
    filterForm->addRow(tr("Вузол"), m_groupFilter);
    filterForm->addRow(tr("Бренд"), m_manufacturer);
    filtersRoot->addLayout(filterForm);
    filtersRoot->addStretch();
    auto* reset = new QPushButton(tr("Скинути фільтри"), m_filtersPanel);
    filtersRoot->addWidget(reset);
    connect(reset, &QPushButton::clicked, this, [this] {
        for (auto* combo : {m_brandFilter, m_modelFilter, m_yearFromFilter, m_yearToFilter,
                            m_fuelFilter, m_groupFilter, m_manufacturer, m_searchMode}) {
            QSignalBlocker blocker(combo);
            combo->setCurrentIndex(0);
        }
        m_searchEdit->clear();
        onFilterChanged();
    });
    m_searchMode = new QComboBox(this);
    m_searchMode->setObjectName("catalogSearchMode");
    m_searchMode->addItems({tr("Усе"), tr("OEM"), tr("Назва")});
    m_searchEdit->setObjectName("catalogSearch");
    auto* searchRow = new QHBoxLayout;
    searchRow->addWidget(m_searchMode);
    m_searchEdit->setClearButtonEnabled(true);
    m_searchDebounce = new QTimer(this);
    m_searchDebounce->setSingleShot(true);
    m_searchDebounce->setInterval(250);
    m_refreshDebounce = new QTimer(this);
    m_refreshDebounce->setSingleShot(true);
    m_refreshDebounce->setInterval(140);
    searchRow->addWidget(m_searchEdit, 1);
    root->addLayout(searchRow);

    m_table = new QTableWidget(this);
    m_table->setObjectName(QStringLiteral("catalogTable"));
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({tr("Запчастина"), tr("Автомобіль"), tr("Вузол")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_table->setMinimumHeight(180);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionsMovable(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setMinimumSectionSize(48);
    m_table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_table->verticalHeader()->setDefaultSectionSize(m_table->fontMetrics().height() * 2 + 16);
    AdaptiveUi::installAdaptiveTable(m_table, {
        {0, 155, 275, 5, 580, false},
        {1, 145, 235, 4, 480, false},
        {2, 95, 130, 2, 350, false}
    }, QStringLiteral("ui.catalog.cards.adaptive"));
    restoreHeaderState();
    connect(m_table->horizontalHeader(), &QHeaderView::sectionResized, this, [this](int, int, int) {
        if (!m_restoringHeaderState) saveHeaderState();
    });
    connect(m_table->horizontalHeader(), &QHeaderView::sectionMoved, this, [this](int, int, int) {
        if (!m_restoringHeaderState) saveHeaderState();
    });
    auto* panes = new QSplitter(Qt::Horizontal, this);
    auto* filterScroll = new QScrollArea(panes);
    filterScroll->setFrameShape(QFrame::NoFrame);
    filterScroll->setWidgetResizable(true);
    filterScroll->setWidget(m_filtersPanel);
    panes->addWidget(filterScroll);
    panes->addWidget(m_table);
    auto* detailScroll = new QScrollArea(panes);
    detailScroll->setWidgetResizable(true);
    detailScroll->setFrameShape(QFrame::NoFrame);
    auto* detail = new QWidget;
    detail->setObjectName("catalogDetailPanel");
    detail->setAttribute(Qt::WA_StyledBackground);
    detail->setStyleSheet("#catalogDetailPanel { background:#f4f7fb; border:1px solid #8d9bab; border-radius:8px; } "
                          "#catalogDetailPanel QLabel { background:transparent; color:#172536; border:0; }");
    detailScroll->setWidget(detail);
    auto* cardLayout = new QVBoxLayout(detail);
    cardLayout->addWidget(new QLabel(tr("Вибрана запчастина"), detail));
    m_selectionCard = new QLabel(detail);
    m_selectionCard->setObjectName("catalogSelectionCard");
    m_selectionCard->setTextFormat(Qt::PlainText);
    m_selectionCard->setWordWrap(true);
    m_selectionCard->setTextInteractionFlags(Qt::TextSelectableByMouse);
    cardLayout->addWidget(m_selectionCard);
    m_copyOem = new QPushButton(tr("Копіювати OEM"), detail);
    m_copyOem->setEnabled(false);
    cardLayout->addWidget(m_copyOem);
    connect(m_copyOem, &QPushButton::clicked, this, [this] {
        const int row = m_table->currentRow();
        if (currentRowId() > 0 && row < m_rows.size())
            QApplication::clipboard()->setText(m_rows.at(row).oem);
    });
    cardLayout->addStretch();
    panes->addWidget(detailScroll);
    panes->setStretchFactor(1, 1);
    panes->setSizes({200, 740, 240});
    root->addWidget(panes, 1);
    connect(m_searchMode, &QComboBox::currentIndexChanged, this, &CatalogPage::onFilterChanged);
    connect(m_manufacturer, &QComboBox::currentIndexChanged, this, &CatalogPage::onFilterChanged);

    setStyleSheet(QString::fromUtf8(
        "#catalogPage {"
        " background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        " stop:0 rgba(108,122,140,0.98),"
        " stop:0.16 rgba(132,145,162,0.98),"
        " stop:0.34 rgba(116,130,148,0.97),"
        " stop:0.55 rgba(86,99,116,0.98),"
        " stop:1 rgba(60,72,88,0.99));"
        " }"
        "#catalogFiltersPanel {"
        " color:#f4f1ea; font-weight:700; background:rgba(33,42,53,0.14);"
        " border:1px solid rgba(255,255,255,0.12); border-radius:16px; margin-top:12px;"
        " }"
        "#catalogFiltersPanel QLabel { color:#f4f1ea; background:transparent; border:none; }"
        "#catalogTable {"
        " background:rgba(255,255,255,0.96); alternate-background-color:rgba(236,242,248,0.96);"
        " color:#121822; border:1px solid rgba(255,255,255,0.28); border-radius:14px;"
        " gridline-color:rgba(18,24,34,0.12);"
        " }"
        "#catalogTable::item { padding:6px 8px; }"
        "#catalogTable::item:selected { background:rgba(186,205,232,0.92); color:#10161d; }"
        "#catalogTable QHeaderView::section {"
        " background:rgba(255,255,255,0.98); color:#11161e; padding:8px 10px;"
        " border:none; border-right:1px solid rgba(18,24,34,0.10); border-bottom:1px solid rgba(18,24,34,0.14); font-weight:700;"
        " }"
    ));

    auto connectFilter = [this](QObject* obj, const char* signal) {
        connect(obj, signal, this, SLOT(onFilterChanged()));
    };
    connectFilter(m_brandFilter, SIGNAL(currentIndexChanged(int)));
    connectFilter(m_modelFilter, SIGNAL(currentIndexChanged(int)));
    connectFilter(m_fuelFilter, SIGNAL(currentIndexChanged(int)));
    connectFilter(m_groupFilter, SIGNAL(currentIndexChanged(int)));
    connectFilter(m_yearFromFilter, SIGNAL(currentIndexChanged(int)));
    connectFilter(m_yearToFilter, SIGNAL(currentIndexChanged(int)));
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (!m_searchDebounce) {
            onSearchFilterChanged();
            return;
        }
        if (text.trimmed().isEmpty()) {
            m_searchDebounce->stop();
            onSearchFilterChanged();
        } else {
            m_searchDebounce->start();
        }
    });
    connect(m_searchEdit, &QLineEdit::returnPressed, this, [this]() {
        if (m_searchDebounce) m_searchDebounce->stop();
        onSearchFilterChanged();
    });
    connect(m_searchDebounce, &QTimer::timeout, this, &CatalogPage::onSearchFilterChanged);
    connect(m_refreshDebounce, &QTimer::timeout, this, &CatalogPage::processDeferredRefresh);

    connect(m_table, &QTableWidget::itemSelectionChanged, this, &CatalogPage::onTableSelectionChanged);
    connect(m_table, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem*) { onSaveClicked(); });
    connect(m_addBtn, &QPushButton::clicked, this, &CatalogPage::onAddClicked);
    connect(m_saveBtn, &QPushButton::clicked, this, &CatalogPage::onSaveClicked);
    connect(m_deleteBtn, &QPushButton::clicked, this, &CatalogPage::onDeleteClicked);
    connect(m_importBtn, &QPushButton::clicked, this, &CatalogPage::onImportFromOrdersClicked);
    connect(m_filtersToggleBtn, &QPushButton::clicked, this, &CatalogPage::onToggleFiltersClicked);

    m_saveBtn->setEnabled(false);
    m_deleteBtn->setEnabled(false);
}
