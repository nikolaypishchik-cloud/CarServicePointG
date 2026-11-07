#include "ui/tabs/settings/PasswordDialog.h"
#include "ui/tabs/settings/PasswordManager.h"
#include "ui/theme/ThemeManager.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

bool dlgIsDark()
{
    return ThemeManager::currentTheme() == QStringLiteral("graphite");
}

QString dlgPrimary()   { return dlgIsDark() ? QStringLiteral("#f7fbff") : QStringLiteral("#111111"); }
QString dlgSecondary() { return dlgIsDark() ? QStringLiteral("#c7d0db") : QStringLiteral("#4b5563"); }
QString dlgBg()        { return dlgIsDark() ? QStringLiteral("#1e2733") : QStringLiteral("#ffffff"); }

QString inputStyle()
{
    return QStringLiteral(
        "QLineEdit{background:#ffffff;border:1px solid rgba(22,28,35,0.28);"
        "border-radius:8px;color:#111111;padding:8px 12px;font-size:11pt;"
        "selection-background-color:#b8d6ff;selection-color:#111111;"
        "placeholder-text-color:#5d6672;}"
        "QLineEdit:focus{border:1px solid #6b7fcc;}"
        "QLineEdit:disabled{background:#edf2f7;color:#2c333d;border:1px solid rgba(22,28,35,0.34);}");
}

QString primaryBtnStyle()
{
    return QStringLiteral(
        "QPushButton{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "stop:0 #4a7cdc,stop:1 #2d5cbf);border:none;border-radius:10px;"
        "color:#ffffff;font-weight:700;font-size:11pt;padding:10px 0;}"
        "QPushButton:hover{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "stop:0 #5a8ce6,stop:1 #3d6ccf);}"
        "QPushButton:pressed{background:#2d5cbf;}");
}

QString resetBtnStyle()
{
    return QStringLiteral(
        "QPushButton{background:transparent;border:1px solid #e53935;"
        "border-radius:10px;color:#e53935;font-weight:600;font-size:10pt;padding:8px 0;}"
        "QPushButton:hover{background:rgba(229,57,53,0.08);}");
}

} // namespace


PasswordDialog::PasswordDialog(QWidget* parent, bool showSetup)
    : QDialog(parent)
    , m_setupMode(showSetup)
{
    setWindowTitle(showSetup
        ? QString::fromUtf8("Встановити пароль")
        : QString::fromUtf8("Захист доступу"));
    setModal(true);
    setFixedWidth(360);
    setStyleSheet(QString("QDialog{background:%1;}").arg(dlgBg()));

    if (showSetup)
        buildSetupUi();
    else
        buildEntryUi();
}

