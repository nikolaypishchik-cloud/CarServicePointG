#include "CashJournalPage.h"
#include "CashAdvanceDialog.h"
#include "CashBankForm.h"
#include "core/services/orders/CashShiftService.h"
#include "core/services/orders/CashOutflow.h"
#include "core/services/orders/CashAdvanceService.h"
#include "core/services/AccessControlService.h"
#include <QMessageBox>
#include "core/services/orders/ClientReceiptReader.h"
#include <QDateEdit>
#include <QHeaderView>
#include <QLabel>
#include <QVBoxLayout>
#include <QPushButton>
#include <QTableWidget>
#include <QTabWidget>

CashJournalPage::CashJournalPage(QWidget* parent) : QWidget(parent)
{
    setObjectName("cashJournalPage");
    setStyleSheet(QStringLiteral(
        "#cashJournalPage{background:#eff3f5;color:#203445;}"
        "#cashJournalPage QLabel{color:#203445;background:transparent;}"
        "#cashJournalPage QTableWidget,#cashJournalPage QDateEdit{background:white;color:#203445;}"
        "#cashJournalPage QLabel#cashTotal{background:#293e4a;color:white;border-radius:6px;padding:11px;}"
        "#cashJournalPage QLabel#cashTitle{font-size:22px;font-weight:bold;}"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6,8,6,8);
    layout->setSpacing(4);
    auto* title = new QLabel(tr("Кошти клієнтів"),this);
    title->setObjectName("cashTitle");
    layout->addWidget(title);
    auto* actions = new QHBoxLayout;
    auto* receive = new QPushButton(tr("Прийняти аванс"),this);
    receive->setObjectName("receiveAdvance");
    auto* allocate = new QPushButton(tr("Зарахувати аванс"),this);
    allocate->setObjectName("allocateAdvance");
    receive->setEnabled(AccessControlService::currentUserHasPermission("cash.receive"));
    allocate->setEnabled(AccessControlService::currentUserHasPermission("cash.allocate"));
    actions->addWidget(receive);
    actions->addWidget(allocate);
    if (AccessControlService::currentUserHasPermission("cash.shift") || AccessControlService::currentUserHasPermission("cash.reconcile")) {
        auto* opening=new QPushButton(tr("Касова зміна"),this);
        opening->setObjectName("cashShift");
        actions->addWidget(opening);
        connect(opening,&QPushButton::clicked,this,[this]{openCashShiftDialog(this);});
    }
    if (AccessControlService::currentUserHasPermission("cash.configure")) {
        auto* station = new QPushButton(tr("Підтвердити єдиний ПК"),this);
        actions->addWidget(station);
        connect(station,&QPushButton::clicked,this,[this] {
            auto* confirm = new QMessageBox(QMessageBox::Question,tr("Облік авансів"),
                tr("Аванси цієї СТО проводяться тільки в цій базі на цьому ПК?"),
                QMessageBox::Yes|QMessageBox::No,this);
            confirm->setDefaultButton(QMessageBox::No);
            confirm->setAttribute(Qt::WA_DeleteOnClose);
            connect(confirm,&QDialog::finished,this,[this](int answer) {
                if (answer!=QMessageBox::Yes) return;
                const auto error=CashAdvanceService::designateSingleWorkstation();
                summary->setText(error.isEmpty()?tr("Єдиний ПК підтверджено."):error);
            });
            confirm->open();
        });
    }
    connect(receive,&QPushButton::clicked,this,[this]{openCashAdvanceDialog(this,false);});
    connect(allocate,&QPushButton::clicked,this,[this]{openCashAdvanceDialog(this,true);});
    layout->addLayout(actions);
    auto* range = new QHBoxLayout;
    firstDay = new QDateEdit(QDate::currentDate(),this);
    lastDay = new QDateEdit(firstDay->date(),this);
    for (auto* field : {firstDay,lastDay}) {
        field->setCalendarPopup(true);
        field->setDisplayFormat("dd.MM.yyyy");
    }
    range->addWidget(new QLabel(tr("Від"),this));
    range->addWidget(firstDay);
    range->addWidget(new QLabel(tr("До"),this));
    range->addWidget(lastDay);
    auto* refreshButton = new QPushButton(tr("Оновити"),this);
    range->addWidget(refreshButton);
    range->addStretch();
    layout->addLayout(range);
    auto* metrics=new QHBoxLayout;
    const QStringList names{tr("Отримано за період"),tr("Повернено за період"),tr("Чистий рух за період"),tr("Вільні аванси")};
    for(const auto& name:names){
        auto* card=new QFrame(this);
        card->setStyleSheet("QFrame{background:white;border:1px solid #cbd5da;border-radius:6px;} QLabel{border:0;}");
        auto* content=new QVBoxLayout(card);
        content->addWidget(new QLabel(name,card));
        auto* value=new QLabel(card);
        value->setStyleSheet("font-size:17px;font-weight:bold;");
        content->addWidget(value);metricValues.append(value);metrics->addWidget(card,1);
    }
    layout->addLayout(metrics);
    auto* body = new QHBoxLayout;
    table = new QTableWidget(0,5,this);
    table->setObjectName("directCashPayments");
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->verticalHeader()->hide();
    table->setHorizontalHeaderLabels({tr("Дата"),tr("UID наряду"),tr("Спосіб"),tr("Сума"),tr("Автор")});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table->setColumnWidth(1,145);
    table->horizontalHeader()->setStretchLastSection(true);
    auto* tabs=new QTabWidget(this);
    tabs->addTab(table,tr("Прямі оплати"));
    auto* advanceTab=new QWidget(tabs);
    auto* advanceLayout=new QVBoxLayout(advanceTab);
    advanceStatus=new QLabel(advanceTab);
    advanceStatus->setTextFormat(Qt::PlainText);
    advanceLayout->addWidget(advanceStatus);
    advanceTable=new QTableWidget(0,5,advanceTab);
    advanceTable->setObjectName("postedAdvances");
    advanceTable->setHorizontalHeaderLabels({tr("Дата"),tr("Операція"),tr("Сума"),tr("Рух коштів"),tr("UID наряду")});
    advanceTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    advanceTable->verticalHeader()->hide();
    advanceTable->horizontalHeader()->setStretchLastSection(true);
    advanceLayout->addWidget(advanceTable);
    tabs->addTab(advanceTab,tr("Аванси"));
    outflowTable=new QTableWidget(0,6,tabs);
    outflowTable->setHorizontalHeaderLabels({tr("Дата"),tr("Операція"),tr("Сума"),tr("Спосіб"),tr("Причина"),tr("Автор")});
    outflowTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tabs->addTab(outflowTable,tr("Повернення й видатки"));
    tabs->addTab(cashBankForm(tabs,false),tr("Банківська звірка"));
    auto* refund=new QPushButton(tr("Повернення"),this);
    auto* expense=new QPushButton(tr("Видаток"),this);
    refund->setObjectName("refundReceipt");
    expense->setObjectName("postExpense");
    refund->setEnabled(CashOutflow::allowed(false));
    expense->setEnabled(CashOutflow::allowed(true));
    actions->addWidget(refund);
    actions->addWidget(expense);
    connect(expense,&QPushButton::clicked,this,[this]{openOutflowDialog(this,"expense",{},tr("Готівка"));});
    connect(refund,&QPushButton::clicked,this,[this,tabs,advanceTab]{
        const bool advance=tabs->currentWidget()==advanceTab;
        auto* source=advance?advanceTable:table;
        auto* entry=source->item(source->currentRow(),0);
        if((!advance && tabs->currentWidget()!=table) || !entry || (advance && source->item(source->currentRow(),1)->text()!=tr("Отримання"))){
            summary->setText(tr("Виберіть первинну пряму оплату або отриманий аванс."));return;
        }
        const auto uid=entry->data(Qt::UserRole).toString();
        openOutflowDialog(this,advance?"advance":"payment",uid,entry->text()+" · "+source->item(source->currentRow(),advance?2:3)->text()+" · "+uid);
    });
    auto* reverse=new QPushButton(tr("Сторно повернення"),this);
    reverse->setObjectName("reverseRefund");
    reverse->setEnabled(CashOutflow::canReverse());
    actions->addWidget(reverse);
    connect(reverse,&QPushButton::clicked,this,[this,tabs]{
        auto* cell=outflowTable->item(outflowTable->currentRow(),0);
        if(tabs->currentWidget()!=outflowTable || !cell || !cell->data(Qt::UserRole+1).toBool()){
            summary->setText(tr("Виберіть повернення у вкладці «Повернення й видатки»."));return;
        }
        openOutflowDialog(this,"reverse",cell->data(Qt::UserRole).toString(),cell->toolTip(),cell->data(Qt::UserRole+2).toLongLong());
    });
    body->addWidget(tabs,1);
    summary = new QLabel(this);
    summary->setObjectName("cashTotal");
    summary->setWordWrap(true);
    summary->setMaximumWidth(250);
    summary->setTextFormat(Qt::PlainText);
    body->addWidget(summary,0,Qt::AlignTop);
    layout->addLayout(body,1);
    connect(refreshButton,&QPushButton::clicked,this,&CashJournalPage::refresh);
    connect(firstDay,&QDateEdit::dateChanged,this,&CashJournalPage::refresh);
    connect(lastDay,&QDateEdit::dateChanged,this,&CashJournalPage::refresh);
}

void CashJournalPage::refresh()
{
    const auto totals=CashOutflow::totals(firstDay->date(),lastDay->date());
    const QList<qint64> values{totals.received,totals.refunded,totals.net,totals.freeAdvances};
    for(int i=0;i<metricValues.size();++i){
        metricValues[i]->setText(totals.error.isEmpty()?ClientReceiptReader::money(values[i]):QStringLiteral("—"));
        metricValues[i]->setToolTip(totals.error);
    }
    table->setRowCount(0);advanceTable->setRowCount(0);
    QString outflowError;
    const auto entries=CashOutflow::ledger(firstDay->date(),lastDay->date(),outflowError);
    outflowTable->setRowCount(entries.size());
    for(int row=0;row<entries.size();++row){
        const auto& item=entries[row];
        const QStringList fields{item.date,item.kind=="expense"?tr("Видаток"):item.kind.startsWith("undo_")?tr("Сторно повернення"):tr("Повернення"),ClientReceiptReader::money(item.cents),item.method,item.reason,item.actor};
        for(int col=0;col<fields.size();++col){
            auto* cell=new QTableWidgetItem(fields[col]);cell->setData(Qt::UserRole,item.uid);cell->setData(Qt::UserRole+1,item.kind=="payment" || item.kind=="advance");cell->setData(Qt::UserRole+2,item.cents);cell->setToolTip(item.uid+" "+item.source);outflowTable->setItem(row,col,cell);
        }
    }
    if(!outflowError.isEmpty()){summary->setText(outflowError);return;}
    const auto advances=CashAdvanceService::ledger(firstDay->date(),lastDay->date());
    advanceStatus->setText(!advances.error.isEmpty()?advances.error:
        (advances.entries.isEmpty()?tr("За обраний період немає операцій з авансами."):QString()));
    advanceStatus->setVisible(!advanceStatus->text().isEmpty());
    advanceTable->setRowCount(advances.entries.size());
    for(int row=0;row<advances.entries.size();++row){
        const auto& entry=advances.entries[row];
        const bool allocated=!entry.source.isEmpty();
        const QStringList values{entry.day.toString("dd.MM.yyyy"),allocated?tr("Зарахування"):tr("Отримання"),
            ClientReceiptReader::money(entry.cents),ClientReceiptReader::money(allocated?0:entry.cents),entry.order};
        for(int column=0;column<values.size();++column){
            auto* item=new QTableWidgetItem(values[column]);
            item->setData(Qt::UserRole,entry.uid);
            advanceTable->setItem(row,column,item);
        }
    }
    const auto journal = ClientReceiptReader::readJournal(firstDay->date(),lastDay->date());
    table->setRowCount(0);
    if (!journal.error.isEmpty() || !journal.available) {
        summary->setText(journal.error.isEmpty() ? tr("Журнал оплат недоступний.") : journal.error);
        return;
    }
    summary->setText(totals.error.isEmpty()
        ?tr("Кошти клієнтів за всім локальним журналом\n%1\n\nПрямі оплати за період\n%2")
            .arg(ClientReceiptReader::money(totals.allNet),ClientReceiptReader::money(journal.directPaymentsCents))
        :totals.error);
    qint64 drawerCents=0;
    QString drawer=CashShiftService::currentBalance(drawerCents);
    if(drawer.isEmpty())drawer=ClientReceiptReader::money(drawerCents);
    summary->setText(summary->text()+tr("\n\nОбліковий залишок готівки\n")+drawer);
    table->setRowCount(journal.entries.size());
    int row = 0;
    for (const auto& entry : journal.entries) {
        const auto& payment = entry.payment;
        QString method = payment.method;
        if (method == "cash") method = tr("Готівка");
        else if (method == "card") method = tr("Картка");
        else if (method == "transfer") method = tr("Переказ");
        const QStringList cells{payment.paidOn.toString("dd.MM.yyyy"),entry.orderUid,method,
                                ClientReceiptReader::money(payment.cents),entry.actor};
        for (int column = 0; column < cells.size(); ++column) {
            auto* item = new QTableWidgetItem(cells[column]);
            item->setData(Qt::UserRole,payment.uid);
            table->setItem(row,column,item);
        }
        ++row;
    }
}
