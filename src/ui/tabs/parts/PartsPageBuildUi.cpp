// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

#include "ui/tabs/parts/PartsPageModulePrelude.h"
#include <QGridLayout>
#include "ui/tabs/parts/PartsSupplierDashboard.h"

void PartsPage::buildUi()
{
    setObjectName(QStringLiteral("partsArchivePage"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral("#partsArchivePage { background: transparent; }"));

    auto* root = new QVBoxLayout(this);
    const qreal layoutScale = AdaptiveUi::dpiScale(this);
    const int horizontalMargin = qMax(2, qRound(4.0 * layoutScale));
    const int verticalMargin = qRound(logicalDpiY() / 25.4);
    root->setContentsMargins(horizontalMargin, verticalMargin, horizontalMargin, verticalMargin);
    root->setSpacing(0);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(QString::fromUtf8("Пошук: назва запчастини, ОЕМ, бренд, № наряду, номер авто, марка авто, VIN…"));
    m_search->setMinimumHeight(34);
    m_search->setMinimumWidth(0);
    m_search->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_search->setClearButtonEnabled(true);

    m_dateFrom = new QDateEdit(QDate::currentDate().addMonths(-1), this);
    m_dateTo = new QDateEdit(QDate::currentDate(), this);
    for (auto* edit : {m_dateFrom, m_dateTo}) {
        edit->setCalendarPopup(true);
        edit->setDisplayFormat(QStringLiteral("dd.MM.yy"));
        edit->setMinimumHeight(AdaptiveUi::px(edit, 34));
        edit->setMinimumWidth(AdaptiveUi::px(edit, 112));
        edit->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
        edit->setKeyboardTracking(false);
        edit->setDateRange(QDate(1900, 1, 1), QDate(9999, 12, 31));
    }

    restoreDateRangeState();

    m_findBtn = new QPushButton(QString::fromUtf8("🔍 Знайти"), this);
    QFont toolBtnFont = m_findBtn->font();
    toolBtnFont.setPointSizeF(qMax(8.0, toolBtnFont.pointSizeF() - 1.0));
    for (auto* btn : {m_findBtn}) {
        btn->setMinimumHeight(AdaptiveUi::px(btn, 34));
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFont(toolBtnFont);
    }
    m_findBtn->setIconSize(QSize(13, 13));

    m_findBtn->hide(); // Search is already debounced and also responds to Enter.
    m_supplierDashboard=new PartsSupplierDashboard(m_dateFrom,m_dateTo,m_search,this);
    m_debtsBanner=m_supplierDashboard->error;
    m_debtsBannerHost=m_supplierDashboard;
    root->addWidget(m_supplierDashboard);
    root->addSpacing(qMax(2, qRound(4.0 * layoutScale)));

    m_table = new QTableView(this);
    m_table->setObjectName(QStringLiteral("partsArchiveTable"));
    m_table->setStyleSheet(QStringLiteral(
        "QTableView#partsArchiveTable {"
        " selection-background-color: #D7E6FA;"
        " selection-color: #111111;"
        " outline: 0;"
        "}"
        "QTableView#partsArchiveTable::item {"
        " background: transparent;"
        " color: #111827;"
        " border-left: 0px;"
        " border-right: 1px solid #e2e7ec;"
        " border-bottom: 1px solid #e8edf2;"
        "}"
        "QTableView#partsArchiveTable::item:selected,"
        "QTableView#partsArchiveTable::item:selected:active,"
        "QTableView#partsArchiveTable::item:selected:!active {"
        " background: #D7E6FA;"
        " color: #111111;"
        " border-left: 0px;"
        " border-right: 1px solid #D7E6FA;"
        " border-bottom: 1px solid #D7E6FA;"
        "}"
        "QTableView#partsArchiveTable::item:focus {"
        " outline: 0;"
        "}"));
    m_table->setHorizontalHeader(new PartsStatusHeaderView(Qt::Horizontal, m_table));
    m_partsModel = new PartsPageTableModelAdapter::PartsArchiveTableModel(m_table);
    m_table->setModel(m_partsModel);
    m_table->setItemDelegate(new PartsArchiveRowFillDelegate(m_table));
    m_table->setItemDelegateForColumn(ColDeliveryDate, new PartsArchiveFitTextDelegate(m_table));
    m_table->setItemDelegateForColumn(ColStore, new PartsArchiveFitTextDelegate(m_table));
    m_table->setItemDelegateForColumn(ColBrand, new PartsArchiveFitTextDelegate(m_table));
    m_table->setItemDelegateForColumn(ColArticle, new PartsArchiveFitTextDelegate(m_table));
    m_table->setItemDelegateForColumn(ColAuto, new PartsArchiveFitTextDelegate(m_table));
    m_table->setItemDelegateForColumn(ColPaid, new PartsPaidStatusDelegate(m_table));
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(false);
    m_table->verticalHeader()->setVisible(false);
    m_table->setShowGrid(true);
    m_table->setSortingEnabled(false);
    m_table->setWordWrap(true);
    m_table->setColumnHidden(ColId, true);
    auto* header = m_table->horizontalHeader();
    header->setSectionsMovable(true);
    header->setSectionsClickable(true);
    header->setStretchLastSection(false);
    header->setMinimumSectionSize(40);
    header->setMinimumHeight(AdaptiveUi::px(m_table, 52));
    header->setFixedHeight(AdaptiveUi::px(m_table, 52));
    header->setSectionResizeMode(ColDate, QHeaderView::Interactive);
    header->setSectionResizeMode(ColDeliveryDate, QHeaderView::Interactive);
    header->setSectionResizeMode(ColStore, QHeaderView::Interactive);
    header->setSectionResizeMode(ColArticle, QHeaderView::Interactive);
    header->setSectionResizeMode(ColBrand, QHeaderView::Interactive);
    header->setSectionResizeMode(ColName, QHeaderView::Interactive);
    header->setSectionResizeMode(ColQty, QHeaderView::Interactive);
    header->setSectionResizeMode(ColPrice, QHeaderView::Interactive);
    header->setSectionResizeMode(ColAmount, QHeaderView::Interactive);
    header->setSectionResizeMode(ColOrder, QHeaderView::Interactive);
    header->setSectionResizeMode(ColAuto, QHeaderView::Interactive);
    header->setSectionResizeMode(ColPaid, QHeaderView::Interactive);
    m_table->setColumnWidth(ColDate, 100);
    m_table->setColumnWidth(ColDeliveryDate, 134);
    m_table->setColumnWidth(ColStore, 118);
    m_table->setColumnWidth(ColArticle, 150);
    m_table->setColumnWidth(ColBrand, 130);
    m_table->setColumnWidth(ColName, 320);
    m_table->setColumnWidth(ColQty, 82);
    m_table->setColumnWidth(ColPrice, 96);
    m_table->setColumnWidth(ColAmount, 98);
    m_table->setColumnWidth(ColOrder, 84);
    m_table->setColumnWidth(ColAuto, 255);
    m_table->setColumnWidth(ColPaid, kPaidColumnDefaultWidth);
    // Для вкладки "Замовлення запчастин" ширини колонок мають жорстко
    // запам'ятовуватися після ручної правки користувачем без подальших
    // авто-перерахунків. Тому adaptive controller тут свідомо не
    // підключаємо: таблиця працює тільки з ручними width/header state.
    header->setDefaultAlignment(Qt::AlignCenter);
    restoreTableHeaderState();
    if (m_table->columnWidth(ColPaid) < kPaidColumnMinWidth) m_table->setColumnWidth(ColPaid, kPaidColumnMinWidth);
    syncPaidColumnWidgetsWidth(m_table);
    root->addWidget(m_table, 1);

    connect(m_findBtn, &QPushButton::clicked, this, [this]() {
        if (m_searchDebounce) m_searchDebounce->stop();
        markDirty();
        refresh();
    });
    connect(m_search, &QLineEdit::returnPressed, this, [this]() {
        if (m_searchDebounce) m_searchDebounce->stop();
        markDirty();
        refresh();
    });
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& text) {
        markDirty();
        if (text.trimmed().isEmpty()) {
            if (m_searchDebounce) m_searchDebounce->stop();
            refresh();
        } else if (m_searchDebounce) {
            m_searchDebounce->start();
        }
    });
    connect(m_searchDebounce, &QTimer::timeout, this, [this]() { refresh(); });
    connect(m_dateFrom, &QDateEdit::dateChanged, this, [this](const QDate& d) {
        m_lastAppliedFrom = d;
        saveDateRangeState();
        markDirty();
        refresh();
    });
    connect(m_dateTo, &QDateEdit::dateChanged, this, [this](const QDate& d) {
        m_lastAppliedTo = d;
        saveDateRangeState();
        markDirty();
        refresh();
    });
    connect(m_table, &QTableView::clicked, this, [this](const QModelIndex& index) {
        if (!m_table || !index.isValid()) return;
        m_table->setCurrentIndex(index);
        m_table->selectRow(index.row());
        if (index.column() != ColPaid) return;
        if (!m_paymentActionsEnabled) return;
        const int row = index.row();
        if (row < 0 || row >= m_rowsSnapshot.size()) return;
        const PartRowSnapshot& snapshot = m_rowsSnapshot.at(row);
        if (snapshot.id > 0) togglePaidForPart(snapshot.id, !snapshot.paid);
    });
    connect(m_table, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
        if (!m_table || !index.isValid() || index.column() == ColPaid) return;
        m_table->selectRow(index.row());
        openSelectedOrder();
    });
    connect(m_table->horizontalHeader(), &QHeaderView::sectionResized, this, [this](int logical, int, int now) {
        if (m_table && logical == ColPaid && now < kPaidColumnMinWidth) {
            const QSignalBlocker blocker(m_table->horizontalHeader());
            m_table->setColumnWidth(ColPaid, kPaidColumnMinWidth);
        }
        syncPaidColumnWidgetsWidth(m_table);
        saveTableHeaderState();
        scheduleRowHeightsUpdate(40);
    });
    connect(m_table->horizontalHeader(), &QHeaderView::sectionMoved, this, [this](int, int, int) { saveTableHeaderState(); scheduleRowHeightsUpdate(40); });
    if (auto* vbar = m_table->verticalScrollBar()) {
        connect(vbar, &QScrollBar::valueChanged, this, [this](int) { scheduleRowHeightsUpdate(25); });
    }

    m_lastAppliedFrom = m_dateFrom->date();
    m_lastAppliedTo = m_dateTo->date();
    m_dateRangeInitialized = true;
}
