#include "CashAdvanceDialog.h"
#include "CashJournalPage.h"
#include "core/services/orders/CashShiftService.h"
#include "core/services/ActivityAuditService.h"
#include "core/finance/ExactMoney.h"
#include "core/services/AccessControlService.h"
#include <QDialog>
#include <QFormLayout>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QMessageBox>
#include <QUuid>

void openCashShiftDialog(CashJournalPage* page)
{
    using namespace CashShiftService;
    const auto shift=current();
    if(!shift.error.isEmpty()){QMessageBox::warning(page,QObject::tr("Касова зміна"),shift.error);return;}
    if(shift.uid.isEmpty()){openCashOpeningDialog(page);return;}
    auto* dialog=new QDialog(page);
    dialog->setObjectName("cashShiftForm");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setWindowTitle(QObject::tr("Касова зміна"));
    auto* form=new QFormLayout(dialog);
    auto* action=new QComboBox(dialog);
    action->setObjectName("shiftAction");
    auto add=[&](QString label,Action value){action->addItem(label,static_cast<int>(value));};
    const auto actor=ActivityAuditService::currentActor().accountId;
    const bool operate=AccessControlService::currentUserHasPermission("cash.shift");
    if(operate && shift.phase=="open" && shift.operatorUid==actor)add(QObject::tr("Почати закриття та перерахунок"),Action::Freeze);
    if(operate && shift.phase=="closing" && shift.operatorUid==actor){
        add(QObject::tr("Підтвердити закриття"),Action::Close);
        add(QObject::tr("Здати касу працівнику"),Action::HandOver);
    }
    if(operate && shift.phase=="handover" && shift.recipient==actor)add(QObject::tr("Підтвердити приймання каси"),Action::Accept);
    if(operate && shift.phase=="closed")add(QObject::tr("Відкрити наступну зміну"),Action::OpenNext);
    if(shift.phase!="open" && AccessControlService::currentUserHasPermission("cash.reconcile"))add(QObject::tr("Документувати коригування розбіжності"),Action::Adjust);
    form->addRow(QObject::tr("Дія"),action);
    auto* receiver=new QComboBox(dialog);
    receiver->setObjectName("shiftRecipient");
    receiver->addItem(QObject::tr("Оберіть працівника"));
    for(const auto& person:recipients())receiver->addItem(person.second,person.first);
    form->addRow(QObject::tr("Отримувач"),receiver);
    auto* count=new QLineEdit(dialog);
    count->setObjectName("shiftCount");
    auto* reason=new QLineEdit(dialog);
    reason->setObjectName("shiftReason");
    form->addRow(QObject::tr("Фактично перераховано, грн"),count);
    form->addRow(QObject::tr("Причина розбіжності або рішення"),reason);
    auto* status=new QLabel(dialog);
    status->setWordWrap(true);
    form->addRow(status);
    auto* submit=new QPushButton(QObject::tr("Підтвердити"),dialog);
    submit->setObjectName("shiftSubmit");
    form->addRow(submit);
    auto refresh=[=]{
        auto kind=static_cast<Action>(action->currentData().toInt());
        form->setRowVisible(receiver,kind==Action::HandOver);
        form->setRowVisible(count,kind!=Action::Freeze);
        form->setRowVisible(reason,kind!=Action::Freeze);
        submit->setEnabled(action->count()>0);
        if(action->count()==0)status->setText(QObject::tr("Зміну веде інший працівник."));
    };
    refresh();
    QObject::connect(action,&QComboBox::currentIndexChanged,dialog,refresh);
    const auto request=QUuid::createUuid().toString(QUuid::WithoutBraces);
    QObject::connect(submit,&QPushButton::clicked,dialog,[=]{
        const auto kind=static_cast<Action>(action->currentData().toInt());
        qint64 amount=0;
        if(kind!=Action::Freeze){
            auto parsed=ExactMoney::payment(count->text().trimmed().replace(',', '.').toStdString());
            auto* cents=std::get_if<ExactMoney::Cents>(&parsed);
            if(!cents || *cents<0){status->setText(QObject::tr("Введіть фактично перераховану суму."));return;}
            amount=*cents;
        }
        const auto result=apply({request,shift.uid,kind,amount,kind==Action::HandOver?receiver->currentData().toString():QString(),reason->text()});
        status->setText(result.message);
        if(result.recorded){submit->setEnabled(false);action->setEnabled(false);receiver->setEnabled(false);count->setReadOnly(true);reason->setReadOnly(true);}
    });
    dialog->open();
}
