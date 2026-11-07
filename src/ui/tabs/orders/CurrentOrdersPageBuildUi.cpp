// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

#include "ui/tabs/orders/CurrentOrdersPageModulePrelude.h"
#include "ui/common/TabularSnapshotTableModel.h"
#include "core/services/AccessControlService.h"
#include "core/services/orders/OrderMutationFacade.h"
#include "ui/tabs/orders/BayOccupancyPage.h"
#include <QInputDialog>
#include "ui/theme/ThemeManager.h"

namespace {
QPixmap toolbarSymbol(int kind, qreal ratio)
{
    QPixmap image(QSize(40,40)*ratio);image.setDevicePixelRatio(ratio);image.fill(Qt::transparent);
    QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor("#78c9df"),3,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
    if(kind==0){p.drawEllipse(QRectF(6,5,21,21));p.drawLine(25,25,35,35);}
    if(kind==1){p.drawLine(20,7,20,33);p.drawLine(7,20,33,20);}
    if(kind==2){p.setBrush(QColor("#78c9df"));p.drawRoundedRect(QRectF(6,6,28,28),2,2);p.setPen(QPen(QColor("#344652"),3));p.drawLine(12,20,18,26);p.drawLine(18,26,29,14);}
    if(kind==3){p.drawRect(QRectF(10,4,23,32));p.drawLine(24,4,24,14);p.drawLine(24,14,33,14);for(int y:{20,26,31})p.drawLine(15,y,28,y);}
    if(kind==4){p.drawLine(3,35,37,35);p.drawLine(7,35,7,7);p.drawLine(33,35,33,7);p.drawRoundedRect(QRectF(10,16,20,12),2,2);p.drawLine(12,16,16,9);p.drawLine(16,9,25,9);p.drawLine(25,9,28,16);p.drawPoint(14,22);p.drawPoint(26,22);}
    if(kind==5){p.drawLine(20,4,20,25);p.drawLine(12,18,20,26);p.drawLine(20,26,28,18);p.drawLine(6,26,6,35);p.drawLine(6,35,34,35);p.drawLine(34,35,34,26);}
    return image;
}
}

