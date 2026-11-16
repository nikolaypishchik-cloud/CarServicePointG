#include "CashAdvanceDialog.h"
#include "CashJournalPage.h"
#include "core/finance/ExactMoney.h"
#include "core/services/orders/CashOpeningService.h"
#include <QDateEdit>
#include <QDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QUuid>
void openCashOpeningDialog(CashJournalPage* page)
{
    auto* d=new QDialog(page);d->setAttribute(Qt::WA_DeleteOnClose);d->setWindowModality(Qt::WindowModal);
    d->setWindowTitle(QObject::tr("Перша касова зміна"));
    auto* form=new QFormLayout(d);
    auto* action=new QComboBox(d);
    action->addItems({QObject::tr("Підтвердити початковий залишок"),QObject::tr("Перерахувати та відкрити зміну"),QObject::tr("Документувати коригування")});
    form->addRow(QObject::tr("Дія"),action);
    auto* date=new QDateEdit(QDate::currentDate(),d);date->setCalendarPopup(true);date->setMaximumDate(QDate::currentDate());
    form->addRow(QObject::tr("Дата початку обліку"),date);
    auto* amount=new QLineEdit(d);form->addRow(QObject::tr("Фактично перераховано, грн"),amount);
    auto* reason=new QLineEdit(d);form->addRow(QObject::tr("Причина розбіжності або рішення"),reason);
    auto* status=new QLabel(d);status->setTextFormat(Qt::PlainText);status->setWordWrap(true);form->addRow(status);
    auto* submit=new QPushButton(QObject::tr("Підтвердити"),d);form->addRow(submit);
    QObject::connect(action,&QComboBox::currentIndexChanged,d,[=](int i){form->setRowVisible(date,i==0);amount->clear();reason->clear();});
    const auto request=QUuid::createUuid().toString(QUuid::WithoutBraces);
    QObject::connect(submit,&QPushButton::clicked,d,[=]{
        const auto parsed=ExactMoney::payment(amount->text().trimmed().replace(',', '.').toStdString());
        const auto* cents=std::get_if<ExactMoney::Cents>(&parsed);
        if(!cents || *cents<0){status->setText(QObject::tr("Перевірте суму в гривнях і копійках."));return;}
        const auto index=action->currentIndex();
        const auto kind=index==0?CashOpeningService::Action::Initial:(index==1?CashOpeningService::Action::Count:CashOpeningService::Action::Adjustment);
        const auto result=CashOpeningService::apply({request,kind,index==0?date->date():QDate::currentDate(),*cents,reason->text()});
        status->setText(result.message);
        if(result.recorded){submit->setEnabled(false);action->setEnabled(false);amount->setReadOnly(true);reason->setReadOnly(true);date->setEnabled(false);}
    });
    d->open();
}
