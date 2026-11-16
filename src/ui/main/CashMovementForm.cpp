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

QWidget* cashMovementForm(QWidget* parent)
{
    auto* page=new QWidget(parent);
    auto* form=new QFormLayout(page);
    auto* mode=new QComboBox(page);
    mode->addItems({QObject::tr("Переміщення між рахунками"),QObject::tr("Початковий залишок на початок дня")});
    form->addRow(QObject::tr("Операція"),mode);
    auto* from=new QComboBox(page);auto* to=new QComboBox(page);
    form->addRow(QObject::tr("З рахунку"),from);form->addRow(QObject::tr("На рахунок"),to);
    auto* day=new QDateEdit(QDate::currentDate(),page);day->setCalendarPopup(true);day->setMaximumDate(QDate::currentDate());
    form->addRow(QObject::tr("Дата початку обліку"),day);
    auto* amount=new QLineEdit(page);auto* reason=new QLineEdit(page);
    reason->setMaxLength(300);
    form->addRow(QObject::tr("Сума, грн"),amount);form->addRow(QObject::tr("Підстава"),reason);
    auto* notice=new QLabel(page);notice->setWordWrap(true);notice->setTextFormat(Qt::PlainText);
    auto reload=[=]{
        from->clear();to->clear();QString error;
        for(const auto& account:CashBankService::accounts(error)){
            auto balance=CashAccountMovement::displayBalance(account.uid);
            const auto label=account.name+" · "+balance;
            from->addItem(label,account.uid);
            if(!mode->currentIndex() || account.uid!="primary-cash")to->addItem(label,account.uid);
        }
        from->setCurrentIndex(-1);to->setCurrentIndex(-1);
        form->setRowVisible(from,!mode->currentIndex());form->setRowVisible(day,mode->currentIndex());
        return error;
    };
    auto* post=new QPushButton(QObject::tr("Підтвердити"),page);form->addRow(post);
    form->addRow(notice);
    auto* refresh=new QPushButton(QObject::tr("Оновити рахунки"),page);form->addRow(refresh);
    notice->setText(reload());
    QObject::connect(refresh,&QPushButton::clicked,page,[=]{notice->setText(reload());});
    QObject::connect(mode,&QComboBox::currentIndexChanged,page,[=]{notice->setText(reload());});
    QObject::connect(post,&QPushButton::clicked,page,[=,uid=QUuid::createUuid().toString(QUuid::WithoutBraces)]()mutable{
        const bool opening=mode->currentIndex()==1;
        if(!opening && from->currentIndex()<0){notice->setText(QObject::tr("Оберіть джерело коштів."));return;}
        const auto parsed=ExactMoney::payment(amount->text().trimmed().replace(',', '.').toStdString());
        auto cents=std::get_if<ExactMoney::Cents>(&parsed);
        if(!cents){notice->setText(QObject::tr("Перевірте суму."));return;}
        auto result=CashAccountMovement::post({uid,opening?QString():from->currentData().toString(),to->currentData().toString(),reason->text(),opening?day->date():QDate::currentDate(),*cents});
        if(!result.error.isEmpty()){notice->setText(result.error);return;}
        uid=QUuid::createUuid().toString(QUuid::WithoutBraces);
        amount->clear();reason->clear();const auto error=reload();notice->setText(error.isEmpty()?QObject::tr("Проведено."):error);
    });
    return page;
}
