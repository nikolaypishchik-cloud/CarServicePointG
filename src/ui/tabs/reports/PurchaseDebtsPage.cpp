#include "ui/tabs/reports/PurchaseDebtsPage.h"
#include "ui/common/dialogs/PurchasePaymentDialog.h"
#include "ui/common/dialogs/ElitInvoicePdfOpen.h"
#include "core/services/PartsQueryService.h"
#include "core/util/FormatUtils.h"
#include "core/util/StoreNameNormalizer.h"
#include <QMap>
#include "core/services/SupplierDebtReadService.h"
#include <QVBoxLayout>
#include <QTableView>
#include "ui/tabs/reports/ReportsTableSupport.h"
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QDate>
#include <QTextBrowser>
#include <QComboBox>
#include <QSignalBlocker>
#include <QDialog>
#include <QScrollArea>
#include <QDoubleSpinBox>
#include <QMessageBox>
#include <QtMath>
#include "core/services/AccessControlService.h"
#include <algorithm>
#include "core/services/DebtAnalysisPdfService.h"
#include "core/services/storage/StorageDirectoriesGuard.h"
#include <QDir>
#include <QDateTime>
#include <QDesktopServices>
#include <QUrl>
#include <QUuid>
#include <QLineEdit>
#include <QTabBar>
#include <QTimer>
#include <QScreen>
#include <QFileInfo>
#include "ui/tabs/reports/ReportsAsyncSupport.h"
#include "ui/tabs/orders/CurrentOrdersCoordinator.h"
#include <QRegularExpression>
#include "core/services/SupplierInvoiceLedger.h"
#include "runtime/AsyncThreadScope.h"
#include <QAbstractTextDocumentLayout>
#include <QTextDocument>

namespace {
class DebtSummaryBrowser final : public QTextBrowser {
public:
    explicit DebtSummaryBrowser(QWidget* parent) : QTextBrowser(parent) {
        parent->installEventFilter(this);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged,
                this, [this] { fitContent(); });
    }
    void fitContent() {
        // Reserve space for debt rows on short screens.
        const int limit = qMax(80, window()->height() - 240);
        const int chrome = height() - viewport()->height();
        const int wanted = qCeil(document()->size().height()) + chrome + 2;
        const int target = qBound(60, wanted, limit);
        if (height() != target) setFixedHeight(target);
    }
protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Resize)
            QTimer::singleShot(0, this, [this] { fitContent(); });
        return QTextBrowser::eventFilter(watched, event);
    }
    void resizeEvent(QResizeEvent* event) override {
        QTextBrowser::resizeEvent(event);
        QTimer::singleShot(0, this, [this] { fitContent(); });
    }
};
}

namespace {
QString sharedDebtSummary(const QVector<PurchaseDebt>& debts,int gap,
                          const QMap<QString,SupplierDebtReconciliation>& facts,
                          const SupplierInvoiceLedger::Summary& result) {
    auto html=PurchaseDebtsPage::summaryHtml(debts,gap,facts,result.remote?&result.totals:nullptr);
    if(result.unresolved) html+=QString::fromUtf8("<p>Є незвірені накладні Еліт. Загальний борг ще не остаточний.</p>");
    return html;
}
}

void PurchaseDebtsPage::showDetails(QWidget* parent) {
    QDialog dialog(parent);
    dialog.setObjectName("supplierDebtDetailsDialog");
    dialog.setWindowTitle(QString::fromUtf8("Деталізація боргів"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* summary = new DebtSummaryBrowser(&dialog);
    summary->setObjectName("supplierDebtDetailsSummary");
    summary->setStyleSheet("QTextBrowser { background:#28323c; color:#ffffff; border:1px solid #7e8b98; padding:8px; }");
    layout->addWidget(summary);
    auto* page = new PurchaseDebtsPage(&dialog);
    page->summaryChanged = [summary](const QString& html) {
        summary->setHtml(html);
        summary->fitContent();
    };
    layout->addWidget(page, 1);
    auto* close = new QPushButton(QString::fromUtf8("Закрити"), &dialog);
    layout->addWidget(close);
    connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    QTimer::singleShot(0,page,&PurchaseDebtsPage::refresh);
    const QRect available = dialog.screen()->availableGeometry().adjusted(32, 32, -32, -32);
    // Reference 1.png: approximately 1950 x 1100 client pixels at Windows 200%.
    // Qt uses logical pixels, so this also preserves the physical UI scale on other screens.
    dialog.resize(qMin(975, available.width()), qMin(550, available.height()));
    dialog.exec();
}

PurchaseDebtsPage::PurchaseDebtsPage(QWidget* parent) : QWidget(parent) {
    setObjectName("purchaseDebtsPage");
    auto* layout=new QVBoxLayout(this); layout->setContentsMargins(0,0,0,0);
    auto* toolbar=new QHBoxLayout;
    m_status=new QLabel(this); m_status->setWordWrap(true);
    m_status->setStyleSheet("color:#ffffff; background:transparent;");
    toolbar->addWidget(m_status,1);
    auto* refreshButton=new QPushButton(QString::fromUtf8("Оновити"),this); toolbar->addWidget(refreshButton);
    m_reconcile=new QPushButton(QString::fromUtf8("Звірка боргів"),this);
    m_reconcile->setObjectName("reconcileSupplierDebts"); toolbar->addWidget(m_reconcile);
    m_reconcile->setVisible(SupplierDebtReconciliationService::canReconcile());
    layout->addLayout(toolbar);
    m_table=new QTableView(this); m_model=new ReportsTableSupport::ReportsSnapshotTableModel(m_table);m_table->setModel(m_model); m_table->setObjectName("purchaseDebtsTable");
    m_model->setSnapshot({QString::fromUtf8("Дата"),QString::fromUtf8("Тип"),QString::fromUtf8("Наряд"),
        QString::fromUtf8("Запчастина / матеріал"),QString::fromUtf8("Кількість"),QString::fromUtf8("Сума закупівлі"),
        QString::fromUtf8("Сплачено"),QString::fromUtf8("Борг"),QString::fromUtf8("Сплатити до")},{});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection); m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3,QHeaderView::Stretch);
    m_table->setStyleSheet("QTableView { background:#ffffff; color:#111827; gridline-color:#cbd5e1; selection-background-color:#dce8fb; selection-color:#111827; }"
                          "QHeaderView::section { background:#e5edf5; color:#111827; padding:10px; }");
    layout->addWidget(m_table,1);
    connect(refreshButton,&QPushButton::clicked,this,[this](){refresh();});
    connect(m_reconcile,&QPushButton::clicked,this,[this](){reconcileDebts();});
    connect(m_table,&QTableView::clicked,this,[this](const QModelIndex& index){
        const auto key=m_model->index(index.row(),0);
        if(index.column()==3 && key.data(Qt::UserRole+1).toString()=="remote_invoice")
            openElitInvoicePdf(this,key.data(Qt::UserRole+2).toString());
    });
    connect(m_table,&QTableView::doubleClicked,this,[this](const QModelIndex& index){
        const auto key=m_model->index(index.row(),0);
        if(!key.data(Qt::UserRole).isValid()) return;
        if(openPurchasePayment(this,key.data(Qt::UserRole+1).toString(),key.data(Qt::UserRole).toLongLong(),key.data(Qt::UserRole+2).toString())) {
            refresh(); if(paymentChanged) paymentChanged();
        }
    });
}
void PurchaseDebtsPage::refresh() {
    m_reconcile->setVisible(SupplierDebtReconciliationService::canReconcile());
    m_table->clearSpans(); m_model->setSnapshot(m_model->headers(),{});
    m_status->setText(QString::fromUtf8("Завантаження боргів…"));
    setProperty("debtDataLoading",true);
    setProperty("debtLoadError",QString());
    if(summaryChanged) summaryChanged(QString());
    if(m_worker) {
        AsyncTaskHandle::cancel(m_cancel);
        m_refreshQueued=true;
        return;
    }
    PartsQueryService::invalidateCache();
    const auto readTask=SupplierDebtReadService::prepare();
    const auto scopeKey=AccessControlService::currentScopeKey();
    const auto scope=AccessControlService::currentOrderScope();
    const int gap=qCeil(logicalDpiX()*7.0/25.4);
    struct Result { QVector<PurchaseDebt> debts; QString summary,error; };
    auto result=std::make_shared<Result>();
    auto cancel=std::make_shared<std::atomic_bool>(false); m_cancel=cancel;
    auto* worker=AsyncThreadScope::startWorker(this,"purchase_debts",cancel,[result,cancel,readTask,gap] {
        const auto data=readTask(*cancel);
        result->debts=data.debts;result->error=data.error;
        if(data.error.isEmpty() && !cancel->load())result->summary=data.restricted
            ? PurchaseDebtsPage::summaryHtml(data.debts,gap)
            : sharedDebtSummary(data.debts,gap,data.reconciliations,data.ledger);
    });
    m_worker=worker;
    connect(worker,&QThread::finished,this,[this,result,cancel,scopeKey] {
        m_worker=nullptr;
        if(m_refreshQueued) { m_refreshQueued=false; refresh(); return; }
        setProperty("debtDataLoading",false);
        if(scopeKey!=AccessControlService::currentScopeKey()) return;
        if(cancel->load()) result->error=QString::fromUtf8("Завантаження боргів скасовано.");
        applyDebts(result->debts,result->summary,result->error);
    });
    worker->start();
}

