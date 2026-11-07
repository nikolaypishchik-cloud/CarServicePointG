#include "ui/common/dialogs/PurchasePaymentDialog.h"
#include "core/util/FormatUtils.h"
#include "core/finance/ExactMoney.h"
#include "core/services/InvoiceRegistryService.h"
#include "core/services/PartsQueryService.h"
#include "core/services/SupplierDebtReadService.h"
#include "core/services/AccessControlService.h"
#include "runtime/AsyncThreadScope.h"
#include <QProgressDialog>
#include <QScopeGuard>
#include <QSet>
#include <algorithm>
#include <QApplication>
#include <QDialog>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QMessageBox>
#include <QScrollArea>
#include <QScreen>
#include <QTimer>
#include <QTableWidget>
#include <QHeaderView>

bool choosePurchasePayment(QWidget* parent, const PurchaseDebt& debt, bool newPurchase,
                           qint64* amount, int* deferredDays, const QVector<PurchaseDebt>* group, bool paymentAllowed) {
    if (!newPurchase && debt.remainingCents() <= 0) {
        QMessageBox::information(parent, QString::fromUtf8("Оплата"),
            debt.totalCents > 0 ? QString::fromUtf8("Оплата вже здійснена. Заборгованість відсутня.")
                                : QString::fromUtf8("Заборгованість відсутня. Сплачувати нічого не потрібно."));
        return false;
    }
    QDialog dialog(parent);
    dialog.setObjectName("purchasePaymentDialog");
    dialog.ensurePolished();
    const qreal fontPoints = 9.0;
    dialog.setStyleSheet(QStringLiteral(
        "QDialog#purchasePaymentDialog { background:#263746; color:#ffffff; }"
        "QDialog#purchasePaymentDialog, QDialog#purchasePaymentDialog QWidget { font-family:'Times New Roman'; font-size:%1pt; font-weight:normal; }"
        "QDialog#purchasePaymentDialog QPushButton { font-family:'Times New Roman'; font-size:12pt; }"
        "QPushButton#payInFull { color:#ffffff; border:1px solid #51b985; background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #35a96c,stop:1 #11673b); }"
        "QPushButton#payPartially { color:#191919; border:1px solid #d78222; background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #ffd08b,stop:1 #ed8b21); }"
        "QPushButton#leaveUnpaid { color:#ffffff; border:1px solid #ed7777; background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #ec5656,stop:1 #af1717); }"
        "QPushButton#payInFull:hover { background:#218b54; }"
        "QPushButton#payPartially:hover { background:#ffb64e; }"
        "QPushButton#leaveUnpaid:hover { background:#d63232; }"
        "QPushButton#payInFull:pressed, QPushButton#payPartially:pressed, QPushButton#leaveUnpaid:pressed { border:2px solid #ffffff; }"
        "QPushButton#payInFull:disabled, QPushButton#payPartially:disabled, QPushButton#leaveUnpaid:disabled { background:#65717b; color:#d8dce0; border:1px solid #89939c; }"
        "QDialog#purchasePaymentDialog QLabel, QDialog#purchasePaymentDialog QCheckBox { background:transparent; color:#ffffff; }"
        "QWidget#paymentContent, QWidget#partialPaymentHost, QScrollArea#paymentScroll { background:#263746; border:none; }"
        "QDialog#purchasePaymentDialog QAbstractSpinBox { background:#ffffff; color:#111827; padding:6px 28px 6px 10px; }"
        "QDialog#purchasePaymentDialog QTableWidget { background:#ffffff; color:#111827; }"
        "QDialog#purchasePaymentDialog QHeaderView::section { background:#e5edf5; color:#111827; padding:6px; }"
        "QDialog#purchasePaymentDialog QCheckBox::indicator { width:26px; height:26px; border:2px solid #111827; border-radius:4px; }"
        "QDialog#purchasePaymentDialog QCheckBox::indicator:unchecked { background:#ffffff; image:none; }"
        "QDialog#purchasePaymentDialog QCheckBox::indicator:checked { background:#0b995e; image:url(:/assets/check_white.png); }"
        "QDialog#purchasePaymentDialog QAbstractSpinBox::up-button { subcontrol-origin:border; subcontrol-position:top right; width:24px; height:20px; }"
        "QDialog#purchasePaymentDialog QAbstractSpinBox::down-button { subcontrol-origin:border; subcontrol-position:bottom right; width:24px; height:20px; }").arg(fontPoints));
    dialog.setWindowTitle(QString::fromUtf8("Оплата придбання"));
    if (group) dialog.setWindowTitle(debt.name);
    const QRect available = (parent ? parent->screen() : QApplication::primaryScreen())->availableGeometry();
    const int extraWidth = qRound(dialog.logicalDpiX()*20.0/25.4);
    dialog.resize(qMin(available.width()-80,480+extraWidth), qMin(available.height()-80,300));
    auto* layout = new QVBoxLayout(&dialog);
    auto* scroll = new QScrollArea(&dialog);
    scroll->setObjectName("paymentScroll");
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* content = new QWidget(scroll);
    content->setObjectName("paymentContent");
    auto* body = new QVBoxLayout(content);
    body->setSizeConstraint(QLayout::SetMinimumSize);
    body->setSpacing(8);
    body->setContentsMargins(4,4,4,4);
    layout->setContentsMargins(10,10,10,10);
    layout->setSpacing(8);
    auto divider = [&](bool vertical=false) {
        auto* line=new QFrame(content);
        line->setObjectName("paymentDivider");
        if(vertical) line->setFixedWidth(1); else line->setFixedHeight(1);
        return line;
    };
    auto* details = new QHBoxLayout;
    details->setSpacing(12);
    auto* left = new QVBoxLayout;
    auto* right = new QVBoxLayout;
    left->setSpacing(8); right->setSpacing(6);
    auto* form = new QFormLayout;
    auto* totals = new QFormLayout;
    for (auto* group : {form,totals}) {
        group->setHorizontalSpacing(8);
        group->setVerticalSpacing(3);
        group->setFormAlignment(Qt::AlignTop);
        group->setRowWrapPolicy(QFormLayout::WrapLongRows);
        group->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    }
    auto field = [&](QFormLayout* group,const QString& label,const QString& text) {
        auto* value = new QLabel(text,content);
        value->setWordWrap(true); value->setTextFormat(Qt::PlainText);
        value->setMinimumWidth(0);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        auto* caption=new QLabel(label,content);
        caption->setWordWrap(true);
        group->addRow(caption,value);
    };
    auto money = [](qint64 value) { return FormatUtils::moneyCents(value)+QString::fromUtf8(" грн"); };
    auto* heading = new QLabel(debt.name,content);
    heading->setTextFormat(Qt::PlainText);
    heading->setWordWrap(true);
    heading->setObjectName("paymentHeading");
    left->addWidget(heading);
    left->addLayout(form);
    left->addStretch();
    field(form,QString::fromUtf8("Магазин"),debt.supplier);
    const auto purchaseDate=PartsQueryService::parseBusinessDate(debt.date);
    field(form,QString::fromUtf8("Дата"),purchaseDate.isValid() ? purchaseDate.toString("dd.MM.yy") : debt.date);
    if (debt.orderNumber) field(form,QString::fromUtf8("Наряд / авто"),QString::number(debt.orderNumber)+" / "+debt.car);
    if (!debt.oem.isEmpty()) field(form,"OEM",debt.oem);
    if (!debt.invoice.isEmpty()) field(form,QString::fromUtf8("Накладна"),debt.invoice);
    if (!group) field(form,QString::fromUtf8("Кількість"),QString::number(debt.quantity)+" "+debt.unit);
    if (!debt.note.isEmpty()) field(form,QString::fromUtf8("Примітка"),debt.note);
    field(totals,QString::fromUtf8("Вартість"),money(debt.totalCents));
    field(totals,QString::fromUtf8("Сплачено"),money(debt.paidCents));
    auto* balance = new QLabel(money(debt.remainingCents()),content);
    balance->setObjectName("paymentBalance");
    balance->setWordWrap(true);
    right->addWidget(new QLabel(QString::fromUtf8("До сплати"),content));
    right->addWidget(balance);
    right->addWidget(divider());
    right->addLayout(totals);
    right->addStretch();
    if (debt.dueDate.isValid()) field(totals,QString::fromUtf8("Сплатити до"),debt.dueDate.toString("dd.MM.yy"));
    details->addLayout(left,3); details->addWidget(divider(true)); details->addLayout(right,2);
    body->addLayout(details);
    body->addWidget(divider());

    const bool allowed = paymentAllowed && PurchasePaymentService::canPay(debt.source);
    auto* deferral = new QCheckBox(QString::fromUtf8("Відтермінувати"),content);
    deferral->setObjectName("deferPayment");
    deferral->setChecked(debt.dueDate.isValid());
    deferral->setEnabled(allowed && debt.remainingCents()>0);
    auto* days = new QSpinBox(content);
    days->setObjectName("paymentDeferredDays"); days->setRange(1,3650);
    days->setSuffix(QString::fromUtf8(" днів"));
    days->setValue(debt.dueDate.isValid() ? int(qBound(qint64(1),QDate::currentDate().daysTo(debt.dueDate),qint64(3650))) : 7);
    days->setEnabled(deferral->isEnabled() && deferral->isChecked());
    auto* dueLabel = new QLabel(content); dueLabel->setWordWrap(true);
    auto* deferLayout = new QGridLayout;
    deferLayout->setSpacing(4);
    deferLayout->addWidget(deferral,0,0,1,2); deferLayout->addWidget(days,1,0);
    deferLayout->addWidget(dueLabel,1,1);
    bool termsChanged = false;
    auto updateDue = [&] {
        days->setEnabled(deferral->isEnabled() && deferral->isChecked());
        const QDate due = !termsChanged && debt.dueDate.isValid() ? debt.dueDate : QDate::currentDate().addDays(days->value());
        dueLabel->setText(deferral->isChecked() ? QString::fromUtf8("До %1").arg(due.toString("dd.MM.yy")) : QString());
    };
    QObject::connect(deferral,&QCheckBox::toggled,&dialog,[&] { termsChanged=true; updateDue(); });
    QObject::connect(days,&QSpinBox::valueChanged,&dialog,[&] { termsChanged=true; updateDue(); });
    updateDue();
    auto acceptPayment = [&](qint64 paid) {
        if (amount) *amount=paid;
        if (deferredDays) *deferredDays = !termsChanged && debt.dueDate.isValid() ? -1 : (deferral->isChecked() ? days->value() : 0);
        dialog.accept();
    };
    auto* partialHost = new QWidget(content);
    partialHost->setObjectName("partialPaymentHost");
    auto* partialLayout = new QGridLayout(partialHost);
    partialLayout->setSizeConstraint(QLayout::SetMinimumSize);
    partialLayout->setSpacing(4);
    partialLayout->setContentsMargins(0,0,0,0);
    auto* partial = new QCheckBox(QString::fromUtf8("Часткова сплата"), partialHost);
    partial->setObjectName("partialPayment");
    partial->setEnabled(allowed && debt.remainingCents() > 0);
    partialLayout->addWidget(partial,0,0,1,2);
    auto* value = new QDoubleSpinBox(partialHost);
    value->setObjectName("partialPaymentAmount"); value->setDecimals(2);
    value->setRange(0,debt.remainingCents()/100.0);
    value->setValue(debt.remainingCents()/100.0);
    value->setEnabled(allowed);
    value->setSuffix(QString::fromUtf8(" грн"));
    value->setButtonSymbols(QAbstractSpinBox::NoButtons);
    value->setMinimumHeight(32);
    partialLayout->addWidget(value,1,0);
    auto* all = new QPushButton(QString::fromUtf8("Уся сума"),partialHost);
    all->setObjectName("useRemainingAmount"); all->setEnabled(allowed);
    all->setMinimumHeight(32);
    partialLayout->addWidget(all,1,1);
    auto updatePaymentMode = [&] {
        value->setEnabled(allowed && partial->isChecked());
        if (!partial->isChecked())
            value->setValue(!group && deferral->isChecked() ? 0 : debt.remainingCents()/100.0);
    };
    QObject::connect(partial,&QCheckBox::toggled,&dialog,updatePaymentMode);
    QObject::connect(deferral,&QCheckBox::toggled,&dialog,updatePaymentMode);
    QObject::connect(all,&QPushButton::clicked,&dialog,[&] {
        deferral->setChecked(false);
        partial->setChecked(false);
        updatePaymentMode();
    });
    updatePaymentMode();
    auto* entryRow=new QHBoxLayout;
    entryRow->setSpacing(16);
    entryRow->addWidget(partialHost,group ? 1 : 3);
    entryRow->addLayout(deferLayout,group ? 0 : 2);
    if (group) {
        deferral->hide(); days->hide(); dueLabel->hide();
    }
    body->addLayout(entryRow);
    auto* remaining = new QLabel(content);
    remaining->setObjectName("paymentRemaining");
    remaining->setWordWrap(true);
    body->addWidget(remaining);
    remaining->setVisible(group != nullptr);
    auto selectedAmount = [&]() -> qint64 {
        if (!partial->isChecked())
            return !group && deferral->isChecked() ? 0 : debt.remainingCents();
        // Parse the displayed decimal amount, not a binary floating-point product.
        QString text = value->cleanText();
        text.remove(value->locale().groupSeparator());
        text.replace(value->locale().decimalPoint(), QStringLiteral("."));
        const auto parsed = ExactMoney::payment(text.toStdString());
        if (const auto* cents = std::get_if<ExactMoney::Cents>(&parsed)) return *cents;
        return -1;
    };
    auto updateRemaining = [&] {
        const auto paid = selectedAmount();
        remaining->setText(paid < 0 ? QString::fromUtf8("Некоректна сума оплати.") : paid==0
            ? QString::fromUtf8("Без платежу: попередні оплати залишаться без змін.")
            : QString::fromUtf8("Після оплати залишиться: %1").arg(money(debt.remainingCents()-paid)));
    };
    QObject::connect(value,&QDoubleSpinBox::valueChanged,&dialog,updateRemaining);
    QObject::connect(partial,&QCheckBox::toggled,&dialog,updateRemaining);
    QObject::connect(deferral,&QCheckBox::toggled,&dialog,updateRemaining);
    QObject::connect(value,&QDoubleSpinBox::valueChanged,&dialog,[&] {
        value->setToolTip(remaining->text());
    });
    updateRemaining();
    auto* history = new QTableWidget(content);
    history->setObjectName("paymentHistory");
    history->setColumnCount(3);
    history->setHorizontalHeaderLabels({QString::fromUtf8("Дата"),QString::fromUtf8("Сума"),QString::fromUtf8("Користувач")});
    history->setEditTriggers(QAbstractItemView::NoEditTriggers);
    history->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    history->verticalHeader()->hide();
    history->setFixedHeight(150);
    history->hide();
    body->addWidget(history);
    auto* historyError=new QLabel(content);
    historyError->setObjectName("paymentHistoryError");
    historyError->setWordWrap(true);historyError->hide();
    body->addWidget(historyError);
    body->addStretch(); scroll->setWidget(content); layout->addWidget(scroll,1);

    auto* buttons = new QHBoxLayout;
    auto* full = new QPushButton(QString::fromUtf8("Підтвердити оплату"),&dialog); full->setObjectName("payInFull");
    auto* historyButton = new QPushButton(QString::fromUtf8("Історія оплат"),&dialog);
    historyButton->setObjectName("togglePaymentHistory"); historyButton->setCheckable(true);
    QObject::connect(historyButton,&QPushButton::toggled,&dialog,[&](bool checked) {
        historyError->clear();historyError->hide();
        if (checked) {
            QString error;
            auto entries=PurchasePaymentService::groupHistory(group ? *group : QVector<PurchaseDebt>{debt},&error);
            if(!error.isEmpty()) {
                history->setRowCount(0);history->hide();
                historyButton->setChecked(false);
                historyError->setText(QStringLiteral("Не вдалося завантажити історію оплат: ")+error);
                historyError->show();
                historyButton->setText(QStringLiteral("Повторити завантаження історії"));
                return;
            }
            std::stable_sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.date < b.date; });
            history->setRowCount(entries.size());
            for(int i=0;i<entries.size();++i) {
                history->setItem(i,0,new QTableWidgetItem(entries[i].date));
                history->setItem(i,1,new QTableWidgetItem(money(entries[i].amountCents)));
                history->setItem(i,2,new QTableWidgetItem(entries[i].actor));
            }
            historyButton->setText(entries.isEmpty() ? QString::fromUtf8("Історія порожня") : QString::fromUtf8("Приховати історію"));
        } else historyButton->setText(QString::fromUtf8("Історія оплат"));
        history->setVisible(checked);
        if (checked) QTimer::singleShot(0,scroll,[=] { scroll->ensureWidgetVisible(history); });
    });
    auto* cancel = new QPushButton(QString::fromUtf8("Скасувати"),&dialog);
    full->setEnabled(allowed && (debt.remainingCents()>0 || (newPurchase && debt.totalCents==0)));
    QObject::connect(full,&QPushButton::clicked,&dialog,[&] {
        value->interpretText();
        const auto paid = selectedAmount();
        if (partial->isChecked() && (paid <= 0 || paid >= debt.remainingCents())) {
            QMessageBox::warning(&dialog, QString::fromUtf8("Часткова сплата"),
                QString::fromUtf8("Вкажіть суму більшу за нуль та меншу за залишок боргу. Для повної оплати натисніть «Уся сума»."));
            return;
        }
        if (allowed && paid>=(group ? 1 : 0) && paid<=debt.remainingCents()) acceptPayment(paid);
    });
    QObject::connect(cancel,&QPushButton::clicked,&dialog,&QDialog::reject);
    buttons->addWidget(historyButton); buttons->addStretch(); buttons->addWidget(cancel); buttons->addWidget(full);
    for (auto* button : dialog.findChildren<QPushButton*>()) {
        button->setAutoDefault(false); button->setDefault(false); button->setMinimumHeight(28);
    }
    layout->addWidget(divider());
    layout->addLayout(buttons);
    dialog.setStyleSheet(dialog.styleSheet()+QStringLiteral(
        "QDialog#purchasePaymentDialog { background:#202930; }"
        "QDialog#purchasePaymentDialog QWidget#paymentContent, QDialog#purchasePaymentDialog QWidget#partialPaymentHost, QDialog#purchasePaymentDialog QScrollArea#paymentScroll { background:transparent; border:none; }"
        "QDialog#purchasePaymentDialog QFrame#paymentDivider { background:#69747d; border:none; }"
        "QDialog#purchasePaymentDialog QLabel#paymentHeading { font-size:14pt; font-weight:bold; }"
        "QDialog#purchasePaymentDialog QLabel#paymentBalance { font-size:24pt; font-weight:bold; }"
        "QDialog#purchasePaymentDialog QPushButton { font-size:9pt; min-width:0px; min-height:0px; padding:4px 8px; border-radius:4px; border:1px solid #84919d; color:white; background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #5b6672,stop:0.12 #414c58,stop:1 #252d35); }"
        "QDialog#purchasePaymentDialog QPushButton#payInFull { background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #388348,stop:1 #125b29); border:1px solid #72b480; }"
        "QDialog#purchasePaymentDialog QPushButton:hover { border-color:#dce5ee; }"
        "QDialog#purchasePaymentDialog QPushButton:disabled { color:#929ca5; background:#30373d; }"
        "QDialog#purchasePaymentDialog QPushButton#togglePaymentHistory { border:none; background:transparent; padding:4px 0px; }"
        "QDialog#purchasePaymentDialog QAbstractSpinBox { min-height:26px; min-width:0px; padding:2px 4px; border:1px solid #aab3bc; border-radius:3px; }"
        "QDialog#purchasePaymentDialog QSpinBox { padding-right:16px; }"
        "QDialog#purchasePaymentDialog QAbstractSpinBox::up-button, QDialog#purchasePaymentDialog QAbstractSpinBox::down-button { width:14px; height:12px; }"
        "QDialog#purchasePaymentDialog QCheckBox::indicator { width:14px; height:14px; border:1px solid #9ca8b3; border-radius:2px; }"));
    for (auto* label : content->findChildren<QLabel*>()) {
        label->ensurePolished();
        label->setMinimumHeight(label->sizeHint().height());
    }
    QTimer::singleShot(0,&dialog,[&] {
        const int margin=qRound(dialog.logicalDpiY()*10/25.4);
        const auto frame=dialog.frameGeometry();
        dialog.resize(qMin(dialog.width(),available.width()-2*margin),
                      qMin(dialog.height(),available.height()-2*margin-(frame.height()-dialog.height())));
        dialog.move(available.center().x()-dialog.frameGeometry().width()/2,
                    available.center().y()-dialog.frameGeometry().height()/2);
    });
    return dialog.exec()==QDialog::Accepted;
}

