#include "ui/tabs/reports/InvoicesPage.h"
#include "ui/theme/ThemeManager.h"
#include "ui/tabs/reports/ReportsTableSupport.h"
#include "ui/tabs/reports/ReportsInteractionSupport.h"
#include "ui/tabs/reports/ReportsDialogSupport.h"
#include "ui/common/dialogs/InvoiceDocumentOpenSupport.h"
#include "ui/common/dialogs/PurchasePaymentDialog.h"
#include <QScreen>
#include <QEvent>
#include "ui/common/dialogs/OrderDialog.h"
#include "ui/common/dialogs/ElitInvoicePdfOpen.h"
#include "core/services/ActiveArchiveRoutingLayer.h"
#include "core/services/AccessControlService.h"
#include "core/util/FormatUtils.h"
#include "core/services/InvoiceReportService.h"
#include "runtime/AsyncThreadScope.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableView>
#include <QTableView>
#include "ui/tabs/reports/ReportsTableSupport.h"
#include <QHeaderView>
#include <QLineEdit>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QMenu>
#include <QPointer>

#include <QTimer>
#include <QSettings>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QRegularExpression>
#include <algorithm>

namespace {
class InvoiceSortComboBox final : public QComboBox {
public:
    using QComboBox::QComboBox;
    void showPopup() override {
        ensurePolished();
        auto* popup = view();
        popup->ensurePolished();
        int contentWidth = 0;
        for (int row = 0; row < count(); ++row)
            contentWidth = qMax(contentWidth, popup->fontMetrics().horizontalAdvance(itemText(row)));
        const int padding = popup->fontMetrics().height() * 2;
        popup->setMinimumWidth(qMax(width(), qMax(contentWidth, popup->sizeHintForColumn(0)) + padding));
        QComboBox::showPopup();
    }
};
QString activeInvoiceAccount() { return InvoiceReportService::activeAccount(); }
class InvoiceAgingDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        const auto background = index.data(Qt::BackgroundRole);
        if (!background.isValid()) { QStyledItemDelegate::paint(painter, option, index); return; }
        QStyleOptionViewItem opt(option); initStyleOption(&opt, index);
        painter->save(); painter->setClipRect(opt.rect);
        painter->fillRect(opt.rect, background.value<QBrush>());
        painter->setFont(opt.font); painter->setPen(Qt::black);
        const QRect textRect = opt.rect.adjusted(8, 0, -8, 0);
        painter->drawText(textRect, opt.displayAlignment, opt.fontMetrics.elidedText(opt.text, Qt::ElideRight, textRect.width()));
        painter->setPen(QColor("#cfd6de")); painter->drawLine(opt.rect.bottomLeft(), opt.rect.bottomRight());
        if (opt.state & QStyle::State_Selected) {
            painter->setPen(QPen(QColor("#306aa1"), 2)); painter->drawRect(opt.rect.adjusted(1, 1, -1, -1));
        }
        painter->restore();
    }
};
}