void PurchaseDebtsPage::applyDebts(const QVector<PurchaseDebt>& debts,const QString& summary,const QString& error) {
    setProperty("debtLoadError",error);
    m_table->clearSpans(); m_model->setSnapshot(m_model->headers(),{});
    if(!error.isEmpty()) {
        m_status->setText(QString::fromUtf8("Не вдалося завантажити борги: ")+error);
        if(summaryChanged) summaryChanged(QString());
        return;
    }
    if(summaryChanged) summaryChanged(summary);
    QVector<ReportsTableSupport::ReportsSnapshotTableModel::Row> rows;
    qint64 total=0;
    QString group;
    int groupRow=-1;
    qint64 groupTotal=0;
    auto finishGroup=[&](){
        if(groupRow>=0) rows[groupRow].cells[0].text=QString::fromUtf8("Магазин: %1 | Борг: %2 грн")
            .arg(group,FormatUtils::money(groupTotal/100.0,2));
    };
    for(const auto& debt:debts) {
        const QString supplier=debt.supplier.trimmed().isEmpty() ? QString::fromUtf8("Без магазину") : debt.supplier;
        if(groupRow<0 || group!=supplier) {
            finishGroup(); group=supplier; groupTotal=0; groupRow=rows.size();
            ReportsTableSupport::ReportsSnapshotTableModel::Row heading;heading.spanAllColumns=true;heading.background=QColor("#e5edf5");
            heading.cells.resize(9);heading.cells[0].bold=true;rows.append(heading);
        }
        total+=debt.remainingCents(); groupTotal+=debt.remainingCents();
        ReportsTableSupport::ReportsSnapshotTableModel::Row record;
        const QDate date=PartsQueryService::parseBusinessDate(debt.date);
        const QStringList cells={date.isValid()?date.toString("dd.MM.yy"):debt.date,
            debt.source=="remote_invoice"?QString::fromUtf8("Накладна Еліт"):debt.source=="part"?QString::fromUtf8("Запчастина"):QString::fromUtf8("Матеріал"),
            debt.orderNumber ? QString::number(debt.orderNumber):QString(),debt.name,QString::number(debt.quantity),
            FormatUtils::money(debt.totalCents/100.0,2),FormatUtils::money(debt.paidCents/100.0,2),FormatUtils::money(debt.remainingCents()/100.0,2),
            debt.dueDate.toString("dd.MM.yy")};
        for(int col=0;col<cells.size();++col) {
            ReportsTableSupport::ReportsSnapshotTableModel::Cell value;value.text=cells[col];
            if(col>=4)value.alignment=Qt::AlignRight|Qt::AlignVCenter;
            if(col==7)value.foreground=QColor(debt.paidCents>0?"#a34b00":"#b91c1c");
            if(col==8 && debt.dueDate.isValid() && debt.dueDate<QDate::currentDate())value.tooltip=QString::fromUtf8("Термін оплати минув");
            if(col==3 && debt.source=="remote_invoice") {value.link=true;value.foreground=QColor("#21627e");value.tooltip=QString::fromUtf8("Відкрити PDF накладної; відсутній файл буде завантажено з eCat");}
            record.cells.append(value);
        }
        if(debt.source!="remote_invoice")record.cells[0].userValue=debt.id;
        record.cells[0].extraRoles={{Qt::UserRole+1,debt.source},{Qt::UserRole+2,debt.orderUid}};
        rows.append(record);
    }
    finishGroup();m_model->setSnapshot(m_model->headers(),rows);
    for(int row:m_model->spanRows())m_table->setSpan(row,0,1,9);
    m_table->resizeRowsToContents();
    m_status->setText(debts.isEmpty()?QString::fromUtf8("Непогашених боргів немає."):
        QString::fromUtf8("Усі непогашені придбання: %1 | Борг: %2 грн").arg(debts.size()).arg(FormatUtils::money(total/100.0,2)));
}

void PurchaseDebtsPage::reconcileDebts() {
    if(m_reconciliationPending || !SupplierDebtReconciliationService::canReconcile()) return;
    m_reconciliationPending=true;
    m_reconcile->setEnabled(false);
    PartsQueryService::invalidateCache();
    const auto readTask=SupplierDebtReadService::prepareReconciliation();
    const auto scopeKey=AccessControlService::currentScopeKey();
    auto result=std::make_shared<SupplierDebtReadService::ReconciliationInput>();
    auto cancel=std::make_shared<std::atomic_bool>(false);
    auto* worker=AsyncThreadScope::startWorker(this,"supplier_reconciliation",cancel,[readTask,result,cancel] {
        *result=readTask(*cancel);
    });
    connect(worker,&QThread::finished,this,[this,result,cancel,scopeKey] {
        const QPointer<PurchaseDebtsPage> guard(this);
        if(!cancel->load() && scopeKey==AccessControlService::currentScopeKey()
            && SupplierDebtReconciliationService::canReconcile())showReconciliation(*result);
        if(!guard)return;
        m_reconciliationPending=false;
        m_reconcile->setEnabled(true);
    });
    worker->start();
}

void PurchaseDebtsPage::showReconciliation(const SupplierDebtReadService::ReconciliationInput& input) {
    QString error=input.error;
    const auto& names=input.names;const auto& debts=input.debts;
    const auto& previous=input.previous;const auto& summary=input.summary;
    if(!error.isEmpty()) { QMessageBox::warning(this,QString::fromUtf8("Звірка боргів"),error); return; }
    QMap<QString,qint64> totals;
    for(const auto& debt:debts) totals[StoreNameNormalizer::canonicalDisplayNameOrDefault(debt.supplier,QString::fromUtf8("Без магазину"))]+=debt.remainingCents();
    auto actualTotals=SupplierDebtReconciliationService::effectiveTotals(debts,previous);
    if(summary.remote) {
        const auto elit=QString::fromUtf8("Еліт");
        for(const auto& name:names) if(SupplierInvoiceLedger::isElit(name)) {
            totals[name]=summary.totals.value(elit);
            actualTotals[name]=summary.totals.value(elit);
        }
    }
    QDialog dialog(this);
    dialog.setObjectName("supplierDebtReconciliationDialog");
    dialog.setWindowTitle(QString::fromUtf8("Звірка боргів"));
    const auto available = dialog.screen()->availableGeometry();
    dialog.resize(qMin(available.width()-40,qMax(760,qRound(available.width()*0.53))),
                  qMin(available.height()-60,qMax(480,qRound(available.height()*0.65))));
    dialog.move(available.center()-dialog.rect().center());
    dialog.setStyleSheet("QDialog { background:#263746; color:white; } QLabel { color:white; background:transparent; }"
        "QTableView { background:white; color:#111827; } QHeaderView::section { background:#e5edf5; color:#111827; padding:8px; }"
        "QDoubleSpinBox { background:white; color:#111827; padding:6px; }"
        "QPushButton { min-height:40px; padding:4px 14px; color:white; background:#344b5d; border:1px solid #91a4b3; }"
        "QPushButton#saveSupplierReconciliation { background:#16764e; border:1px solid #64c49a; }"
        "QPushButton:hover { border:2px solid white; }");
    auto* layout=new QVBoxLayout(&dialog);
    if (names.isEmpty()) {
        auto* empty = new QLabel(QString::fromUtf8("Додайте магазини у «Налаштування → Фінанси»."), &dialog);
        empty->setWordWrap(true); layout->addWidget(empty);
    }
    auto* table=new QTableView(&dialog);
    auto* model=new ReportsTableSupport::ReportsSnapshotTableModel(table);table->setModel(model);
    QVector<ReportsTableSupport::ReportsSnapshotTableModel::Row> rows;
    for(const auto& name:names){ReportsTableSupport::ReportsSnapshotTableModel::Row row;row.cells.resize(3);row.cells[0].text=name;row.cells[1].text=FormatUtils::money(totals.value(name)/100.0,2);rows.append(row);} table->setObjectName("supplierReconciliationTable");
    model->setSnapshot({QString::fromUtf8("Магазин"),QString::fromUtf8("За системою, грн"),QString::fromUtf8("Фактичний борг, грн")},rows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers); table->verticalHeader()->hide();
    table->horizontalHeader()->setMinimumSectionSize(130);
    table->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2,QHeaderView::ResizeToContents);
    QVector<QDoubleSpinBox*> values;
    for(int row=0;row<names.size();++row) {
        const auto& name=names[row];
        auto* value=new QDoubleSpinBox(table); value->setDecimals(2); value->setRange(0,9e12);
        value->setGroupSeparatorShown(true); value->setValue(actualTotals.value(name)/100.0);
        if(summary.remote && SupplierInvoiceLedger::isElit(name)) {
            value->setReadOnly(true); value->setButtonSymbols(QAbstractSpinBox::NoButtons);
            value->setToolTip(QString::fromUtf8("Актуальний залишок із синхронізації Еліт. Оновлюється через «Оновити дані»."));
        }
        value->setMinimumWidth(190); table->setIndexWidget(model->index(row,2),value); values.append(value);
    }
    table->resizeRowsToContents(); layout->addWidget(table,1);
    auto* actions=new QHBoxLayout;
    auto* save=new QPushButton(QString::fromUtf8("Зберегти"),&dialog); save->setObjectName("saveSupplierReconciliation");
    auto* cancel=new QPushButton(QString::fromUtf8("Скасувати"),&dialog);
    actions->addStretch(); actions->addWidget(save); actions->addWidget(cancel); layout->addLayout(actions);
    save->setEnabled(!names.isEmpty()); save->setAutoDefault(false); cancel->setAutoDefault(false);
    connect(cancel,&QPushButton::clicked,&dialog,&QDialog::reject);
    connect(save,&QPushButton::clicked,&dialog,[&] {
        QVector<SupplierDebtReconciliation> entries;
        for(int row=0;row<names.size();++row) {
            if(summary.remote && SupplierInvoiceLedger::isElit(names[row])) continue;
            values[row]->interpretText();
            SupplierDebtReconciliation entry;
            entry.supplier=names[row]; entry.revision=previous.value(names[row]).revision;
            entry.computedCents=totals.value(names[row]); entry.actualCents=PurchasePaymentService::cents(values[row]->value());
            entries.append(entry);
        }
        if(!SupplierDebtReadService::saveReconciliation(entries,&error)) {
            QMessageBox::warning(&dialog,QString::fromUtf8("Звірка боргів"),error); return;
        }
        dialog.accept();
    });
    if(dialog.exec()==QDialog::Accepted) { refresh(); if(paymentChanged) paymentChanged(); }
}

QString PurchaseDebtsPage::summaryHtml(const QVector<PurchaseDebt>& debts, int gapPixels,
        const QMap<QString,SupplierDebtReconciliation>& reconciliations,const QMap<QString,qint64>* verifiedTotals) {
    struct Amounts { qint64 paid=0, remaining=0; };
    QMap<QString,Amounts> partial, unpaid;
    for (const auto& debt : debts) {
        if (debt.remainingCents()<=0) continue;
        QString group=StoreNameNormalizer::canonicalDisplayNameOrDefault(debt.supplier,QString::fromUtf8("Без магазину"));
        if (debt.source=="inventory") group=QString::fromUtf8("Склад СТО / %1").arg(group);
        auto& amounts=(debt.paidCents>0 ? partial : unpaid)[group];
        amounts.paid+=debt.paidCents; amounts.remaining+=debt.remainingCents();
    }
    const bool reconciled=verifiedTotals || !reconciliations.isEmpty();
    if(reconciled) {
        unpaid.clear();
        const auto actual=verifiedTotals?*verifiedTotals:SupplierDebtReconciliationService::effectiveTotals(debts,reconciliations);
        for(auto it=actual.cbegin();it!=actual.cend();++it) if(it.value()>0) unpaid[it.key()].remaining=it.value();
    }
    auto cell=[](const QString& text,bool orange,bool right=false,bool bold=false,bool underline=false,int widthPercent=0) {
        QString value=text.toHtmlEscaped();
        if(underline) value="<u>"+value+"</u>";
        if(bold) value="<b>"+value+"</b>";
        return QString("<td%1 align='%2' style='color:%3;'>%4</td>")
            .arg(widthPercent>0 ? QString(" width='%1%'").arg(widthPercent) : QString(),
                 right?"right":"left",orange?"#ffbb66":"#ff6961",value);
    };
    auto money=[](qint64 value) { return FormatUtils::money(value/100.0,2); };
    const QString gap = QString("<td width='%1'>&nbsp;</td>").arg(qMax(27,gapPixels));
    QString html="<table width='100%' cellspacing='5' style='font-family:Times New Roman; font-size:10pt; font-weight:normal;'><tr>";
    html+=cell(QString::fromUtf8("Частково сплачено"),true,false,true,true,32)
        +cell(QString::fromUtf8("Сплачено"),true,true,true,true,10)
        +cell(QString::fromUtf8("Залишок, грн"),true,true,true,true,10)+gap
        +cell(!reconciled?QString::fromUtf8("Борги: не сплачено"):QString::fromUtf8("Борг після звірки"),false,false,true,true,36)
        +cell(QString::fromUtf8("Залишок, грн"),false,true,true,true,12)+"</tr>";
    auto p=partial.cbegin(); auto u=unpaid.cbegin();
    Amounts partialTotal,unpaidTotal;
    while(p!=partial.cend() || u!=unpaid.cend()) {
        html+="<tr>";
        if(p!=partial.cend()) {
            html+=cell(p.key(),true)+cell(money(p->paid),true,true)+cell(money(p->remaining),true,true);
            partialTotal.paid+=p->paid; partialTotal.remaining+=p->remaining; ++p;
        } else html+="<td></td><td></td><td></td>";
        html+=gap;
        if(u!=unpaid.cend()) {
            html+=cell(u.key(),false)+cell(money(u->remaining),false,true);
            unpaidTotal.remaining+=u->remaining; ++u;
        } else html+="<td></td><td></td>";
        html+="</tr>";
    }
    html+="<tr><td colspan='3'><hr /></td>"+gap+"<td colspan='2'><hr /></td></tr><tr>";
    html+=cell(QString::fromUtf8("Разом"),true,false,true)+cell(money(partialTotal.paid),true,true,true)
        +cell(money(partialTotal.remaining),true,true,true)+gap+cell(QString::fromUtf8("Разом"),false,false,true)
        +cell(money(unpaidTotal.remaining),false,true,true);
    html+="</tr></table>";
    if(reconciled) html+=QString::fromUtf8("<p style='color:#ffffff;'>Еліт: залишок за останньою повною синхронізацією, без повторного додавання локальних закупівель. Інші постачальники: локальний облік та ручна звірка. Часткові оплати зліва наведено довідково й не додано до підсумку.</p>");
    return html;
}

