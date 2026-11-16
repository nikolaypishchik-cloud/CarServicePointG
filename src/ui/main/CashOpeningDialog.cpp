#include "CashAdvanceDialog.h"
#include "CashJournalPage.h"
#include "core/finance/ExactMoney.h"
#include "core/services/orders/CashOpeningService.h"
#include "core/services/AccessControlService.h"
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
    using Action=CashOpeningService::Action;
    if(AccessControlService::currentUserHasPermission("cash.reconcile"))action->addItem(QObject::tr("Підтвердити початковий залишок"),int(Action::Initial));
    if(AccessControlService::currentUserHasPermission("cash.shift"))action->addItem(QObject::tr("Перерахувати та відкрити зміну"),int(Action::Count));
    if(AccessControlService::currentUserHasPermission("cash.reconcile"))action->addItem(QObject::tr("Документувати коригування"),int(Action::Adjustment));
    form->addRow(QObject::tr("Дія"),action);
    auto* date=new QDateEdit(QDate::currentDate(),d);date->setCalendarPopup(true);date->setMaximumDate(QDate::currentDate());
    form->addRow(QObject::tr("Дата початку обліку"),date);
    auto* amount=new QLineEdit(d);form->addRow(QObject::tr("Фактично перераховано, грн"),amount);
    auto* reason=new QLineEdit(d);form->addRow(QObject::tr("Причина розбіжності або рішення"),reason);
    auto* status=new QLabel(d);status->setTextFormat(Qt::PlainText);status->setWordWrap(true);form->addRow(status);
    auto* submit=new QPushButton(QObject::tr("Підтвердити"),d);form->addRow(submit);
    submit->setEnabled(action->count()>0);
    form->setRowVisible(date,action->currentData().toInt()==int(Action::Initial));
    QObject::connect(action,&QComboBox::currentIndexChanged,d,[=]{form->setRowVisible(date,action->currentData().toInt()==int(Action::Initial));amount->clear();reason->clear();});
    const auto request=QUuid::createUuid().toString(QUuid::WithoutBraces);
    QObject::connect(submit,&QPushButton::clicked,d,[=]{
        const auto parsed=ExactMoney::payment(amount->text().trimmed().replace(',', '.').toStdString());
        const auto* cents=std::get_if<ExactMoney::Cents>(&parsed);
        if(!cents || *cents<0){status->setText(QObject::tr("Перевірте суму в гривнях і копійках."));return;}
        const auto kind=static_cast<Action>(action->currentData().toInt());
        const auto result=CashOpeningService::apply({request,kind,kind==Action::Initial?date->date():QDate::currentDate(),*cents,reason->text()});
        status->setText(result.message);
        if(result.recorded){submit->setEnabled(false);action->setEnabled(false);amount->setReadOnly(true);reason->setReadOnly(true);date->setEnabled(false);}
    });
    d->open();
}
