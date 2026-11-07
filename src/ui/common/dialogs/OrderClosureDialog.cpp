#include "OrderClosureDialog.h"
#include <QComboBox>
#include <QDateEdit>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace {
QString money(qint64 cents) {
    QString whole = QString::number(cents / 100);
    for (int i = whole.size()-3; i > 0; i -= 3) whole.insert(i, QLatin1Char(' '));
    return whole + "," + QString::number(cents % 100).rightJustified(2, '0')
        + QString::fromUtf8(" грн");
}
qint64 enteredAmount(const QString& value) {
    static const QRegularExpression expression(QStringLiteral("^([0-9]{1,12})(?:[.,]([0-9]{1,2}))?$"));
    const auto match = expression.match(value.trimmed());
    if (!match.hasMatch()) return -1;
    return match.captured(1).toLongLong() * 100 + match.captured(2).leftJustified(2, '0').toLongLong();
}
}

OrderClosureDialog::OrderClosureDialog(const OrderClosurePreview& preview, QWidget* parent)
    : QDialog(parent) {
    setObjectName("orderClosureDialog");
    setWindowTitle(QString::fromUtf8("Закриття наряду №%1").arg(preview.orderNumber));
    setModal(true);
    setStyleSheet(QStringLiteral(
        "QDialog#orderClosureDialog {background:#191f27;} "
        "QDialog#orderClosureDialog QLabel {color:white;background:transparent;} "
        "QDialog#orderClosureDialog QLineEdit,QDialog#orderClosureDialog QComboBox,QDialog#orderClosureDialog QDateEdit "
        "{background:white;color:black;padding:4px;} "
        "QDialog#orderClosureDialog QPushButton#closeWithPayment:enabled "
        "{color:white;background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #658856,stop:1 #305327);}"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16,14,16,14);
    auto* columns = new QHBoxLayout;
    layout->addLayout(columns);
    auto* left = new QVBoxLayout;
    columns->addLayout(left, 5);
    auto heading = [&](const QString& text, int points) {
        auto* label = new QLabel(text, this);
        auto font = label->font();
        font.setPointSize(points);
        font.setBold(true);
        label->setFont(font);
        return label;
    };
    left->addWidget(heading(windowTitle(),18));
    auto* form = new QFormLayout;
    left->addLayout(form);
    auto* divider = new QFrame(this);
    divider->setFrameShape(QFrame::VLine);
    columns->addWidget(divider);
    auto* right = new QVBoxLayout;
    columns->addLayout(right, 4);
    const qint64 balance = preview.totalCents - preview.receivedCents;
    right->addWidget(heading(balance >= 0 ? tr("Залишок до сплати") : tr("Переплата"),14));
    right->addWidget(heading(money(qAbs(balance)),24));
    auto* totals = new QFormLayout;
    right->addLayout(totals);
    totals->addRow(tr("Загальна сума наряду"), new QLabel(money(preview.totalCents),this));
    totals->addRow(tr("Уже отримано від клієнта"), new QLabel(money(preview.receivedCents),this));
    right->addStretch();
    auto* amount = new QLineEdit(this);
    amount->setObjectName("receivedNow");
    amount->setPlaceholderText(QString::fromUtf8("Введіть фактично отриману суму"));
    form->addRow(QString::fromUtf8("Отримано зараз"), amount);
    auto* method = new QComboBox(this);
    method->setObjectName("paymentMethod");
    method->addItem(QString::fromUtf8("Готівка"), "cash");
    method->addItem(QString::fromUtf8("Картка"), "card");
    method->addItem(QString::fromUtf8("Банківський переказ"), "bank_transfer");
    form->addRow(QString::fromUtf8("Спосіб оплати"), method);
    auto* date = new QDateEdit(QDate::currentDate(), this);
    date->setObjectName("paymentDate");
    date->setDisplayFormat("dd.MM.yyyy");
    date->setCalendarPopup(true);
    form->addRow(QString::fromUtf8("Дата платежу"), date);
    auto* notice = new QLabel(QString::fromUtf8("Після закриття запчастини, видані в цей наряд, будуть позначені як встановлені"), this);
    notice->setWordWrap(true);
    left->addWidget(notice);
    left->addStretch();
    auto* blockers = new QLabel(this);
    blockers->setTextFormat(Qt::PlainText);
    blockers->setWordWrap(true);
    blockers->setText(preview.unresolvedParts.isEmpty() ? QString() :
        QString::fromUtf8("Перед закриттям оформіть видачу, повернення або виключення позицій:\n")
        + preview.unresolvedParts.join('\n'));
    blockers->setVisible(!preview.unresolvedParts.isEmpty());
    layout->addWidget(blockers);
    auto* error = new QLabel(this);
    error->setObjectName("paymentError");
    error->setWordWrap(true);
    layout->addWidget(error);
    auto* pay = new QPushButton(QString::fromUtf8("Закрити й зарахувати оплату"), this);
    pay->setObjectName("closeWithPayment");
    auto* unpaid = new QPushButton(QString::fromUtf8("Закрити без нової оплати"), this);
    unpaid->setObjectName("closeWithoutPayment");
    auto* cancel = new QPushButton(QString::fromUtf8("Скасувати"), this);
    cancel->setObjectName("cancelClosure");
    auto* actions = new QHBoxLayout;
    layout->addLayout(actions);
    actions->addStretch();
    for (auto* button : {cancel, unpaid, pay}) {
        button->setAutoDefault(false);
        actions->addWidget(button);
    }
    cancel->setDefault(true);
    const bool valid = preview.totalCents >= 0 && preview.receivedCents >= 0 && !preview.revision.isEmpty();
    const bool canClose = valid && preview.unresolvedParts.isEmpty();
    unpaid->setEnabled(canClose);
    amount->setEnabled(canClose && balance > 0);
    method->setEnabled(amount->isEnabled());
    date->setEnabled(amount->isEnabled());
    auto validate = [=] {
        const qint64 cents = enteredAmount(amount->text());
        pay->setEnabled(canClose && balance > 0 && (amount->text().isEmpty() || (cents > 0 && cents <= balance)));
        error->setText(!amount->text().trimmed().isEmpty() && cents > balance
            ? QString::fromUtf8("Сума перевищує залишок. Облік нових переплат не підтримується.")
            : (!amount->text().trimmed().isEmpty() && cents <= 0
                ? QString::fromUtf8("Введіть додатну суму з не більш ніж двома знаками після коми.") : QString()));
        error->setVisible(!error->text().isEmpty());
    };
    connect(amount, &QLineEdit::textChanged, this, validate);
    validate();
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(unpaid, &QPushButton::clicked, this, [this, preview] {
        m_choice = {};
        m_choice.expectedRevision = preview.revision;
        accept();
    });
    connect(pay, &QPushButton::clicked, this, [this, validate, pay, unpaid, amount, method, date, preview, balance] {
        if (amount->text().isEmpty() && balance > 0)
            amount->setText(QString::number(balance / 100) + "," + QString::number(balance % 100).rightJustified(2, '0'));
        validate();
        if (!pay->isEnabled()) return;
        pay->setEnabled(false);
        unpaid->setEnabled(false);
        m_choice = {true, enteredAmount(amount->text()), method->currentData().toString(),
                    date->date(), preview.revision};
        accept();
    });
}