namespace {
QString debtSupplier(const PurchaseDebt& debt) {
    return StoreNameNormalizer::canonicalDisplayNameOrDefault(debt.supplier, QString::fromUtf8("Без магазину"));
}
QString debtMoney(qint64 cents) { return FormatUtils::money(cents / 100.0, 2); }
QString htmlRow(const QStringList& values, bool header = false, int linkColumn = -1, const QString& href = {}) {
    QString row = "<tr>";
    for (int column=0; column<values.size(); ++column) {
        QString value=values[column];
        if(!header && column!=linkColumn && (value=="0" || value==QString::fromUtf8("0 грн")
            || value==QString::fromUtf8("Не визначено") || value==QString::fromUtf8("Не задано")
            || value==QString::fromUtf8("Немає даних"))) value.clear();
        value=value.toHtmlEscaped();
        if(column==linkColumn && !href.isEmpty())
            value=QString("<a href='%1'>%2</a>").arg(href.toHtmlEscaped(),value.isEmpty()?QString::fromUtf8("Без назви"):value);
        row += QString(header ? "<th align='left'>%1</th>" : "<td>%1</td>").arg(value);
    }
    return row + "</tr>";
}
struct InstallationPeriod { int days=-1; QString evidence; };
InstallationPeriod installationPeriod(const PartRowSnapshot& part,const QDate& ordered,
        const QDate& today,bool verified) {
    InstallationPeriod result;
    const auto delivered=PartsQueryService::parseBusinessDate(part.deliveryDateRaw);
    const auto installed=PartsQueryService::parseBusinessDate(part.installedAtRaw);
    const auto opened=PartsQueryService::parseBusinessDate(part.orderOpenedAtRaw);
    const bool closed=part.orderClosedDate.isValid() || part.orderStatus.toLower()=="closed"
        || part.orderStatus.toLower()=="completed" || part.orderStatus.toLower()==QString::fromUtf8("закрито")
        || part.orderStatus.toLower()==QString::fromUtf8("закритий");
    result.evidence=QString::fromUtf8("Замовлення: %1; доставка: %2; встановлення: %3")
        .arg(ordered.isValid()?ordered.toString("dd.MM.yyyy"):QString::fromUtf8("невідомо"),
             delivered.isValid()?delivered.toString("dd.MM.yyyy"):QString::fromUtf8("невідомо"),
             installed.isValid()?installed.toString("dd.MM.yyyy"):QString::fromUtf8("невідомо"));
    // A legacy date years before the order needs evidence, not a silently substituted year.
    if(!today.isValid() || !ordered.isValid() || ordered>today || (installed.isValid() && (installed<ordered || installed>today))
       || (delivered.isValid() && (delivered<ordered || delivered>today || (installed.isValid() && delivered>installed)))
       || (!verified && opened.isValid() && ordered.daysTo(opened)>365)) {
        result.evidence+=QString::fromUtf8(". Дати потребують звірки з накладною; тривалість не підтверджена");
        return result;
    }
    if(closed && !installed.isValid()) {
        result.evidence+=QString::fromUtf8(". Наряд закрито, але дата встановлення відсутня; кінцеву подію не підтверджено");
        return result;
    }
    const auto end=installed.isValid()?installed:today;
    result.days=ordered.daysTo(end);
    if(delivered.isValid()) result.evidence+=QString::fromUtf8(". До доставки: %1 дн; після доставки: %2 дн")
        .arg(ordered.daysTo(delivered)).arg(delivered.daysTo(end));
    return result;
}
}

#include "ui/tabs/reports/SupplierDebtOverview.h"
#include "core/services/ElitStartupSync.h"

