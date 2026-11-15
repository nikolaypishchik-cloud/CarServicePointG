#include "ReportsDialogSupport.h"
#include "core/util/FormatUtils.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QModelIndex>
#include <QGuiApplication>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QPushButton>
#include <QSizePolicy>
#include <QTableView>
#include <QStringList>
#include <QtGlobal>
#include <QVariant>
#include <QVector>
#include <QVBoxLayout>
#include <memory>

#include "app/AppContext.h"
#include "core/cache/SnapshotCacheInvalidator.h"
#include "core/services/AppRuntimeService.h"
#include "core/backup/BackupPerformanceGuard.h"
#include "core/services/ActiveArchiveRoutingLayer.h"
#include "core/services/ReportsSyncSupport.h"
#include "ui/common/dialogs/OrderDialog.h"
#include "ui/tabs/reports/ReportsUiSupport.h"
#include "ui/tabs/reports/ReportsTableSupport.h"

namespace {

constexpr int ColDateStart = 0;
constexpr int ColDateEnd = 1;
constexpr int ColOrder = 2;
constexpr int ColCar = 3;
constexpr int ColWorks = 4;
constexpr int ColPayrollAmount = 5;
constexpr int ColAbsent = 6;
constexpr int ColDelete = 7;
constexpr int PayrollOrdersActionRowHeight = 38;
constexpr int PayrollOrdersAbsentColumnWidth = 92;
constexpr int PayrollOrdersDeleteColumnWidth = 112;
constexpr int PayrollOrdersAbsentCheckHitSize = 28;
constexpr int PayrollOrdersDeleteButtonWidth = 78;
constexpr int PayrollOrdersDeleteButtonHeight = 24;

void applyPayrollOrdersTableStyle(QTableView* table)
{
    if (!table) return;
    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setWordWrap(true);
    table->setShowGrid(true);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(PayrollOrdersActionRowHeight);
    table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table->horizontalHeader()->setStretchLastSection(false);
}

QStringList payrollOrdersHeaders()
{
    return {
        QString::fromUtf8("Дата відкриття"),
        QString::fromUtf8("Дата закриття"),
        QString::fromUtf8("Наряд"),
        QString::fromUtf8("Авто"),
        QString::fromUtf8("Виконані роботи"),
        QString::fromUtf8("Сума в зарплату"),
        QString::fromUtf8("Відсутній"),
        QString::fromUtf8("Видалити")
    };
}

ReportsTableSupport::ReportsSnapshotTableModel::Cell makePayrollOrdersCell(
    const QString& text,
    int alignment = int(Qt::AlignLeft | Qt::AlignVCenter),
    const QVariant& userValue = QVariant(),
    const QString& tooltip = QString(),
    bool bold = false)
{
    ReportsTableSupport::ReportsSnapshotTableModel::Cell cell;
    cell.text = text;
    cell.alignment = alignment;
    cell.userValue = userValue;
    cell.tooltip = tooltip;
    cell.bold = bold;
    return cell;
}

QVector<ReportsTableSupport::ReportsSnapshotTableModel::Row> buildPayrollOrdersRows(
    const QList<PayrollOrderRow>& orders)
{
    QVector<ReportsTableSupport::ReportsSnapshotTableModel::Row> rows;
    rows.reserve(qMax(1, orders.size()));

    for (const auto& order : orders) {
        const double payrollAmount = order.absent ? 0.0 : order.masterAmount;
        ReportsTableSupport::ReportsSnapshotTableModel::Row out;
        out.cells = {
            makePayrollOrdersCell(order.dateStart),
            makePayrollOrdersCell(order.dateEnd),
            makePayrollOrdersCell(order.orderNumber, int(Qt::AlignLeft | Qt::AlignVCenter), order.internalOrderNumber),
            makePayrollOrdersCell(order.car),
            makePayrollOrdersCell(order.works),
            makePayrollOrdersCell(FormatUtils::money(payrollAmount,2), int(Qt::AlignRight | Qt::AlignVCenter), payrollAmount),
            makePayrollOrdersCell(QString(), int(Qt::AlignCenter | Qt::AlignVCenter), order.absent),
            makePayrollOrdersCell(QString(), int(Qt::AlignCenter | Qt::AlignVCenter))
        };
        rows.push_back(out);
    }

    if (rows.isEmpty()) {
        ReportsTableSupport::ReportsSnapshotTableModel::Row row;
        row.cells.resize(payrollOrdersHeaders().size());
        row.cells[0] = makePayrollOrdersCell(QString::fromUtf8("Немає виконаних нарядів за вибраний період"));
        row.spanAllColumns = true;
        rows.push_back(row);
    }

    return rows;
}

double recalcPayrollSummary(const QList<PayrollOrderRow>& orders)
{
    double total = 0.0;
    for (const auto& order : orders) {
        if (!order.absent) total += order.masterAmount;
    }
    return total;
}

void updatePayrollSummaryLabel(QLabel* label, const QList<PayrollOrderRow>& orders, const QString& masterLabel)
{
    if (!label) return;
    label->setText(QString::fromUtf8("%1 — нараховано: %2 грн")
        .arg(masterLabel, FormatUtils::money(recalcPayrollSummary(orders),2)));
}

int rowForWidget(QTableView* table, QWidget* widget)
{
    if (!table || !widget) return -1;
    return table->indexAt(widget->mapTo(table->viewport(), QPoint(1, 1))).row();
}

QWidget* makeCenteredCellWidget(QWidget* parent, QWidget* content)
{
    auto* cell = new QWidget(parent);
    cell->setObjectName(QStringLiteral("payrollOrdersActionCell"));
    cell->setMinimumHeight(PayrollOrdersActionRowHeight);
    cell->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    content->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto* layout = new QGridLayout(cell);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->setAlignment(Qt::AlignCenter);
    layout->addWidget(content, 0, 0, Qt::AlignCenter);
    return cell;
}

void applyPayrollOrdersSnapshot(QTableView* table,
                                ReportsTableSupport::ReportsSnapshotTableModel* model,
                                QLabel* title,
                                const QString& masterId,
                                const QString& masterLabel,
                                const std::shared_ptr<QList<PayrollOrderRow>>& state)
{
    if (!table || !model || !state) return;
    model->setSnapshot(payrollOrdersHeaders(), buildPayrollOrdersRows(*state));
    table->clearSpans();
    for (int row = 0; row < model->rowCount(); ++row) {
        if (model->rowSpansAllColumns(row)) {
            table->setSpan(row, 0, 1, qMax(1, model->columnCount()));
            continue;
        }
        table->setRowHeight(row, PayrollOrdersActionRowHeight);

        auto* absent = new QCheckBox(table);
        absent->setObjectName(QStringLiteral("payrollOrdersAbsentCheck"));
        absent->setChecked(state->at(row).absent);
        absent->setToolTip(QString::fromUtf8("Майстер був відсутній / не виконав цей наряд"));
        absent->setCursor(Qt::PointingHandCursor);
        absent->setFixedSize(PayrollOrdersAbsentCheckHitSize, PayrollOrdersAbsentCheckHitSize);
        absent->setFocusPolicy(Qt::StrongFocus);
        table->setIndexWidget(model->index(row, ColAbsent), makeCenteredCellWidget(table, absent));
        QObject::connect(absent, &QCheckBox::toggled, table, [table, model, title, masterId, masterLabel, state, absent](bool checked) {
            const int row = rowForWidget(table, absent);
            if (row < 0 || row >= state->size()) return;
            bool ok = false;
            const int orderNumber = model->cellUserValue(row, 2).toInt(&ok);
            if (!ok || orderNumber <= 0) return;
            QString error;
            if (!ReportsSyncSupport::setPayrollOrderAbsent(masterId, orderNumber, checked, &error)) {
                absent->blockSignals(true);
                absent->setChecked(!checked);
                absent->blockSignals(false);
                QMessageBox::warning(table, QString::fromUtf8("Зарплата"), error.trimmed().isEmpty()
                    ? QString::fromUtf8("Не вдалося зберегти стан наряду.")
                    : error);
                return;
            }
            (*state)[row].absent = checked;
            SnapshotCacheInvalidator::invalidateReportsAndStats(QStringLiteral("payroll_order_adjustment_absent"));
            applyPayrollOrdersSnapshot(table, model, title, masterId, masterLabel, state);
        });

        auto* remove = new QPushButton(QString::fromUtf8("Видалити"), table);
        remove->setObjectName(QStringLiteral("payrollOrdersDeleteButton"));
        remove->setCursor(Qt::PointingHandCursor);
        remove->setFixedSize(PayrollOrdersDeleteButtonWidth, PayrollOrdersDeleteButtonHeight);
        remove->setFocusPolicy(Qt::StrongFocus);
        table->setIndexWidget(model->index(row, ColDelete), makeCenteredCellWidget(table, remove));
        QObject::connect(remove, &QPushButton::clicked, table, [table, model, title, masterId, masterLabel, state, remove]() {
            const int row = rowForWidget(table, remove);
            if (row < 0 || row >= state->size()) return;
            const auto answer = QMessageBox::question(table,
                QString::fromUtf8("Зарплата"),
                QString::fromUtf8("Видалити цей наряд із деталізації зарплати майстра?"),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);
            if (answer != QMessageBox::Yes) return;
            bool ok = false;
            const int orderNumber = model->cellUserValue(row, 2).toInt(&ok);
            if (!ok || orderNumber <= 0) return;
            QString error;
            if (!ReportsSyncSupport::setPayrollOrderDeleted(masterId, orderNumber, true, &error)) {
                QMessageBox::warning(table, QString::fromUtf8("Зарплата"), error.trimmed().isEmpty()
                    ? QString::fromUtf8("Не вдалося видалити наряд із деталізації.")
                    : error);
                return;
            }
            state->removeAt(row);
            SnapshotCacheInvalidator::invalidateReportsAndStats(QStringLiteral("payroll_order_adjustment_deleted"));
            applyPayrollOrdersSnapshot(table, model, title, masterId, masterLabel, state);
        });
    }
    updatePayrollSummaryLabel(title, *state, masterLabel);
}

} // namespace