InvoicesPage::InvoicesPage(QWidget* parent) : QWidget(parent) {
    setObjectName("invoicesPage");
    setStyleSheet("QWidget#invoicesPage { background: transparent; border: none; } QLabel { background: transparent; }");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_controls=new QWidget(this);
    m_controls->setObjectName("invoiceControlsHost");
    m_controls->setStyleSheet("#invoiceControlsHost {background:transparent;border:none;} QLabel {background:transparent;}");
    auto* controls=new QVBoxLayout(m_controls);
    controls->setContentsMargins(0,0,0,0);
    layout->addWidget(m_controls);
    auto* bar = new QHBoxLayout;
    m_search = new QLineEdit(this);
    m_search->setObjectName("invoiceSearch");
    m_search->setPlaceholderText(QString::fromUtf8("Пошук: № наряду, номер авто, марка / модель, накладна, магазин"));
    m_search->setToolTip(m_search->placeholderText());
    m_search->setClearButtonEnabled(true);
    m_statusFilter = new QComboBox(this);
    m_statusFilter->setObjectName("invoiceStatusFilter");
    m_statusFilter->addItems({QString::fromUtf8("Усі накладні"), QString::fromUtf8("Не сплачені"), QString::fromUtf8("Сплачені")});
    auto* refreshButton = new QPushButton(QString::fromUtf8("Оновити"), this);
    refreshButton->setObjectName("invoiceRefresh");
    m_sort=new InvoiceSortComboBox(this);m_sort->setObjectName("invoiceSort");
    using Sort=InvoiceRegistryService::ReportSort;
    m_sort->addItem(QString::fromUtf8("За датою ↑"),int(Sort::DateAscending));
    m_sort->addItem(QString::fromUtf8("За датою ↓"),int(Sort::Date));
    m_sort->addItem(QString::fromUtf8("За постачальником"),int(Sort::Supplier));
    m_sort->addItem(QString::fromUtf8("Найбільший термін"),int(Sort::LongestOverdue));
    m_sort->addItem(QString::fromUtf8("Без боргів"),int(Sort::NoDebt));
    const int savedSort=QSettings().value("reports/invoices/sortMode",int(Sort::DateAscending)).toInt();
    m_sort->setCurrentIndex(qMax(0,m_sort->findData(savedSort)));
    bar->setSpacing(12);
    bar->addWidget(m_search, 1); bar->addWidget(m_statusFilter);bar->addWidget(m_sort);bar->addWidget(refreshButton);
    controls->addLayout(bar);
    m_status = new QLabel(this); m_status->setWordWrap(true);
    m_status->setObjectName("invoiceSummary"); controls->addWidget(m_status);
    m_table = new QTableView(this); m_table->setObjectName("invoicesTable");
    m_model = new ReportsTableSupport::ReportsSnapshotTableModel(this);
    m_table->setModel(m_model); ReportsTableSupport::setTableBaseStyle(m_table);
    m_table->setItemDelegate(new InvoiceAgingDelegate(m_table));
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_table->horizontalHeader()->setMinimumSectionSize(20);
    m_table->horizontalHeader()->setSectionsMovable(true);
    m_table->verticalHeader()->setDefaultSectionSize(46);
    layout->addWidget(m_table, 1);
    connect(refreshButton, &QPushButton::clicked, this, [this] { refresh(); });
    connect(m_search, &QLineEdit::textChanged, this, [this] { render(); });
    connect(m_statusFilter, &QComboBox::currentIndexChanged, this, [this] { render(); });
    connect(m_sort,&QComboBox::currentIndexChanged,this,[this]{
        QSettings().setValue("reports/invoices/sortMode",m_sort->currentData());
        render();
    });
    connect(m_table, &QTableView::clicked, this, [this](const QModelIndex& i) { activate(i.row(), i.column()); });
    render();
    auto* header=m_table->horizontalHeader();
    header->restoreState(QSettings().value("reports/invoices/header_v2").toByteArray());
    header->setSectionResizeMode(QHeaderView::Fixed);
    m_table->viewport()->installEventFilter(this);
    fitColumns();
    auto persist=[header]{QSettings().setValue("reports/invoices/header_v2",header->saveState());};
    connect(header,&QHeaderView::sectionMoved,this,persist);
    auto* agingTimer = new QTimer(this); agingTimer->setInterval(60000);
    connect(agingTimer, &QTimer::timeout, this, [this] { if (isVisible()) render(); }); agingTimer->start();
}
InvoicesPage::~InvoicesPage() { AsyncTaskHandle::cancel(m_cancel); }
bool InvoicesPage::eventFilter(QObject* watched, QEvent* event) {
    if(watched==m_table->viewport() && event->type()==QEvent::Resize) fitColumns();
    return QWidget::eventFilter(watched,event);
}
void InvoicesPage::fitColumns() {
    const QList<int> weights{9,24,13,10,17,15,12};
    auto* header=m_table->horizontalHeader();
    const int width=m_table->viewport()->width();
    if(header->count()!=weights.size() || width<=0) return;
    const QSignalBlocker blocker(header);
    int used=0;
    for(int column=0;column<weights.size();++column) {
        const int size=column==weights.size()-1?width-used:width*weights[column]/100;
        header->resizeSection(column,size);used+=size;
    }
}
void InvoicesPage::selectInvoice(const QString& key) {
    m_selectedInvoice=key;m_statusFilter->setCurrentIndex(0);m_search->clear();refresh();
}
void InvoicesPage::payInvoiceByKey(const QString& key) {
    QString error;const auto invoices=InvoiceReportService::localInvoices(&error);
    if(!error.isEmpty()){QMessageBox::warning(this,QString::fromUtf8("Накладні"),error);return;}
    for(const auto& invoice:invoices)if(invoice.key==key){
        if(openInvoicePurchasePayment(this,invoice)){refresh();if(paymentChanged)paymentChanged();}
        return;
    }
    QMessageBox::warning(this,QString::fromUtf8("Накладні"),QString::fromUtf8("Накладну не знайдено. Оновіть звіт."));
}