PurchaseDebtAnalysisPage::PurchaseDebtAnalysisPage(QWidget* parent) : QWidget(parent) {
    setObjectName("purchaseDebtAnalysisPage");
    setAttribute(Qt::WA_StyledBackground,true);
    setStyleSheet(R"(
        QWidget#purchaseDebtAnalysisPage { background:#f5f7f9; color:#192631; border:1px solid #c4cfd7; border-radius:8px; }
        QWidget#purchaseDebtAnalysisPage QLabel { font-family:'Segoe UI'; color:#192631; background:transparent; border:0; }
        QWidget#purchaseDebtAnalysisPage QPushButton { font-family:'Segoe UI'; font-size:10pt; font-weight:600; color:#253541; background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #ffffff,stop:1 #e3eaf0); border:1px solid #a5b5c1; border-bottom:2px solid #899ca9; border-radius:6px; padding:8px 14px; min-height:20px; }
        QWidget#purchaseDebtAnalysisPage QPushButton:hover { background:#edf4f8; border-color:#6f8c9e; }
        QWidget#purchaseDebtAnalysisPage QPushButton:pressed { background:#dce8f0; }
        QWidget#purchaseDebtAnalysisPage QPushButton:disabled { color:#82909b; background:#f2f5f7; }
        QWidget#purchaseDebtAnalysisPage QPushButton#debtAnalysisPdf { background:#344e61; color:white; border-color:#344e61; }
        QWidget#purchaseDebtAnalysisPage QComboBox, QWidget#purchaseDebtAnalysisPage QLineEdit { font-family:'Segoe UI'; font-size:10pt; color:#253541; background:white; border:1px solid #b9c5cd; border-radius:6px; padding:8px 10px; min-height:20px; selection-background-color:#dceaf2; selection-color:#192631; }
        QWidget#purchaseDebtAnalysisPage QComboBox::drop-down { width:22px; border:0; background:transparent; }
        QWidget#purchaseDebtAnalysisPage QComboBox::down-arrow { image:url(:/assets/combo_arrow_black.png); width:10px; height:6px; }
        QWidget#purchaseDebtAnalysisPage QTabBar::tab { font-family:'Times New Roman'; font-size:11pt; color:#253541; background:#e3ebf0; border:1px solid #aebdc8; border-bottom:2px solid #aebdc8; border-radius:0; padding:7px 12px; }
        QWidget#purchaseDebtAnalysisPage QTabBar::tab:selected { color:#6e4e16; background:#fff2d6; border:1px solid #dcb064; border-bottom:3px solid #d58a07; border-top-left-radius:6px; border-top-right-radius:6px; font-weight:600; }
    )");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24,20,24,12); layout->setSpacing(12);
    auto* headingRow=new QHBoxLayout;
    m_heading=new QLabel(QString::fromUtf8("Огляд боргів і строків"),this);
    m_heading->setObjectName("debtAnalysisHeading");
    m_heading->setStyleSheet("font-family:'Times New Roman'; font-size:20pt; font-weight:600; color:#192631;");
    m_heading->setWordWrap(true); headingRow->addWidget(m_heading,1);
    layout->addLayout(headingRow);
    m_views=new QTabBar(this);
    m_views->setObjectName("debtAnalysisViews");
    for(const auto& title:{"Огляд","Потребує дії","Реєстр"}) m_views->addTab(QString::fromUtf8(title));
    m_views->setExpanding(false);
    m_views->addTab(QString::fromUtf8("Накладні та звірка"));
    m_views->setCurrentIndex(3);
    layout->addWidget(m_views);
    auto* toolbar = new QHBoxLayout;
    m_supplier = new QComboBox(this);
    m_supplier->setObjectName("debtAnalysisSupplier");
    m_supplier->setMinimumWidth(150); m_supplier->setMaximumWidth(260);
    toolbar->addWidget(m_supplier);
    m_source=new QComboBox(this); m_source->setObjectName("debtAnalysisSource");
    m_source->addItem(QString::fromUtf8("Усі джерела"),"");
    m_source->addItem(QString::fromUtf8("Накладні"),"invoice");
    m_source->addItem(QString::fromUtf8("Ручні / старі записи"),"manual");
    m_source->addItem(QString::fromUtf8("Склад СТО"),"inventory");
    toolbar->addWidget(m_source);
    m_source->setMinimumWidth(150); m_source->setMaximumWidth(220);
    m_reconcile=new QPushButton(QString::fromUtf8("Звірка боргів"),this);
    headingRow->addWidget(m_reconcile);
    connect(m_reconcile,&QPushButton::clicked,this,[this] { if(reconciliationRequested) reconciliationRequested(); });
    m_previous=new QPushButton(QString::fromUtf8("Попередні"),this);
    m_next=new QPushButton(QString::fromUtf8("Наступні"),this);
    m_pageStatus=new QLabel(this);
    m_pageStatus->setStyleSheet("color:#60717d; background:transparent;");
    m_previous->setToolTip(QString::fromUtf8("Попередні 100 позицій деталізації"));
    m_next->setToolTip(QString::fromUtf8("Наступні 100 позицій деталізації"));
    auto* pagination=new QHBoxLayout; pagination->addStretch();
    pagination->addWidget(m_previous); pagination->addWidget(m_pageStatus); pagination->addWidget(m_next);
    auto* button = new QPushButton(QString::fromUtf8("Оновити дані"),this);
    button->setObjectName("debtAnalysisRefreshRemote");
    button->setToolTip(QString::fromUtf8("Перевірити актуальні залишки й оплати в Еліт та eCat. Збережені накладні не завантажуються повторно; завантажуються лише нові."));
    headingRow->addWidget(button);
    auto* cancelSync=new QPushButton(QString::fromUtf8("Скасувати синхронізацію"),this);
    cancelSync->hide();headingRow->addWidget(cancelSync);
    connect(cancelSync,&QPushButton::clicked,this,[]{ElitStartupSync::cancel();});
    m_pdf=new QPushButton("PDF",this); m_pdf->setObjectName("debtAnalysisPdf");
    m_pdf->setToolTip(QString::fromUtf8("Зберегти весь аналіз у PDF, A4 книжковий"));
    m_pdf->setEnabled(false); headingRow->addWidget(m_pdf);
    connect(m_pdf,&QPushButton::clicked,this,[this] { exportPdf(); });
    layout->addLayout(toolbar);
    m_search=new QLineEdit(this); m_search->setObjectName("debtAnalysisSearch");
    m_search->setPlaceholderText(QString::fromUtf8("Пошук: накладна, наряд, постачальник, запчастина"));
    m_search->setClearButtonEnabled(true); m_search->setMinimumWidth(150); toolbar->addWidget(m_search,1);
    m_asOfLabel=new QLabel(this); m_asOfLabel->setStyleSheet("color:#60717d; font-size:9pt;");
    toolbar->addWidget(m_asOfLabel);
    m_expenses=new QLabel(this); m_expenses->setObjectName("debtAnalysisExpenses"); m_expenses->setWordWrap(true);
    m_expenses->setStyleSheet("color:#253c48; background:#eef3f5; padding:12px;");
    m_expenses->hide();
    m_report = new QTextBrowser(this);
    m_report->setObjectName("debtAnalysisReport");
    m_report->setOpenExternalLinks(false);
    m_report->setStyleSheet("QTextBrowser { background:#ffffff; color:#192631; border:1px solid #c8d3db; border-bottom:2px solid #aebdc8; border-radius:8px; padding:10px; }");
    m_report->document()->setDocumentMargin(4);
    layout->addWidget(m_report,1);
    m_overview=new SupplierDebtOverview(this);
    layout->addWidget(m_overview,1);
    m_report->hide();
    m_overview->inventoryRequested=[this](qint64 id){if(inventoryRequested) inventoryRequested(id);};
    m_overview->suppliersChanged=[this](const QStringList& suppliers){
        const QSignalBlocker blocked(m_supplier);
        for(const auto& supplier:suppliers) if(m_supplier->findData(supplier)<0) m_supplier->addItem(supplier,supplier);
    };
    layout->addLayout(pagination);
    m_overview->associationsChanged=[this]{if(reconciliationChanged)reconciliationChanged();};
    connect(button,&QPushButton::clicked,this,[this] {
        refresh();
        ElitStartupSync::request(this,[this](bool success,const QString& message){
            refresh();if(reconciliationChanged)reconciliationChanged();
            if(!success) QMessageBox::warning(this,QString::fromUtf8("Еліт · оновлення даних"),message);
        });
    });
    auto* syncState=new QTimer(this); syncState->setInterval(500);
    connect(syncState,&QTimer::timeout,this,[this,button,cancelSync,wasRunning=false]() mutable {
        const bool running=ElitStartupSync::isRunning();
        button->setEnabled(!running);
        cancelSync->setVisible(running && SupplierDebtReconciliationService::canReconcile());
        button->setText(running?QString::fromUtf8("Синхронізація…"):QString::fromUtf8("Оновити дані"));
        if(wasRunning && !running) refresh();
        wasRunning=running;
    }); syncState->start();
    connect(m_supplier,&QComboBox::currentIndexChanged,this,[this] { m_detailPage=0; render(); });
    connect(m_previous,&QPushButton::clicked,this,[this] { --m_detailPage; render(); });
    connect(m_next,&QPushButton::clicked,this,[this] { ++m_detailPage; render(); });
    connect(m_views,&QTabBar::currentChanged,this,[this] { m_detailPage=0; render(); });
    connect(m_source,&QComboBox::currentIndexChanged,this,[this] { m_detailPage=0; m_renderCache.clear(); render(); });
    auto* searchDelay=new QTimer(this); searchDelay->setSingleShot(true); searchDelay->setInterval(180);
    connect(m_search,&QLineEdit::textChanged,searchDelay,qOverload<>(&QTimer::start));
    connect(searchDelay,&QTimer::timeout,this,[this] { m_detailPage=0; m_renderCache.clear(); render(); });
    connect(m_report,&QTextBrowser::anchorClicked,this,[this](const QUrl& url) {
        if(m_loadedScope!=AccessControlService::currentScopeKey()) { refresh(); return; }
        const auto key=url.path(QUrl::FullyDecoded);
        if(url.scheme()=="supplier") {
            m_supplier->setCurrentIndex(qMax(0,m_supplier->findData(key))); m_views->setCurrentIndex(2);
        } else if(url.scheme()=="record") {
            for(const auto& debt:m_debts) {
                if(PurchasePaymentService::key(debt.source,debt.id,debt.orderUid)!=key) continue;
                if(debt.source=="remote_invoice") {openElitInvoicePdf(this,debt.orderUid);return;}
                const PurchaseDebt selected=debt;
                QDialog dialog(this); dialog.setObjectName("purchaseDebtRecordDialog"); dialog.setWindowTitle(QString::fromUtf8("Дані закупівлі та строків")); dialog.resize(1000,600);
                auto* layout=new QVBoxLayout(&dialog); auto* detail=new QTextBrowser(&dialog);
                detail->setOpenLinks(false);
                detail->setLineWrapMode(QTextEdit::WidgetWidth);
                auto textOption=detail->document()->defaultTextOption();
                textOption.setWrapMode(QTextOption::WordWrap);
                detail->document()->setDefaultTextOption(textOption);
                QString html=analysisHtml({debt},m_parts,m_asOf,{},-1,{},m_evidence,2);
                html.replace("<table border='1' cellspacing='0' cellpadding='6'>",
                    "<table width='99%' border='1' cellspacing='0' cellpadding='3' style='font-size:9pt;'>");
                const QRegularExpression headers("<th align='left'>(.*?)</th>");
                auto matches=headers.globalMatch(html);
                QList<QRegularExpressionMatch> cells;
                while(matches.hasNext()) cells.append(matches.next());
                for(auto it=cells.crbegin();it!=cells.crend();++it) {
                    QString caption=it->captured(1);
                    html.replace(it->capturedStart(),it->capturedLength(),
                        "<th align='left' style='font-size:8pt;'>"+caption+"</th>");
                }
                detail->setHtml(html); layout->addWidget(detail);
                auto* actions=new QHBoxLayout;
                auto* navigate=new QPushButton(debt.source=="inventory"?QString::fromUtf8("Перейти до складу"):
                    QString::fromUtf8("Перейти до наряду №%1").arg(debt.orderNumber),&dialog);
                navigate->setObjectName("goToPurchaseSource");
                navigate->setEnabled(debt.source=="inventory" || (debt.source=="part" && debt.orderNumber>0));
                actions->addWidget(navigate);
                connect(navigate,&QPushButton::clicked,&dialog,[this,&dialog,selected] {
                    if(m_loadedScope!=AccessControlService::currentScopeKey()) { dialog.reject(); return; }
                    if(selected.source=="inventory") {
                        dialog.accept();
                        QTimer::singleShot(0,this,[this,selected] {
                            if(m_loadedScope==AccessControlService::currentScopeKey() && inventoryRequested)
                                inventoryRequested(selected.id);
                        });
                        return;
                    } else {
                        const auto result=CurrentOrdersCoordinator::openExistingOrderEditor(&dialog,selected.orderNumber,QString());
                        if(!result.launched) QMessageBox::warning(&dialog,QString::fromUtf8("Наряд"),result.errorText);
                    }
                    dialog.accept();
                });
                auto* pay=new QPushButton(QString::fromUtf8("Сплатити"),&dialog);
                pay->setObjectName("payDebtRecord");
                pay->setEnabled(debt.source!="remote_invoice" && PurchasePaymentService::canPay(debt.source) && debt.remainingCents()>0);
                actions->addWidget(pay); actions->addStretch();
                auto* close=new QPushButton(QString::fromUtf8("Закрити"),&dialog);
                actions->addWidget(close);
                connect(close,&QPushButton::clicked,&dialog,&QDialog::reject);
                connect(pay,&QPushButton::clicked,&dialog,[this,&dialog,selected] {
                    if(m_loadedScope!=AccessControlService::currentScopeKey()) { dialog.reject(); return; }
                    if(selected.source!="remote_invoice" && openPurchasePayment(&dialog,selected.source,selected.id,selected.orderUid)) dialog.accept();
                });
                layout->addLayout(actions);
                const auto fact=m_evidence.value(InvoiceEvidenceService::key(debt.orderUid,int(debt.id)));
                if(debt.source=="part" && !fact.filePath.isEmpty()) {
                    auto* open=new QPushButton(QString::fromUtf8("Відкрити накладну"),&dialog); layout->addWidget(open);
                    connect(open,&QPushButton::clicked,&dialog,[this,fact] {
                        if(m_loadedScope!=AccessControlService::currentScopeKey()) return;
                        if(!QFileInfo::exists(fact.filePath) || !QDesktopServices::openUrl(QUrl::fromLocalFile(fact.filePath)))
                            QMessageBox::warning(this,QString::fromUtf8("Накладна"),QString::fromUtf8("Документ недоступний. Перевірте вкладення наряду."));
                    });
                }
                const bool changed=dialog.exec()==QDialog::Accepted;
                if(changed) QTimer::singleShot(0,this,[this] { refresh(); });
                break;
            }
        }
    });
    m_report->setOpenLinks(false);
}

PurchaseDebtAnalysisPage::~PurchaseDebtAnalysisPage() {
    AsyncTaskHandle::cancel(m_expenseCancel);
    AsyncTaskHandle::cancel(m_analysisCancel);
}

void PurchaseDebtAnalysisPage::setExpensePeriod(const QDate& from,const QDate& to) {
    m_expenseFrom=from; m_expenseTo=to;
}

