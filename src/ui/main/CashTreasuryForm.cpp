#include "CashMovementForm.h"
#include "core/services/orders/CashTreasury.h"
#include "core/services/orders/CashBankService.h"
#include "core/services/orders/ClientReceiptReader.h"
#include "core/finance/ExactMoney.h"
#include <QFormLayout>
#include <QComboBox>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QUuid>
#include <memory>

QWidget* cashTreasuryForm(QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* form = new QFormLayout(page);
    auto* kind = new QComboBox(page);
    kind->addItem(QObject::tr("Службове внесення"), "deposit");
    kind->addItem(QObject::tr("Службове вилучення"), "withdrawal");
    kind->addItem(QObject::tr("Оплата постачальнику"), "supplier");
    auto* account = new QComboBox(page);
    auto* source = new QComboBox(page);
    auto* amount = new QLineEdit(page);
    auto* reason = new QLineEdit(page);
    reason->setMaxLength(300);
    auto* reload = new QPushButton(QObject::tr("Оновити джерела"), page);
    auto* confirm = new QPushButton(QObject::tr("Провести"), page);
    auto* status = new QLabel(page);
    status->setWordWrap(true);
    status->setTextFormat(Qt::PlainText);
    form->addRow(QObject::tr("Операція"), kind);
    form->addRow(QObject::tr("Рахунок"), account);
    form->addRow(QObject::tr("Придбання"), source);
    form->addRow(reload);
    form->addRow(QObject::tr("Сума, грн"), amount);
    form->addRow(QObject::tr("Підстава"), reason);
    form->addRow(confirm);
    form->addRow(status);
    auto debts = std::make_shared<QVector<PurchaseDebt>>();
    auto refresh = [=] {
        const bool supplier = kind->currentData() == "supplier";
        form->setRowVisible(source, supplier);
        account->clear();
        source->clear();
        debts->clear();
        QString error;
        for (const auto& row : CashBankService::accounts(error)) {
            if (row.uid == "primary-cash" || (supplier && row.type == "bank"))
                account->addItem(row.name, row.uid);
        }
        if (supplier && error.isEmpty()) {
            *debts = CashTreasury::openDebts(error);
            for (const auto& row : *debts) {
                source->addItem(QString("%1 · %2 · %3 · #%4 · %5")
                    .arg(row.supplier,row.date,row.name).arg(row.id).arg(ClientReceiptReader::money(row.remainingCents())));
            }
            source->setCurrentIndex(-1);
        }
        return error;
    };
    status->setText(refresh());
    QObject::connect(reload,&QPushButton::clicked,page,[=]{ status->setText(refresh()); });
    QObject::connect(kind,&QComboBox::currentIndexChanged,page,[=]{ status->setText(refresh()); });
    QObject::connect(confirm,&QPushButton::clicked,page,[=,uid=QUuid::createUuid().toString(QUuid::WithoutBraces)]() mutable {
        auto parsed = ExactMoney::payment(amount->text().trimmed().replace(',', '.').toStdString());
        auto cents = std::get_if<ExactMoney::Cents>(&parsed);
        if (!cents || *cents <= 0) { status->setText(QObject::tr("Введіть додатну суму.")); return; }
        PurchaseDebt debt;
        if (kind->currentData() == "supplier") {
            if (source->currentIndex() < 0) { status->setText(QObject::tr("Оберіть придбання.")); return; }
            debt = debts->at(source->currentIndex());
        }
        const auto result = CashTreasury::post({uid,kind->currentData().toString(),account->currentData().toString(),
            reason->text(),*cents,QDate::currentDate(),debt});
        if (!result.error.isEmpty()) { status->setText(result.error); return; }
        amount->clear();
        reason->clear();
        uid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto error = refresh();
        status->setText(error.isEmpty() ? QObject::tr("Проведено.") : error);
    });
    return page;
}