bool chooseGroupedPurchasePayment(QWidget* parent, const QString& title, const QVector<PurchaseDebt>& debts, qint64* amount) {
    PurchaseDebt total;
    total.source = "part";
    total.name = title;
    total.note = QString::fromUtf8("Часткова оплата погашає позиції послідовно. В історії платежі показані за позиціями.");
    QStringList suppliers;
    for (const auto& debt : debts) {
        total.totalCents += debt.totalCents;
        total.paidCents += debt.paidCents;
        if (!debt.supplier.isEmpty() && !suppliers.contains(debt.supplier)) suppliers << debt.supplier;
    }
    total.supplier = suppliers.join(", ");
    return choosePurchasePayment(parent, total, false, amount, nullptr, &debts);
}

bool openOrderPartsPayment(QWidget* parent, int orderNumber) {
    // Keep the synchronous caller contract while the modal event loop remains responsive.
    static QSet<int> pendingOrders;
    if(pendingOrders.contains(orderNumber))return false;
    pendingOrders.insert(orderNumber);
    const auto release=qScopeGuard([orderNumber]{pendingOrders.remove(orderNumber);});
    const QPointer<QWidget> owner(parent);
    const auto scope=AccessControlService::currentScopeKey();
    const auto task=SupplierDebtReadService::prepareOrderPayment(orderNumber);
    const auto cancel=std::make_shared<std::atomic_bool>(false);
    struct Result { PurchasePaymentService::OrderPaymentPreparation data;QString error;bool ready=false; };
    const auto result=std::make_shared<Result>();
    QPointer<QProgressDialog> progress=new QProgressDialog(QString::fromUtf8("Підготовка оплати…"),QString::fromUtf8("Скасувати"),0,0,parent);
    progress->setObjectName("orderPaymentPreparation");
    progress->setWindowModality(Qt::ApplicationModal);
    progress->setMinimumDuration(0);
    QObject::connect(progress,&QProgressDialog::canceled,progress,[cancel]{cancel->store(true);});
    QObject::connect(progress,&QDialog::rejected,progress,[cancel]{cancel->store(true);});
    auto* worker=AsyncThreadScope::startWorker(progress,"order_payment_preparation",cancel,[task,cancel,result]{
        result->data=task(*cancel,&result->error);
    });
    QObject::connect(worker,&QThread::finished,progress,[progress,result]{
        result->ready=true;
        if(progress)progress->accept();
    });
    worker->start();
    progress->exec();
    if(!result->ready)cancel->store(true);
    const bool cancelled=cancel->load();
    if(progress)delete progress.data();
    if(cancelled || !result->ready || (parent && !owner)
        || scope!=AccessControlService::currentScopeKey())return false;
    QString error=result->error;
    const auto preparation=result->data;
    if (!error.isEmpty()) { QMessageBox::warning(parent, QString::fromUtf8("Оплата"), error); return false; }
    qint64 amount = 0;
    if (!chooseGroupedPurchasePayment(parent, QString::fromUtf8("Оплата запчастин наряду №%1").arg(orderNumber), preparation.displayedPurchases, &amount)) return false;
    if (!PurchasePaymentService::payOrderParts(orderNumber, preparation.expectedDebts, &error, amount)) {
        QMessageBox::warning(parent, QString::fromUtf8("Оплата"), error); return false;
    }
    showPurchasePaymentSaved(parent, amount);
    return true;
}