namespace {
class LinkRowDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QStyledItemDelegate::paint(painter, option, index);
        painter->save();
        painter->setPen(QColor("#8adbe4"));
        painter->drawText(option.rect.adjusted(0,0,-5,0), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("›"));
        painter->restore();
    }
};

class RecordPopup final : public QDialog {
public:
    explicit RecordPopup(QWidget* parent) : QDialog(parent, Qt::Popup | Qt::FramelessWindowHint) {
        setAttribute(Qt::WA_TranslucentBackground);
    }
    int tipX = 16;
    bool below = true;
protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (!rect().contains(event->position().toPoint())) reject();
        else QDialog::mousePressEvent(event);
    }
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QRectF body = QRectF(rect()).adjusted(2, below ? 9 : 2, -3, below ? -3 : -9);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 85));
        painter.drawRoundedRect(body.translated(1, 2), 8, 8);
        painter.setBrush(QColor("#17242d"));
        painter.setPen(QColor("#53afbd"));
        painter.drawRoundedRect(body, 8, 8);
        const qreal edge = below ? body.top() : body.bottom();
        const QPolygonF tip{QPointF(tipX-7, edge), QPointF(tipX, edge+(below ? -8 : 8)), QPointF(tipX+7, edge)};
        painter.setPen(Qt::NoPen);
        painter.drawPolygon(tip);
        painter.setPen(QColor("#53afbd"));
        painter.drawPolyline(tip);
    }
};
}