void InvoicesPage::refresh() {
    AsyncTaskHandle::cancel(m_cancel);
    const auto generation = ++m_generation;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    m_table->setEnabled(false);
    m_status->setText(QString::fromUtf8("Завантаження накладних…"));
    const auto context = InvoiceReportService::context();
    if (!context.allowed) {
        m_invoices.clear(); render(); m_status->setText(QString::fromUtf8("Недостатньо прав для перегляду накладних.")); return;
    }
    const auto scope=context.scope;
    const auto account=context.account;
    if(m_loadedScope!=scope || m_loadedAccount!=account){m_invoices.clear();m_loadedScope=scope;m_loadedAccount=account;render();}
    QPointer<InvoicesPage> self(this);
    auto* worker = AsyncThreadScope::startWorker(this, "invoice_registry", m_cancel,
        [self, context, generation, scope, account](const auto& cancel) {
            QString error;
            QVector<RegisteredInvoice> invoices;
            invoices = InvoiceReportService::load(context, &error);
            if (cancel->load() || !self) return;
            QMetaObject::invokeMethod(self, [self, cancel, generation, context, invoices = std::move(invoices), error] {
                if (!self || cancel->load() || generation != self->m_generation) return;
                if (!InvoiceReportService::isCurrent(context)) {
                    self->m_invoices.clear(); self->render(); return;
                }
                self->m_invoices = invoices; self->render(); self->m_table->setEnabled(error.isEmpty());
                if (!error.isEmpty()) self->m_status->setText(QString::fromUtf8("Не вдалося завантажити накладні: %1").arg(error));
            }, Qt::QueuedConnection);
        });
    worker->start();
}

void InvoicesPage::setPeriod(const QDate& from, const QDate& to) {
    if(m_from==from && m_to==to)return;
    m_from=from;m_to=to;render();
}

