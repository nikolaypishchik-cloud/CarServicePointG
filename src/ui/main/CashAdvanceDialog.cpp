#include "CashAdvanceDialog.h"
#include "CashJournalPage.h"
#include "core/services/orders/CashAdvanceService.h"
#include "core/finance/ExactMoney.h"
#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QUuid>

using namespace CashAdvanceService;
namespace {
void populate(QComboBox* box,const Choices& choices)
{
    box->clear();
    box->addItem(choices.error.isEmpty()?QObject::tr("Оберіть зі списку"):choices.error,QString());
    for (const auto& row:choices.rows) box->addItem(row.label.isEmpty()?row.uid:row.label,row.uid);
}
}
void openCashAdvanceDialog(CashJournalPage* page,bool allocate)
{
    if (auto* previous=page->findChild<QDialog*>("cashAdvanceForm")) { previous->raise();previous->activateWindow();return; }
    auto* dialog=new QDialog(page);
    dialog->setObjectName("cashAdvanceForm");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setWindowTitle(allocate?QObject::tr("Зарахувати аванс"):QObject::tr("Прийняти аванс"));
    auto* form=new QFormLayout(dialog);
    auto combo=[&](const char* name,const QString& title){
        auto* box=new QComboBox(dialog);box->setObjectName(name);form->addRow(title,box);return box;
    };
    auto* client=combo("advanceClient",QObject::tr("Клієнт"));
    auto* source=combo("advanceSource",QObject::tr("Аванс"));
    auto* order=combo("advanceOrder",allocate?QObject::tr("Наряд"):QObject::tr("Наряд (необов’язково)"));
    auto* method=combo("advanceMethod",QObject::tr("Спосіб отримання"));
    method->addItem(QObject::tr("Оберіть спосіб"),QString());
    method->addItem(QObject::tr("Готівка"),"cash");
    method->addItem(QObject::tr("Картка"),"card");
    method->addItem(QObject::tr("Переказ"),"transfer");
    form->setRowVisible(source,allocate);
    form->setRowVisible(method,!allocate);
    auto* amount=new QLineEdit(dialog);
    amount->setObjectName("advanceAmount");form->addRow(QObject::tr("Сума, грн"),amount);
    auto* note=new QLineEdit(dialog);form->addRow(QObject::tr("Примітка"),note);
    auto* postButton=new QPushButton(dialog->windowTitle(),dialog);
    postButton->setObjectName("advancePost");
    auto* status=new QLabel(dialog);status->setObjectName("advanceStatus");status->setTextFormat(Qt::PlainText);status->setWordWrap(true);
    form->addRow(status);form->addRow(postButton);
    populate(client,clients());
    QObject::connect(client,&QComboBox::currentIndexChanged,dialog,[=]{
        const auto uid=client->currentData().toString();
        populate(order,orders(uid));
        if (allocate) populate(source,advances(uid));
    });
    const auto request=QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto date=QDate::currentDate();
    QObject::connect(postButton,&QPushButton::clicked,dialog,[=]{
        auto parsed=ExactMoney::payment(amount->text().trimmed().replace(',', '.').toStdString());
        const auto* cents=std::get_if<ExactMoney::Cents>(&parsed);
        if (!cents || *cents<=0) { status->setText(QObject::tr("Введіть додатну суму з точністю до копійки."));return; }
        Command c{request,client->currentData().toString(),order->currentData().toString(),
            allocate?source->currentData().toString():QString(),allocate?QStringLiteral("allocation"):method->currentData().toString(),
            note->text(),date,*cents};
        const auto result=post(c);
        if (!result.error.isEmpty()) {status->setText(result.error);return;}
        for(auto* box:{client,order,source,method})box->setEnabled(false);
        amount->setReadOnly(true);note->setReadOnly(true);
        status->setText((result.replay?QObject::tr("Уже проведено"):QObject::tr("Проведено"))+" · "+result.operationUid);
        postButton->setText(QObject::tr("Перевірити проведення"));
        page->refresh();
    });
    dialog->open();
}
