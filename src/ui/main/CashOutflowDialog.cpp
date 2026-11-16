#include "CashAdvanceDialog.h"
#include "CashJournalPage.h"
#include "core/services/orders/CashOutflow.h"
#include "core/finance/ExactMoney.h"
#include <QDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QUuid>

void openOutflowDialog(CashJournalPage* page,QString kind,QString uid,QString label,qint64 fullCents)
{
    const bool expense=kind=="expense";
    auto* dialog=new QDialog(page);
    const bool reverse=kind=="reverse" || kind.startsWith("void_");
    dialog->setObjectName("outflowDialog");
    dialog->setWindowTitle(reverse?QObject::tr("Повне сторно операції"):expense?QObject::tr("Видаток готівки"):QObject::tr("Повернення клієнту"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::WindowModal);
    auto* form=new QFormLayout(dialog);
    form->addRow(reverse?QObject::tr("Початкова операція"):expense?QObject::tr("Каса"):QObject::tr("Надходження"),new QLabel(label,dialog));
    const auto day=QDate::currentDate();
    form->addRow(QObject::tr("Дата"),new QLabel(day.toString("dd.MM.yyyy"),dialog));
    auto* amount=new QLineEdit(dialog);
    if(reverse){amount->setText(QString("%1.%2").arg(fullCents/100).arg(fullCents%100,2,10,QChar('0')));amount->setReadOnly(true);}
    amount->setObjectName("outflowAmount");
    auto* category=new QLineEdit(dialog);
    category->setObjectName("outflowCategory");
    category->setMaxLength(300);
    if(expense)form->addRow(QObject::tr("Стаття витрати"),category);
    else category->hide();
    auto* reason=new QLineEdit(dialog);
    reason->setObjectName("outflowReason");
    reason->setMaxLength(300);
    form->addRow(QObject::tr("Сума, грн"),amount);
    form->addRow(QObject::tr("Причина"),reason);
    if(!expense && !reverse)form->addRow(new QLabel(QObject::tr("Спосіб повернення відповідає початковому надходженню."),dialog));
    auto* status=new QLabel(dialog);
    status->setObjectName("outflowStatus");
    status->setWordWrap(true);
    status->setTextFormat(Qt::PlainText);
    form->addRow(status);
    auto* submit=new QPushButton(QObject::tr("Провести"),dialog);
    submit->setObjectName("outflowPost");
    form->addRow(submit);
    const auto request=QUuid::createUuid().toString(QUuid::WithoutBraces);
    QObject::connect(submit,&QPushButton::clicked,dialog,[=]{
        auto parsed=ExactMoney::payment(amount->text().trimmed().replace(',', '.').toStdString());
        const auto* cents=std::get_if<ExactMoney::Cents>(&parsed);
        if(!cents || *cents<=0){status->setText(QObject::tr("Введіть додатну суму."));return;}
        const auto result=CashOutflow::post({request,kind,uid,reason->text(),day,*cents,category->text()});
        if(!result.error.isEmpty()){status->setText(result.error);return;}
        status->setText(result.replay?QObject::tr("Уже проведено."):QObject::tr("Проведено."));
        category->setReadOnly(true);amount->setReadOnly(true);reason->setReadOnly(true);submit->setEnabled(false);
        page->refresh();
    });
    dialog->open();
}