void InvoicesPage::render() {
    m_status->setStyleSheet(QStringLiteral("color:%1;padding:0px;font-size:9pt;")
        .arg(ThemeManager::currentTheme()=="light"?"#111111":"#ffffff"));
    if(m_loadedScope!=AccessControlService::currentScopeKey() || m_loadedAccount!=activeInvoiceAccount()) m_invoices.clear();
    for(const auto& invoice:m_invoices)if(invoice.key==m_selectedInvoice && invoice.date.isValid()) {
        bool expanded=false;
        if(m_from.isValid() && invoice.date<m_from){m_from=invoice.date;expanded=true;}
        if(m_to.isValid() && invoice.date>m_to){m_to=invoice.date;expanded=true;}
        if(expanded && periodExpanded)periodExpanded(m_from,m_to);
        break;
    }
    m_visible.clear();
    QVector<ReportsTableSupport::ReportsSnapshotTableModel::Row> rows;
    qint64 remaining = 0;
    int paidCount=0,unpaidCount=0;
    const auto today=QDate::currentDate();
    const auto sorted=InvoiceRegistryService::sortedForReport(m_invoices,
        static_cast<InvoiceRegistryService::ReportSort>(m_sort->currentData().toInt()),today);
    const auto headerState=m_table->horizontalHeader()->saveState();
    for (const auto& invoice : sorted) {
        if((m_from.isValid() || m_to.isValid()) && !invoice.date.isValid())continue;
        if(m_from.isValid() && invoice.date<m_from)continue;
        if(m_to.isValid() && invoice.date>m_to)continue;
        QStringList orders; for (const auto& display : invoice.orders) orders << display;
        for(const auto& part:invoice.parts) if(part.source=="inventory") {orders<<QString::fromUtf8("Склад СТО");break;}
        if ((m_statusFilter->currentIndex() == 1 && invoice.paid()) || (m_statusFilter->currentIndex() == 2 && !invoice.paid())) continue;
        const QString searchable = (invoice.number + " " + invoice.supplier + " " + orders.join(" ")
            + " " + invoice.vehicles.join(" ")).toCaseFolded();
        const QString query = m_search->text().simplified().toCaseFolded();
        bool matches = true;
        for (const auto& word : query.split(' ', Qt::SkipEmptyParts)) matches &= searchable.contains(word);
        // A registration plate may be entered with or without spaces.
        QString compact = searchable;
        compact.remove(QRegularExpression("\\s+"));
        if (!matches && !compact.contains(QString(query).remove(' '))) continue;
        m_visible.append(invoice); remaining += invoice.remainingCents();
        invoice.paid()?++paidCount:++unpaidCount;
        ReportsTableSupport::ReportsSnapshotTableModel::Row row;
        if (invoice.remainingCents() > 0 && invoice.date.isValid()) {
            const auto days = invoice.date.daysTo(QDate::currentDate());
            if (days > 5) row.background = QColor("#ff9f9f");
            else if (days >= 3) row.background = QColor("#ffe28a");
        }
        const QString status = invoice.apportioned ? QString::fromUtf8("Розподіл · розрахункова оплата") : invoice.amountMismatch ? QString::fromUtf8("Не сплачена · звірити суми")
            : invoice.paid() ? QString::fromUtf8("Сплачена") : invoice.paidCents > 0 ? QString::fromUtf8("Частково сплачена") : QString::fromUtf8("Не сплачена");
        for (const auto& text : QStringList{invoice.date.isValid() ? invoice.date.toString("dd.MM.yy") : QString(),
            invoice.number.isEmpty() ? QString::fromUtf8("Без номера") : invoice.number,
            invoice.supplier, orders.join(", "), FormatUtils::money(invoice.totalCents / 100.0, 2), status,
            invoice.paidAt.isValid()?invoice.paidAt.toString("dd.MM.yy"):invoice.paid()?QString::fromUtf8("Не зафіксовано"):QString()}) {
            ReportsTableSupport::ReportsSnapshotTableModel::Cell cell;
            cell.text = text;
            const int column=row.cells.size();
            cell.link = column==1 || column==5 || (column==3 && (!invoice.orders.isEmpty()
                || std::any_of(invoice.parts.cbegin(), invoice.parts.cend(), [](const auto& part){return part.source=="inventory";})));
            cell.tooltip = QString::fromUtf8("Позицій: %1. Сплачено: %2 грн. Залишок: %3 грн.")
                .arg(invoice.parts.size()).arg(FormatUtils::money(invoice.paidCents / 100.0, 2), FormatUtils::money(invoice.remainingCents() / 100.0, 2));
            row.cells.append(cell);
        }
        row.cells[4].alignment = Qt::AlignRight | Qt::AlignVCenter;
        rows.append(row);
    }
    m_model->setSnapshot({QString::fromUtf8("Дата"), QString::fromUtf8("Накладна №"), QString::fromUtf8("Магазин"),
        QString::fromUtf8("Наряди"), QString::fromUtf8("Сума накладної, грн"), QString::fromUtf8("Статус"),QString::fromUtf8("Дата сплати")}, rows);
    if(!headerState.isEmpty())m_table->horizontalHeader()->restoreState(headerState);
    fitColumns();
    if(!m_selectedInvoice.isEmpty())for(int row=0;row<m_visible.size();++row)if(m_visible[row].key==m_selectedInvoice) {
        m_table->selectRow(row);m_table->scrollTo(m_model->index(row,0));m_selectedInvoice.clear();break;
    }
    if(metricsChanged)metricsChanged(m_visible.size(),paidCount,unpaidCount,remaining);
    m_status->setText(m_visible.isEmpty() ? QString::fromUtf8("Накладних за вибраними умовами немає.")
        : QString::fromUtf8("Накладних: %1 | Сплачено: %2 | Не сплачено: %3 | До сплати: %4 грн").arg(m_visible.size()).arg(paidCount).arg(unpaidCount).arg(FormatUtils::money(remaining / 100.0, 2)));
}