namespace ReportsDialogSupport {
int chooseLinkedRecord(QTableView* table, const QModelIndex& cell, const QString& title,
                       const QStringList& labels, bool invoices) {
    const QRect visibleCell = table->visualRect(cell).intersected(table->viewport()->rect());
    if (visibleCell.isEmpty() || labels.isEmpty()) return -1;
    const QRect anchor(table->viewport()->mapToGlobal(visibleCell.topLeft()), visibleCell.size());
    QScreen* screen = QGuiApplication::screenAt(anchor.center());
    const QRect area = (screen ? screen : table->screen())->availableGeometry();
    RecordPopup popup(table);
    popup.setWindowTitle(title);
    auto* layout = new QVBoxLayout(&popup);
    layout->setSpacing(2);
    auto* heading = new QHBoxLayout;
    auto* icon = new QLabel(&popup);
    icon->setPixmap(table->style()->standardIcon(invoices ? QStyle::SP_FileIcon : QStyle::SP_FileDialogDetailedView).pixmap(18,18));
    heading->addWidget(icon);
    auto* caption = new QLabel(title, &popup);
    caption->setStyleSheet("color:#eef7fa;font-weight:bold;background:transparent;");
    heading->addWidget(caption, 1);
    layout->addLayout(heading);
    auto* list = new QListWidget(&popup);
    list->setAccessibleName(title);
    list->setMouseTracking(true);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setTextElideMode(Qt::ElideRight);
    list->setStyleSheet("QListWidget{background:#17242d;color:white;border:0;border-top:1px solid #52707b;}"
        "QListWidget::item{padding:5px 18px 5px 6px;border-left:3px solid transparent;border-bottom:1px solid #34434e;}"
        "QListWidget::item:hover,QListWidget::item:selected{background:#215264;color:white;border-left:3px solid #71e7ed;}");
    layout->addWidget(list);
    const int rowHeight = list->fontMetrics().height()+13;
    int width = caption->sizeHint().width()+62;
    for (const auto& label : labels) {
        auto* item = new QListWidgetItem(label, list);
        item->setToolTip(label);
        item->setSizeHint(QSize(0, rowHeight));
        width = qMax(width, list->fontMetrics().horizontalAdvance(label)+76);
    }
    list->setItemDelegate(new LinkRowDelegate(list));
    const int desiredHeight = heading->sizeHint().height()+qMin(list->count(),10)*rowHeight+33;
    const int below = area.bottom()-anchor.bottom();
    const int above = anchor.top()-area.top();
    popup.below = below >= desiredHeight || below >= above;
    layout->setContentsMargins(10,popup.below ? 15 : 8,10,popup.below ? 8 : 15);
    layout->setSizeConstraint(QLayout::SetNoConstraint);
    popup.resize(qMin(area.width(), qMin(width, 580)), qMin(desiredHeight, qMax(1, qMax(above,below))));
    if (popup.below) popup.resize(popup.width(), qMin(popup.height(), qMax(1,below)));
    const int x = qBound(area.left(), anchor.left(), area.right()-popup.width()+1);
    popup.tipX = qBound(13, anchor.center().x()-x, popup.width()-13);
    popup.move(x, popup.below ? anchor.bottom()+1 : anchor.top()-popup.height());
    list->setCurrentRow(0);
    list->setFocus();
    QObject::connect(list,&QListWidget::itemClicked,&popup,&QDialog::accept);
    QObject::connect(list,&QListWidget::itemActivated,&popup,&QDialog::accept);
    return popup.exec()==QDialog::Accepted ? list->currentRow() : -1;
}


void openPayrollOrdersDialog(QWidget* parent,
                             const QString& masterId,
                             const QString& masterLabel,
                             const QList<PayrollOrderRow>& orders)
{
    QDialog dlg(parent);
    dlg.setObjectName(QStringLiteral("payrollOrdersDialog"));
    dlg.setWindowTitle(QString::fromUtf8("Виконані наряди по зарплаті"));
    dlg.resize(1180, 700);
    dlg.setStyleSheet(QString::fromUtf8(R"(
        #payrollOrdersDialog {
            background: #5f6b7d;
            border: none;
        }
        #payrollOrdersSummaryLabel {
            background: transparent;
            color: #f7fbff;
            font-size: 13pt;
            font-weight: 700;
            padding: 2px 0 8px 0;
        }
        #payrollOrdersTable {
            background: #f8fafc;
            color: #12161d;
            border: 1px solid rgba(33, 41, 54, 0.22);
            gridline-color: rgba(33, 41, 54, 0.22);
            selection-background-color: #cfdcf0;
            selection-color: #111827;
            alternate-background-color: #eef3f9;
        }
        #payrollOrdersTable::item {
            color: #12161d;
            padding: 6px 8px;
        }
        #payrollOrdersTable QHeaderView::section {
            background: #e4ebf3;
            color: #111827;
            border: 1px solid rgba(33, 41, 54, 0.16);
            padding: 8px 10px;
            font-weight: 700;
        }
        #payrollOrdersTable QWidget#payrollOrdersActionCell {
            background: transparent;
        }
        #payrollOrdersTable QCheckBox#payrollOrdersAbsentCheck {
            background: transparent;
            padding: 0;
            margin: 0;
            spacing: 0;
        }
        #payrollOrdersTable QCheckBox#payrollOrdersAbsentCheck::indicator {
            width: 20px;
            height: 20px;
            subcontrol-origin: content;
            subcontrol-position: center;
        }
        #payrollOrdersTable QCheckBox#payrollOrdersAbsentCheck::indicator:unchecked {
            background: #ffffff;
            border: 1px solid #111827;
            border-radius: 4px;
        }
        #payrollOrdersTable QCheckBox#payrollOrdersAbsentCheck::indicator:unchecked:hover {
            background: #f3f7fb;
            border-color: #0f172a;
        }
        #payrollOrdersTable QCheckBox#payrollOrdersAbsentCheck::indicator:checked {
            background: #dbeafe;
            image: url(:/assets/checkbox_checked_black.png);
            border: 1px solid #0f172a;
            border-radius: 4px;
        }
        #payrollOrdersTable QCheckBox#payrollOrdersAbsentCheck::indicator:checked:hover {
            background: #bfdbfe;
            border-color: #020617;
        }
        #payrollOrdersTable QPushButton#payrollOrdersDeleteButton {
            background: #f8fafc;
            color: #111827;
            border: 1px solid rgba(31, 41, 55, 0.42);
            border-radius: 6px;
            padding: 0 6px;
            font-weight: 700;
            min-width: 78px;
            min-height: 24px;
            max-width: 78px;
            max-height: 24px;
        }
        #payrollOrdersTable QPushButton#payrollOrdersDeleteButton:hover {
            background: #eef3f9;
            border-color: rgba(15, 23, 42, 0.72);
        }
        #payrollOrdersTable QPushButton#payrollOrdersDeleteButton:pressed {
            background: #dbe6f3;
        }
    )"));

    auto* root = new QVBoxLayout(&dlg);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    auto* title = new QLabel(&dlg);
    title->setObjectName(QStringLiteral("payrollOrdersSummaryLabel"));
    title->setWordWrap(true);
    root->addWidget(title);

    auto* table = new QTableView(&dlg);
    table->setObjectName(QStringLiteral("payrollOrdersTable"));
    applyPayrollOrdersTableStyle(table);

    auto* model = new ReportsTableSupport::ReportsSnapshotTableModel(table);
    const auto state = std::make_shared<QList<PayrollOrderRow>>(orders);
    model->setSnapshot(payrollOrdersHeaders(), buildPayrollOrdersRows(orders));
    table->setModel(model);
    applyPayrollOrdersSnapshot(table, model, title, masterId, masterLabel, state);

    table->horizontalHeader()->setSectionResizeMode(ColDateStart, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(ColDateEnd, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(ColOrder, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(ColCar, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(ColWorks, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(ColPayrollAmount, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(ColAbsent, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(ColDelete, QHeaderView::Fixed);
    table->setColumnWidth(ColAbsent, PayrollOrdersAbsentColumnWidth);
    table->setColumnWidth(ColDelete, PayrollOrdersDeleteColumnWidth);

    root->addWidget(table, 1);

    auto* bottom = new QHBoxLayout();
    bottom->addStretch(1);
    auto* btnClose = new QPushButton(QString::fromUtf8("Закрити"), &dlg);
    bottom->addWidget(btnClose);
    root->addLayout(bottom);
    QObject::connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::accept);

    QObject::connect(table, &QTableView::doubleClicked, &dlg, [parent, table, model](const QModelIndex& index) {
        if (!table || !model || !index.isValid()) return;
        if (index.column() == ColAbsent || index.column() == ColDelete) return;
        const int orderRow = index.row();
        if (orderRow < 0 || orderRow >= model->rowCount()) return;
        bool ok = false;
        int orderNumber = model->cellUserValue(orderRow, 2).toInt(&ok);
        if (!ok || orderNumber <= 0) {
            orderNumber = model->cellText(orderRow, 2).trimmed().toInt(&ok);
        }
        if (!ok || orderNumber <= 0) return;
        const QString orderUid = AppRuntimeService::resolveOrderUid(orderNumber);
        QString routingError;
        if (!orderUid.trimmed().isEmpty()) {
            if (!ActiveArchiveRoutingLayer::prepareOrderForOpen(orderUid, orderNumber, &orderNumber, &routingError)) {
                QMessageBox::critical(parent, QString::fromUtf8("Помилка"), routingError);
                return;
            }
        }
        table->selectRow(orderRow);
        BackupPerformanceGuard::hold(AppContext::instance().backupManager(), 2500);
        OrderDialog orderDlg(parent, orderNumber);
        orderDlg.setWindowTitle(QString::fromUtf8("Виконаний наряд"));
        orderDlg.openWorkPartsTab();
        orderDlg.exec();
    });

    dlg.exec();
}

} // namespace ReportsDialogSupport
