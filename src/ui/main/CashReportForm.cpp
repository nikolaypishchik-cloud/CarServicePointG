#include "CashMovementForm.h"
#include "CashOperationsWidget.h"
#include "core/services/orders/CashReport.h"
#include "core/services/orders/CashBankService.h"
#include "core/services/AccessControlService.h"
#include <QComboBox>
#include <QDateEdit>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>
#include <memory>

QWidget* cashReportForm(QWidget* parent)
{
    using Type=CashReport::Type;
    auto* page=new QWidget(parent);
    auto* layout=new QVBoxLayout(page);
    auto* form=new QFormLayout;
    layout->addLayout(form);
    auto* kind=new QComboBox(page);
    for(const auto type:{Type::Period,Type::Shift,Type::Account,Type::Advances,Type::Orders,Type::Refunds,Type::Transfers,Type::Discrepancies,Type::Acquiring,Type::Employees})
        kind->addItem(CashReport::title(type),static_cast<int>(type));
    auto* selector=new QComboBox(page);
    auto* first=new QDateEdit(QDate::currentDate(),page);
    auto* last=new QDateEdit(QDate::currentDate(),page);
    first->setCalendarPopup(true);last->setCalendarPopup(true);
    form->addRow(QObject::tr("Звіт"),kind);
    form->addRow(QObject::tr("Каса, рахунок або зміна"),selector);
    form->addRow(QObject::tr("Від"),first);form->addRow(QObject::tr("До"),last);
    auto* generate=new QPushButton(QObject::tr("Сформувати"),page);
    auto* pdf=new QPushButton(QObject::tr("PDF"),page);
    auto* actions=new QHBoxLayout;
    actions->addWidget(generate);actions->addWidget(pdf);layout->addLayout(actions);
    auto* table=new QTableWidget(page);table->setObjectName("cashReportTable");
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->hide();
    layout->addWidget(table);
    auto* status=new QLabel(page);status->setTextFormat(Qt::PlainText);status->setWordWrap(true);layout->addWidget(status);
    auto snapshot=std::make_shared<CashReport::Table>();
    auto clear=[=]{
        *snapshot={};table->clear();table->setRowCount(0);table->setColumnCount(0);pdf->setEnabled(false);
        status->setText(QObject::tr("Оберіть параметри та сформуйте звіт."));
    };
    auto configure=[=]{
        clear();
        const auto type=static_cast<Type>(kind->currentData().toInt());
        form->setRowVisible(selector,type==Type::Account || type==Type::Shift);
        form->setRowVisible(first,type!=Type::Shift);form->setRowVisible(last,type!=Type::Shift);
        QSignalBlocker block(selector);selector->clear();
        QString error;
        if(type==Type::Account)for(const auto& row:CashBankService::accounts(error,true))selector->addItem(row.name,row.uid);
        if(type==Type::Shift)for(const auto& row:CashReport::shifts(error))selector->addItem(row.second,row.first);
        if(!error.isEmpty())status->setText(error);
    };
    QObject::connect(kind,&QComboBox::currentIndexChanged,page,configure);
    QObject::connect(selector,&QComboBox::currentIndexChanged,page,clear);
    QObject::connect(first,&QDateEdit::dateChanged,page,clear);QObject::connect(last,&QDateEdit::dateChanged,page,clear);
    QObject::connect(generate,&QPushButton::clicked,page,[=]{
        clear();
        *snapshot=CashReport::read(static_cast<Type>(kind->currentData().toInt()),first->date(),last->date(),selector->currentData().toString());
        if(!snapshot->error.isEmpty()){status->setText(snapshot->error);return;}
        table->setColumnCount(snapshot->columns.size());table->setHorizontalHeaderLabels(snapshot->columns);
        table->setRowCount(snapshot->rows.size());
        for(int r=0;r<snapshot->rows.size();++r)for(int c=0;c<snapshot->columns.size();++c)
            table->setItem(r,c,new QTableWidgetItem(snapshot->rows.at(r).at(c)));
        table->resizeColumnsToContents();
        status->setText(QObject::tr("Рядків %1 · %2").arg(snapshot->rows.size()).arg(snapshot->totals.join(" · ")));
        pdf->setEnabled(AccessControlService::currentUserHasPermission("cash.export"));
    });
    QObject::connect(pdf,&QPushButton::clicked,page,[=]{
        auto* dialog=new QFileDialog(page,QObject::tr("Зберегти касовий звіт"));
        dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setAcceptMode(QFileDialog::AcceptSave);
        dialog->setNameFilter("PDF (*.pdf)");dialog->setDefaultSuffix("pdf");dialog->selectFile(snapshot->title+".pdf");
        QObject::connect(dialog,&QFileDialog::fileSelected,page,[status,report=*snapshot](const QString& path){
            const auto error=writeCashReportPdf(path,report);
            status->setText(error.isEmpty()?QObject::tr("PDF збережено."):error);
        });
        dialog->open();
    });
    configure();
    return page;
}
