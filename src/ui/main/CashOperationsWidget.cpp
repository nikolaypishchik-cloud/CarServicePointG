#include "CashOperationsWidget.h"
#include "core/services/orders/ClientReceiptReader.h"
#include <QLineEdit>
#include <QComboBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QScrollBar>
#include <QPushButton>
#include <QLabel>
#include <QBoxLayout>
#include <QMessageBox>
#include <QTimer>
#include <QFileDialog>
#include "core/services/AccessControlService.h"

CashOperationsWidget::CashOperationsWidget(QWidget* parent):QWidget(parent)
{
    auto* layout=new QVBoxLayout(this);
    search=new QLineEdit(this);search->setClearButtonEnabled(true);
    search->setPlaceholderText(tr("Клієнт, авто, наряд, примітка або UID"));
    layout->addWidget(search);
    auto* filters=new QHBoxLayout;
    kind=new QComboBox(this);kind->addItem(tr("Усі операції"),"");
    for(const auto key:{"payment","advance","allocation","refund","void","expense","credit","match","move","opening","deposit","withdrawal","supplier"})kind->addItem(CashOperations::title(key),key);
    method=new QComboBox(this);method->addItem(tr("Усі способи"),"");
    for(const auto key:{"cash","card","transfer","allocation"})method->addItem(CashOperations::methodTitle(key),key);
    filters->addWidget(kind);filters->addWidget(method);layout->addLayout(filters);
    table=new QTableWidget(0,6,this);table->setObjectName("cashOperations");
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->verticalHeader()->hide();
    table->setHorizontalHeaderLabels({tr("Дата"),tr("Клієнт / наряд"),tr("Операція"),tr("Спосіб"),tr("Сума"),tr("Рух коштів клієнта")});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);table->horizontalHeader()->setStretchLastSection(true);
    // Leave room for a complete operation below the header in the compact layout.
    table->setMinimumHeight(table->horizontalHeader()->sizeHint().height()
        + table->verticalHeader()->defaultSectionSize() + table->horizontalScrollBar()->sizeHint().height()
        + 2*table->frameWidth());
    layout->addWidget(table);
    auto* navigation=new QHBoxLayout;
    previous=new QPushButton(tr("Назад"),this);next=new QPushButton(tr("Далі"),this);
    status=new QLabel(this);status->setTextFormat(Qt::PlainText);
    navigation->addWidget(previous);navigation->addWidget(status,1);navigation->addWidget(next);layout->addLayout(navigation);
    auto* pdf = new QPushButton(tr("PDF"),this);
    navigation->addWidget(pdf);
    pdf->setEnabled(AccessControlService::currentUserHasPermission("cash.export"));
    connect(pdf,&QPushButton::clicked,this,[this]{
        auto* dialog = new QFileDialog(this,tr("Зберегти касовий журнал"));
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setAcceptMode(QFileDialog::AcceptSave);
        dialog->setNameFilter("PDF (*.pdf)");
        dialog->setDefaultSuffix("pdf");
        dialog->selectFile("Журнал каси.pdf");
        connect(dialog,&QFileDialog::fileSelected,this,[this](const QString& path){
            const auto error = writeCashJournalPdf(path,start,end,search->text(),kind->currentData().toString(),method->currentData().toString());
            status->setText(error.isEmpty() ? tr("PDF збережено.") : error);
        });
        dialog->open();
    });
    auto* delay=new QTimer(this);delay->setSingleShot(true);delay->setInterval(180);
    connect(search,&QLineEdit::textChanged,this,[=]{delay->start();});
    auto reset=[this]{offset=0;loadPage();};
    connect(delay,&QTimer::timeout,this,reset);
    connect(kind,&QComboBox::currentIndexChanged,this,reset);connect(method,&QComboBox::currentIndexChanged,this,reset);
    connect(previous,&QPushButton::clicked,this,[this]{offset=qMax(0,offset-100);loadPage();});
    connect(next,&QPushButton::clicked,this,[this]{offset+=100;loadPage();});
    connect(table,&QTableWidget::cellDoubleClicked,this,[this](int index,int){
        if(index<0 || index>=rows.size())return;
        const auto& row=rows[index];
        auto* box=new QMessageBox(QMessageBox::Information,CashOperations::title(row.kind),
            tr("Документ %1\nДжерело %2\nНаряд %3\nАвто %4\nАвтор %5\nПідстава %6")
                .arg(row.uid,row.source,row.order,row.vehicle,row.actor,row.reason),QMessageBox::Ok,this);
        box->setTextFormat(Qt::PlainText);box->setAttribute(Qt::WA_DeleteOnClose);box->open();
    });
}
void CashOperationsWidget::refresh(QDate first,QDate last)
{
    if(first!=start || last!=end)offset=0;
    start=first;end=last;loadPage();
}
void CashOperationsWidget::loadPage()
{
    const auto page=CashOperations::read(start,end,search->text(),kind->currentData().toString(),method->currentData().toString(),offset);
    rows=page.rows;table->setRowCount(rows.size());
    for(int r=0;r<rows.size();++r){
        const auto& row=rows[r];
        const QString reference = row.number.isEmpty() ? row.order : "№" + row.number;
        const QStringList cells{row.day,(row.name + " " + reference).trimmed(),CashOperations::title(row.kind),
            CashOperations::methodTitle(row.method),ClientReceiptReader::money(row.amount),ClientReceiptReader::money(row.net)};
        for(int c=0;c<cells.size();++c){auto* item=new QTableWidgetItem(cells[c]);item->setData(Qt::UserRole,row.uid);table->setItem(r,c,item);}
    }
    status->setText(!page.error.isEmpty()?page.error:rows.isEmpty()?tr("Операцій за цими параметрами немає."):tr("%1–%2").arg(offset+1).arg(offset+rows.size()));
    previous->setEnabled(offset>0);next->setEnabled(page.more);
}
