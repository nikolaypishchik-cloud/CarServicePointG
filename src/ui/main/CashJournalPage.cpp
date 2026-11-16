#include "CashJournalPage.h"
#include "core/services/orders/ClientReceiptReader.h"
#include <QDateEdit>
#include <QHeaderView>
#include <QLabel>
#include <QVBoxLayout>
#include <QPushButton>
#include <QTableWidget>

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
    layout->addWidget(new QLabel(tr("Операції · Прямі оплати нарядів"),this));
    auto* body = new QHBoxLayout;
    table = new QTableWidget(0,5,this);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->verticalHeader()->hide();
    table->setHorizontalHeaderLabels({tr("Дата"),tr("UID наряду"),tr("Спосіб"),tr("Сума"),tr("Автор")});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table->setColumnWidth(1,145);
    table->horizontalHeader()->setStretchLastSection(true);
    body->addWidget(table,1);
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
    const auto journal = ClientReceiptReader::readJournal(firstDay->date(),lastDay->date());
    table->setRowCount(0);
    if (!journal.error.isEmpty() || !journal.available) {
        summary->setText(journal.error.isEmpty() ? tr("Журнал оплат недоступний.") : journal.error);
        return;
    }
    summary->setText(tr("Прямі оплати за період\n%1").arg(ClientReceiptReader::money(journal.directPaymentsCents)));
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