bool openInvoicePurchasePayment(QWidget* parent, const RegisteredInvoice& invoice) {
    if(invoice.amountMismatch) {
        QMessageBox::warning(parent,QString::fromUtf8("Звірка накладної"),
            QString::fromUtf8("Сума накладної №%1 не збігається із сумою пов’язаних позицій. Звірте накладну перед оплатою.").arg(invoice.number));
        return false;
    }
    if(invoice.paid()) {
        QString message=QString::fromUtf8("Накладна №%1 сплачена. Заборгованість відсутня.").arg(invoice.number);
        if(invoice.paidAt.isValid())message+=QString::fromUtf8("\nДата сплати: %1").arg(invoice.paidAt.toString("dd.MM.yyyy"));
        QMessageBox::information(parent,QString::fromUtf8("Оплата"),message);return false;
    }
    PurchaseDebt summary;
    summary.source = "part";
    summary.name = QString::fromUtf8("Оплата накладної №%1").arg(invoice.number);
    summary.invoice = invoice.number;
    summary.supplier = invoice.supplier;
    summary.date = invoice.date.toString(Qt::ISODate);
    summary.totalCents = invoice.totalCents;
    summary.paidCents = invoice.paidCents;
    const bool allowed = PurchasePaymentService::canPayInvoice(invoice);
    summary.note = allowed
        ? QString::fromUtf8("Частковий платіж погашає позиції послідовно. Розрахунок клієнта не змінюється.")
        : QString::fromUtf8("Локальна оплата недоступна. Для накладних Еліт без пов'язаних закупівель перевірте оплату в постачальника. Для складських або розподілених закупівель використайте їхню форму оплати; розбіжності сум спочатку звірте.");
    qint64 amount = 0;
    if (!choosePurchasePayment(parent, summary, false, &amount, nullptr, &invoice.parts, allowed)) return false;
    QString error;
    if (!PurchasePaymentService::payInvoice(invoice, &error, amount)) {
        QMessageBox::warning(parent, QString::fromUtf8("Оплата"), error); return false;
    }
    showPurchasePaymentSaved(parent, amount);
    return true;
}

