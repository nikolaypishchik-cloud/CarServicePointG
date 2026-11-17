#include "CashMovementForm.h"
#include "core/services/orders/CashBankService.h"
#include "core/services/orders/CashAccountMovement.h"
#include "core/services/orders/ClientReceiptReader.h"
#include "core/finance/ExactMoney.h"
#include <QComboBox>
#include <QDateEdit>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QUuid>
#include "core/services/AccessControlService.h"

QWidget* cashMovementForm(QWidget* parent)
{
    auto* page=new QWidget(parent);
    auto* form=new QFormLayout(page);
    auto* mode=new QComboBox(page);mode->setObjectName("cashMoveMode");
    mode->addItems({QObject::tr("Переміщення між рахунками"),QObject::tr("Початковий залишок на початок дня"),QObject::tr("Відправити фізично"),QObject::tr("Підтвердити фактичне отримання"),QObject::tr("Підтвердити фактичне повернення")});
    form->addRow(QObject::tr("Операція"),mode);
    auto* from=new QComboBox(page);auto* to=new QComboBox(page);
    form->addRow(QObject::tr("З рахунку"),from);form->addRow(QObject::tr("На рахунок"),to);
    auto* recipient=new QComboBox(page);form->addRow(QObject::tr("Відповідальний отримувач"),recipient);
    auto* shipment=new QComboBox(page);form->addRow(QObject::tr("У дорозі"),shipment);
    auto* day=new QDateEdit(QDate::currentDate(),page);day->setCalendarPopup(true);day->setMaximumDate(QDate::currentDate());
    form->addRow(QObject::tr("Дата початку обліку"),day);
    auto* amount=new QLineEdit(page);auto* reason=new QLineEdit(page);
    reason->setMaxLength(300);
    form->addRow(QObject::tr("Сума, грн"),amount);form->addRow(QObject::tr("Підстава"),reason);
    auto* notice=new QLabel(page);notice->setWordWrap(true);notice->setTextFormat(Qt::PlainText);
    auto* post=new QPushButton(QObject::tr("Підтвердити"),page);post->setObjectName("cashMovePost");form->addRow(post);
    from->setObjectName("cashMoveSource");to->setObjectName("cashMoveDestination");
    recipient->setObjectName("cashMoveRecipient");shipment->setObjectName("cashMoveShipment");
    reason->setObjectName("cashMoveReason");amount->setObjectName("cashMoveAmount");
    auto reload=[=]{
        from->clear();to->clear();QString error;
        for(const auto& account:CashBankService::accounts(error)){
            auto balance=CashAccountMovement::displayBalance(account.uid);
            const auto label=account.name+" · "+balance;
            from->addItem(label,account.uid);
            if(mode->currentIndex()!=1 || account.uid!="primary-cash")to->addItem(label,account.uid);
        }
        from->setCurrentIndex(-1);to->setCurrentIndex(-1);
        const auto action=mode->currentIndex();
        post->setEnabled(AccessControlService::currentUserHasPermission(action==1?"cash.reconcile":"cash.transfer"));
        form->setRowVisible(from,action==0 || action==2);form->setRowVisible(to,action<=2);
        form->setRowVisible(day,action==1);form->setRowVisible(amount,action<=2);
        form->setRowVisible(recipient,action==2);form->setRowVisible(shipment,action>=3);
        recipient->clear();shipment->clear();
        if(action==2 && error.isEmpty())for(const auto& item:CashAccountMovement::recipients(error))recipient->addItem(item.second,item.first);
        if(action>=3 && error.isEmpty())for(const auto& item:CashAccountMovement::inTransit(error))shipment->addItem(item.second,item.first);
        recipient->setCurrentIndex(-1);shipment->setCurrentIndex(-1);
        return error;
    };
    form->addRow(notice);
    auto* refresh=new QPushButton(QObject::tr("Оновити рахунки"),page);form->addRow(refresh);
    notice->setText(reload());
    QObject::connect(refresh,&QPushButton::clicked,page,[=]{notice->setText(reload());});
    QObject::connect(mode,&QComboBox::currentIndexChanged,page,[=]{notice->setText(reload());});
    QObject::connect(post,&QPushButton::clicked,page,[=,uid=QUuid::createUuid().toString(QUuid::WithoutBraces)]()mutable{
        const bool opening=mode->currentIndex()==1;
        const int action=mode->currentIndex();
        if((action==0 || action==2) && from->currentIndex()<0){notice->setText(QObject::tr("Оберіть джерело коштів."));return;}
        const auto parsed=ExactMoney::payment(amount->text().trimmed().replace(',', '.').toStdString());
        auto cents=std::get_if<ExactMoney::Cents>(&parsed);
        if(action<=2 && !cents){notice->setText(QObject::tr("Перевірте суму."));return;}
        CashAccountMovement::Result result;
        if(action==2)result=CashAccountMovement::physical({uid,"send",{},from->currentData().toString(),to->currentData().toString(),recipient->currentData().toString(),reason->text(),*cents});
        else if(action>=3)result=CashAccountMovement::physical({uid,action==3?"receive":"return",shipment->currentData().toString(),{},{},{},reason->text()});
        else result=CashAccountMovement::post({uid,opening?QString():from->currentData().toString(),to->currentData().toString(),reason->text(),opening?day->date():QDate::currentDate(),*cents});
        if(!result.error.isEmpty()){notice->setText(result.error);return;}
        uid=QUuid::createUuid().toString(QUuid::WithoutBraces);
        amount->clear();reason->clear();const auto error=reload();notice->setText(error.isEmpty()?QObject::tr("Проведено."):error);
    });
    return page;
}