void PurchaseDebtAnalysisPage::refresh() {
    PartsQueryService::invalidateCache();
    m_overview->refresh();
    m_loaded=false; m_pdf->setEnabled(false);
    m_asOf=QDate::currentDate(); m_renderCache.clear(); ++m_generation;
    AsyncTaskHandle::cancel(m_expenseCancel);
    AsyncTaskHandle::cancel(m_analysisCancel);
    m_expenses->clear(); m_expenseDetailsHtml.clear(); m_showExpenses=false;
    m_asOfLabel->setText(QString::fromUtf8("Стан на %1").arg(m_asOf.toString("dd.MM.yyyy")));
    m_reconcile->setVisible(SupplierDebtReconciliationService::canReconcile());
    const auto scope=AccessControlService::currentOrderScope();
    const auto scopeKey=AccessControlService::currentScopeKey();
    const auto generation=m_generation;
    const auto readTask=SupplierDebtReadService::prepare(true);
    const int gap=qCeil(logicalDpiX()*7.0/25.4);
    m_report->setPlainText(QString::fromUtf8("Завантаження аналізу боргів…"));
    m_previous->setEnabled(false); m_next->setEnabled(false); m_pageStatus->clear();
    struct Snapshot : SupplierDebtReadService::Snapshot {QString summary;};
    auto result=std::make_shared<Snapshot>();
    auto cancel=std::make_shared<std::atomic_bool>(false); m_analysisCancel=cancel;
    auto* worker=AsyncThreadScope::startWorker(this,"purchase_debt_analysis",cancel,[result,cancel,readTask,gap] {
        static_cast<SupplierDebtReadService::Snapshot&>(*result)=readTask(*cancel);
        if(result->error.isEmpty() && !cancel->load())result->summary=result->restricted
            ? PurchaseDebtsPage::summaryHtml(result->debts,gap)
            : sharedDebtSummary(result->debts,gap,result->reconciliations,result->ledger);
    });
    connect(worker,&QThread::finished,this,[this,result,cancel,generation,scopeKey,scope] {
    if(cancel->load() || generation!=m_generation || scopeKey!=AccessControlService::currentScopeKey()) return;
    const auto& error=result->error;
    m_debts=std::move(result->debts); m_parts=std::move(result->parts);
    m_reconciliations=std::move(result->reconciliations); m_evidence=std::move(result->evidence);
    const QString selected = m_supplier->currentData().toString();
    const QSignalBlocker blocked(m_supplier);
    m_supplier->clear();
    m_supplier->addItem(QString::fromUtf8("Усі магазини"),QString());
    if (!error.isEmpty()) {
        m_debts.clear(); m_parts.clear(); m_reconciliations.clear(); m_evidence.clear();
        m_previous->setEnabled(false); m_next->setEnabled(false); m_pageStatus->clear();
        if (summaryChanged) summaryChanged(QString());
        m_report->setPlainText(QString::fromUtf8("Не вдалося завантажити аналіз боргів: ")+error);
        return;
    }
    QMap<QString,bool> suppliers;
    for (const auto& debt : m_debts) suppliers.insert(debtSupplier(debt),true);
    for(const auto& fact:m_evidence) suppliers.insert(StoreNameNormalizer::canonicalDisplayNameOrDefault(fact.supplier,QString::fromUtf8("Без магазину")),true);
    for(auto it=m_reconciliations.cbegin();it!=m_reconciliations.cend();++it) suppliers.insert(it.key(),true);
    for (auto it=suppliers.cbegin();it!=suppliers.cend();++it) m_supplier->addItem(it.key(),it.key());
    m_supplier->setCurrentIndex(qMax(0,m_supplier->findData(selected)));
    if (summaryChanged) summaryChanged(result->summary);
    m_loadedScope=AccessControlService::currentScopeKey(); m_loaded=true; m_pdf->setEnabled(true);
    m_showExpenses=!scope.restricted && AccessControlService::currentUserHasPermission("nav.reports");
    if(m_showExpenses) {
        const auto from=m_expenseFrom.isValid()?m_expenseFrom:QDate(m_asOf.year(),m_asOf.month(),1);
        const auto to=m_expenseTo.isValid()?m_expenseTo:m_asOf;
        m_pdf->setEnabled(false);
        m_expenses->setText(QString::fromUtf8("Витрати СТО за період: завантаження…"));
        m_expenseDetailsHtml=QString::fromUtf8("<p>Завантаження витрат…</p>");
        const auto generation=m_generation; const auto scopeKey=m_loadedScope;
        ReportsAsyncSupport::startTotalExpensesLoad(this,AsyncTaskHandle(this,generation,AsyncTaskHandle::renewCancellationToken(m_expenseCancel)),
            ReportsDataLoader::currentDatabasePath(),from.toString("yyyy-MM-dd"),to.toString("yyyy-MM-dd"),
            [this,generation,scopeKey,from,to](const TotalExpensesSnapshot& snapshot) {
                if(generation!=m_generation || scopeKey!=AccessControlService::currentScopeKey()) return;
                m_pdf->setEnabled(true);
                m_expenses->setText(snapshot.error.isEmpty()?QString::fromUtf8("Витрати %1–%2: СТО %3 грн + додаткові %4 грн = сукупні %5 грн. Це витрати, не додатковий борг: оплати й строки цих витрат окремо не підтверджені.")
                    .arg(from.toString("dd.MM.yy"),to.toString("dd.MM.yy"),debtMoney(snapshot.periodicCents),debtMoney(snapshot.additionalCents),debtMoney(snapshot.periodicCents+snapshot.additionalCents))
                    :QString::fromUtf8("Не вдалося отримати витрати: ")+snapshot.error);
                m_expenseDetailsHtml="<p style='color:#60717d;'>"+from.toString("dd.MM.yyyy")+" - "+to.toString("dd.MM.yyyy")+"</p>";
                if(snapshot.error.isEmpty()) {
                    m_expenseDetailsHtml+="<table width='100%' cellspacing='0' cellpadding='9'>";
                    if(snapshot.periodicCents>0) m_expenseDetailsHtml+=htmlRow({QString::fromUtf8("Витрати СТО за період"),debtMoney(snapshot.periodicCents)+QString::fromUtf8(" грн")});
                    if(snapshot.additionalCents>0) m_expenseDetailsHtml+=htmlRow({QString::fromUtf8("Додаткові витрати"),debtMoney(snapshot.additionalCents)+QString::fromUtf8(" грн")});
                    m_expenseDetailsHtml+=htmlRow({QString::fromUtf8("Сукупні витрати"),debtMoney(snapshot.periodicCents+snapshot.additionalCents)+QString::fromUtf8(" грн")})+"</table>";
                    if(snapshot.periodicCents+snapshot.additionalCents<=0) m_expenseDetailsHtml.clear();
                } else m_expenseDetailsHtml+="<p>"+m_expenses->text().toHtmlEscaped()+"</p>";
                m_renderCache.clear(); render();
            });
    }
    render();
    });
    worker->start();
}

QVector<PurchaseDebt> PurchaseDebtAnalysisPage::filteredDebts() const {
    QVector<PurchaseDebt> result;
    const auto source=m_source->currentData().toString(); const auto search=m_search->text().trimmed();
    for(const auto& debt:m_debts) {
        const auto fact=m_evidence.constFind(InvoiceEvidenceService::key(debt.orderUid,int(debt.id)));
        const bool invoice=debt.source=="remote_invoice" || (debt.source=="part" && fact!=m_evidence.cend());
        if(source=="invoice" && !invoice) continue;
        if(source=="manual" && (invoice || debt.source!="part")) continue;
        if(source=="inventory" && debt.source!="inventory") continue;
        const auto text=debt.name+" "+debt.supplier+" "+QString::number(debt.orderNumber)+(fact!=m_evidence.cend()?" "+fact->invoiceNumber+" "+fact->name:QString());
        if(!search.isEmpty() && !text.contains(search,Qt::CaseInsensitive)) continue;
        result.append(debt);
    }
    return result;
}