void PasswordDialog::buildEntryUi()
{
    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(28, 28, 28, 24);
    l->setSpacing(14);

    auto* title = new QLabel(QString::fromUtf8("Введіть пароль"), this);
    title->setStyleSheet(QString("font-size:14pt;font-weight:800;color:%1;").arg(dlgPrimary()));
    l->addWidget(title);

    m_statusLabel = new QLabel(
        QString::fromUtf8("Застосунок захищено паролем."), this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet(QString("font-size:10pt;color:%1;").arg(dlgSecondary()));
    l->addWidget(m_statusLabel);

    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText(QString::fromUtf8("Пароль"));
    m_passwordEdit->setStyleSheet(inputStyle());
    l->addWidget(m_passwordEdit);

    m_confirmBtn = new QPushButton(QString::fromUtf8("Увійти"), this);
    m_confirmBtn->setFixedHeight(44);
    m_confirmBtn->setCursor(Qt::PointingHandCursor);
    m_confirmBtn->setStyleSheet(primaryBtnStyle());
    l->addWidget(m_confirmBtn);

    // Reset button — hidden until MAX_ATTEMPTS failures
    m_resetBtn = new QPushButton(QString::fromUtf8("Скинути пароль"), this);
    m_resetBtn->setFixedHeight(40);
    m_resetBtn->setCursor(Qt::PointingHandCursor);
    m_resetBtn->setStyleSheet(resetBtnStyle());
    m_resetBtn->hide();
    l->addWidget(m_resetBtn);

    connect(m_confirmBtn, &QPushButton::clicked, this, &PasswordDialog::onConfirm);
    connect(m_resetBtn,   &QPushButton::clicked, this, &PasswordDialog::onReset);
    connect(m_passwordEdit, &QLineEdit::returnPressed, this, &PasswordDialog::onConfirm);
}

void PasswordDialog::buildSetupUi()
{
    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(28, 28, 28, 24);
    l->setSpacing(14);

    auto* title = new QLabel(QString::fromUtf8("Новий пароль"), this);
    title->setStyleSheet(QString("font-size:14pt;font-weight:800;color:%1;").arg(dlgPrimary()));
    l->addWidget(title);

    m_statusLabel = new QLabel(
        QString::fromUtf8("Введіть пароль для захисту застосунку."), this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet(QString("font-size:10pt;color:%1;").arg(dlgSecondary()));
    l->addWidget(m_statusLabel);

    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText(QString::fromUtf8("Пароль"));
    m_passwordEdit->setStyleSheet(inputStyle());
    l->addWidget(m_passwordEdit);

    m_confirmEdit = new QLineEdit(this);
    m_confirmEdit->setEchoMode(QLineEdit::Password);
    m_confirmEdit->setPlaceholderText(QString::fromUtf8("Підтвердіть пароль"));
    m_confirmEdit->setStyleSheet(inputStyle());
    l->addWidget(m_confirmEdit);

    m_confirmBtn = new QPushButton(QString::fromUtf8("Зберегти пароль"), this);
    m_confirmBtn->setFixedHeight(44);
    m_confirmBtn->setCursor(Qt::PointingHandCursor);
    m_confirmBtn->setStyleSheet(primaryBtnStyle());
    l->addWidget(m_confirmBtn);

    connect(m_confirmBtn,  &QPushButton::clicked,      this, &PasswordDialog::onConfirm);
    connect(m_confirmEdit, &QLineEdit::returnPressed,  this, &PasswordDialog::onConfirm);
}

void PasswordDialog::onConfirm()
{
    if (m_setupMode) {
        // ── Setup: validate and save ─────────────────────────────────────────
        const QString pw  = m_passwordEdit->text();
        const QString pw2 = m_confirmEdit ? m_confirmEdit->text() : pw;

        if (pw.isEmpty()) {
            m_statusLabel->setStyleSheet(
                QString("font-size:10pt;color:#e53935;font-weight:600;"));
            m_statusLabel->setText(QString::fromUtf8("Пароль не може бути порожнім."));
            m_passwordEdit->setFocus();
            return;
        }
        if (pw != pw2) {
            m_statusLabel->setStyleSheet(
                QString("font-size:10pt;color:#e53935;font-weight:600;"));
            m_statusLabel->setText(QString::fromUtf8("Паролі не збігаються."));
            if (m_confirmEdit) m_confirmEdit->setFocus();
            return;
        }
        PasswordManager::setPassword(pw);
        PasswordManager::setPasswordEnabled(true);
        accept();
        return;
    }

    // ── Entry: verify ────────────────────────────────────────────────────────
    if (PasswordManager::verifyPassword(m_passwordEdit->text())) {
        accept();
        return;
    }

    ++m_failedAttempts;
    m_passwordEdit->clear();
    m_passwordEdit->setFocus();

    const int remaining = MAX_ATTEMPTS - m_failedAttempts;
    if (remaining > 0) {
        m_statusLabel->setStyleSheet(
            QString("font-size:10pt;color:#e53935;font-weight:600;"));
        m_statusLabel->setText(
            QString::fromUtf8("Невірний пароль. Залишилось спроб: %1.").arg(remaining));
    } else {
        // 3 fails — reveal reset button, no further questions
        m_statusLabel->setStyleSheet(
            QString("font-size:10pt;color:#e53935;font-weight:600;"));
        m_statusLabel->setText(
            QString::fromUtf8("Вичерпано всі спроби. Скиньте пароль нижче."));
        m_confirmBtn->setEnabled(false);
        if (m_resetBtn) m_resetBtn->show();
    }
}

void PasswordDialog::onReset()
{
    // No confirmation questions — reset immediately
    PasswordManager::resetPassword();
    accept();
}
