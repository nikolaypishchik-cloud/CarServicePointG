#include "ui/tabs/settings/ResetPasswordDialog.h"
#include "ui/tabs/settings/PasswordManager.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

ResetPasswordDialog::ResetPasswordDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QString::fromUtf8("Скидання паролю"));
    setModal(true);
    setFixedWidth(380);
    setStyleSheet(QStringLiteral(
        "QDialog{background:#1e2733;}"
        "QLabel{color:#f7fbff;}"));

    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(28, 28, 28, 24);
    l->setSpacing(16);

    auto* title = new QLabel(QString::fromUtf8("Не вдалось ввести пароль"), this);
    title->setStyleSheet(QStringLiteral("font-size:14pt;font-weight:800;color:#f7fbff;"));
    l->addWidget(title);

    auto* hint = new QLabel(
        QString::fromUtf8("Вичерпано всі спроби введення паролю. "
                          "Натисніть кнопку нижче, щоб скинути пароль і увійти у застосунок."),
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("font-size:10pt;color:#c7d0db;"));
    l->addWidget(hint);

    auto* resetBtn = new QPushButton(QString::fromUtf8("Скинути пароль"), this);
    resetBtn->setFixedHeight(44);
    resetBtn->setCursor(Qt::PointingHandCursor);
    resetBtn->setStyleSheet(QStringLiteral(
        "QPushButton{background:transparent;border:1.5px solid #e53935;"
        "border-radius:10px;color:#e53935;font-weight:700;font-size:11pt;padding:10px 0;}"
        "QPushButton:hover{background:rgba(229,57,53,0.10);}"
        "QPushButton:pressed{background:rgba(229,57,53,0.20);}"));
    l->addWidget(resetBtn);

    connect(resetBtn, &QPushButton::clicked, this, [this]() {
        PasswordManager::resetPassword();
        accept();
    });
}
