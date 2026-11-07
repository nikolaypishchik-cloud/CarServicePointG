#pragma once
#include "core/services/PurchasePaymentService.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <functional>

inline void showInventoryDetails(QWidget* parent,
                          const QString& title,
                          const QList<QPair<QString, QString>>& fields,
                          std::function<bool(QWidget*)> payment = {})
{
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setModal(true);
    dialog.resize(620, 430);
    dialog.setStyleSheet(QStringLiteral(
        "QDialog{background:#2b3d4f;} QLabel{color:#edf4fa;}"
        "QLabel#inventoryDetailValue{color:#ffffff;background:#22354b;border:1px solid #688096;border-radius:4px;padding:7px 9px;}"
        "QPushButton{background:#264762;color:#ffffff;border:1px solid #aac0d1;border-radius:4px;padding:7px 14px;font-weight:700;}"
        "QPushButton:hover{background:#315d7d;}"));

    auto* root = new QVBoxLayout(&dialog);
    root->setContentsMargins(22, 18, 22, 16);
    root->setSpacing(10);
    auto* heading = new QLabel(title, &dialog);
    heading->setStyleSheet(QStringLiteral("font-size:16pt;font-weight:800;color:#ffffff;"));
    root->addWidget(heading);

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(8);
    for (const auto& field : fields) {
        auto* value = new QLabel(field.second, &dialog);
        value->setObjectName(QStringLiteral("inventoryDetailValue"));
        value->setWordWrap(true);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(field.first + QStringLiteral(":"), value);
    }
    root->addLayout(form);
    root->addStretch(1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    if (payment) {
        auto* pay = buttons->addButton(QString::fromUtf8("Сплатити"), QDialogButtonBox::ActionRole);
        pay->setObjectName("inventoryDetailPay");
        pay->setEnabled(PurchasePaymentService::canPay("inventory"));
        QObject::connect(pay, &QPushButton::clicked, &dialog, [&dialog, payment] {
            if (payment(&dialog)) dialog.accept();
        });
    }
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    root->addWidget(buttons);
    dialog.exec();
}