void CurrentOrdersPage::buildUi()
{
    setObjectName(QStringLiteral("currentOrdersPage"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral("#currentOrdersPage { background: transparent; }"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0,0,0,0);
    root->setSpacing(10);

    // Toolbar
    auto* bar = new QHBoxLayout();
    bar->setContentsMargins(0,0,0,0);

    auto* btnNew = new QToolButton(this);
    btnNew->setText(QString::fromUtf8("Новий"));
    auto* btnClose = new QToolButton(this);
    btnClose->setText(QString::fromUtf8("Закрити"));
    auto* btnPdf = new QToolButton(this);
    m_btnPdf = btnPdf;
    btnPdf->setVisible(!AccessControlService::currentActorIsMaster());
    btnPdf->setText(QString::fromUtf8("PDF"));
    for (auto* btn : {btnNew, btnClose, btnPdf}) {
        btn->setMinimumHeight(AdaptiveUi::px(btn, 34));
        btn->setMinimumWidth(AdaptiveUi::px(btn, 108));
    }
    m_btnImportInvoice = new QToolButton(this);
    m_btnImportInvoice->setVisible(!AccessControlService::currentActorIsMaster());
    m_btnImportInvoice->setText(QString::fromUtf8("Імпорт накладної"));
    m_btnImportInvoice->setMinimumHeight(AdaptiveUi::px(m_btnImportInvoice, 34));
    m_btnImportInvoice->setMinimumWidth(AdaptiveUi::px(m_btnImportInvoice, 156));

    m_search = new QLineEdit(this);
    m_searchDebounce = new QTimer(this);
    m_searchDebounce->setSingleShot(true);
    m_searchDebounce->setInterval(250);
    m_refreshAllDebounce = new QTimer(this);
    m_refreshAllDebounce->setSingleShot(true);
    connect(m_refreshAllDebounce, &QTimer::timeout, this, [this]() { emit requestRefreshAll(); });
    m_search->setPlaceholderText(QString::fromUtf8("№ наряду, авто, марка…"));
    m_search->setToolTip(QString::fromUtf8("Пошук за номером наряду, держномером, маркою авто або VIN"));

    AdaptiveUi::relaxLineEditWidth(m_search, 240, 560);
    m_search->setClearButtonEnabled(true);

    bar->setSpacing(6);
    const auto addTile=[&](QWidget* field,int symbol,int stretch) {
        auto* card=new QWidget(this);card->setObjectName(QString("currentOrderActionTile%1").arg(symbol));
        card->setAttribute(Qt::WA_StyledBackground);
        card->setStyleSheet(QString("#%1 {border:1.5px solid #101820;border-radius:5px;background:rgba(68,83,98,110);}").arg(card->objectName()));
        auto* row=new QHBoxLayout(card);row->setContentsMargins(9,5,9,5);row->setSpacing(10);
        auto* icon=new QLabel(card);icon->setFixedSize(36,36);
        icon->setPixmap(toolbarSymbol(symbol,devicePixelRatioF()).scaled(QSize(36,36)*devicePixelRatioF(),Qt::KeepAspectRatio,Qt::SmoothTransformation));
        row->addWidget(icon);row->addWidget(field,1);
        card->setMinimumHeight(52);
        field->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
        field->setMinimumWidth(symbol==0?120:field->fontMetrics().horizontalAdvance(qobject_cast<QToolButton*>(field)->text())+26);
        field->setMaximumWidth(QWIDGETSIZE_MAX);field->setFixedHeight(38);
        if(symbol!=0)field->setStyleSheet("QToolButton {color:white;font-family:'Times New Roman';font-weight:bold;border:1px solid #74808b;border-radius:14px;padding:3px 10px;background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #66717e,stop:0.16 #414b58,stop:1 #1d242c);} QToolButton:hover {border-color:#9ddfee;} QToolButton:pressed {background:#253b49;} QToolButton:disabled {color:#9b9fa4;}");
        bar->addWidget(card,stretch);
        if((field==btnPdf || field==m_btnImportInvoice) && AccessControlService::currentActorIsMaster())card->hide();
    };
    addTile(m_search,0,23);addTile(btnNew,1,13);addTile(btnClose,2,13);addTile(btnPdf,3,12);
    auto* postButton=new QToolButton(this);
    postButton->setObjectName("changeWorkshopPost");
    postButton->setText(QString::fromUtf8("Змінити номер посту"));
    postButton->setMinimumHeight(AdaptiveUi::px(postButton,34));
    addTile(postButton,4,21);
    connect(postButton,&QToolButton::clicked,this,[this]{
        const int number=selectedOrderNumber();
        if(number<=0 || selectedOrderNumbers().size()!=1) {
            QMessageBox::information(this,QString::fromUtf8("Пост"),QString::fromUtf8("Виберіть один наряд."));return;
        }
        BayOccupancyPage::open(this,selectedOrderUid());
        markDirty();refresh();queueRefreshAll(0);
    });
    addTile(m_btnImportInvoice,5,18);

    root->addLayout(bar);
    auto* bayTimer = new QTimer(this);
    auto refreshBays = [this] {
        if(isVisible()) BaySnapshot::refresh(this,[this]{markDirty();refresh();});
    };
    connect(bayTimer,&QTimer::timeout,this,refreshBays);
    bayTimer->start(30000);
    QTimer::singleShot(0,this,refreshBays);

    // Table
    m_table = new QTableView(this);
    m_table->setObjectName("premiumCurrentOrdersTable");
    m_table->setStyleSheet(QStringLiteral(
        "QTableView#premiumCurrentOrdersTable::item { background: transparent; color: #111827; }"
        "QTableView#premiumCurrentOrdersTable::item:selected { background: transparent; color: #111827; }"));
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(false);
    m_table->setSortingEnabled(false);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    if (m_table->viewport()) m_table->viewport()->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->setMouseTracking(true);
    m_table->setShowGrid(true);
    m_table->setWordWrap(false);
    m_table->setCornerButtonEnabled(false);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(AdaptiveUi::px(m_table, 34));
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->horizontalHeader()->setSortIndicatorShown(false);

    m_model = new TabularSnapshotTableModel(this);
    m_proxy = CurrentOrdersSupport::createDisplayProxy(this);
    m_proxy->setSourceModel(m_model);
    m_table->setItemDelegate(CurrentOrdersSupport::createWordStyleDelegate(m_table));
    m_proxy->setSortCaseSensitivity(Qt::CaseInsensitive);
    m_proxy->setDynamicSortFilter(false);
    m_table->setModel(m_proxy);
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this, &CurrentOrdersPage::publishSettlement);
    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &CurrentOrdersPage::publishSettlement);
    connect(m_proxy, &QAbstractItemModel::modelReset, this, &CurrentOrdersPage::publishSettlement);

    auto* hh = m_table->horizontalHeader();
    hh->setStretchLastSection(false);
    hh->setDefaultAlignment(Qt::AlignCenter);
    hh->setMinimumSectionSize(54);
    hh->setSectionsMovable(true);
    hh->setSectionResizeMode(QHeaderView::Interactive);

    m_table->setColumnWidth(0, 66);   // №
    m_table->setColumnWidth(1, 178);  // Відкрито
    m_table->setColumnWidth(2, 108);  // Статус
    m_table->setColumnWidth(3, 130);  // Номер
    m_table->setColumnWidth(4, 210);  // Авто
    m_table->setColumnWidth(5, 170);  // Клієнт
    m_table->setColumnWidth(6, 130);  // Майстер
    m_table->setColumnWidth(7, 110);  // Роботи
    m_table->setColumnWidth(8, 130);  // Запчастини

    restoreHeaderState();
    connect(hh, &QHeaderView::sectionResized, this, [this](int, int, int) { saveHeaderState(); });
    connect(hh, &QHeaderView::sectionMoved, this, [this](int, int, int) { saveHeaderState(); });
    connect(hh, &QHeaderView::sortIndicatorChanged, this, [this](int, Qt::SortOrder) { saveHeaderState(); });


    root->addWidget(m_table, 1);

    connect(btnNew, &QToolButton::clicked, this, [this]() {
        BackupPerformanceGuard::hold(AppContext::instance().backupManager(), 2500);
        OrderDialog dlg(this);
        dlg.setWindowTitle(QString::fromUtf8("Новий наряд"));
        if (dlg.exec() == QDialog::Accepted) {
            queueRefreshAll(0);
        }
    });

    connect(m_table, &QTableView::doubleClicked, this, [this](const QModelIndex&) {
        openSelectedOrder();
    });

    connect(btnClose, &QToolButton::clicked, this, [this]() {
        if (setSelectedOrdersClosed(true)) queueRefreshAll(0);
    });

    connect(btnPdf, &QToolButton::clicked, this, &CurrentOrdersPage::exportSelectedOrderPdf);
    connect(m_btnImportInvoice, &QToolButton::clicked, this, &CurrentOrdersPage::importInvoiceFromExcel);

    const auto showOrdersContextMenu = [this](const QPoint& pos) {
        if (!m_table || !m_proxy || !m_model) return;
        const QModelIndex idx = m_table->indexAt(pos);
        if (idx.isValid()) {
            if (auto* sel = m_table->selectionModel()) {
                const bool alreadySelected = sel->isRowSelected(idx.row(), idx.parent());
                const auto flags = alreadySelected
                    ? (QItemSelectionModel::Current | QItemSelectionModel::Rows | QItemSelectionModel::NoUpdate)
                    : (QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
                sel->setCurrentIndex(idx, flags);
            } else {
                m_table->setCurrentIndex(idx);
            }
        }

        const QList<int> nums = selectedOrderNumbers();
        const int selectedCount = nums.size();
        const int num = selectedOrderNumber();
        if (num <= 0 && selectedCount <= 0) return;
        const QString status = selectedOrderStatus().trimmed().toLower();
        const bool isClosed = (status == QStringLiteral("closed") || status == QString::fromUtf8("закритий"));

        QMenu menu(this);
        QAction* actOpen = menu.addAction(QString::fromUtf8("Відкрити наряд"));
        QAction* actOpenNew = menu.addAction(QString::fromUtf8("Відкрити новий наряд"));
        QAction* actPdf = menu.addAction(QString::fromUtf8("PDF"));
        actPdf->setVisible(!AccessControlService::currentActorIsMaster());
        QAction* actToggle = menu.addAction(isClosed ? QString::fromUtf8("Повторно відкрити") : QString::fromUtf8("Закрити наряд"));
        QAction* actDelete = menu.addAction(QString::fromUtf8("Видалити наряд"));
        if (selectedCount > 1) {
            actOpen->setEnabled(false);
            actOpenNew->setEnabled(false);
            actPdf->setEnabled(false);
            actToggle->setText(isClosed ? QString::fromUtf8("Повторно відкрити вибрані") : QString::fromUtf8("Закрити вибрані наряди"));
            actDelete->setText(QString::fromUtf8("Видалити вибрані наряди"));
        }
        CurrentOrdersSupport::prepareActionMenu(menu);

        QAction* chosen = menu.exec(m_table->viewport()->mapToGlobal(pos));
        if (!chosen) return;
        if (chosen == actOpen) {
            openSelectedOrder();
            return;
        }
        if (chosen == actOpenNew) {
            openSelectedVehicleAsNewOrder();
            return;
        }
        if (chosen == actPdf) {
            exportSelectedOrderPdf();
            return;
        }
        if (chosen == actToggle) {
            if (setSelectedOrdersClosed(!isClosed)) queueRefreshAll(0);
            return;
        }
        if (chosen == actDelete) {
            if (deleteSelectedOrders()) queueRefreshAll(0);
            return;
        }
    };
    connect(m_table, &QTableView::customContextMenuRequested, this, showOrdersContextMenu);
    if (m_table->viewport()) {
        connect(m_table->viewport(), &QWidget::customContextMenuRequested, this, showOrdersContextMenu);
    }

    auto* deleteSelectedOrdersAction = new QAction(this);
    deleteSelectedOrdersAction->setShortcut(QKeySequence::Delete);
    deleteSelectedOrdersAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_table->addAction(deleteSelectedOrdersAction);
    connect(deleteSelectedOrdersAction, &QAction::triggered, this, [this]() {
        if (deleteSelectedOrders()) queueRefreshAll(0);
    });

    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& text) {
        markDirty();
        if (!m_searchDebounce) {
            refresh();
            return;
        }
        if (text.trimmed().isEmpty()) {
            m_searchDebounce->stop();
            refresh();
        } else {
            m_searchDebounce->start();
        }
    });
    connect(m_search, &QLineEdit::returnPressed, this, [this]() {
        if (m_searchDebounce) m_searchDebounce->stop();
        markDirty();
        refresh();
    });
    connect(m_searchDebounce, &QTimer::timeout, this, [this]() { refresh(); });

    // MainWindow performs the initial refresh for the visible page.
    // Skipping a constructor refresh avoids duplicate first-load work.
}