void PurchaseDebtAnalysisPage::exportPdf() {
    if(!m_loaded || m_loadedScope!=AccessControlService::currentScopeKey()) {
        refresh(); if(!m_loaded || !m_pdf->isEnabled()) return;
    }
    QDialog choice(this); choice.setObjectName("debtReportChoice");
    choice.setStyleSheet("QDialog#debtReportChoice { background:#f5f7f9; } QLabel { color:#253541; background:transparent; } QPushButton { color:#253541; background:#ffffff; border:1px solid #a5b5c1; border-bottom:2px solid #899ca9; border-radius:6px; padding:12px 24px; font-size:12pt; } QPushButton:hover { background:#e4eef5; } QPushButton:default { border:2px solid #34739a; }");
    choice.setWindowTitle(QString::fromUtf8("Який звіт підготувати?"));
    auto* choiceLayout=new QVBoxLayout(&choice);
    choiceLayout->setContentsMargins(24,20,24,20); choiceLayout->setSpacing(18);
    auto* prompt=new QLabel(QString::fromUtf8("Який звіт підготувати?"),&choice);
    prompt->setStyleSheet("font-size:14pt; font-weight:600;"); choiceLayout->addWidget(prompt);
    auto* choices=new QHBoxLayout; choiceLayout->addLayout(choices);
    auto* full=new QPushButton(QString::fromUtf8("Повний"),&choice); full->setObjectName("fullDebtReport");
    auto* brief=new QPushButton(QString::fromUtf8("Стислий"),&choice); brief->setObjectName("briefDebtReport");
    choices->addWidget(full); choices->addWidget(brief); full->setDefault(true);
    if(m_views->currentIndex()==3) { brief->hide(); full->setText(QString::fromUtf8("Поточний відбір")); }
    bool shortReport=false;
    connect(full,&QPushButton::clicked,&choice,&QDialog::accept);
    connect(brief,&QPushButton::clicked,&choice,[&] { shortReport=true; choice.accept(); });
    if(choice.exec()!=QDialog::Accepted) return;
    if(m_loadedScope!=AccessControlService::currentScopeKey()) { refresh(); return; }
    QString error;
    const QString path=StorageDirectoriesGuard::managedReportFilePath(QString::fromUtf8("Аналіз боргів"),
        "CarServicePointG_DebtAnalysis_"+QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz")
        +"_"+QUuid::createUuid().toString(QUuid::Id128)+".pdf",&error);
    if(path.isEmpty()) { QMessageBox::warning(this,QString::fromUtf8("PDF"),error); return; }
    if(m_loadedScope!=AccessControlService::currentScopeKey()) {
        QMessageBox::warning(this,QString::fromUtf8("PDF"),QString::fromUtf8("Користувача змінено. Оновіть аналіз перед експортом.")); return;
    }
    const auto facts=m_source->currentIndex()==0 && m_search->text().trimmed().isEmpty()?m_reconciliations:QMap<QString,SupplierDebtReconciliation>();
    QString report=m_views->currentIndex()==3?m_overview->reportHtml():analysisHtml(filteredDebts(),m_parts,m_asOf,m_supplier->currentData().toString(),-1,facts,m_evidence,shortReport?3:-1);
    if(report.isEmpty()) { QMessageBox::information(this,QString::fromUtf8("PDF"),QString::fromUtf8("Дочекайтеся завантаження реєстру.")); return; }
    if(m_views->currentIndex()!=3 && !shortReport && m_showExpenses && !m_expenseDetailsHtml.isEmpty())
        report.replace("</body>",QString::fromUtf8("<h3>Витрати СТО</h3>")+m_expenseDetailsHtml+"</body>");
    if(!DebtAnalysisPdfService::save(path,report,&error)) {
        QMessageBox::warning(this,QString::fromUtf8("PDF"),error); return;
    }
    if(!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
        QMessageBox::information(this,QString::fromUtf8("PDF"),QString::fromUtf8("PDF збережено: %1").arg(QDir::toNativeSeparators(path)));
}

void PurchaseDebtAnalysisPage::render() {
    const bool overview=m_views->currentIndex()==3;
    m_overview->setVisible(overview); m_report->setVisible(!overview);
    m_previous->setVisible(!overview); m_next->setVisible(!overview); m_pageStatus->setVisible(!overview);
    if(overview) {
        m_heading->setText(QString::fromUtf8("Аналіз боргів"));
        m_overview->setFilters(m_supplier->currentData().toString(),m_source->currentData().toString(),m_search->text());
        return;
    }
    if(!m_loaded) return;
    if(m_loadedScope!=AccessControlService::currentScopeKey()) { refresh(); return; }
    const QStringList headings={QString::fromUtf8("Аналіз боргів · Контроль затримок"),QString::fromUtf8("Що потребує дії"),QString::fromUtf8("Єдиний реєстр боргів")};
    m_heading->setText(headings.value(m_views->currentIndex()));
    const QString supplier=m_supplier->currentData().toString();
    const auto debts=filteredDebts();
    QSet<QString> waitingKeys;
    for(const auto& part:m_parts) {
        const auto status=part.orderStatus.trimmed().toLower();
        const bool closed=part.orderClosedDate.isValid() || status=="closed" || status=="completed"
            || status==QString::fromUtf8("закрито") || status==QString::fromUtf8("закритий");
        if(!part.installed && !closed) waitingKeys.insert(PurchasePaymentService::key("part",part.id,part.orderUid));
    }
    int urgent=0, unknown=0, waiting=0;
    const int count=std::count_if(debts.cbegin(),debts.cend(),[&](const auto& d) {
        const auto fact=m_evidence.constFind(InvoiceEvidenceService::key(d.orderUid,int(d.id)));
        const QString store=d.source=="part" && fact!=m_evidence.cend()
            ?StoreNameNormalizer::canonicalDisplayNameOrDefault(fact->supplier,QString::fromUtf8("Без магазину")):debtSupplier(d);
        if(d.remainingCents()<=0 || (!supplier.isEmpty() && store!=supplier)) return false;
        if(!d.dueDate.isValid()) ++unknown;
        else if(d.dueDate<m_asOf) ++urgent;
        if(waitingKeys.contains(PurchasePaymentService::key(d.source,d.id,d.orderUid))) ++waiting;
        return true;
    });
    const int pagedCount=m_views->currentIndex()==1?qMax(urgent,qMax(unknown,waiting)):count;
    const int pages=qMax(1,(pagedCount+99)/100);
    const bool paginated=m_views->currentIndex()!=0;
    m_previous->setVisible(paginated); m_next->setVisible(paginated); m_pageStatus->setVisible(paginated);
    m_detailPage=qBound(0,m_detailPage,pages-1);
    m_previous->setEnabled(m_detailPage>0); m_next->setEnabled(m_detailPage+1<pages);
    m_pageStatus->setText(QString::fromUtf8("%1 / %2").arg(m_detailPage+1).arg(pages));
    const auto cacheKey=supplier+"|"+m_source->currentData().toString()+"|"+m_search->text().trimmed()+"|"+QString::number(m_views->currentIndex())+"|"+QString::number(m_detailPage);
    const auto facts=m_source->currentIndex()==0 && m_search->text().trimmed().isEmpty()?m_reconciliations:QMap<QString,SupplierDebtReconciliation>();
    if(!m_renderCache.contains(cacheKey)) {
        if(m_renderCache.size()>=16) m_renderCache.clear();
        m_renderCache.insert(cacheKey,analysisHtml(debts,m_parts,m_asOf,supplier,m_detailPage,facts,m_evidence,m_views->currentIndex()));
    }
    auto html=m_renderCache.value(cacheKey);
    if(!m_showExpenses || m_expenseDetailsHtml.isEmpty()) html.replace(QString::fromUtf8("<h3>Витрати та рух коштів</h3>"),QString());
    html.replace("<!--EXPENSES-->",m_showExpenses?m_expenseDetailsHtml:QString());
    html.replace("width='100%'","width='99%'");
    m_report->setHtml(DebtAnalysisPdfService::compactHtml(html));
}

QString PurchaseDebtAnalysisPage::analysisHtml(const QVector<PurchaseDebt>& debts,
        const QVector<PartRowSnapshot>& parts, const QDate& today, const QString& supplierFilter, int detailPage,
        const QMap<QString,SupplierDebtReconciliation>& reconciliations,
        const QHash<QString,InvoiceEvidence>& evidence, int view) {
    struct Totals {
        int count=0, noDate=0, warning=0, critical=0, overSeven=0, maxLateDays=0, maxAge=-1, noAge=0;
        qint64 remaining=0, partial=0, unpaid=0, lateCents=0, undatedCents=0;
        bool hasParts=false, hasInventory=false, hasInvoices=false, hasManual=false;
    };
    QHash<QString,PartRowSnapshot> contexts;
    for (const auto& part : parts)
        contexts.insert(PurchasePaymentService::key("part",part.id,part.orderUid),part);
    QMap<QString,Totals> suppliers;
    QMap<QString,QPair<int,qint64>> causes;
    QMap<int,QPair<int,qint64>> aging;
    QString details;
    QStringList urgentRows, unknownRows, waitingRows;
    QStringList controlRows;
    Totals all;
    int confirmed=0, differences=0;
    qint64 confirmedPurchases=0;
    int unknownContext=0, missingDelivery=0;
    for (const auto& debt : debts) {
        if (debt.remainingCents()<=0) continue;
        const auto fact=evidence.constFind(InvoiceEvidenceService::key(debt.orderUid,int(debt.id)));
        const bool verified=debt.source=="part" && fact!=evidence.cend();
        const QString supplier=verified?StoreNameNormalizer::canonicalDisplayNameOrDefault(fact->supplier,QString::fromUtf8("Без магазину")):debtSupplier(debt);
        if (!supplierFilter.isEmpty() && supplier != supplierFilter) continue;
        const QString key=PurchasePaymentService::key(debt.source,debt.id,debt.orderUid);
        const auto context=contexts.constFind(key);
        const bool hasPart=debt.source=="part" && context!=contexts.cend();
        PartRowSnapshot part;
        if (hasPart) { part=*context; PartsQueryService::refreshDerivedFields(&part,today); }
        if(verified) {
            ++confirmed; confirmedPurchases+=fact->purchaseCents;
            if(hasPart) part.dateRaw=fact->invoiceDate;
        }
        const QDate ordered=PartsQueryService::parseBusinessDate(verified?fact->invoiceDate:hasPart?part.dateRaw:debt.date);
        const auto period=hasPart?installationPeriod(part,ordered,today,verified):InstallationPeriod();
        const int lateDays=debt.dueDate.isValid() && today.isValid()?qMax(0,debt.dueDate.daysTo(today)):-1;
        const bool late=lateDays>0;
        const int bucket=lateDays<0 ? 0 : lateDays==0 ? 1 : lateDays<3 ? 2 : lateDays<5 ? 3 : lateDays<=7 ? 4 : 5;
        const auto opened=hasPart?PartsQueryService::parseBusinessDate(part.orderOpenedAtRaw):QDate();
        const int purchaseAge=ordered.isValid() && ordered<=today && (verified || !opened.isValid() || ordered.daysTo(opened)<=365)
            ?ordered.daysTo(today):-1;
        const bool operational=lateDays>=3;
        const qint64 remaining=debt.remainingCents();
        auto add=[&](Totals& t) {
            ++t.count; t.remaining+=remaining;
            t.hasParts|=debt.source=="part"; t.hasInventory|=debt.source=="inventory";
            t.hasInvoices|=verified; t.hasManual|=!verified && debt.source!="inventory";
            if (debt.paidCents>0) t.partial+=remaining; else t.unpaid+=remaining;
            if (late) t.maxLateDays=qMax(t.maxLateDays,lateDays);
            if (late) t.lateCents+=remaining;
            t.maxAge=qMax(t.maxAge,purchaseAge); if(purchaseAge<0) ++t.noAge;
            if (lateDays<0) { ++t.noDate; t.undatedCents+=remaining; }
            if (operational) {
                if(lateDays>7) ++t.overSeven; else if(lateDays>=5) ++t.critical; else ++t.warning;
            }
        };
        add(all); add(suppliers[supplier]);
        ++aging[bucket].first; aging[bucket].second+=remaining;
        QString circumstance;
        const QString orderState=hasPart?part.orderStatus.trimmed().toLower():QString();
        const bool closed=hasPart && (part.orderClosedDate.isValid() || orderState=="closed" || orderState=="completed"
            || orderState==QString::fromUtf8("закрито") || orderState==QString::fromUtf8("закритий"));
        if(closed) {
            circumstance=QString::fromUtf8("Наряд закрито; борг постачальнику не погашено");
        } else if (hasPart && !part.installed) {
            circumstance=QString::fromUtf8("Не встановлено; оплата не завершена");
        } else if (hasPart && part.installed) {
            circumstance=QString::fromUtf8("Встановлено; оплата не завершена");
        } else if (late) {
            circumstance=QString::fromUtf8("Строк оплати минув; залишок не погашено");
        } else if (debt.paidCents>0) {
            circumstance=QString::fromUtf8("Часткова оплата; залишок не погашено");
        } else {
            circumstance=QString::fromUtf8("Борг постачальнику не погашено");
        }
        auto& cause=causes[circumstance]; ++cause.first; cause.second+=remaining;
        if(hasPart && period.days>=0) {
            QStringList dates;
            if(ordered.isValid()) dates<<QString::fromUtf8("Замовлення: %1").arg(ordered.toString("dd.MM.yyyy"));
            const auto delivered=PartsQueryService::parseBusinessDate(part.deliveryDateRaw);
            const auto installed=PartsQueryService::parseBusinessDate(part.installedAtRaw);
            if(delivered.isValid()) dates<<QString::fromUtf8("Доставка: %1").arg(delivered.toString("dd.MM.yyyy"));
            if(installed.isValid()) dates<<QString::fromUtf8("Встановлення: %1").arg(installed.toString("dd.MM.yyyy"));
            if(delivered.isValid()) {
                if(ordered.daysTo(delivered)>0) dates<<QString::fromUtf8("До доставки: %1 дн").arg(ordered.daysTo(delivered));
                const auto end=installed.isValid()?installed:today;
                if(delivered.daysTo(end)>0) dates<<QString::fromUtf8("після доставки: %1 дн").arg(delivered.daysTo(end));
            }
            circumstance+=". "+dates.join("; ");
        }
        if(verified) {
            circumstance+=QString::fromUtf8(". Накладна №%1 від %2; підтверджено %3 грн, кількість %4")
                .arg(fact->invoiceNumber.isEmpty()?QString::fromUtf8("не задано"):fact->invoiceNumber,
                     fact->invoiceDate.isEmpty()?QString::fromUtf8("не задано"):fact->invoiceDate,
                     debtMoney(fact->purchaseCents),QString::number(fact->quantity,'g',12));
            if(fact->purchaseCents!=debt.totalCents || qAbs(fact->quantity-debt.quantity)>0.000001
               || fact->article!=debt.oem || fact->supplier!=debt.supplier) {
                ++differences;
                circumstance+=QString::fromUtf8(". Розбіжність з накладною; в обліку закупівля %1 грн").arg(debtMoney(debt.totalCents));
            }
        } else circumstance+=QString::fromUtf8(". Сума за обліком");
        if (debt.source=="part" && !hasPart) ++unknownContext;
        if (hasPart && period.days<0) ++missingDelivery;
        const QString state=debt.source=="inventory" ? QString::fromUtf8("Матеріал складу") : !hasPart ? QString::fromUtf8("Немає даних") :
            (part.installed ? QString::fromUtf8("Встановлено") : QString::fromUtf8("Не встановлено"));
        const QString age=period.days>=0?QString::number(period.days):QString::fromUtf8("Не визначено");
        const auto source=debt.source=="remote_invoice"?QString::fromUtf8("Залишок за даними Еліт"):verified?QString::fromUtf8("Накладна №%1").arg(fact->invoiceNumber):debt.source=="inventory"?QString::fromUtf8("Склад СТО"):QString::fromUtf8("Ручний / старий запис");
        const auto lateText=lateDays>=0?QString::number(lateDays):QString::fromUtf8("Не визначено");
        const auto ageText=purchaseAge>=0?QString::number(purchaseAge):QString::fromUtf8("Не визначено");
        const auto link="record:"+QString::fromLatin1(QUrl::toPercentEncoding(key));
        if(view==0) {
            const auto received=hasPart?PartsQueryService::parseBusinessDate(part.deliveryDateRaw):QDate();
            const int days=hasPart && !part.installed && !closed && received.isValid() && received<=today
                ?received.daysTo(today):-1;
            if(controlRows.size()<8) {
                auto row=htmlRow({supplier,(verified?fact->name:debt.name)+(hasPart?" / "+part.carModel:QString()),
                    debt.orderNumber>0?QString::number(debt.orderNumber):QString(),
                    days>=0?QString::number(days):QString::fromUtf8("Немає даних"),
                    debt.dueDate.isValid()?debt.dueDate.toString("dd.MM.yy"):QString::fromUtf8("Не задано"),
                    lateText,debtMoney(remaining)},false,1,link);
                row.replace("<tr>",late?"<tr bgcolor='#ffe5e5'>":days>7?"<tr bgcolor='#fff0d5'>":"<tr bgcolor='#ffffff'>");
                controlRows.append(row);
            }
        }
        auto actionRow=htmlRow({supplier,QString::number(debt.orderNumber),verified?fact->name:debt.name,debtMoney(remaining),lateText,source},false,2,link);
        if(late) urgentRows.append(actionRow);
        if(lateDays<0) unknownRows.append(actionRow);
        if(hasPart && !part.installed && !closed) waitingRows.append(htmlRow({supplier,QString::number(debt.orderNumber),debt.name,age,state}));
        if (detailPage<0 || (all.count>qint64(qMax(0,detailPage))*100 && all.count<=qint64(qMax(0,detailPage)+1)*100)) {
            auto row=htmlRow({supplier,QString::number(debt.orderNumber),verified?fact->name:debt.name,debtMoney(debt.totalCents),
            debtMoney(debt.paidCents),debtMoney(remaining),debt.dueDate.isValid()?debt.dueDate.toString("dd.MM.yy"):QString::fromUtf8("Не задано"),
            lateText,ageText,source},false,2,link);
            details+=row;
            if(view<0 || detailPage<0 || debts.size()==1) details+="<tr><td colspan='10'>"+circumstance.toHtmlEscaped()+"</td></tr>";
        }
    }
    QString html=QString::fromUtf8("<html><head><style>body { font-family:'Segoe UI'; font-size:10pt; color:#21343e; } h2 { font-size:19pt; } h3 { font-size:13pt; margin-top:18px; margin-bottom:10px; } th { background:#edf2f5; font-size:9pt; } td { padding:9px; } a { color:#21627e; text-decoration:none; } </style></head><body>");
    if(view<0 || view==3) html+=QString::fromUtf8("<h2>%1</h2><p>Стан на %2</p>").arg(view==3?QString::fromUtf8("Стислий аналіз боргів"):QString::fromUtf8("Аналіз боргів"),today.toString("dd.MM.yy"));
    qint64 factual=all.remaining;
    if(!reconciliations.isEmpty()) {
        factual=0; const auto amounts=SupplierDebtReconciliationService::effectiveTotals(debts,reconciliations);
        for(auto it=amounts.cbegin();it!=amounts.cend();++it) if(supplierFilter.isEmpty() || it.key()==supplierFilter) factual+=it.value();
    }
    QList<QPair<QString,qint64>> metrics;
    if(all.remaining>0 || view==0) metrics.append({QString::fromUtf8("Борг за обліком"),all.remaining});
    if(!reconciliations.isEmpty() && factual>0) metrics.append({QString::fromUtf8("Після звірки"),factual});
    if(all.lateCents>0 || view==0) metrics.append({QString::fromUtf8("Прострочена оплата"),all.lateCents});
    if(all.undatedCents>0 || view==0) metrics.append({QString::fromUtf8("Без строку оплати"),all.undatedCents});
    if(!metrics.isEmpty()) html+="<table width='100%' cellspacing='6' cellpadding='12'><tr>";
    int metricIndex=0;
    const QStringList metricColors={"#253c48","#237054","#b52b33","#94620f"};
    const QStringList metricBackgrounds={"#f1f5f7","#eef6f2","#fff0f0","#fff7e8"};
    const QStringList metricNotes={QString::fromUtf8("%1 неоплачених позицій").arg(all.count),
        factual==all.remaining?QString::fromUtf8("Звірку збережено"):QString::fromUtf8("Різниця з обліком: %1 грн").arg(debtMoney(factual-all.remaining)),
        QString::fromUtf8("Після підтвердженого строку оплати"),QString::fromUtf8("%1 позицій потребують уточнення").arg(all.noDate)};
    for(const auto& metric:metrics)
    {
        const auto width=QString::number(100/metrics.size());
        if(view<0 || view==3) {
            html+="<td width='"+width+"%'>"+metric.first+"<br><b>"+debtMoney(metric.second)+QString::fromUtf8(" грн</b></td>");
            continue;
        }
        metricIndex=metric.first==QString::fromUtf8("Після звірки")?1:metric.first==QString::fromUtf8("Прострочена оплата")?2:metric.first==QString::fromUtf8("Без строку оплати")?3:0;
        const auto color=metricColors[metricIndex];
        html+="<td width='"+width+"%' valign='top' bgcolor='"+metricBackgrounds[metricIndex]+"' style='border:1px solid #c8d3db; border-bottom:2px solid #aabac5; border-top:3px solid "+color+";'>"
            +metric.first+"<p style='margin-top:8px; margin-bottom:8px; font-size:22pt; font-weight:600; color:"+color+";'>"
            +debtMoney(metric.second)+QString::fromUtf8(" грн</p><span style='font-size:9pt; color:#60717d;'>")
            +metricNotes[metricIndex]+"</span></td>";
        ++metricIndex;
    }
    if(!metrics.isEmpty()) html+="</tr></table>";
    if(view==0 && !controlRows.isEmpty()) {
        html+=QString::fromUtf8("<h3>Закупівлі, що потребують уваги</h3><table width='100%' cellspacing='1' cellpadding='6'>");
        html+=htmlRow({QString::fromUtf8("Постачальник"),QString::fromUtf8("Запчастина / авто"),QString::fromUtf8("Наряд"),
            QString::fromUtf8("На складі, днів"),QString::fromUtf8("Сплатити до"),QString::fromUtf8("Прострочено, днів"),QString::fromUtf8("Борг, грн")},true);
        html+=controlRows.join("")+"</table>";
        html+=QString::fromUtf8("<p>Показано %1 з %2 неоплачених позицій. Повний перелік у вкладці «Реєстр». Натисніть назву запчастини для оплати та переходу до джерела.</p>")
            .arg(controlRows.size()).arg(all.count);
    }
    QStringList evidenceLines;
    if(confirmed>0) evidenceLines<<QString::fromUtf8("За накладними: %1 позицій").arg(confirmed);
    if(confirmedPurchases>0) evidenceLines<<QString::fromUtf8("Закупівля: %1 грн").arg(debtMoney(confirmedPurchases));
    if(differences>0) evidenceLines<<QString::fromUtf8("Розбіжностей з обліком: %1").arg(differences);
    const QString evidenceSummary=evidenceLines.isEmpty()?QString():"<p style='font-size:9pt; color:#60717d;'>"+evidenceLines.join(" · ")+"</p>";
    if(view<0) {
    html+=evidenceSummary;
    QStringList totals;
    if(all.count>0) totals<<QString::fromUtf8("Позицій: %1").arg(all.count);
    if(all.partial>0) totals<<QString::fromUtf8("Залишок часткових оплат: %1 грн").arg(debtMoney(all.partial));
    if(all.warning>0) totals<<QString::fromUtf8("Не сплата 3 дні: %1").arg(all.warning);
    if(all.critical>0) totals<<QString::fromUtf8("Не сплата 5 днів: %1").arg(all.critical);
    if(all.overSeven>0) totals<<QString::fromUtf8("Не сплата більше 7 днів: %1").arg(all.overSeven);
    if(!totals.isEmpty()) html+="<p>"+totals.join(" · ")+"</p>";
    }
    if(view<0 && !reconciliations.isEmpty()) {
        const auto actual=SupplierDebtReconciliationService::effectiveTotals(debts,reconciliations);
        qint64 actualCents=0;
        for(auto it=actual.cbegin();it!=actual.cend();++it)
            if(supplierFilter.isEmpty() || supplierFilter==it.key()) actualCents+=it.value();
        if(actualCents>0) html+=QString::fromUtf8("<p><b>Фактичний борг після звірки: %1 грн.</b> Враховано зміни закупівель і оплат після звірки.</p>").arg(debtMoney(actualCents));
    }
    if(view==3) {
        html+=QString::fromUtf8("<h3>Борги за магазинами</h3><table width='100%' border='1' cellspacing='0' cellpadding='6'>");
        html+=htmlRow({QString::fromUtf8("Магазин"),QString::fromUtf8("Позицій"),QString::fromUtf8("За обліком, грн"),QString::fromUtf8("Після звірки, грн"),QString::fromUtf8("Прострочено, грн")},true);
        auto effective=SupplierDebtReconciliationService::effectiveTotals(debts,reconciliations);
        QSet<QString> stores;
        for(auto it=suppliers.cbegin();it!=suppliers.cend();++it) stores.insert(it.key());
        for(auto it=reconciliations.cbegin();it!=reconciliations.cend();++it) stores.insert(it.key());
        auto names=stores.values(); names.sort();
        for(const auto& name:names) {
            if(!supplierFilter.isEmpty() && name!=supplierFilter) continue;
            const auto t=suppliers.value(name);
            const auto actual=effective.value(name);
            if(t.remaining<=0 && actual<=0) continue;
            html+=htmlRow({name,QString::number(t.count),debtMoney(t.remaining),!reconciliations.isEmpty()?debtMoney(actual):QString(),debtMoney(t.lateCents)});
        }
        if(all.remaining>0 || factual>0) html+=htmlRow({QString::fromUtf8("Разом"),QString::number(all.count),debtMoney(all.remaining),!reconciliations.isEmpty()?debtMoney(factual):QString(),debtMoney(all.lateCents)});
        return html+"</table></body></html>";
    }
    if(view<0 || view==0) {
    html+=QString::fromUtf8("<h3>Постачальники та зобов'язання</h3><table width='100%' border='0' cellspacing='1' cellpadding='6'>");
    if(view<0) html+=htmlRow({QString::fromUtf8("Магазин"),QString::fromUtf8("Позицій"),QString::fromUtf8("Борг, грн"),QString::fromUtf8("Вік закупівлі, макс. дн"),QString::fromUtf8("Прострочено, грн"),QString::fromUtf8("Оплата прострочена, дн (макс.)"),QString::fromUtf8("Без строку, позицій"),QString::fromUtf8("Без строку, грн")},true);
    else html+=htmlRow({QString::fromUtf8("Постачальник"),QString::fromUtf8("Напрям"),QString::fromUtf8("Борг, грн"),QString::fromUtf8("Вік закупівлі, макс. дн"),QString::fromUtf8("Прострочено, грн"),QString::fromUtf8("Затримка оплати, макс. дн"),QString::fromUtf8("Джерело даних")},true);
    int supplierRow=0;
    for (auto it=suppliers.cbegin();it!=suppliers.cend();++it) {
        const auto& t=it.value();
        const QString maxDays=t.noDate==t.count?QString():QString::number(t.maxLateDays);
        const QString age=t.maxAge<0?QString():QString::number(t.maxAge);
        QStringList directions,sources;
        if(t.hasParts) directions<<QString::fromUtf8("Наряди");
        if(t.hasInventory) { directions<<QString::fromUtf8("Склад СТО"); sources<<QString::fromUtf8("Склад"); }
        if(t.hasInvoices) sources<<QString::fromUtf8("Накладні");
        if(t.hasManual) sources<<QString::fromUtf8("Ручні / старі записи");
        const QStringList cells=view<0?QStringList{it.key(),QString::number(t.count),debtMoney(t.remaining),age,
            debtMoney(t.lateCents),maxDays,QString::number(t.noDate),debtMoney(t.undatedCents)}:
            QStringList{it.key(),directions.join(", "),debtMoney(t.remaining),age,debtMoney(t.lateCents),maxDays,sources.join(", ")};
        auto row=htmlRow(cells,false,0,"supplier:"+QString::fromLatin1(QUrl::toPercentEncoding(it.key())));
        if(view==0) {
            const QString badgeColor=t.noDate==t.count?"#94620f":t.maxLateDays>0?"#b52b33":"#237054";
            row.replace("<td>"+maxDays.toHtmlEscaped()+"</td>","<td><span style='color:"+badgeColor+"; font-weight:600;'>"+maxDays.toHtmlEscaped()+"</span></td>");
        }
        row.replace("<tr>",supplierRow++%2?"<tr bgcolor='#f4f7f9'>":"<tr bgcolor='#ffffff'>");
        html+=row;
    }
    if(view==0 && all.remaining>0) html+=QString::fromUtf8("<tr bgcolor='#eaf0f4'><td><b>Разом за обліком</b></td><td></td><td><b>%1</b></td><td></td><td><b>%2</b></td><td></td><td>%3</td></tr>")
        .arg(debtMoney(all.remaining),all.lateCents>0?debtMoney(all.lateCents):QString(),all.undatedCents>0?QString::fromUtf8("Без строку: %1 грн").arg(debtMoney(all.undatedCents)):QString());
    html+="</table>";
    if(view==0) {
        html+=QString::fromUtf8("<table width='100%' cellspacing='0' cellpadding='0'><tr><td width='52%' valign='top'>");
        if(all.lateCents>0) html+=QString::fromUtf8("<h3>Оплата після строку</h3>");
        html+="<table width='100%' cellspacing='0' cellpadding='4'>";
        const QStringList labels={QString::fromUtf8("1–2 дні"),QString::fromUtf8("3–4 дні"),QString::fromUtf8("5–7 днів"),QString::fromUtf8("Понад 7 днів")};
        qint64 maximum=1;
        for(int i=2;i<=5;++i) maximum=qMax(maximum,aging[i].second);
        for(int i=2;i<=5;++i) {
            if(aging[i].second<=0) continue;
            const int fill=qBound(0,qRound(double(aging[i].second)/double(maximum)*100),100);
            html+="<tr><td width='23%'>"+labels[i-2]+"</td><td width='50%'><table width='100%' cellspacing='0' cellpadding='0'><tr>";
            if(fill>0) html+=QString("<td width='%1%' bgcolor='#bd4750' style='font-size:4pt;'>&nbsp;</td>").arg(fill);
            if(fill<100) html+=QString("<td width='%1%' bgcolor='#edf2f5' style='font-size:4pt;'>&nbsp;</td>").arg(100-fill);
            html+="</tr></table></td><td align='right'><b>"+debtMoney(aging[i].second)+QString::fromUtf8(" грн</b></td></tr>");
        }
        html+="</table>";
        if(aging[1].second>0) html+=QString::fromUtf8("<p>%1 грн: строк сьогодні або ще не настав.</p>").arg(debtMoney(aging[1].second));
        html+=QString::fromUtf8("</td><td width='3%'></td><td width='45%' valign='top'><h3>Витрати та рух коштів</h3><!--EXPENSES--></td></tr></table>");
    } else {
    html+=QString::fromUtf8("<h3>Строки погашення</h3><table width='100%' border='0' cellspacing='1' cellpadding='6'>");
    html+=htmlRow({QString::fromUtf8("Строк оплати"),QString::fromUtf8("Позицій"),QString::fromUtf8("Борг, грн")},true);
    const QStringList buckets={QString::fromUtf8("Строк оплати не задано"),QString::fromUtf8("Строк сьогодні або ще не настав"),QString::fromUtf8("Прострочено 1–2 дні"),
        QString::fromUtf8("Не сплата 3 дні (3–4)"),QString::fromUtf8("Не сплата 5 днів (5–7)"),QString::fromUtf8("Не сплата більше 7 днів")};
    for (int i=0;i<buckets.size();++i) if(aging[i].second>0) html+=htmlRow({buckets[i],QString::number(aging[i].first),debtMoney(aging[i].second)});
    html+="</table>";
    }
    }
    if((view<0 || view==1) && all.count>0) {
        auto actionTable=[&](const QString& title,const QStringList& rows,const QStringList& headers) {
            if(rows.isEmpty()) return QString();
            QString block="<h3>"+title+QString::fromUtf8(" · %1 позицій</h3>").arg(rows.size());
            block+="<table width='100%' cellspacing='1' cellpadding='6'>"+htmlRow(headers,true);
            const int start=detailPage<0?0:qMin(rows.size(),qMax(0,detailPage)*100);
            const int end=detailPage<0?rows.size():qMin(rows.size(),start+100);
            for(int i=start;i<end;++i) block+=rows[i];
            return block+"</table>";
        };
        const QStringList headers={QString::fromUtf8("Магазин"),QString::fromUtf8("Наряд"),QString::fromUtf8("Позиція"),QString::fromUtf8("Борг, грн"),QString::fromUtf8("Затримка оплати, дн"),QString::fromUtf8("Джерело")};
        html+=actionTable(QString::fromUtf8("Сплатити прострочені зобов'язання"),urgentRows,headers);
        html+=actionTable(QString::fromUtf8("Уточнити строк оплати"),unknownRows,headers);
        html+=actionTable(QString::fromUtf8("Очікують встановлення"),waitingRows,{QString::fromUtf8("Магазин"),QString::fromUtf8("Наряд"),QString::fromUtf8("Позиція"),QString::fromUtf8("Днів від замовлення"),QString::fromUtf8("Стан")});
        html+=QString::fromUtf8("<p>Суми черг дій не додаються: одна закупівля може бути неоплаченою та очікувати встановлення. Для оплати відкрийте вкладку «Борги».</p>");
    }
    if((view<0 || view==1) && !causes.isEmpty()) {
    html+=QString::fromUtf8("<h3>Обставини заборгованості</h3><table border='1' cellspacing='0' cellpadding='6'>");
    html+=htmlRow({QString::fromUtf8("Підтверджена обставина"),QString::fromUtf8("Позицій"),QString::fromUtf8("Борг, грн")},true);
    for (auto it=causes.cbegin();it!=causes.cend();++it)
        html+=htmlRow({it.key(),QString::number(it->first),debtMoney(it->second)});
    html+="</table>";
    }
    if(!reconciliations.isEmpty()) {
        html+=QString::fromUtf8("<h3>Звірка магазинів</h3><table border='1' cellspacing='0' cellpadding='6'>");
        html+=htmlRow({QString::fromUtf8("Магазин"),QString::fromUtf8("За системою на дату звірки, грн"),QString::fromUtf8("Фактичний борг, грн"),QString::fromUtf8("Різниця, грн"),QString::fromUtf8("Дата звірки"),QString::fromUtf8("Висновок за звіркою")},true);
        for(auto it=reconciliations.cbegin();it!=reconciliations.cend();++it) {
            if(!supplierFilter.isEmpty() && supplierFilter!=it.key()) continue;
            const auto& r=it.value();
            if(r.actualCents<=0 && r.computedCents<=0) continue;
            html+=htmlRow({r.supplier,debtMoney(r.computedCents),debtMoney(r.actualCents),debtMoney(r.actualCents-r.computedCents),r.checkedAt,
                r.differs()?QString::fromUtf8("Розбіжність за звіркою"):QString::fromUtf8("Борг підтверджено")});
        }
        html+="</table>";
    }
    if((view<0 || view==2) && all.count>0) {
    html+=QString::fromUtf8("<h3>Єдиний реєстр боргів</h3><p>");
    html+=detailPage<0?QString::fromUtf8("Повний перелік: %1 позицій.").arg(all.count):
        QString::fromUtf8("Сторінка %1, до 100 позицій. Підсумки охоплюють усі %2 позицій вибраного магазину або всіх магазинів.").arg(qMax(0,detailPage)+1).arg(all.count);
    html+="</p><table border='1' cellspacing='0' cellpadding='6'>";
    html+=htmlRow({QString::fromUtf8("Магазин"),QString::fromUtf8("Наряд"),QString::fromUtf8("Позиція"),QString::fromUtf8("Закупівля, грн"),QString::fromUtf8("Сплачено, грн"),QString::fromUtf8("Борг, грн"),QString::fromUtf8("Сплатити до"),QString::fromUtf8("Затримка оплати, дн"),QString::fromUtf8("Вік закупівлі, дн"),QString::fromUtf8("Джерело")},true);
    html+=details+"</table>";
    }
    if (!all.count) html+=reconciliations.isEmpty()?QString::fromUtf8("<p>Непогашених боргів немає.</p>"):
        QString::fromUtf8("<p>Непогашених позицій за обліком немає; фактичний борг наведено за звіркою.</p>");
    if(view>=0) html+=evidenceSummary;
    return html+"</body></html>";
}
