#include "ui/tabs/settings/StaffSessionDialog.h"
#include "ui/tabs/settings/PasswordVisibilityIcon.h"

#include "core/services/StaffAccountService.h"

#include <QAction>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>
#include <QShowEvent>
#include <QTimer>
#include <QScreen>

namespace {

QString inputStyle()
{
    return QStringLiteral(
        "QLineEdit,QComboBox{background:#ffffff;color:#151b22;border:1px solid #b8c5d1;border-radius:5px;padding:7px 9px;font-size:11pt;}"
        "QLineEdit:focus,QComboBox:focus{border:1px solid #4b77a6;}");
}

} // namespace

QIcon passwordVisibilityIcon(bool visible)
{
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor color(QStringLiteral("#17324a"));
    painter.setPen(QPen(color, 1.7));
    painter.setBrush(Qt::NoBrush);

    QPainterPath eye;
    eye.moveTo(2.0, 10.0);
    eye.cubicTo(5.0, 5.5, 7.3, 4.0, 10.0, 4.0);
    eye.cubicTo(12.7, 4.0, 15.0, 5.5, 18.0, 10.0);
    eye.cubicTo(15.0, 14.5, 12.7, 16.0, 10.0, 16.0);
    eye.cubicTo(7.3, 16.0, 5.0, 14.5, 2.0, 10.0);
    painter.drawPath(eye);
    painter.setBrush(color);
    painter.drawEllipse(QPointF(10.0, 10.0), 2.1, 2.1);

    if (visible) {
        painter.setPen(QPen(color, 2.0));
        painter.drawLine(QPointF(3.2, 3.2), QPointF(16.8, 16.8));
    }
    return QIcon(pixmap);
}

namespace {

void setPasswordVisibility(QLineEdit* field, QAction* action, bool visible)
{
    if (!field || !action) return;
    field->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password);
    action->setIcon(passwordVisibilityIcon(visible));
    action->setToolTip(visible
        ? QString::fromUtf8("Приховати символи PIN")
        : QString::fromUtf8("Показати символи PIN"));
}

QAction* addPasswordVisibilityToggle(QLineEdit* field)
{
    auto* action = field->addAction(passwordVisibilityIcon(false), QLineEdit::TrailingPosition);
    action->setCheckable(true);
    action->setToolTip(QString::fromUtf8("Показати символи PIN"));
    QObject::connect(action, &QAction::toggled, field, [field, action](bool visible) {
        setPasswordVisibility(field, action, visible);
    });
    return action;
}

} // namespace

StaffSessionDialog::StaffSessionDialog(QWidget* parent)
    : QDialog(parent)
{
    // The sign-in gate is an independent top-level window so Windows keeps
    // CarServicePointG visible on the taskbar before the main shell is shown.
    setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowMinimizeButtonHint | Qt::WindowCloseButtonHint);
    setWindowTitle(QString::fromUtf8("Вхід працівника"));
    setWindowIcon(QIcon(QStringLiteral(":/assets/app_icon.png")));
    setModal(true);
    setMinimumWidth(430);
    setStyleSheet(QStringLiteral(
        "QDialog{background:#263442;} QLabel{color:#f4f7fa;}"
        "QFrame#staffLoginForm{background:#263442;border:none;}"
        "QPushButton{background:#1f405d;color:#ffffff;border:1px solid #aac0d2;border-radius:4px;padding:9px 16px;font-weight:700;}"
        "QPushButton:hover{background:#2d587a;}"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 22, 24, 22);
    root->setSpacing(12);
    root->setAlignment(Qt::AlignTop);

    auto* title = new QLabel(QString::fromUtf8("Персональний вхід"), this);
    title->setStyleSheet(QStringLiteral("font-size:18pt;font-weight:800;"));
    root->addWidget(title);

    auto* subtitle = new QLabel(QString::fromUtf8("Оберіть свій обліковий запис. Подальші дії будуть зафіксовані в журналі СТО."), this);
    subtitle->setWordWrap(true);
    subtitle->setStyleSheet(QStringLiteral("color:#d5e0ea;font-size:10.5pt;"));
    root->addWidget(subtitle);

    auto* form = new QFrame(this);
    form->setObjectName(QStringLiteral("staffLoginForm"));
    form->setAttribute(Qt::WA_StyledBackground, true);
    auto* formLayout = new QFormLayout(form);
    formLayout->setContentsMargins(0, 8, 0, 4);
    formLayout->setSpacing(8);
    m_account = new QComboBox(form);
    m_account->setStyleSheet(inputStyle());
    m_pin = new QLineEdit(form);
    m_pin->setEchoMode(QLineEdit::Password);
    m_pin->setPlaceholderText(QString::fromUtf8("Персональний PIN"));
    m_pin->setStyleSheet(inputStyle());
    m_pinVisibilityToggle = addPasswordVisibilityToggle(m_pin);
    m_confirmPin = new QLineEdit(form);
    m_confirmPin->setEchoMode(QLineEdit::Password);
    m_confirmPin->setPlaceholderText(QString::fromUtf8("Повторіть PIN"));
    m_confirmPin->setStyleSheet(inputStyle());
    m_confirmPinVisibilityToggle = addPasswordVisibilityToggle(m_confirmPin);
    formLayout->addRow(QString::fromUtf8("Працівник:"), m_account);
    formLayout->addRow(QString::fromUtf8("PIN:"), m_pin);
    formLayout->addRow(QString::fromUtf8("Підтвердження:"), m_confirmPin);
    root->addWidget(form);

    m_pinHint = new QLabel(this);
    m_pinHint->setWordWrap(true);
    m_pinHint->setStyleSheet(QStringLiteral("color:#b9cfdf;font-size:9.5pt;"));
    auto* loginFooter = new QVBoxLayout;
    loginFooter->setSpacing(qRound(screen()->logicalDotsPerInchY()*3.0/25.4));
    loginFooter->addWidget(m_pinHint);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->hide();
    m_status->setStyleSheet(QStringLiteral("color:#ffb4ae;font-weight:600;"));
    loginFooter->addWidget(m_status);

    auto* actions = new QHBoxLayout();
    actions->addStretch(1);
    m_signIn = new QPushButton(QString::fromUtf8("Увійти"), this);
    m_signIn->setDefault(true);
    actions->addWidget(m_signIn);
    loginFooter->addLayout(actions);
    root->addLayout(loginFooter);

    connect(m_account, qOverload<int>(&QComboBox::currentIndexChanged), this, &StaffSessionDialog::updatePinMode);
    connect(m_account, qOverload<int>(&QComboBox::activated), this, [this](int) {
        // Let the combo finish restoring focus after its keyboard/popup event.
        QTimer::singleShot(0, this, [this] {
            if (isVisible() && m_signIn->isEnabled())
                m_pin->setFocus(Qt::OtherFocusReason);
        });
    });
    connect(m_signIn, &QPushButton::clicked, this, &StaffSessionDialog::signIn);
    connect(m_pin, &QLineEdit::returnPressed, this, &StaffSessionDialog::signIn);
    connect(m_confirmPin, &QLineEdit::returnPressed, this, &StaffSessionDialog::signIn);

    loadAccounts();
}

void StaffSessionDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    QTimer::singleShot(0, this, [this] {
        if (isVisible() && m_signIn->isEnabled()) m_pin->setFocus(Qt::OtherFocusReason);
    });
}

void StaffSessionDialog::loadAccounts()
{
    QString errorText;
    m_accounts = StaffAccountService::accounts( &errorText);
    if (!errorText.trimmed().isEmpty()) {
        m_status->setText(errorText);
        m_status->show();
        m_signIn->setEnabled(false);
        return;
    }
    for (const StaffAccount& account : m_accounts) {
        m_account->addItem(QStringLiteral("%1 (%2)").arg(account.displayName, account.role), account.id);
    }
    m_signIn->setEnabled(!m_accounts.isEmpty());
    updatePinMode();
}

void StaffSessionDialog::updatePinMode()
{
    const int index = m_account->currentIndex();
    const bool hasPin = index >= 0 && index < m_accounts.size() && m_accounts.at(index).pinConfigured;
    m_confirmPin->setVisible(!hasPin);
    m_confirmPin->setEnabled(!hasPin);
    m_pinVisibilityToggle->setChecked(false);
    m_confirmPinVisibilityToggle->setChecked(false);
    setPasswordVisibility(m_pin, m_pinVisibilityToggle, false);
    setPasswordVisibility(m_confirmPin, m_confirmPinVisibilityToggle, false);
    m_pin->clear();
    m_confirmPin->clear();
    m_status->clear();
    m_status->hide();
    m_pinHint->setText(hasPin
        ? QString::fromUtf8("Введіть персональний PIN для продовження.")
        : QString::fromUtf8("Перший вхід для цього облікового запису: встановіть PIN щонайменше з 4 символів."));
}

void StaffSessionDialog::signIn()
{
    const int index = m_account->currentIndex();
    if (index < 0 || index >= m_accounts.size()) return;
    const StaffAccount account = m_accounts.at(index);
    const QString pin = m_pin->text();
    if (!account.pinConfigured) {
        if (pin != m_confirmPin->text()) {
            m_status->setText(QString::fromUtf8("PIN і підтвердження не збігаються."));
            m_status->show();
            return;
        }
        QString setupError;
        if (!StaffAccountService::setPin( account.id, pin, &setupError)) {
            m_status->setText(setupError);
            m_status->show();
            return;
        }
    }

    QString errorText;
    if (!StaffAccountService::signIn( account.id, pin, nullptr, &errorText)) {
        m_status->setText(errorText);
        m_status->show();
        m_pin->clear();
        m_confirmPin->clear();
        m_pin->setFocus();
        return;
    }
    accept();
}