void InvoicesPage::activate(int row, int column) {
    if(m_loadedScope!=AccessControlService::currentScopeKey() || m_loadedAccount!=activeInvoiceAccount()){m_invoices.clear();render();return;}
    if (row < 0 || row >= m_visible.size()) return;
    const auto invoice = m_visible[row];
    if(column!=1 && column!=3 && column!=5) return;
    if (column==1 && !invoice.remoteIdentity.isEmpty()) {
        openElitInvoicePdf(this, invoice.remoteIdentity, [this,invoice](const QString& path) {showInvoice(invoice,true,path);});
        return;
    }
    if(column==5) {
        if(openInvoicePurchasePayment(this,invoice)){refresh();if(paymentChanged)paymentChanged();}
        return;
    }
    if (column == 1 && !invoice.filePath.isEmpty()) {
        InvoiceDocumentOpenSupport::prepareAsync(this,invoice.filePath,invoice.number,
            invoice.date.toString("dd.MM.yy"),invoice.supplier,[this,invoice](const QString& path,const QString& error) {
                if(path.isEmpty()){QMessageBox::warning(this,QString::fromUtf8("Накладна"),error);return;}
                showInvoice(invoice,true,path);
            });
        return;
    }
    if (column == 3 && !invoice.orders.isEmpty()) {
        int number = invoice.orders.firstKey();
        if (invoice.orders.size() > 1) {
            QMenu menu(this);
            for (auto it = invoice.orders.cbegin(); it != invoice.orders.cend(); ++it) menu.addAction(it.value())->setData(it.key());
            const auto* selected = menu.exec(QCursor::pos()); if (!selected) return;
            number = selected->data().toInt();
        }
        QString uid; for (const auto& part : invoice.parts) if (part.orderNumber == number) { uid = part.orderUid; break; }
        QString error;
        if (!ActiveArchiveRoutingLayer::prepareOrderForOpen(uid, number, &number, &error)) {
            QMessageBox::warning(this, QString::fromUtf8("Наряд"), error); return;
        }
        OrderDialog dialog(this, number);
        dialog.openWorkPartsTab();
        connect(&dialog, &OrderDialog::purchasePaymentsChanged, this, [this] { refresh(); if (paymentChanged) paymentChanged(); });
        dialog.exec(); refresh(); return;
    }
    // A stock-only invoice has no order to open, but must still expose its
    // warehouse locations and read-only supplier payment details.
    showInvoice(invoice,column!=3);
}

