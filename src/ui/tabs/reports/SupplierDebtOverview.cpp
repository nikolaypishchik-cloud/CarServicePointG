#include "ui/tabs/reports/SupplierDebtOverview.h"
#include "core/services/ElitDocumentAssociation.h"
#include "core/services/AccessControlService.h"
#include "core/services/AppRuntimeService.h"
#include "core/util/StoreNameNormalizer.h"
#include "core/util/FormatUtils.h"
#include "core/services/SupplierDebtApplicationService.h"
#include "runtime/AsyncThreadScope.h"
#include "ui/tabs/orders/CurrentOrdersCoordinator.h"
#include "ui/common/dialogs/ElitInvoicePdfOpen.h"
#include <QVBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QLabel>
#include <QTableView>
#include "ui/tabs/reports/ReportsTableSupport.h"
#include <QHeaderView>
#include <QDialog>
#include <QTextBrowser>
#include <QMessageBox>
#include <QTimer>
#include <QResizeEvent>
#include <algorithm>
#include <utility>
#include <QComboBox>
#include <QTreeWidget>
#include <QPainter>
#include "core/services/SupplierDebtReconciliationService.h"

namespace {
class DebtTile final : public QPushButton {
public:
    DebtTile(int index,QWidget* parent):QPushButton(parent),m_index(index) {}
protected:
    void paintEvent(QPaintEvent*) override {
        static const QStringList colors={"#253c48","#b52b33","#94620f","#237054","#41658a"};
        static const QStringList backgrounds={"#f1f5f7","#fff0f0","#fff7e8","#eef6f2","#eff4fa"};
        QPainter painter(this);
        painter.fillRect(rect(),QColor(backgrounds[m_index]));
        painter.setPen(QPen(QColor(isChecked() || hasFocus()?colors[m_index]:"#aabac5"),isChecked()?2:1));
        painter.drawRect(rect().adjusted(1,1,-2,-2));
        painter.fillRect(0,0,width(),4,QColor(colors[m_index]));
        const auto lines=text().split('\n');
        // Theme sizing can shrink buttons after construction; use their actual height.
        const int inset=qMin(12,qMax(4,width()/20));
        const int top=7, usable=qMax(3,height()-top-5);
        const int titleHeight=usable*35/100, amountHeight=usable*43/100;
        const QRect title(inset,top,width()-2*inset,titleHeight);
        const QRect amount(inset,title.bottom()+1,width()-2*inset,amountHeight);
        const QRect count(inset,amount.bottom()+1,width()-2*inset,usable-titleHeight-amountHeight);
        auto draw=[&](const QRect& box,const QString& value,int points,bool bold,const QString& color,bool wrap) {
            QFont f("Segoe UI",points);f.setWeight(bold?QFont::DemiBold:QFont::Normal);
            const int flags=(wrap?Qt::AlignLeft:Qt::AlignHCenter)|Qt::AlignVCenter|(wrap?Qt::TextWordWrap:Qt::TextSingleLine);
            while(f.pointSizeF()>6) {
                const auto bounds=QFontMetrics(f).boundingRect(box,flags,value);
                if(bounds.width()<=box.width() && bounds.height()<=box.height()) break;
                f.setPointSizeF(f.pointSizeF()-0.5);
            }
            painter.setFont(f);painter.setPen(QColor(color));painter.drawText(box,flags,value);
        };
        draw(title,lines.value(0),10,false,"#253c48",true);
        draw(amount,lines.value(1),20,true,colors[m_index],false);
        draw(count,lines.value(2),9,false,"#60717d",false);
    }
private:
    int m_index;
};
const QStringList titles={QString::fromUtf8("Загальний борг"),QString::fromUtf8("Прострочено"),
    QString::fromUtf8("Сплатити цього тижня")};
const QStringList headers={QString::fromUtf8("Постачальник"),QString::fromUtf8("Накладна"),QString::fromUtf8("Дата"),
    QString::fromUtf8("Сплатити до"),QString::fromUtf8("Протерміновано\nдн"),QString::fromUtf8("Сума, грн"),
    QString::fromUtf8("Борг, грн"),QString::fromUtf8("Наряд / склад"),QString::fromUtf8("Звірка")};
QString money(qint64 cents) { return FormatUtils::money(cents/100.0,2); }
QString supplier(const QString& value) { return StoreNameNormalizer::canonicalDisplayNameOrDefault(value,QString::fromUtf8("Без магазину")); }
QString cell(const QString& value) { return "<td>"+value.toHtmlEscaped()+"</td>"; }
}
SupplierDebtOverview::SupplierDebtOverview(QWidget* parent):QWidget(parent) {
    setObjectName("supplierDebtOverview");
    auto* root=new QVBoxLayout(this); root->setContentsMargins(0,0,0,0); root->setSpacing(10);
    m_grid=new QGridLayout; m_grid->setSpacing(8); root->addLayout(m_grid);
    for(int i=0;i<titles.size();++i) {
        auto* tile=new DebtTile(i,this);tile->setText(titles[i]); tile->setObjectName("debtTile"+QString::number(i));
        tile->setCheckable(true); tile->setMinimumHeight(160); tile->setMinimumWidth(0);
        tile->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
        tile->setToolTip(titles[i]);
        tile->setStyleSheet("QPushButton{background:#eef3f5;color:#253541;border:1px solid #aebdc8;border-bottom:3px solid #8ba4b4;border-radius:6px;padding:8px;font:11pt 'Segoe UI';} QPushButton:checked{border:2px solid #c38b2e;background:#fff2d6;color:#604613;} QPushButton:hover{background:#e0ebf1;}");
        connect(tile,&QPushButton::clicked,this,[this,i]{m_filter=i;m_page=0;render();}); m_tiles.append(tile); m_grid->addWidget(tile,0,i);
        m_grid->setColumnStretch(i,1);
    }
    auto* bar=new QHBoxLayout;
    m_status=new QLabel(this); m_status->setWordWrap(true); bar->addWidget(m_status,1);
    m_status->setStyleSheet("color:#253541;background:#eef3f5;padding:6px;font:10pt 'Segoe UI';");
    root->addLayout(bar);
    m_status->hide();
    m_table=new QTableView(this); m_model=new ReportsTableSupport::ReportsSnapshotTableModel(m_table); m_table->setModel(m_model); m_table->setObjectName("supplierInvoiceLedger");
    m_model->setSnapshot(headers,{}); m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows); m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->viewport()->setCursor(Qt::PointingHandCursor);
    m_table->setWordWrap(true); m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->horizontalHeader()->setMinimumSectionSize(35);
    m_table->horizontalHeader()->setFixedHeight(44);
    m_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_table->setStyleSheet("QTableView{background:#f5f7f8;color:#17232a;gridline-color:#c7d0d6;font:10pt 'Segoe UI';selection-background-color:#d3e5dc;selection-color:#17232a;} QHeaderView::section{background:#dfe6eb;color:#17232a;font:9pt 'Segoe UI';padding:3px;border:1px solid #c7d0d6;}");
    root->addWidget(m_table,1);
    connect(m_table,&QTableView::clicked,this,[this](const QModelIndex& index){
        const int row=index.row(), column=index.column();
        if(column==1 && authorized() && row>=0 && row+m_page*100<m_visible.size()) {
            const auto& entry=m_ledger.entries[m_visible[row+m_page*100]];
            if(entry.remote) {openElitInvoicePdf(this,entry.key);return;}
        }
        openLocation(row);
    });
    auto* open=new QPushButton(QString::fromUtf8("Деталі накладної"),this); open->setObjectName("debtInvoiceDetails");
    auto* navigation=new QHBoxLayout;
    m_previous=new QPushButton(QString::fromUtf8("Попередні 100"),this);
    m_next=new QPushButton(QString::fromUtf8("Наступні 100"),this);
    m_previous->setEnabled(false);m_next->setEnabled(false);
    navigation->addWidget(m_previous);navigation->addWidget(m_next);navigation->addStretch();navigation->addWidget(open);root->addLayout(navigation);
    connect(m_previous,&QPushButton::clicked,this,[this]{--m_page;render();});
    connect(m_next,&QPushButton::clicked,this,[this]{++m_page;render();});
    connect(open,&QPushButton::clicked,this,[this]{details(m_table->currentIndex().row());});
    for(auto* tile:m_tiles) tile->setEnabled(false);
}
SupplierDebtOverview::~SupplierDebtOverview(){ if(m_cancel) m_cancel->store(true); }
bool SupplierDebtOverview::authorized() const {
    if(m_scope!=AccessControlService::currentScopeKey()) return false;
    GeneralSettingsService::ElitCredentials credentials;
    return GeneralSettingsService::loadElitCredentials(&credentials) && ElitInvoiceStore::accountKey(credentials)==m_account;
}
void SupplierDebtOverview::refresh() {
    const auto scope=AccessControlService::currentScopeKey();
    GeneralSettingsService::ElitCredentials credentials; QString error;
    const bool credentialsOk=GeneralSettingsService::loadElitCredentials(&credentials,&error);
    const auto account=ElitInvoiceStore::accountKey(credentials);
    // Finish the current snapshot instead of starving it with repeated refreshes.
    if(m_loading && credentialsOk && m_scope==scope && m_account==account) {
        m_refreshPending=true;
        return;
    }
    if(m_cancel) m_cancel->store(true);
    const auto generation=++m_generation;
    m_loading=false; m_refreshPending=false;
    if(m_scope!=scope || m_account!=account) { m_loaded=false;m_ledger={};m_visible.clear();m_model->setSnapshot(headers,{}); }
    m_scope=scope; m_account=account;
    if(!SupplierDebtApplicationService::canRead() || !credentialsOk) {
        m_loaded=false;m_ledger={};m_visible.clear();m_model->setSnapshot(headers,{});
        for(auto* tile:m_tiles){tile->setEnabled(false);tile->setText(titles[m_tiles.indexOf(tile)]);}
        m_status->setText(credentialsOk?QString::fromUtf8("Повний реєстр постачальників недоступний для цієї ролі."):error);m_status->show(); return;
    }
    m_status->setText(QString::fromUtf8("Оновлення реєстру накладних…"));
    m_status->show();
    auto result=std::make_shared<SupplierInvoiceLedger::Ledger>();
    auto summary=std::make_shared<SupplierInvoiceLedger::Summary>();
    auto cancel=std::make_shared<std::atomic_bool>(false); m_cancel=cancel;
    m_loading=true;
    const auto read=SupplierDebtApplicationService::prepareRead(account);
    auto* worker=AsyncThreadScope::startWorker(this,"supplier_invoice_overview",cancel,[result,summary,read,cancel] {
        read(*result,*summary,*cancel);
    });
    connect(worker,&QThread::finished,this,[this,result,summary,cancel,generation] {
        if(generation!=m_generation) return;
        m_loading=false;
        const bool pending=std::exchange(m_refreshPending,false);
        if(pending) QTimer::singleShot(0,this,&SupplierDebtOverview::refresh);
        if(!authorized()) {
            m_loaded=false;m_ledger={};m_visible.clear();m_model->setSnapshot(headers,{});
            m_status->setText(QString::fromUtf8("Доступ або облікові дані змінено. Оновіть реєстр."));m_status->show();return;
        }
        if(cancel->load()) {
            m_status->setText(QString::fromUtf8("Не вдалося завершити оновлення. Спробуйте ще раз."));m_status->show();return;
        }
        if(!result->error.isEmpty()){m_status->setText(QString::fromUtf8("Не вдалося оновити. Попередні дані збережено: ")+result->error);m_status->show();return;}
        m_ledger=*result;m_summary=*summary;m_loaded=true;
        m_remoteBalances=SupplierInvoiceLedger::hasRemoteBalances(m_ledger);
        m_status->hide();
        QStringList stores;for(const auto& entry:m_ledger.entries) stores.append(supplier(entry.supplier));
        stores.append(m_summary.totals.keys());
        stores.removeDuplicates(); stores.sort(); if(suppliersChanged) suppliersChanged(stores);
        render();
    });worker->start();
}
void SupplierDebtOverview::setFilters(const QString& store,const QString& source,const QString& search){
    if(m_supplier==store && m_source==source && m_search==search.trimmed() && authorized()) return;
    if(m_supplier!=store || m_source!=source || m_search!=search.trimmed()) m_page=0;
    m_supplier=store;m_source=source;m_search=search.trimmed();render();
}
bool SupplierDebtOverview::accepts(const SupplierInvoiceLedger::Entry& e,int filter) const {
    if(e.remaining<=0) return false;
    if(!m_supplier.isEmpty() && supplier(e.supplier)!=m_supplier) return false;
    if(m_source=="invoice" && e.number.isEmpty()) return false;
    if(m_source=="manual" && (e.remote || !e.number.isEmpty())) return false;
    if(m_source=="inventory" && std::none_of(e.items.cbegin(),e.items.cend(),[](const auto& item){return item.purchase.source=="inventory";})) return false;
    QString text=e.supplier+" "+e.number;
    for(const auto& line:e.lines) text+=" "+line.reference+" "+line.itemCode+" "+line.description;
    for(const auto& item:e.items) text+=" "+item.purchase.name+" "+item.purchase.oem+" "+QString::number(item.purchase.orderNumber);
    if(!m_search.isEmpty() && !text.contains(m_search,Qt::CaseInsensitive)) return false;
    const auto today=QDate::currentDate(); const auto monday=today.addDays(1-today.dayOfWeek());
    const bool counted=m_remoteBalances && SupplierInvoiceLedger::isElit(e.supplier)?e.remote:e.counted;
    switch(filter) {
        case 0:return counted && e.remaining>0;
        case 1:return counted && e.remaining>0 && e.due.isValid() && e.due<today;
        case 2:return counted && e.remaining>0 && e.due.isValid() && e.due>=monday && e.due<=monday.addDays(6);
        case 3:return e.remote && (e.isNew || !e.matched || !e.counted);
        case 4:return e.remote && (e.discrepancy || !e.counted);
        default:return e.remaining>0 || e.discrepancy || (e.remote && !e.counted);
    }
}
QStringList SupplierDebtOverview::columns(const SupplierInvoiceLedger::Entry& e) const {
    QStringList locations;
    for(const auto& item:e.items) {
        if(item.purchase.source=="inventory") locations.append(QString::fromUtf8("Склад СТО"));
        for(int order:item.orders) locations.append(QString::fromUtf8("№%1").arg(order));
    }locations.removeDuplicates();
    const int overdue=e.remaining>0 && e.due.isValid()?qMax(0,int(e.due.daysTo(QDate::currentDate()))):0;
    return {supplier(e.supplier),e.number.isEmpty()?QString::fromUtf8("Без накладної"):e.number,
        e.date.toString("dd.MM.yy"),e.due.isValid()?e.due.toString("dd.MM.yy"):QString::fromUtf8("Не задано"),
        QString::number(overdue),money(e.total)+(e.currency=="UAH"?QString():" "+e.currency),
        money(e.remaining)+(e.currency=="UAH"?QString():" "+e.currency),locations.join(", "),
        e.missingFromLatest?QString::fromUtf8("Статус оплати не підтверджено"):e.remote && !e.counted?QString::fromUtf8("Потрібна звірка"):e.remote && e.discrepancy?QString::fromUtf8("Розбіжність оплат"):e.reason};
}
void SupplierDebtOverview::render() {
    if(!authorized()) {
        m_loaded=false;m_ledger={};m_visible.clear();m_model->setSnapshot(headers,{});
        for(int i=0;i<m_tiles.size();++i){m_tiles[i]->setEnabled(false);m_tiles[i]->setText(titles[i]);}
        m_status->clear();return;
    }
    if(!m_loaded) return;
    for(int i=0;i<m_tiles.size();++i) {
        qint64 amount=0;int count=0;bool foreign=false;
        for(const auto& entry:m_ledger.entries) if(accepts(entry,i)) {
            ++count;if(entry.currency=="UAH") amount+=i==3?entry.total:entry.remaining;else foreign=true;
        }
        if(i==0 && m_source.isEmpty() && m_search.isEmpty()) {
            amount=0;
            for(auto it=m_summary.totals.cbegin();it!=m_summary.totals.cend();++it)
                if(m_supplier.isEmpty() || m_supplier==it.key()) amount+=it.value();
            if(m_supplier.isEmpty()) count=m_summary.count;
        }
        m_tiles[i]->setEnabled(true);m_tiles[i]->setChecked(i==m_filter);
        m_tiles[i]->setText(titles[i]+"\n"+money(amount)+QString::fromUtf8(" грн\n%1 позицій").arg(count)+(foreign?" *":""));
        m_tiles[i]->setToolTip(titles[i]+(foreign?QString::fromUtf8(". Суми в інших валютах не включено до підсумку в гривнях."):QString()));
    }
    m_table->setUpdatesEnabled(false);m_model->setSnapshot(headers,{});m_visible.clear();
    for(int index=0;index<m_ledger.entries.size();++index) {
        const auto& entry=m_ledger.entries[index];if(!accepts(entry,m_filter)) continue;
        m_visible.append(index);
    }
    m_page=qBound(0,m_page,qMax(0,(int(m_visible.size())-1)/100));
    m_previous->setEnabled(m_page>0);m_next->setEnabled((m_page+1)*100<m_visible.size());
    QVector<ReportsTableSupport::ReportsSnapshotTableModel::Row> rows;
    for(int position=m_page*100;position<qMin(int(m_visible.size()),(m_page+1)*100);++position) {
        const auto& entry=m_ledger.entries[m_visible[position]];
        ReportsTableSupport::ReportsSnapshotTableModel::Row row;
        const auto values=columns(entry);
        if(entry.remaining>0 && entry.due.isValid() && entry.due<QDate::currentDate()) row.background=QColor("#f9d9db");
        else if(entry.remote && !entry.counted) row.background=QColor("#fff1c9");
        for(int col=0;col<values.size();++col) {
            ReportsTableSupport::ReportsSnapshotTableModel::Cell value;
            value.text=values[col];value.tooltip=col==8?entry.reason:values[col];value.link=true;
            row.cells.append(value);
        }
        rows.append(row);
    }
    m_model->setSnapshot(headers,rows);
    m_table->resizeRowsToContents();m_table->setUpdatesEnabled(true);
    m_status->setText((m_filter<0?QString::fromUtf8("Усі накладні"):titles[m_filter])+QString::fromUtf8(" · %1 позицій. ").arg(m_visible.size())
        +(m_ledger.snapshot.fetchedAt.isValid()?QString::fromUtf8("Еліт: %1. ").arg(m_ledger.snapshot.fetchedAt.toLocalTime().toString("dd.MM.yy HH:mm")):QString::fromUtf8("Еліт ще не синхронізовано. "))
        +QString::fromUtf8("Борг Еліт — за останніми відомими даними кожної накладної. Для відсутніх у поточному списку документів збережено попередній залишок; статус оплати не підтверджено. Локальні закупівлі Еліт повторно не додаються. Інші постачальники — за локальним обліком.")
        +(!m_summary.manualSuppliers.isEmpty()?QString::fromUtf8(" Ручна звірка має пріоритет: %1. Її сума не розподіляється по строках накладних.").arg(QStringList(m_summary.manualSuppliers.values()).join(", ")):QString())
        +(!m_source.isEmpty() || !m_search.isEmpty()?QString::fromUtf8(" Пошук/джерело: суми лише показаних позицій, без загальних коригувань."):QString())
        +(m_ledger.hasUnresolved?QString::fromUtf8(" Є незвірені накладні: загальна сума ще не остаточна."):QString()));
}
QString SupplierDebtOverview::reportHtml() const {
    if(!m_loaded || !authorized()) return {};
    QString html="<html><body><h2>"+QString::fromUtf8("Аналіз боргів")+"</h2><p>"+m_status->text().toHtmlEscaped()+"</p>";
    if(m_source.isEmpty() && m_search.isEmpty()) {
        html+=QString::fromUtf8("<h3>Загальний борг за постачальниками</h3><table border='1' cellspacing='0' cellpadding='4'>");
        qint64 total=0;
        for(auto it=m_summary.totals.cbegin();it!=m_summary.totals.cend();++it) if(m_supplier.isEmpty() || m_supplier==it.key()) {
            total+=it.value();html+="<tr>"+cell(it.key())+cell(money(it.value()))+cell(m_summary.manualSuppliers.contains(it.key())?QString::fromUtf8("Ручна звірка"):QString::fromUtf8("Накладні та локальний облік"))+"</tr>";
        }
        html+="<tr>"+cell(QString::fromUtf8("Разом, грн"))+cell(money(total))+"</tr></table>";
    }
    html+="<table width='100%' border='1' cellspacing='0' cellpadding='3' style='font-size:8pt;'><tr>";
    for(auto header:headers) html+="<th>"+header.replace('\n',' ').toHtmlEscaped()+"</th>";
    html+="</tr>";for(int index:m_visible){html+="<tr>";for(const auto& value:columns(m_ledger.entries[index])) html+=cell(value);html+="</tr>";}
    return html+"</table></body></html>";
}
void SupplierDebtOverview::openLocation(int row) {
    if(!authorized() || row<0 || row+m_page*100>=m_visible.size())return;
    const auto entry=m_ledger.entries[m_visible[row+m_page*100]];
    if(entry.items.size()!=1){details(row);return;}
    const auto purchase=entry.items.first().purchase;
    if(purchase.source=="inventory") {
        if(inventoryRequested)inventoryRequested(purchase.id);
    } else if(purchase.orderNumber>0) {
        const auto result=CurrentOrdersCoordinator::openExistingOrderEditor(this,purchase.orderNumber,{});
        if(!result.launched)QMessageBox::warning(this,QString::fromUtf8("Наряд"),result.errorText);
    } else details(row);
}
void SupplierDebtOverview::details(int row) {
    if(!authorized() || row<0 || row>=m_model->rowCount() || row+m_page*100>=m_visible.size()) return;
    const auto entry=m_ledger.entries[m_visible[row+m_page*100]];
    QDialog dialog(this);dialog.setWindowTitle(QString::fromUtf8("Накладна %1 · %2").arg(entry.number,entry.supplier));dialog.resize(850,520);
    auto* layout=new QVBoxLayout(&dialog);auto* browser=new QTextBrowser(&dialog);layout->addWidget(browser,1);
    QString html="<h3>"+entry.number.toHtmlEscaped()+"</h3><p>"+entry.reason.toHtmlEscaped()+" · "+entry.currency.toHtmlEscaped()+"</p><table width='100%' border='1' cellspacing='0' cellpadding='5'><tr><th>OEM</th><th>"+QString::fromUtf8("Запчастина</th><th>Кількість</th><th>Ціна</th><th>Сума</th><th>Прив’язка</th></tr>");
    auto unit=[](qint64 total,double quantity){return quantity>0?FormatUtils::money(total/100.0/quantity,2):QString();};
    for(int i=0;i<entry.lines.size();++i){const auto& line=entry.lines[i];html+="<tr>"+cell(line.reference.trimmed().isEmpty()?line.itemCode:line.reference)+cell(line.description)+cell(QString::number(line.quantity))+cell(unit(line.grossCents,line.quantity))+cell(money(line.grossCents))+cell(!entry.matched || entry.links.value(i).isEmpty()?QString::fromUtf8("Не прив’язано"):QString::fromUtf8("Прив’язано"))+"</tr>";}
    if(entry.lines.isEmpty()) for(const auto& item:entry.items) html+="<tr>"+cell(item.purchase.oem)+cell(item.purchase.name)+cell(QString::number(item.purchase.quantity))+cell(unit(item.purchase.totalCents,item.purchase.quantity))+cell(money(item.purchase.totalCents))+cell(QString::fromUtf8("Локальний облік"))+"</tr>";
    html+="</table>";
    if(!entry.allocations.isEmpty()) {
        html+=QString::fromUtf8("<h3>Розподіл закупівель між рядками</h3><table width='100%' border='1' cellspacing='0' cellpadding='5'><tr><th>Рядок</th><th>Закупівля</th><th>Кількість</th><th>Вартість, грн</th></tr>");
        for(const auto& allocation:entry.allocations) {
            QString location=allocation.purchaseKey;
            for(const auto& item:entry.items)if(item.key==allocation.purchaseKey) {
                const auto& p=item.purchase;
                location=(p.source=="inventory"?QString::fromUtf8("Склад СТО · #%1").arg(p.id):QString::fromUtf8("Наряд №%1 · #%2").arg(p.orderNumber).arg(p.id))+" · "+p.name;break;
            }
            html+="<tr>"+cell(QString::number(allocation.line+1))+cell(location)+cell(QString::number(allocation.quantity))+cell(money(allocation.totalCents))+"</tr>";
        }
        html+="</table>";
    }
    browser->setHtml(html);
    QSet<QString> actions;
    auto* destinations=new QComboBox(&dialog);
    QVector<PurchaseDebt> purchases;
    for(const auto& item:entry.items) {
        const auto purchase=item.purchase;
        const auto key=purchase.source=="inventory"?"inventory:"+QString::number(purchase.id):"order:"+QString::number(purchase.orderNumber);
        if(actions.contains(key)) continue;actions.insert(key);
        destinations->addItem(purchase.source=="inventory"?QString::fromUtf8("Склад СТО · %1").arg(purchase.name):QString::fromUtf8("Наряд №%1").arg(purchase.orderNumber));
        purchases.append(purchase);
    }
    if(!purchases.isEmpty()) {
        auto* navigation=new QHBoxLayout;navigation->addWidget(destinations,1);
        auto* button=new QPushButton(QString::fromUtf8("Перейти до обліку"),&dialog);navigation->addWidget(button);layout->addLayout(navigation);
        connect(button,&QPushButton::clicked,&dialog,[&]{
            const auto purchase=purchases.value(destinations->currentIndex());
            if(!authorized()){dialog.reject();return;}
            if(purchase.source=="inventory") {dialog.accept();QTimer::singleShot(0,this,[this,purchase]{if(authorized() && inventoryRequested)inventoryRequested(purchase.id);});}
            else {const auto result=CurrentOrdersCoordinator::openExistingOrderEditor(&dialog,purchase.orderNumber,{});if(!result.launched)QMessageBox::warning(&dialog,QString::fromUtf8("Наряд"),result.errorText);}
        });
    } else destinations->hide();
    if(entry.remote && SupplierDebtApplicationService::canReconcile()) {
        auto* documentMatch=new QPushButton(QString::fromUtf8("Прив’язати документ до запчастин…"),&dialog);
        documentMatch->setObjectName("elitDocumentCandidates");layout->addWidget(documentMatch);
        connect(documentMatch,&QPushButton::clicked,&dialog,[&]{
            const auto documentAccount=m_account;const auto documentScope=m_scope;
            QString error;const auto candidates=SupplierDebtApplicationService::candidates(documentAccount,documentScope,&error);
            if(!error.isEmpty()){QMessageBox::warning(&dialog,QString::fromUtf8("Прив’язка документа"),error);return;}
            QDialog review(&dialog);review.setWindowTitle(QString::fromUtf8("Прив’язка документа %1").arg(entry.number));review.resize(1000,480);
            auto* box=new QVBoxLayout(&review);
            auto* explanation=new QLabel(QString::fromUtf8("Перевірте обидві назви, артикули й дати. Вибрані зв’язки збережуться без зміни цін, оплат або боргів. Різні дати, префікси OEM та відмінні назви потребують вашого підтвердження."),&review);
            explanation->setWordWrap(true);explanation->setStyleSheet("color:#f2f4f6;font:11pt 'Segoe UI';padding:4px;");box->addWidget(explanation);
            auto* choices=new QTreeWidget(&review);choices->setObjectName("elitDocumentChoices");
            choices->setStyleSheet("QTreeWidget{background:#f5f7f8;color:#17232a;font:10pt 'Segoe UI';border:1px solid #a7b2ba;selection-background-color:#d3e5dc;selection-color:#17232a;} QTreeWidget::item{min-height:28px;} QHeaderView::section{background:#dfe6eb;color:#17232a;padding:5px;font:10pt 'Segoe UI';border:1px solid #c7d0d6;}");
            choices->setHeaderLabels({QString::fromUtf8("Наряд / запчастина"),QString::fromUtf8("OEM у наряді / накладній"),QString::fromUtf8("Дата додавання / накладної"),QString::fromUtf8("Назва в накладній"),QString::fromUtf8("Кількість")});box->addWidget(choices,1);
            for(int i=0;i<candidates.size();++i){const auto& c=candidates[i];if(c.invoiceId!=entry.key)continue;
                auto* row=new QTreeWidgetItem(choices,{QString::fromUtf8("№%1 · %2").arg(c.orderNumber).arg(c.name),c.article+" / "+c.invoiceArticle,c.partDate.toString("dd.MM.yy")+" / "+c.invoiceDate.toString("dd.MM.yy"),c.invoiceName,QString::number(c.quantity)});
                row->setCheckState(0,Qt::Unchecked);row->setData(0,Qt::UserRole,i);
            }
            choices->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
            auto* actions=new QHBoxLayout;box->addLayout(actions);
            auto* cancel=new QPushButton(QString::fromUtf8("Скасувати"),&review);actions->addWidget(cancel);connect(cancel,&QPushButton::clicked,&review,&QDialog::reject);
            auto* accept=new QPushButton(QString::fromUtf8("Підтвердити прив’язку документа"),&review);accept->setObjectName("elitConfirmDocuments");actions->addWidget(accept);
            accept->setEnabled(choices->topLevelItemCount()>0);
            connect(accept,&QPushButton::clicked,&review,[&]{
                if(!authorized() || documentAccount!=m_account || documentScope!=m_scope){review.reject();return;}
                QVector<ElitDocumentAssociation::Candidate> selected;
                for(int i=0;i<choices->topLevelItemCount();++i){auto* row=choices->topLevelItem(i);if(row->checkState(0)==Qt::Checked)selected.append(candidates[row->data(0,Qt::UserRole).toInt()]);}
                if(selected.isEmpty())return;
                if(!SupplierDebtApplicationService::confirmDocuments(documentAccount,documentScope,selected,&error)){QMessageBox::warning(&review,QString::fromUtf8("Прив’язка документа"),error);return;}
                review.accept();
            });
            if(review.exec()==QDialog::Accepted){dialog.accept();refresh();if(associationsChanged)associationsChanged();}
        });
        auto* match=new QPushButton(QString::fromUtf8("Звірити прив’язки"),&dialog);
        match->setObjectName("debtReconcileLinks");layout->addWidget(match);
        connect(match,&QPushButton::clicked,&dialog,[&]{if(reconcileLinks(entry,&dialog)){dialog.accept();refresh();if(associationsChanged)associationsChanged();}});
    }
    auto* close=new QPushButton(QString::fromUtf8("Закрити"),&dialog);layout->addWidget(close);connect(close,&QPushButton::clicked,&dialog,&QDialog::accept);dialog.exec();
}
bool SupplierDebtOverview::reconcileLinks(const SupplierInvoiceLedger::Entry& entry,QWidget* parent) {
    if(!authorized()) return false;
    const auto ledger=m_ledger;
    const auto account=m_account;
    const auto scope=m_scope;
    QDialog dialog(parent);dialog.setWindowTitle(QString::fromUtf8("Прив’язування накладної %1").arg(entry.number));dialog.resize(900,560);
    auto* layout=new QVBoxLayout(&dialog);
    auto* note=new QLabel(QString::fromUtf8("Закупівлю можна розподілити між рядками та різними накладними в межах її вільної кількості. Невикористана частина залишається в локальному обліку. Кількість і вартість рядків мають збігатися. Оплати не змінюються."),&dialog);
    note->setWordWrap(true);note->setStyleSheet("color:#f2f4f6;font:11pt 'Segoe UI';padding:4px;");layout->addWidget(note);
    auto* tree=new QTreeWidget(&dialog);tree->setObjectName("elitLinkCandidates");
    tree->setHeaderLabels({QString::fromUtf8("Накладна / позиція обліку"),"OEM",QString::fromUtf8("Дата"),QString::fromUtf8("Кількість"),QString::fromUtf8("Сума, грн")});
    tree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);tree->header()->setSectionResizeMode(0,QHeaderView::Stretch);
    tree->setStyleSheet("QTreeWidget{background:#f5f7f8;color:#17232a;font:10pt 'Segoe UI';border:1px solid #a7b2ba;selection-background-color:#d3e5dc;selection-color:#17232a;} QTreeWidget::item{min-height:28px;} QHeaderView::section{background:#dfe6eb;color:#17232a;padding:5px;font:10pt 'Segoe UI';border:1px solid #c7d0d6;}");
    layout->addWidget(tree,1);
    for(int i=0;i<entry.lines.size();++i) {
        const auto& line=entry.lines[i];
        auto* group=new QTreeWidgetItem(tree,{line.description,line.reference.trimmed().isEmpty()?line.itemCode:line.reference,entry.date.toString("dd.MM.yy"),QString::number(line.quantity),money(line.grossCents)});
        for(int column=0;column<5;++column){group->setBackground(column,QColor("#dfe6eb"));auto font=group->font(column);font.setBold(true);group->setFont(column,font);}
        for(const auto& item:SupplierInvoiceLedger::linkCandidates(ledger,entry,i)) {
            const auto& p=item.purchase;
            QString location=p.source=="inventory"?QString::fromUtf8("Склад СТО · #%1").arg(p.id):QString::fromUtf8("Наряд №%1 · #%2").arg(p.orderNumber).arg(p.id);
            auto* candidate=new QTreeWidgetItem(group,{location+" · "+p.name,p.oem,p.date,QString::number(p.quantity),money(p.totalCents)});
            candidate->setData(0,Qt::UserRole,item.key);
            candidate->setToolTip(0,location+" · "+p.name);
            candidate->setCheckState(0,entry.links.value(i).contains(item.key)?Qt::Checked:Qt::Unchecked);
        }
        if(group->childCount()==0) group->setToolTip(0,QString::fromUtf8("Немає вільних позицій з відповідним постачальником, OEM, ціною та датою."));
        group->setExpanded(true);
    }
    auto* buttons=new QHBoxLayout;buttons->addStretch();layout->addLayout(buttons);
    auto* cancel=new QPushButton(QString::fromUtf8("Скасувати"),&dialog);buttons->addWidget(cancel);
    auto* save=new QPushButton(QString::fromUtf8("Підтвердити прив’язки"),&dialog);save->setObjectName("elitConfirmLinks");buttons->addWidget(save);
    connect(cancel,&QPushButton::clicked,&dialog,&QDialog::reject);
    connect(save,&QPushButton::clicked,&dialog,[&]{
        if(!authorized() || account!=m_account || scope!=m_scope){dialog.reject();return;}
        QVector<QVector<QString>> links(entry.lines.size());
        for(int i=0;i<tree->topLevelItemCount();++i) {
            const auto* group=tree->topLevelItem(i);
            for(int j=0;j<group->childCount();++j) if(group->child(j)->checkState(0)==Qt::Checked)
                links[i].append(group->child(j)->data(0,Qt::UserRole).toString());
        }
        QString error;
        if(!SupplierDebtApplicationService::confirmLinks(account,scope,ledger.snapshot.revision,entry.key,links,&error)) {
            QMessageBox::warning(&dialog,QString::fromUtf8("Звірка накладної"),error);return;
        }
        dialog.accept();
    });
    return dialog.exec()==QDialog::Accepted;
}
