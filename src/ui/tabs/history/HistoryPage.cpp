#include "core/util/DateFormats.h"
#include "core/util/OrderClosureMarker.h"
#include "ui/tabs/history/HistoryPage.h"
#include "ui/common/TabularSnapshotTableModel.h"
#include "core/services/settings/UiStateSettingsService.h"

#include "core/services/ActiveArchiveRoutingLayer.h"
#include "core/services/HistoryQueryService.h"
#include "core/services/orders/OrderBatchMutationService.h"
#include "core/services/HistoryRefreshJob.h"
#include "app/AppContext.h"
#include "core/backup/BackupPerformanceGuard.h"
#include "ui/common/dialogs/OrderDialog.h"
#include "core/services/AccessControlService.h"
#include "core/util/FormatUtils.h"
#include "core/util/PlateUtils.h"
#include "core/perf/PerfWatchdog.h"
#include "core/backup/BackupCommitPublisher.h"
#include "core/services/AppRuntimeService.h"
#include "ui/common/AdaptiveUi.h"
#include "ui/common/OrderTableUiMapper.h"
#include "ui/common/TabularReadModelMapper.h"
#include "core/readmodel/history/HistoryViewModelBuilder.h"
#include "runtime/AsyncTaskHandle.h"
#include "runtime/AsyncThreadScope.h"
#include "runtime/RuntimeJobDispatcher.h"

#include <QPointer>
#include <QDateTime>
#include <QSqlError>
#include <QHBoxLayout>
#include <QPainter>
#include <QVBoxLayout>
#include <QToolButton>
#include <QTableView>
#include <QPushButton>
#include <QHeaderView>
#include <QLineEdit>
#include <QLabel>
#include <QDateEdit>
#include <QMessageBox>
#include <QMenu>
#include <QMouseEvent>
#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QAction>
#include <QHash>
#include <QStringList>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QDate>
#include <algorithm>
#include <QElapsedTimer>
#include <QDebug>
#include <QScrollBar>
#include <QThread>
#include <QCloseEvent>
#include <QByteArray>
#include <memory>

namespace {
int modelColumnByHeader(const QAbstractItemModel* model, const QString& header)
{
    if (!model) return -1;
    for (int col = 0; col < model->columnCount(); ++col) {
        if (model->headerData(col, Qt::Horizontal).toString().trimmed() == header) return col;
    }
    return -1;
}

static QDateTime parseHistoryDateTime(const QString& raw)
{
    const QString t = raw.trimmed();
    if (t.isEmpty()) return {};
    for (const QString& fmt : {QString(), QStringLiteral("yyyy-MM-dd HH:mm:ss"), QStringLiteral("yyyy-MM-ddTHH:mm:ss"), QStringLiteral("yyyy-MM-dd HH:mm"), QStringLiteral("yyyy-MM-ddTHH:mm"), QStringLiteral("dd.MM.yy HH:mm"), QStringLiteral("dd.MM.yyyy HH:mm")}) {
        QDateTime dt = fmt.isEmpty() ? DateFormats::parseDateTime(t) : QDateTime::fromString(t, fmt);
        if (dt.isValid()) {
            if (dt.date().year() >= 1900 && dt.date().year() < 2000) dt = dt.addYears(100);
            return dt;
        }
    }
    return {};
}

static QString displayDateTimeStandard(const QVariant& v)
{
    const QString raw = v.toString().trimmed();
    if (raw.isEmpty()) return QString();
    const QDateTime dt = parseHistoryDateTime(raw);
    if (dt.isValid()) return dt.toString(QStringLiteral("dd.MM.yy/HH.mm"));

    QString t = raw;
    const int tpos = t.indexOf('T');
    if (tpos > 0) t = t.left(tpos);
    for (const QString& fmt : {QStringLiteral("yyyy-MM-dd"), QStringLiteral("dd.MM.yy"), QStringLiteral("dd.MM.yyyy")}) {
        QDate d = QDate::fromString(t, fmt);
        if (d.isValid()) {
            if (d.year() >= 1900 && d.year() < 2000) d = d.addYears(100);
            return d.toString(QStringLiteral("dd.MM.yy"));
        }
    }
    return raw;
}

static QString displayStatusUa(const QVariant& v)
{
    const QString raw = v.toString().trimmed();
    const QString normalized = raw.toLower();
    if (normalized == QStringLiteral("open")) return QString::fromUtf8("Відкритий");
    if (normalized == QStringLiteral("closed")) return QString::fromUtf8("Закритий");
    if (normalized == QStringLiteral("completed") || normalized == QStringLiteral("done")) return QString::fromUtf8("Виконаний");
    if (normalized == QStringLiteral("planned")) return QString::fromUtf8("Заплановано");
    if (normalized == QStringLiteral("paid")) return QString::fromUtf8("Сплачено");
    if (normalized == QStringLiteral("archived") || normalized == QStringLiteral("archive")) return QString::fromUtf8("Архівний");
    if (normalized == QStringLiteral("cancelled") || normalized == QStringLiteral("canceled")) return QString::fromUtf8("Скасований");
    if (normalized == QStringLiteral("in_progress")) return QString::fromUtf8("В роботі");
    return raw;
}

static QString historyHeaderSchema(const QAbstractItemModel* model)
{
    if (!model || model->columnCount() <= 0) return QString();
    QStringList headers;
    headers.reserve(model->columnCount());
    for (int col = 0; col < model->columnCount(); ++col) {
        headers << model->headerData(col, Qt::Horizontal).toString().trimmed();
    }
    return headers.join(QChar(0x1F));
}

static bool isHistoryInternalColumnName(const QString& header)
{
    return header.trimmed().startsWith(QStringLiteral("__"));
}

static int defaultHistoryColumnWidth(const QString& header)
{
    const QString h = header.trimmed();
    if (h == QString::fromUtf8("№")) return 68;
    if (h == QString::fromUtf8("Відкрито")) return 130;
    if (h == QString::fromUtf8("Закрито")) return 130;
    if (h == QString::fromUtf8("Статус")) return 230;
    if (h == QString::fromUtf8("Номер")) return 118;
    if (h == QString::fromUtf8("Авто")) return 260;
    if (h == QString::fromUtf8("Клієнт")) return 190;
    if (h == QString::fromUtf8("Майстер")) return 160;
    if (h == QString::fromUtf8("Роботи")) return 116;
    if (h == QString::fromUtf8("Запчастини")) return 128;
    return isHistoryInternalColumnName(h) ? 0 : 110;
}

static void configureHistoryHeaderForManualWidths(QTableView* table)
{
    if (!table || !table->model() || !table->horizontalHeader()) return;
    auto* header = table->horizontalHeader();
    header->setStretchLastSection(false);
    header->setSortIndicatorShown(false);
    header->setSectionsMovable(true);
    header->setSectionsClickable(true);
    header->setHighlightSections(false);
    header->setMinimumSectionSize(36);
    header->setDefaultSectionSize(116);
    for (int col = 0; col < table->model()->columnCount(); ++col) {
        header->setSectionResizeMode(col, QHeaderView::Interactive);
    }
}

static void enforceHistoryVisibleColumns(QTableView* table)
{
    if (!table || !table->model()) return;
    configureHistoryHeaderForManualWidths(table);
    for (int col = 0; col < table->model()->columnCount(); ++col) {
        const QString header = table->model()->headerData(col, Qt::Horizontal).toString().trimmed();
        const bool internal = isHistoryInternalColumnName(header);
        table->setColumnHidden(col, internal);
        if (internal) continue;

        const int defaultWidth = defaultHistoryColumnWidth(header);
        if (table->columnWidth(col) < 36) {
            table->setColumnWidth(col, defaultWidth);
        }
    }
    if (table->horizontalScrollBar() && table->horizontalScrollBar()->value() < 0) {
        table->horizontalScrollBar()->setValue(0);
    }
    table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
}

class HistoryDisplayProxy final : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid()) return QSortFilterProxyModel::data(index, role);
        if (role == Qt::ToolTipRole && index.column() == 3) return data(index, Qt::DisplayRole);
        if (role == Qt::DisplayRole) {
            const QVariant src = QSortFilterProxyModel::data(index, role);
            switch (index.column()) {
            case 1:
            case 2: return displayDateTimeStandard(src);
            case 3: {
                QString text = displayStatusUa(src);
                for (int col = 10; col < columnCount(); ++col) {
                    if (headerData(col, Qt::Horizontal).toString() != QStringLiteral("__mobile_state")) continue;
                    const QString marker = OrderClosureMarker::label(src.toString(),
                        QSortFilterProxyModel::data(index.siblingAtColumn(2), role).toString(),
                        QSortFilterProxyModel::data(index.siblingAtColumn(col), role).toString());
                    if (!marker.isEmpty()) text += QStringLiteral(" · ") + marker;
                    break;
                }
                return text + QStringLiteral("  ▾");
            }
            case 4: return PlateUtils::formatPlateDisplay(src.toString());
            case 8:
            case 9: return FormatUtils::money(src.toDouble(), 0);
            default: break;
            }
        }
        return QSortFilterProxyModel::data(index, role);
    }
};
}

