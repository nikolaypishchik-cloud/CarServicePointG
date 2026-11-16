#include "CashBankForm.h"
#include "core/services/orders/CashDeskPolicy.h"
#include "core/services/orders/CashBankService.h"
#include "core/services/orders/CashShiftService.h"
#include "core/services/AccessControlService.h"
#include "core/services/orders/ClientReceiptReader.h"
#include "core/finance/ExactMoney.h"
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTimeEdit>
#include <QTimeZone>
#include <QUuid>
#include <QVBoxLayout>

QWidget* cashSettingsForm(QWidget* parent)
{
    auto* scroll=new QScrollArea(parent);scroll->setWidgetResizable(true);
    auto* page=new QWidget(scroll);scroll->setWidget(page);
    auto* layout=new QVBoxLayout(page);
    auto group=[&](const QString& name){
        auto* box=new QGroupBox(name,page);layout->addWidget(box);return new QFormLayout(box);
    };
    auto* accounts=group(QObject::tr("Каси та рахунки"));
    accounts->addRow(cashBankForm(page,true));
    auto* methods=group(QObject::tr("Способи оплати"));
    methods->addRow(new QLabel(QObject::tr("Готівка · каса. Картка · до банківської звірки. Переказ · за фактом підтвердження."),page));
    auto* work=group(QObject::tr("Робочий день і зміни"));
    QString error;
    const auto current=CashDeskPolicy::latest(error);
    auto* operatorBox=new QComboBox(page);
    operatorBox->addItem(QObject::tr("Оберіть відповідального"),QString());
    for(const auto& row:CashDeskPolicy::operators(error))operatorBox->addItem(row.second,row.first);
    operatorBox->setCurrentIndex(qMax(0,operatorBox->findData(current.responsible)));
    work->addRow(QObject::tr("Відповідальний"),operatorBox);
    auto* branch=new QLineEdit(current.branch,page);branch->setMaxLength(100);
    work->addRow(QObject::tr("Підрозділ"),branch);
    auto* zone=new QComboBox(page);
    for(const auto& name:QTimeZone::availableTimeZoneIds())zone->addItem(QString::fromUtf8(name));
    zone->setCurrentText(current.zone);work->addRow(QObject::tr("Часовий пояс СТО"),zone);
    auto* start=new QTimeEdit(current.start,page);auto* end=new QTimeEdit(current.end,page);
    work->addRow(QObject::tr("Початок робочого дня"),start);work->addRow(QObject::tr("Кінець робочого дня"),end);
    auto* change=new QLineEdit(QString::number(current.changeCents/100)+"."+QString("%1").arg(current.changeCents%100,2,10,QChar('0')),page);
    work->addRow(QObject::tr("Рекомендований розмін, грн"),change);
    auto* reason=new QLineEdit(page);reason->setMaxLength(300);work->addRow(QObject::tr("Підстава зміни"),reason);
    auto* save=new QPushButton(QObject::tr("Зберегти з наступної зміни"),page);work->addRow(save);
    save->setEnabled(AccessControlService::currentUserHasPermission("cash.configure"));
    auto* rights=group(QObject::tr("Права та обмеження"));
    rights->addRow(new QLabel(QObject::tr("Дозволи ролей призначає власник у «Користувачі, ролі й права доступу». Грошові пороги для уповноважених працівників відсутні."),page));
    auto* types=group(QObject::tr("Статті руху коштів"));
    types->addRow(new QLabel(QObject::tr("Оплата, аванс, повернення, внесення, вилучення, видаток, постачальник, переміщення, комісія, коригування та початковий залишок."),page));
    auto* transition=group(QObject::tr("Початкові залишки та перехід"));
    auto* phase=new QLabel(page);transition->addRow(phase);
    auto* check=new QPushButton(QObject::tr("Перевірити налаштування"),page);transition->addRow(check);
    auto* status=new QLabel(page);status->setTextFormat(Qt::PlainText);status->setWordWrap(true);transition->addRow(status);
    const auto reportTransition=[=]{
        qint64 cents=0;const auto diagnostic=CashShiftService::currentBalance(cents);
        phase->setText(diagnostic.isEmpty()?QObject::tr("Підтверджено · облікова готівка %1").arg(ClientReceiptReader::money(cents)):diagnostic);
    };
    auto draft=[=]{
        const auto sum=ExactMoney::payment(change->text().trimmed().replace(',', '.').toStdString());
        const auto* cents=std::get_if<ExactMoney::Cents>(&sum);
        return CashDeskPolicy::Policy{QUuid::createUuid().toString(QUuid::WithoutBraces),operatorBox->currentData().toString(),
            branch->text(),zone->currentText(),reason->text(),start->time(),end->time(),cents?*cents:-1};
    };
    QObject::connect(check,&QPushButton::clicked,page,[=]{
        const auto err=CashDeskPolicy::validate(draft());reportTransition();
        status->setText(err.isEmpty()?QObject::tr("Відповідальний, каса, часовий пояс та робочі години перевірені."):err);
    });
    QObject::connect(save,&QPushButton::clicked,page,[=]{
        const auto err=CashDeskPolicy::save(draft());
        if(err.isEmpty())reason->clear();
        status->setText(err.isEmpty()?QObject::tr("Збережено для наступної зміни."):err);reportTransition();
    });
    for(auto* label:page->findChildren<QLabel*>())label->setWordWrap(true);
    reportTransition();status->setText(error);layout->addStretch();return scroll;
}