void InvoicesPage::showInvoice(const RegisteredInvoice& invoice, bool invoiceOnly, const QString& pdfPath) {
    QDialog dialog(this); dialog.setWindowTitle(QString::fromUtf8("Накладна №%1").arg(invoice.number));
    dialog.setObjectName("invoicePreviewDialog");
    QFont font = dialog.font();
    font.setPointSizeF((font.pointSizeF() > 0 ? font.pointSizeF() : 12) + 2);
    dialog.setFont(font);
    dialog.setStyleSheet("QDialog{background:#343b43;} QLabel{color:white;} QLineEdit{background:white;color:black;padding:8px;border-radius:5px;}");
    auto* layout = new QVBoxLayout(&dialog);
    auto* info = new QLabel(QString::fromUtf8("%1\nСума накладної: %2 грн\nСплачено: %3 грн\nДо сплати: %4 грн")
        .arg(invoice.supplier, FormatUtils::money(invoice.totalCents / 100.0, 2),
             FormatUtils::money(invoice.paidCents / 100.0, 2), FormatUtils::money(invoice.remainingCents() / 100.0, 2)), &dialog);
    info->setWordWrap(true);info->setMinimumHeight(info->fontMetrics().lineSpacing()*4); layout->addWidget(info);
    if(!pdfPath.isEmpty())layout->addWidget(ReportsDialogSupport::createInvoicePdfPreview(&dialog,pdfPath),1);
    if(invoiceOnly && invoice.parts.isEmpty() && !invoice.paid()) {
        auto* hint=new QLabel(QString::fromUtf8("Немає пов’язаних закупівель для локальної оплати. Статус Еліт оновлюється через перевірку оплати постачальника."),&dialog);
        hint->setWordWrap(true);layout->addWidget(hint);
    }
    if(invoice.apportioned) {
        auto* warning=new QLabel(QString::fromUtf8("Закупівлю розподілено між накладними. Локальні оплати показано пропорційно лише для порівняння. Сплачуйте через первинний облік закупівлі."),&dialog);
        warning->setWordWrap(true);layout->addWidget(warning);
    }
    if(!invoice.distribution.isEmpty()) {
          auto* caption=new QLabel(QString::fromUtf8("Розподіл надходжень: видано в наряди та залишок на складі. Вартість розподілу не є сумою платежу."),&dialog);
          caption->setWordWrap(true);layout->addWidget(caption);
        auto* allocation=new QTableView(&dialog);
        auto* allocationModel=new ReportsTableSupport::ReportsSnapshotTableModel(allocation);allocation->setModel(allocationModel);
        QVector<ReportsTableSupport::ReportsSnapshotTableModel::Row> allocationRows;allocation->setObjectName("invoiceDistribution");
        const QStringList allocationHeaders={QString::fromUtf8("Закупівля"),QString::fromUtf8("Наряд / залишок"),QString::fromUtf8("Кількість"),QString::fromUtf8("Вартість, грн")};
        allocation->setEditTriggers(QAbstractItemView::NoEditTriggers);allocation->verticalHeader()->hide();
        allocation->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        allocation->setStyleSheet("QTableView{background:#f5f7f8;color:#17232a;font:10pt 'Segoe UI';} QHeaderView::section{background:#dfe6eb;color:#17232a;font:10pt 'Segoe UI';padding:4px;}");
        for(int row=0;row<invoice.distribution.size();++row) {
            const auto& slice=invoice.distribution[row];
            QString name=QString::fromUtf8("#%1").arg(slice.purchaseId);
            for(const auto& part:invoice.parts)if(part.source=="inventory" && part.id==slice.purchaseId){name+=" · "+part.name;break;}
            const QStringList values={name,slice.partId?QString::fromUtf8("Наряд №%1").arg(invoice.orders.value(slice.orderNumber,QString::number(slice.orderNumber))):QString::fromUtf8("Залишок на складі"),QString::number(slice.quantity),FormatUtils::money(slice.total/100.0,2)};
            ReportsTableSupport::ReportsSnapshotTableModel::Row record;
            for(const auto& text:values){ReportsTableSupport::ReportsSnapshotTableModel::Cell value;value.text=text;value.tooltip=text;record.cells.append(value);}
            allocationRows.append(record);
        }
        allocationModel->setSnapshot(allocationHeaders,allocationRows);
        allocation->setMinimumHeight(100);allocation->setMaximumHeight(260);layout->addWidget(allocation);
    }
    auto* orderCaption=new QLabel(QString::fromUtf8("Сплатити за наряд"), &dialog);
    layout->addWidget(orderCaption);orderCaption->setVisible(!invoiceOnly);
    auto* orderInput = new QLineEdit(&dialog);
    orderInput->setObjectName("invoiceOrderNumbers");
    orderInput->setPlaceholderText(QString::fromUtf8("Номери нарядів через кому"));
    layout->addWidget(orderInput);
    orderInput->setVisible(!invoiceOnly);
    const bool hasStock=std::any_of(invoice.parts.cbegin(),invoice.parts.cend(),[](const auto& p){return p.source=="inventory";});
    if(hasStock) {
        auto* stock=new QComboBox(&dialog);stock->setObjectName("invoiceStockPurchases");
        for(const auto& part:invoice.parts) if(part.source=="inventory") stock->addItem(part.name+QString::fromUtf8(" · #%1").arg(part.id),part.id);
        layout->addWidget(stock);
        auto* open=new QPushButton(QString::fromUtf8("Перейти до складу"),&dialog);open->setObjectName("invoiceOpenStock");layout->addWidget(open);
        const auto scope=m_loadedScope;
        connect(open,&QPushButton::clicked,&dialog,[&,stock,scope]{
            const auto id=stock->currentData().toLongLong();dialog.accept();
            QTimer::singleShot(0,this,[this,id,scope]{if(scope==AccessControlService::currentScopeKey() && inventoryRequested)inventoryRequested(id);});
        });
        auto* hint=new QLabel(QString::fromUtf8("Складські позиції оплачуються у формі складу. Тут показано локальний стан оплат."),&dialog);hint->setWordWrap(true);layout->addWidget(hint);
    }
    if (invoice.amountMismatch) {
        auto* warning = new QLabel(QString::fromUtf8("Суми в обліку відрізняються від підтверджених сум накладної. Спочатку виконайте звірку."), &dialog);
        warning->setWordWrap(true); layout->addWidget(warning);
    }
    auto* buttons = new QDialogButtonBox(&dialog);
    auto* close = buttons->addButton(QString::fromUtf8("Закрити"), QDialogButtonBox::RejectRole);
    auto* pay = buttons->addButton(QString::fromUtf8("Сплатити накладну"), QDialogButtonBox::ActionRole);
    pay->setObjectName("payInvoice");
    auto* paySelected = buttons->addButton(QString::fromUtf8("Сплатити за наряди"), QDialogButtonBox::ActionRole);
    paySelected->setObjectName("payInvoiceOrders");
    paySelected->setVisible(!invoiceOnly);
    pay->setEnabled(InvoiceReportService::canPayFullInvoice(invoice));
    paySelected->setEnabled(pay->isEnabled() && !invoice.orders.isEmpty());
    orderInput->setEnabled(!hasStock && !invoice.orders.isEmpty());
    for (auto* button : {close, pay, paySelected}) { button->setMinimumHeight(48); button->setCursor(Qt::PointingHandCursor); }
    connect(paySelected, &QPushButton::clicked, &dialog, [this, &dialog, invoice, orderInput] {
        QVector<int> selected;
        for (const QString& token : orderInput->text().split(',')) {
            const QString number = token.trimmed();
            QVector<int> matches;
            for (auto it = invoice.orders.cbegin(); it != invoice.orders.cend(); ++it)
                if (it.value() == number) matches.append(it.key());
            if (number.isEmpty() || matches.size() != 1) {
                QMessageBox::warning(&dialog, QString::fromUtf8("Оплата"), QString::fromUtf8("Вкажіть однозначні номери нарядів цієї накладної через кому.")); return;
            }
            if (!selected.contains(matches.first())) selected.append(matches.first());
        }
        qint64 amount = 0;
        for (const auto& part : invoice.parts) if (selected.contains(part.orderNumber)) amount += part.remainingCents();
        if (amount <= 0) { QMessageBox::information(&dialog, QString::fromUtf8("Оплата"), QString::fromUtf8("Вибрані наряди вже сплачені.")); return; }
        QVector<PurchaseDebt> debts;
        for (const auto& part : invoice.parts) if (selected.contains(part.orderNumber)) debts << part;
        if (!chooseGroupedPurchasePayment(&dialog, QString::fromUtf8("Оплата вибраних нарядів накладної №%1").arg(invoice.number), debts, &amount)) return;
        QString error;
        if (!InvoiceReportService::payOrders(invoice, selected, &error, amount)) {
            QMessageBox::warning(&dialog, QString::fromUtf8("Оплата"), error); return;
        }
        showPurchasePaymentSaved(&dialog, amount);
        dialog.accept(); refresh(); if (paymentChanged) paymentChanged();
    });
    layout->addWidget(buttons);
    connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(pay, &QPushButton::clicked, &dialog, [this, &dialog, invoice] {
        if (!openInvoicePurchasePayment(&dialog, invoice)) return;
        dialog.accept(); refresh(); if (paymentChanged) paymentChanged();
    });
    dialog.setMinimumWidth(480);
    if(!pdfPath.isEmpty())dialog.resize(dialog.screen()->availableGeometry().size()*0.85);
    dialog.exec();
}