HistoryPage::HistoryPage(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("historyPage"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral("#historyPage { background: transparent; }"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0,0,0,0);
    root->setSpacing(10);

    auto* bar = new QHBoxLayout();
    bar->setContentsMargins(0,0,0,0);

    auto* btnOpenNewOrder = new QToolButton(this);
    btnOpenNewOrder->setText(QString::fromUtf8("Відкрити наряд"));
    auto* btnPdf = new QToolButton(this);
    btnPdf->setText(QString::fromUtf8("PDF"));
    for (auto* btn : {btnOpenNewOrder, btnPdf}) {
        btn->setMinimumHeight(AdaptiveUi::px(btn, 34));
        btn->setMinimumWidth(AdaptiveUi::px(btn, btn == btnPdf ? 84 : 132));
    }

    m_dateFrom = new QDateEdit(QDate::currentDate().addMonths(-1), this);
    m_dateTo = new QDateEdit(QDate::currentDate(), this);
    m_dateFrom->setObjectName("historyDateFrom");
    m_dateTo->setObjectName("historyDateTo");
    for (auto* edit : {m_dateFrom, m_dateTo}) {
        edit->setCalendarPopup(true);
        edit->setDisplayFormat(QStringLiteral("dd.MM.yy"));
        edit->setMinimumHeight(34);
        edit->setMinimumWidth(128);
        edit->setKeyboardTracking(false);
        edit->setDateRange(QDate(1900, 1, 1), QDate(9999, 12, 31));
    }

    m_dateFrom->setToolTip(QString::fromUtf8("Початок періоду"));
    m_dateTo->setToolTip(QString::fromUtf8("Кінець періоду"));

    m_search = new QLineEdit(this);
    m_search->setObjectName("historySearch");
    m_searchDebounce = new QTimer(this);
    m_searchDebounce->setSingleShot(true);
    m_searchDebounce->setInterval(200);
    m_refreshAllDebounce = new QTimer(this);
    m_refreshAllDebounce->setSingleShot(true);
    connect(m_refreshAllDebounce, &QTimer::timeout, this, [this]() { emit requestRefreshAll(); });
    m_search->setPlaceholderText(QString::fromUtf8("№ наряду, авто…"));
    m_search->setToolTip(QString::fromUtf8("Пошук за номером наряду, держномером, маркою авто або VIN"));
    AdaptiveUi::relaxLineEditWidth(m_search, 260, 620);
    m_search->setClearButtonEnabled(true);

    bar->setSpacing(8);
    const auto addTile=[&](int kind,const QString& title,const QList<QWidget*>& fields,int stretch) {
        auto* card=new QWidget(this);card->setObjectName(QString("historyActionTile%1").arg(kind));
        card->setAttribute(Qt::WA_StyledBackground);
        card->setStyleSheet(QString("#%1 {border:1.5px solid #101820;border-radius:9px;background:rgba(68,83,98,110);}").arg(card->objectName()));
        auto* row=new QHBoxLayout(card);row->setContentsMargins(10,5,10,5);row->setSpacing(12);
        const qreal ratio=devicePixelRatioF();
        QPixmap image(QSize(36,36)*ratio);image.setDevicePixelRatio(ratio);image.fill(Qt::transparent);
        QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor("#78c9df"),2.5,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        if(kind==0){p.drawRoundedRect(QRectF(4,7,28,25),2,2);p.drawLine(4,14,32,14);p.drawLine(11,3,11,10);p.drawLine(25,3,25,10);for(int y:{20,26})for(int x:{10,18,26})p.drawPoint(x,y);}
        if(kind==1){p.drawEllipse(QRectF(4,4,21,21));p.drawLine(23,23,33,33);}
        if(kind==2){p.drawLine(4,29,4,9);p.drawLine(4,9,14,9);p.drawLine(14,9,18,13);p.drawLine(18,13,30,13);p.drawLine(30,13,30,17);p.drawPolygon(QPolygonF{QPointF(4,29),QPointF(9,17),QPointF(34,17),QPointF(29,29)});}
        if(kind==3){p.drawRect(QRectF(8,3,23,30));p.drawLine(22,3,22,13);p.drawLine(22,13,31,13);p.drawLine(13,20,26,20);p.drawLine(13,26,26,26);}
        p.end();
        auto* icon=new QLabel(card);icon->setFixedSize(36,36);icon->setPixmap(image);row->addWidget(icon);
        auto* label=new QLabel(title,card);label->setStyleSheet("color:white;font-family:'Times New Roman';font-weight:bold;");row->addWidget(label);
        for(auto* field:fields){
            field->setMinimumWidth(qobject_cast<QDateEdit*>(field)?100:kind==1?110:field->fontMetrics().horizontalAdvance(qobject_cast<QToolButton*>(field)->text())+28);
            field->setMaximumWidth(QWIDGETSIZE_MAX);field->setFixedHeight(38);
            field->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);row->addWidget(field,1);
            if(qobject_cast<QToolButton*>(field))field->setStyleSheet("QToolButton {color:white;font-family:'Times New Roman';font-weight:bold;border:1px solid #74808b;border-radius:14px;padding:3px 10px;background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #66717e,stop:0.16 #414b58,stop:1 #1d242c);} QToolButton:hover {border-color:#9ddfee;} QToolButton:pressed {background:#253b49;} QToolButton:disabled {color:#9b9fa4;}");
        }
        card->setMinimumHeight(54);bar->addWidget(card,stretch);
    };
    addTile(0,QString::fromUtf8("Період"),{m_dateFrom,m_dateTo},30);
    addTile(1,QString::fromUtf8("Пошук"),{m_search},25);
    addTile(2,QString::fromUtf8("Наряд"),{btnOpenNewOrder},23);
    addTile(3,QString::fromUtf8("Документи"),{btnPdf},20);

    root->addLayout(bar);

    m_table = new QTableView(this);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(false);
    m_table->setSortingEnabled(false);

    m_model = new TabularSnapshotTableModel(this);
    m_proxy = new HistoryDisplayProxy(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setDynamicSortFilter(true);
    m_proxy->setSortCaseSensitivity(Qt::CaseInsensitive);
    m_table->setModel(m_proxy);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSortIndicatorShown(false);
    m_table->horizontalHeader()->setSectionsMovable(true);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setMinimumSectionSize(28);
    m_table->setColumnWidth(0, 60);
    m_table->setColumnWidth(1, 140);
    m_table->setColumnWidth(2, 140);
    m_table->setColumnWidth(3, 230);
    m_table->setColumnWidth(4, 120);
    m_table->setColumnWidth(5, 280);
    m_table->setColumnWidth(6, 170);
    m_table->setColumnWidth(7, 150);
    m_table->setColumnWidth(8, 110);
    m_table->setColumnWidth(9, 120);
    AdaptiveUi::installAdaptiveTable(m_table, {
        {0, 56, 60, 0, 84, true},
        {1, 124, 140, 2, 220, false},
        {2, 124, 140, 2, 220, false},
        {3, 88, 96, 1, 140, false},
        {4, 104, 120, 1, 170, false},
        {5, 220, 320, 5, 540, false},
        {6, 150, 180, 2, 280, false},
        {7, 150, 170, 2, 260, false},
        {8, 96, 110, 1, 150, false},
        {9, 104, 120, 1, 170, false}
    }, QStringLiteral("ui.history.adaptive.v2"));
    enforceHistoryVisibleColumns(m_table);
    connect(m_table->horizontalHeader(), &QHeaderView::sectionResized, this, [this](int, int, int) { saveHeaderState(); });
    connect(m_table->horizontalHeader(), &QHeaderView::sectionMoved, this, [this](int, int, int) { saveHeaderState(); });

    root->addWidget(m_table, 1);
    auto* pages = new QHBoxLayout;
    m_previousPage = new QPushButton(QStringLiteral("Попередня"), this);
    m_nextPage = new QPushButton(QStringLiteral("Наступна"), this);
    m_pageLabel = new QLabel(this);
    pages->addWidget(m_previousPage); pages->addWidget(m_pageLabel); pages->addWidget(m_nextPage); pages->addStretch();
    root->addLayout(pages);
    connect(m_previousPage, &QPushButton::clicked, this, [this]() { m_pageOffset=qMax(0,m_pageOffset-200); markDirty(); refresh(); });
    connect(m_nextPage, &QPushButton::clicked, this, [this]() { m_pageOffset+=200; markDirty(); refresh(); });

    btnOpenNewOrder->setEnabled(false);
    if (auto* sel = m_table->selectionModel()) {
        connect(sel, &QItemSelectionModel::selectionChanged, this, [this, btnOpenNewOrder]() {
            btnOpenNewOrder->setEnabled(selectedOrderNumber() > 0);
        });
    }

    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    if (m_table->viewport()) {
        m_table->viewport()->setContextMenuPolicy(Qt::CustomContextMenu);
        m_table->viewport()->installEventFilter(this);
    }
    m_table->installEventFilter(this);
    const auto showHistoryContextMenu = [this](const QPoint& pos, QWidget* sourceWidget) {
        if (!m_table || !m_table->model()) return;

        QPoint viewportPos;
        QPoint globalPos;
        if (m_table->viewport() && (sourceWidget == m_table->viewport() || sourceWidget == m_table)) {
            // For QAbstractScrollArea/QTableView customContextMenuRequested delivers
            // viewport coordinates. Treat both table and viewport signals as viewport-space
            // to avoid row offset by header/frame height.
            viewportPos = pos;
            globalPos = m_table->viewport()->mapToGlobal(pos);
        } else if (sourceWidget && m_table->viewport()) {
            globalPos = sourceWidget->mapToGlobal(pos);
            viewportPos = m_table->viewport()->mapFromGlobal(globalPos);
        } else {
            globalPos = QCursor::pos();
            viewportPos = m_table->viewport() ? m_table->viewport()->mapFromGlobal(globalPos) : QPoint();
        }

        const QModelIndex idx = m_table->indexAt(viewportPos);
        if (idx.isValid() && m_table->selectionModel()) {
            const QModelIndex rowAnchor = m_table->model()->index(idx.row(), 0);
            QSignalBlocker selectionBlocker(m_table->selectionModel());
            m_table->selectionModel()->clearSelection();
            m_table->selectionModel()->setCurrentIndex(rowAnchor, QItemSelectionModel::NoUpdate);
            m_table->selectionModel()->select(rowAnchor, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            m_table->setCurrentIndex(rowAnchor);
        }

        QMenu menu(m_table);
        QAction* actOpen = menu.addAction(QString::fromUtf8("Відкрити наряд"));
        actOpen->setEnabled(selectedOrderNumber() > 0);
        QAction* actCopy = menu.addAction(QString::fromUtf8("Копіювати"));
        actCopy->setShortcut(QKeySequence::Copy);
        actCopy->setEnabled(m_table->currentIndex().isValid());
        QAction* actSelectAll = menu.addAction(QString::fromUtf8("Виділити все"));
        actSelectAll->setShortcut(QKeySequence::SelectAll);
        actSelectAll->setEnabled(m_table->model()->rowCount() > 0);
        QAction* actClearSelection = menu.addAction(QString::fromUtf8("Зняти виділення"));
        actClearSelection->setEnabled(m_table->selectionModel() && m_table->selectionModel()->hasSelection());
        QAction* actDelete = menu.addAction(QString::fromUtf8("Видалити наряд"));
        actDelete->setShortcut(QKeySequence::Delete);
        const int selectedRowsCount = m_table->selectionModel() ? m_table->selectionModel()->selectedRows().size() : 0;
        actDelete->setEnabled(selectedRowsCount > 0);
        if (selectedRowsCount > 1) actDelete->setText(QString::fromUtf8("Видалити вибрані наряди"));

        QAction* picked = menu.exec(globalPos);
        if (!picked) return;

        if (picked == actOpen) {
            const QModelIndex current = m_table->currentIndex();
            if (!current.isValid() || !m_table || !m_table->model()) return;
            m_table->selectRow(current.row());
            QString routingError;
            const QString orderUid = selectedOrderUid();
            int num = selectedOrderNumber();
            if (!orderUid.trimmed().isEmpty()) {
                if (!ActiveArchiveRoutingLayer::prepareOrderForOpen(orderUid, num, &num, &routingError)) {
                    QMessageBox::critical(this, QString::fromUtf8("Помилка"), routingError);
                    return;
                }
            }
            if (num <= 0) return;
            clearSearchStateForReturn();
            BackupPerformanceGuard::hold(AppContext::instance().backupManager(), 2500);
            OrderDialog dlg(this, num);
            dlg.setWindowTitle(QString::fromUtf8("Редагування наряду"));
            dlg.openWorkPartsTab();
            const int rc = dlg.exec();
            if (rc == QDialog::Accepted) queueRefreshAll(0);
            return;
        }
        if (picked == actCopy) {
            QString text;
            const auto indexes = m_table->selectionModel() ? m_table->selectionModel()->selectedIndexes() : QModelIndexList();
            if (!indexes.isEmpty()) {
                QModelIndexList sorted = indexes;
                std::sort(sorted.begin(), sorted.end(), [](const QModelIndex& a, const QModelIndex& b) {
                    if (a.row() != b.row()) return a.row() < b.row();
                    return a.column() < b.column();
                });
                int currentRow = sorted.first().row();
                QStringList rowParts;
                for (const QModelIndex& mi : sorted) {
                    if (mi.row() != currentRow) {
                        text += rowParts.join(QStringLiteral("\t")) + QStringLiteral("\n");
                        rowParts.clear();
                        currentRow = mi.row();
                    }
                    rowParts << mi.data().toString();
                }
                if (!rowParts.isEmpty()) text += rowParts.join(QStringLiteral("\t"));
            } else if (m_table->currentIndex().isValid()) {
                text = m_table->currentIndex().data().toString();
            }
            QApplication::clipboard()->setText(text);
            return;
        }
        if (picked == actSelectAll) {
            m_table->selectAll();
            return;
        }
        if (picked == actClearSelection) {
            if (m_table->selectionModel()) m_table->selectionModel()->clearSelection();
            return;
        }
        if (picked == actDelete) {
            deleteSelectedOrders();
            return;
        }
    };
    connect(m_table, &QWidget::customContextMenuRequested, this, [showHistoryContextMenu, this](const QPoint& pos) {
        showHistoryContextMenu(pos, m_table);
    });
    if (m_table->viewport()) {
        connect(m_table->viewport(), &QWidget::customContextMenuRequested, this, [showHistoryContextMenu, this](const QPoint& pos) {
            showHistoryContextMenu(pos, m_table->viewport());
        });
    }

    auto* deleteSelectedOrdersAction = new QAction(this);
    deleteSelectedOrdersAction->setShortcut(QKeySequence::Delete);
    deleteSelectedOrdersAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(deleteSelectedOrdersAction);
    if (m_table) m_table->addAction(deleteSelectedOrdersAction);
    if (m_table && m_table->viewport()) m_table->viewport()->addAction(deleteSelectedOrdersAction);
    connect(deleteSelectedOrdersAction, &QAction::triggered, this, [this]() {
        const bool tableFocused = m_table && (m_table->hasFocus() || (m_table->viewport() && m_table->viewport()->hasFocus()));
        if (tableFocused) deleteSelectedOrders();
    });

    connect(m_table, &QTableView::clicked, this, [this](const QModelIndex& index) {
        if (!index.isValid() || index.column() != 3) return;
        m_table->setCurrentIndex(index);
        int number = selectedOrderNumber();
        const QString uid = selectedOrderUid();
        if (number <= 0) return;
        QMenu menu(this);
        menu.setObjectName(QStringLiteral("historyStatusMenu"));
        auto* open = menu.addAction(QString::fromUtf8("Відкритий"));
        auto* inProgress = menu.addAction(QString::fromUtf8("В роботі"));
        const bool allowed = AccessControlService::currentUserHasPermission( QStringLiteral("orders.edit"));
        open->setEnabled(allowed); inProgress->setEnabled(allowed);
        const auto* chosen = menu.exec(m_table->viewport()->mapToGlobal(m_table->visualRect(index).bottomLeft()));
        if (!chosen) return;
        QString error;
        if (!uid.isEmpty() && !ActiveArchiveRoutingLayer::prepareOrderForOpen(uid, number, &number, &error)) {
            QMessageBox::warning(this, QString::fromUtf8("Статус наряду"), error); return;
        }
        const auto result = OrderBatchMutationService::reopen(number,
            chosen == open ? QStringLiteral("open") : QStringLiteral("in_progress"));
        if (!result.ok) QMessageBox::warning(this, QString::fromUtf8("Статус наряду"), result.errorText);
        else BackupCommitPublisher::publish(BackupCommitPublisher::makeEvent(
            QStringLiteral("orders"), QStringLiteral("history"), QStringLiteral("work_order"),
            QString::number(number), uid, number, QStringLiteral("order_status_reopened"),
            QString::fromUtf8("Повторно відкрито наряд №%1").arg(number), {QStringLiteral("work_orders")}));
        markDirty(); refresh(); queueRefreshAll(0);
    });

    connect(m_table, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
        if (index.column() == 3) return;
        if (!index.isValid() || !m_table || !m_table->model()) return;
        m_table->selectRow(index.row());
        QString routingError;
        const QString orderUid = selectedOrderUid();
        int num = selectedOrderNumber();
        if (!orderUid.trimmed().isEmpty()) {
            if (!ActiveArchiveRoutingLayer::prepareOrderForOpen(orderUid, num, &num, &routingError)) {
                QMessageBox::critical(this, QString::fromUtf8("Помилка"), routingError);
                return;
            }
        }
        if (num <= 0) return;
        clearSearchStateForReturn();
        BackupPerformanceGuard::hold(AppContext::instance().backupManager(), 2500);
        OrderDialog dlg(this, num);
        connect(&dlg, &OrderDialog::purchasePaymentsChanged, this, [this] { queueRefreshAll(0); });
        dlg.setWindowTitle(QString::fromUtf8("Редагування наряду"));
        dlg.openWorkPartsTab();
        const int rc = dlg.exec();
        if (rc == QDialog::Accepted) {
            queueRefreshAll(0);
        }
    });

    connect(btnOpenNewOrder, &QToolButton::clicked, this, [this]() {
        openSelectedVehicleAsNewOrder();
    });

    connect(btnPdf, &QToolButton::clicked, this, [this]() {
        QString routingError;
        int num = selectedOrderNumber();
        const QString orderUid = selectedOrderUid();
        if (!orderUid.trimmed().isEmpty()) {
            if (!ActiveArchiveRoutingLayer::prepareOrderForOpen(orderUid, num, &num, &routingError)) {
                QMessageBox::critical(this, QString::fromUtf8("Помилка"), routingError);
                return;
            }
        }
        if (num <= 0) return;
        BackupPerformanceGuard::hold(AppContext::instance().backupManager(), 2500);
        OrderDialog dlg(this, num);
        dlg.openWorkPartsTab();
        dlg.exportPdfPreviewAndOpen(false);
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
    connect(m_dateFrom, &QDateEdit::dateChanged, this, [this](const QDate&) {
        markDirty();
        if (m_searchDebounce) m_searchDebounce->start(200);
        else refresh();
    });
    connect(m_dateTo, &QDateEdit::dateChanged, this, [this](const QDate&) {
        markDirty();
        if (m_searchDebounce) m_searchDebounce->start(200);
        else refresh();
    });

    // MainWindow triggers the first refresh when the page is opened.
    // Do not re-query once more from the constructor.
}

void HistoryPage::queueRefreshAll(int delayMs)
{
    if (!m_refreshAllDebounce) {
        emit requestRefreshAll();
        return;
    }
    m_refreshAllDebounce->start(qMax(0, delayMs));
}

int HistoryPage::selectedOrderNumber() const
{
    if (!m_table || !m_model) return -1;
    const QModelIndex idx = m_table->currentIndex();
    if (!idx.isValid()) return -1;
    const int internalCol = modelColumnByHeader(m_table->model(), QStringLiteral("__internal_number"));
    if (internalCol < 0) return -1;
    const QModelIndex numIdx = m_table->model()->index(idx.row(), internalCol);
    bool ok = false;
    const int num = m_table->model()->data(numIdx).toInt(&ok);
    return ok ? num : -1;
}

QString HistoryPage::selectedOrderUid() const
{
    if (!m_table || !m_model) return QString();
    const QModelIndex idx = m_table->currentIndex();
    if (!idx.isValid()) return QString();
    const int uidCol = modelColumnByHeader(m_table->model(), QStringLiteral("__order_uid"));
    if (uidCol < 0) return QString();
    return m_table->model()->index(idx.row(), uidCol).data().toString().trimmed();
}


QList<int> HistoryPage::selectedOrderNumbers() const
{
    QList<int> out;
    if (!m_table || !m_model) return out;
    auto* sel = m_table->selectionModel();
    if (!sel) return out;
    const int internalCol = modelColumnByHeader(m_table->model(), QStringLiteral("__internal_number"));
    if (internalCol < 0) return out;
    const QModelIndexList rows = sel->selectedRows(0);
    for (const QModelIndex& idx : rows) {
        const QModelIndex internalIdx = m_table->model()->index(idx.row(), internalCol);
        bool ok = false;
        int num = m_table->model()->data(internalIdx).toInt(&ok);
        if (ok) out.append(num);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

void HistoryPage::markDirty()
{
    m_dirty = true;
}


bool HistoryPage::deleteSelectedOrders()
{
    if (!m_table || !m_table->model() || !m_table->selectionModel()) return false;

    const int internalCol = modelColumnByHeader(m_table->model(), QStringLiteral("__internal_number"));
    const int uidCol = modelColumnByHeader(m_table->model(), QStringLiteral("__order_uid"));
    if (internalCol < 0) return false;

    struct OrderRef {
        int sourceNumber = -1;
        QString orderUid;
    };

    QList<OrderRef> refs;
    const QModelIndexList rows = m_table->selectionModel()->selectedRows(0);
    for (const QModelIndex& idx : rows) {
        if (!idx.isValid()) continue;
        bool ok = false;
        const int sourceNumber = m_table->model()->index(idx.row(), internalCol).data().toInt(&ok);
        if (!ok || sourceNumber <= 0) continue;
        const QString orderUid = uidCol >= 0 ? m_table->model()->index(idx.row(), uidCol).data().toString().trimmed() : QString();
        bool duplicate = false;
        for (const auto& ref : refs) {
            if (ref.sourceNumber == sourceNumber && ref.orderUid == orderUid) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) refs.push_back({sourceNumber, orderUid});
    }
    if (refs.isEmpty()) return false;

    QStringList numbers;
    for (const auto& ref : refs) numbers << QString::number(ref.sourceNumber);
    const QString prompt = refs.size() > 1
        ? QString::fromUtf8("Видалити %1 нарядів?\n№: %2").arg(refs.size()).arg(numbers.join(QStringLiteral(", ")))
        : QString::fromUtf8("Видалити наряд №%1?").arg(numbers.value(0));
    if (QMessageBox::question(this,
                              QString::fromUtf8("Підтвердження"),
                              prompt,
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes) {
        return false;
    }

    BackupPerformanceGuard::hold(AppContext::instance().backupManager(), 3000);
    QHash<int, QString> deletedUidsByNumber;
    for (const auto& ref : refs) {
        int routedNumber = ref.sourceNumber;
        if (!ref.orderUid.isEmpty()) {
            QString routingError;
            if (!ActiveArchiveRoutingLayer::prepareOrderForOpen(ref.orderUid, ref.sourceNumber, &routedNumber, &routingError)) {
                QMessageBox::critical(this, QString::fromUtf8("Помилка"), routingError);
                return false;
            }
        }

        QString deleteError;
        if (!AppRuntimeService::hardDeleteOrder(routedNumber, &deleteError)) {
            QMessageBox::critical(this,
                                  QString::fromUtf8("Помилка"),
                                  deleteError.trimmed().isEmpty()
                                      ? QString::fromUtf8("Не вдалося повністю видалити наряд")
                                      : deleteError);
            return false;
        }
        deletedUidsByNumber.insert(routedNumber, ref.orderUid);
    }

    for (const auto& ref : refs) {
        const int eventNumber = ref.sourceNumber > 0 ? ref.sourceNumber : 0;
        const QString eventUid = deletedUidsByNumber.value(ref.sourceNumber, ref.orderUid);
        BackupCommitPublisher::publish(
            BackupCommitPublisher::makeEvent(QStringLiteral("HistoryPage"),
                                             QStringLiteral("history_orders"),
                                             QStringLiteral("order"),
                                             QString::number(eventNumber),
                                             eventUid,
                                             eventNumber,
                                             QStringLiteral("order_delete"),
                                             QString::fromUtf8("Видалено наряд №%1").arg(eventNumber),
                                             {QStringLiteral("work_orders"), QStringLiteral("work_order_services"), QStringLiteral("work_order_parts")}),
            nullptr);
    }

    markDirty();
    refresh();
    emit requestRefreshAll();
    return true;
}

void HistoryPage::openSelectedVehicleAsNewOrder()
{
    int num = selectedOrderNumber();
    const QString orderUid = selectedOrderUid();
    if (!orderUid.trimmed().isEmpty()) {
        QString routingError;
        if (!ActiveArchiveRoutingLayer::prepareOrderForOpen(orderUid, num, &num, &routingError)) {
            QMessageBox::critical(this, QString::fromUtf8("Помилка"), routingError);
            return;
        }
    }
    if (num <= 0) {
        QMessageBox::information(this,
                                 QString::fromUtf8("Новий наряд"),
                                 QString::fromUtf8("Спочатку виберіть авто у таблиці."));
        return;
    }

    BackupPerformanceGuard::hold(AppContext::instance().backupManager(), 2500);
    OrderDialog dlg(this);
    dlg.setWindowTitle(QString::fromUtf8("Новий наряд"));
    dlg.prefillFromExistingOrder(num);
    if (dlg.exec() == QDialog::Accepted) queueRefreshAll(0);
}

void HistoryPage::clearSearchStateForReturn()
{
    const bool hadLocalSearch = m_search && !m_search->text().isEmpty();

    if (hadLocalSearch && m_search) {
        const QSignalBlocker blocker(m_search);
        m_search->clear();
    }
    if (hadLocalSearch) {
        markDirty();
        refresh();
    }
}

bool HistoryPage::eventFilter(QObject* watched, QEvent* event)
{
    if (m_table && (watched == m_table || watched == m_table->viewport()) && event && event->type() == QEvent::MouseButtonDblClick) {
        if (auto* me = static_cast<QMouseEvent*>(event); me->button() == Qt::RightButton) {
            event->accept();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void HistoryPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    // MainWindow already refreshes the active page on tab navigation. Only do an
    // eager load here for the very first show to avoid repeated full reloads on
    // every return to the tab.
    if (!m_loaded) {
        markDirty();
        refresh();
    }
}

void HistoryPage::refresh()
{
    const QString scopeKey = AccessControlService::currentScopeKey();
    if (m_actorScopeKey != scopeKey) {
        m_actorScopeKey = scopeKey;
        ++m_refreshGeneration;
        m_pageOffset = 0;
        m_dirty = true;
        TabularReadModelMapper::apply(m_model, TabularReadModel{});
    }
    if (m_loaded && !m_dirty && !m_refreshWorker) return;
    if (m_refreshInProgress || m_refreshWorker) {
        AsyncTaskHandle::cancel(m_refreshCancelToken);
        if (m_refreshWorker) m_refreshWorker->requestInterruption();
        m_refreshQueued = true;
        m_dirty = true;
        return;
    }

    m_refreshInProgress = true;
    startAsyncRefresh();
}

void HistoryPage::startAsyncRefresh()
{
    const int selectedBefore = selectedOrderNumber();
    const int vScrollBefore = (m_table && m_table->verticalScrollBar()) ? m_table->verticalScrollBar()->value() : 0;
    const int hScrollBefore = (m_table && m_table->horizontalScrollBar()) ? m_table->horizontalScrollBar()->value() : 0;
    const QString filter = m_search ? m_search->text().trimmed() : QString();
    const QDate from = m_dateFrom ? m_dateFrom->date() : QDate();
    const QDate to = m_dateTo ? m_dateTo->date() : QDate();
    const QString scopeKey = AccessControlService::currentScopeKey();
    const QString pageKey=from.toString(Qt::ISODate)+"|"+to.toString(Qt::ISODate)+"|"+filter+"|"+scopeKey;
    if(m_pageFilterKey!=pageKey) { m_pageFilterKey=pageKey; m_pageOffset=0; }
    const int pageOffset=m_pageOffset;
    m_previousPage->setEnabled(false); m_nextPage->setEnabled(false);
    const int generation = ++m_refreshGeneration;
    const QString refreshCacheKey = QStringLiteral("history|from=%1|to=%2|filter=%3")
        .arg(from.toString(Qt::ISODate), to.toString(Qt::ISODate), filter.toLower());
    const auto runtimeTicket = RuntimeJob::RuntimeJobDispatcher::instance().startLatest(
        RuntimeJob::Family::History,
        QStringLiteral("HistoryPage"),
        20000,
        70,
        refreshCacheKey);
    m_refreshCancelToken = runtimeTicket.cancelToken;
    const auto cancelToken = m_refreshCancelToken;
    const AsyncTaskHandle asyncHandle(this, static_cast<quint64>(generation), cancelToken);

    PerfWatchdog::asyncJobStarted(QStringLiteral("history-page"), QString::number(generation));

    struct AsyncResult {
        HistoryQueryResult queryResult;
        TabularReadModel readModel;
    };
    const auto result = std::make_shared<AsyncResult>();

    auto* worker = AsyncThreadScope::startWorker(this,
                                               QStringLiteral("history_refresh_%1").arg(generation),
                                               cancelToken,
                                               [result, from, to, filter, runtimeTicket, pageOffset](const std::shared_ptr<std::atomic_bool>& cancelToken) {
        RuntimeJob::RuntimeJobDispatcher::instance().markRunning(runtimeTicket);
        auto cancelled = [&cancelToken]() { return AsyncThreadScope::isCancellationRequested(cancelToken); };
        if (cancelled()) return;

        HistoryQueryParams params{from,to,filter}; params.pageSize=200; params.offset=pageOffset;
        const HistoryRefreshJob::Result localResult = HistoryRefreshJob::run(params, cancelToken);
        if (!localResult.cancelled && !cancelled()) {
            result->queryResult = localResult.queryResult;
            result->readModel = localResult.readModel;
        }
    });

    m_refreshWorker = worker;
    if (runtimeTicket.timeoutMs > 0) {
        const QPointer<QThread> workerGuard(worker);
        QTimer::singleShot(qMax(1000, runtimeTicket.timeoutMs), this, [workerGuard, cancelToken, runtimeTicket, generation]() {
            if (!workerGuard) return;
            if (!RuntimeJob::RuntimeJobDispatcher::instance().shouldApply(runtimeTicket)) return;
            if (cancelToken) cancelToken->store(true);
            workerGuard->requestInterruption();
            RuntimeJob::RuntimeJobDispatcher::instance().cancelFamily(runtimeTicket.family);
            qWarning().noquote() << QStringLiteral("ASYNC_LIFECYCLE history_refresh_timeout_cancel generation=") << generation
                                 << QStringLiteral(" timeoutMs=") << runtimeTicket.timeoutMs;
        });
    }
    // This mutex-protected counter must finish before deferred worker deletion;
    // queued callbacks targeting the worker can be discarded with that object.
    connect(worker, &QThread::finished, worker, [generation]() {
        PerfWatchdog::asyncJobFinished(QStringLiteral("history-page"), QString::number(generation));
    }, Qt::DirectConnection);
    connect(worker, &QThread::finished, this, [this, worker, result, asyncHandle, runtimeTicket, selectedBefore, vScrollBefore, hScrollBefore, scopeKey]() {
        if (m_refreshWorker == worker) m_refreshWorker = nullptr;
        const bool canApply = scopeKey == AccessControlService::currentScopeKey()
            && asyncHandle.shouldApply(static_cast<quint64>(m_refreshGeneration))
            && RuntimeJob::RuntimeJobDispatcher::instance().shouldApply(runtimeTicket);
        RuntimeJob::RuntimeJobDispatcher::instance().finish(runtimeTicket, canApply ? RuntimeJob::Status::Succeeded : RuntimeJob::Status::Cancelled);
        if (canApply) {
            applyAsyncSnapshot(result->queryResult, result->readModel, selectedBefore, vScrollBefore, hScrollBefore);
        } else {
            qWarning().noquote() << QStringLiteral("ASYNC_LIFECYCLE history_refresh_result_detached generation=") << asyncHandle.generation();
            m_refreshInProgress = false;
            if (m_refreshQueued) {
                m_refreshQueued = false;
                QPointer<HistoryPage> self(this);
                QTimer::singleShot(0, this, [self]() { if (self) self->refresh(); });
            }
        }
    });
    worker->start();
}

void HistoryPage::applyAsyncSnapshot(const HistoryQueryResult& queryResult,
                                     const TabularReadModel& readModel,
                                     int selectedBefore,
                                     int vScrollBefore,
                                     int hScrollBefore)
{
    QElapsedTimer t; t.start();
    PerfWatchdog::ScopedTimer perfTimer(QStringLiteral("HistoryPage::applyAsyncSnapshot"), 140);

    if (!queryResult.errorText.trimmed().isEmpty()) {
        qWarning() << "HistoryPage query error" << queryResult.errorText;
    }

    if (m_table) m_table->setUpdatesEnabled(false);
    TabularReadModelMapper::apply(m_model, readModel);
    m_previousPage->setEnabled(m_pageOffset>0);
    m_nextPage->setEnabled(queryResult.hasMore);
    m_pageLabel->setText(QStringLiteral("Сторінка %1 · Нарядів: %2").arg(m_pageOffset/200+1).arg(queryResult.rows.size()));
    restoreHeaderState();
    OrderTableUiMapper::applyHistoryModelShape(m_table, m_table ? m_table->model() : nullptr);
    enforceHistoryVisibleColumns(m_table);
    if (m_table) m_table->setUpdatesEnabled(true);

    bool restoredSelection = false;
    if (m_table && m_table->model()) {
        int targetRow = -1;
        if (selectedBefore > 0 && m_model) {
            // P1/pass9 current/history snapshot migration closure: restore selection through
            // the source snapshot index and map through the proxy; never scan view rows.
            const int sourceRow = m_model->rowByColumnValue(QStringLiteral("__internal_number"), selectedBefore);
            if (sourceRow >= 0) {
                const QModelIndex sourceIndex = m_model->index(sourceRow, 0);
                const QModelIndex proxyIndex = m_proxy ? m_proxy->mapFromSource(sourceIndex) : sourceIndex;
                if (proxyIndex.isValid()) targetRow = proxyIndex.row();
            }
        }
        if (targetRow >= 0) {
            const QModelIndex current = m_table->model()->index(targetRow, 0);
            if (current.isValid()) {
                m_table->setCurrentIndex(current);
                m_table->selectRow(targetRow);
                m_table->scrollTo(current, QAbstractItemView::PositionAtCenter);
                restoredSelection = true;
            }
        }
        if (!restoredSelection) {
            if (auto* vbar = m_table->verticalScrollBar()) vbar->setValue(vScrollBefore);
            if (auto* hbar = m_table->horizontalScrollBar()) hbar->setValue(hScrollBefore);
        }
        if (m_table->viewport()) m_table->viewport()->update();
    }

    m_loaded = true;
    m_dirty = false;
    m_refreshInProgress = false;
    qDebug() << "HistoryPage applyAsyncSnapshot ms=" << t.elapsed() << "rows=" << (m_model ? m_model->rowCount() : 0);
    if (m_refreshQueued) {
        m_refreshQueued = false;
        QPointer<HistoryPage> self(this);
        QTimer::singleShot(0, this, [self]() { if (self) self->refresh(); });
    }
}


void HistoryPage::restoreHeaderState()
{
    if (!m_table || !m_table->model() || !m_table->horizontalHeader()) return;
    const QString schema = historyHeaderSchema(m_table->model());
    if (schema.trimmed().isEmpty()) return;

    const QString storedSchema = UiStateSettingsService::loadString(QStringLiteral("ui.history.header_schema.v2"));
    const QString raw = UiStateSettingsService::loadString(QStringLiteral("ui.history.header_state.v2"));
    if (storedSchema != schema || raw.trimmed().isEmpty()) {
        configureHistoryHeaderForManualWidths(m_table);
        const int yearColumn = modelColumnByHeader(m_table->model(), QString::fromUtf8("Рік"));
        if (yearColumn >= 0) {
            auto* header = m_table->horizontalHeader();
            header->moveSection(header->visualIndex(yearColumn), 1);
        }
        return;
    }

    const bool restored = m_table->horizontalHeader()->restoreState(QByteArray::fromBase64(raw.toUtf8()));
    if (!restored) {
        configureHistoryHeaderForManualWidths(m_table);
        return;
    }
    configureHistoryHeaderForManualWidths(m_table);
}

void HistoryPage::saveHeaderState() const
{
    if (!m_table || !m_table->model() || !m_table->horizontalHeader()) return;
    const QString schema = historyHeaderSchema(m_table->model());
    if (schema.trimmed().isEmpty()) return;
    UiStateSettingsService::saveString(QStringLiteral("ui.history.header_schema.v2"), schema);
    UiStateSettingsService::saveString(QStringLiteral("ui.history.header_state.v2"), QString::fromLatin1(m_table->horizontalHeader()->saveState().toBase64()));
}

HistoryPage::~HistoryPage()
{
    cancelActiveRefreshForOwnerShutdown("destructor");
    saveHeaderState();
}

void HistoryPage::closeEvent(QCloseEvent* event)
{
    AsyncTaskHandle::cancel(m_refreshCancelToken);
    if (m_refreshWorker) {
        m_refreshWorker->requestInterruption();
    }
    saveHeaderState();
    QWidget::closeEvent(event);
}

void HistoryPage::cancelActiveRefreshForOwnerShutdown(const char* reason)
{
    AsyncTaskHandle::cancel(m_refreshCancelToken);
    m_refreshQueued = false;
    m_refreshInProgress = false;

    if (m_refreshWorker) {
        qWarning().noquote() << QStringLiteral("ASYNC_LIFECYCLE history_owner_shutdown reason=")
                             << QString::fromLatin1(reason ? reason : "unknown")
                             << QStringLiteral(" generation=") << m_refreshGeneration;
        QObject::disconnect(m_refreshWorker, nullptr, this, nullptr);
        m_refreshWorker->requestInterruption();
        m_refreshWorker.clear();
    }
}