bool openPurchasePayment(QWidget* parent,const QString& source,qint64 id,const QString& uid) {
    const auto requestKey=source+QStringLiteral(":")+QString::number(id)+QStringLiteral(":")+uid;
    // Keep the synchronous caller contract while the modal event loop remains responsive.
    static QSet<QString> pendingPayments;
    if(pendingPayments.contains(requestKey))return false;
    pendingPayments.insert(requestKey);
    const auto release=qScopeGuard([requestKey]{pendingPayments.remove(requestKey);});
    const QPointer<QWidget> owner(parent);
    const auto scope=AccessControlService::currentScopeKey();
    const auto task=SupplierDebtReadService::preparePayment(source,id,uid);
    const auto cancel=std::make_shared<std::atomic_bool>(false);
    struct Result { PurchaseDebt data;QString error;bool ready=false; };
    const auto result=std::make_shared<Result>();
    QPointer<QProgressDialog> progress=new QProgressDialog(QString::fromUtf8("Підготовка оплати…"),QString::fromUtf8("Скасувати"),0,0,parent);
    progress->setObjectName("purchasePaymentPreparation");
    progress->setWindowModality(Qt::ApplicationModal);
    progress->setMinimumDuration(0);
    QObject::connect(progress,&QProgressDialog::canceled,progress,[cancel]{cancel->store(true);});
    QObject::connect(progress,&QDialog::rejected,progress,[cancel]{cancel->store(true);});
    auto* worker=AsyncThreadScope::startWorker(progress,"purchase_payment_preparation",cancel,[task,cancel,result]{
        result->data=task(*cancel,&result->error);
    });
    QObject::connect(worker,&QThread::finished,progress,[progress,result]{
        result->ready=true;
        if(progress)progress->accept();
    });
    worker->start();
    progress->exec();
    if(!result->ready)cancel->store(true);
    const bool cancelled=cancel->load();
    if(progress)delete progress.data();
    if(cancelled || !result->ready || (parent && !owner)
        || scope!=AccessControlService::currentScopeKey())return false;
    QString error=result->error;
    const auto debt=result->data;
    if (!error.isEmpty()) { QMessageBox::warning(parent, QString::fromUtf8("Оплата"), error); return false; }
    if (!debt.id || !error.isEmpty()) { QMessageBox::warning(parent,QString::fromUtf8("Оплата"),error); return false; }
    qint64 amount=0;
    int deferredDays=-1;
    if (!choosePurchasePayment(parent,debt,false,&amount,&deferredDays)) return false;
    if (!PurchasePaymentService::pay(debt,amount,&error,deferredDays)) {
        QMessageBox::warning(parent,QString::fromUtf8("Оплата"),error); return false;
    }
    showPurchasePaymentSaved(parent, amount);
    return true;
}

void showPurchasePaymentSaved(QWidget* parent, qint64 amountCents) {
    if (amountCents > 0)
        QMessageBox::information(parent, QString::fromUtf8("Оплату здійснено"),
            QString::fromUtf8("Оплату %1 грн успішно збережено.").arg(FormatUtils::moneyCents(amountCents)));
}
