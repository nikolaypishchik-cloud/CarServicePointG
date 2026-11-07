#include "ui/tabs/settings/LockScreenWindow.h"
#include "ui/tabs/settings/PasswordManager.h"
#include "ui/tabs/settings/ResetPasswordDialog.h"

#include <QApplication>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QString primaryButtonStyle()
{
    return QStringLiteral(
        "QPushButton{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "stop:0 #4a7cdc,stop:1 #2d5cbf);border:none;border-radius:12px;"
        "color:#ffffff;font-weight:700;font-size:12pt;padding:14px 0;}"
        "QPushButton:hover{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "stop:0 #5a8ce6,stop:1 #3d6ccf);}"
        "QPushButton:pressed{background:#2d5cbf;}"
        "QPushButton:disabled{background:#3a4456;color:#8090a8;}");
}

QString inputStyle()
{
    return QStringLiteral(
        "QLineEdit{background:#ffffff;border:1px solid rgba(22,28,35,0.28);"
        "border-radius:10px;color:#111111;padding:12px 14px;font-size:12pt;"
        "selection-background-color:#b8d6ff;selection-color:#111111;"
        "placeholder-text-color:#5d6672;}"
        "QLineEdit:focus{border:1.5px solid #6b7fcc;}"
        "QLineEdit:disabled{background:#edf2f7;color:#2c333d;border:1px solid rgba(22,28,35,0.34);}");
}

} // namespace


LockScreenWindow::LockScreenWindow(QWidget* parent)
    : QDialog(parent)
{
    // Frameless full-screen modal — user cannot bypass by closing
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setModal(true);
    setWindowTitle(QString::fromUtf8("Захист доступу"));

    if (auto* sc = QGuiApplication::primaryScreen()) {
        setGeometry(sc->geometry());
    }

    setStyleSheet(QStringLiteral(
        "QDialog{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "stop:0 #1e2733, stop:1 #0f1419);}"
        "QWidget#lockCard{background:rgba(30,39,51,0.96);border-radius:18px;"
        "border:1px solid rgba(255,255,255,0.10);}"));

    buildUi();
}

void LockScreenWindow::buildUi()
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addStretch(1);

    auto* hWrap = new QHBoxLayout();
    hWrap->setContentsMargins(0, 0, 0, 0);
    hWrap->setSpacing(0);
    hWrap->addStretch(1);

    // ── Centered card ────────────────────────────────────────────────────────
    m_card = new QWidget(this);
    m_card->setObjectName(QStringLiteral("lockCard"));
    m_card->setFixedWidth(420);

    auto* cardLayout = new QVBoxLayout(m_card);
    cardLayout->setContentsMargins(36, 36, 36, 32);
    cardLayout->setSpacing(14);

    auto* logo = new QLabel(m_card);
    logo->setAlignment(Qt::AlignCenter);
    QPixmap logoPx(QStringLiteral(":/assets/app_icon_2_1.png"));
    if (!logoPx.isNull()) {
        logo->setPixmap(logoPx.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    cardLayout->addWidget(logo);

    auto* title = new QLabel(QString::fromUtf8("Захист доступу"), m_card);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(QStringLiteral("font-size:18pt;font-weight:800;color:#f7fbff;"));
    cardLayout->addWidget(title);

    auto* subtitle = new QLabel(QString::fromUtf8("Введіть пароль для входу у CarService PointG 2.1.2"), m_card);
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setWordWrap(true);
    subtitle->setStyleSheet(QStringLiteral("font-size:10.5pt;color:#c7d0db;"));
    cardLayout->addWidget(subtitle);

    cardLayout->addSpacing(8);

    m_passwordEdit = new QLineEdit(m_card);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText(QString::fromUtf8("Пароль"));
    m_passwordEdit->setStyleSheet(inputStyle());
    cardLayout->addWidget(m_passwordEdit);

    m_status = new QLabel(QString(), m_card);
    m_status->setAlignment(Qt::AlignCenter);
    m_status->setWordWrap(true);
    m_status->setStyleSheet(QStringLiteral("font-size:10pt;color:#e53935;font-weight:600;"));
    m_status->setMinimumHeight(20);
    cardLayout->addWidget(m_status);

    m_confirmBtn = new QPushButton(QString::fromUtf8("Увійти"), m_card);
    m_confirmBtn->setFixedHeight(48);
    m_confirmBtn->setCursor(Qt::PointingHandCursor);
    m_confirmBtn->setStyleSheet(primaryButtonStyle());
    cardLayout->addWidget(m_confirmBtn);

    hWrap->addWidget(m_card, 0, Qt::AlignCenter);
    hWrap->addStretch(1);

    outer->addLayout(hWrap);
    outer->addStretch(1);

    connect(m_confirmBtn,   &QPushButton::clicked,     this, &LockScreenWindow::onConfirm);
    connect(m_passwordEdit, &QLineEdit::returnPressed, this, &LockScreenWindow::onConfirm);
}

void LockScreenWindow::showEvent(QShowEvent* e)
{
    QDialog::showEvent(e);
    if (m_screensaverDone) return;
    m_screensaverDone = true;
    // Defer screensaver launch so the lock window paints first; otherwise the
    // screensaver may start before our background is visible, briefly showing
    // the desktop or main window underneath.
    QTimer::singleShot(50, this, [this]() { launchScreensaverIfConfigured(); });
}

void LockScreenWindow::launchScreensaverIfConfigured()
{
    const QString id   = PasswordManager::lockScreenStyle();
    const QString path = PasswordManager::screensaverPath(id);

    if (path.isEmpty()) {
        // No screensaver configured — show the password card immediately
        revealCard();
        return;
    }

    // Hide the card while the screensaver runs in fullscreen
    m_card->setVisible(false);

    auto* proc = new QProcess(this);
    m_screensaverProc = proc;
    auto onDone = [this, proc]() {
        if (proc) proc->deleteLater();
        revealCard();
    };
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [onDone](int, QProcess::ExitStatus) { onDone(); });
    connect(proc, &QProcess::errorOccurred, this,
            [onDone](QProcess::ProcessError) { onDone(); });

    proc->start(path, QStringList{QStringLiteral("/s")});
    if (!proc->waitForStarted(1500)) {
        // Failed to launch — fall back to password prompt
        proc->kill();
        proc->deleteLater();
        revealCard();
    }
}

void LockScreenWindow::revealCard()
{
    if (m_card) m_card->setVisible(true);
    if (m_passwordEdit) {
        m_passwordEdit->clear();
        m_passwordEdit->setFocus();
    }
    raise();
    activateWindow();
}

void LockScreenWindow::onConfirm()
{
    if (PasswordManager::verifyPassword(m_passwordEdit->text())) {
        accept();
        return;
    }

    ++m_failedAttempts;
    m_passwordEdit->clear();
    m_passwordEdit->setFocus();

    const int remaining = MAX_ATTEMPTS - m_failedAttempts;
    if (remaining > 0) {
        m_status->setText(
            QString::fromUtf8("Невірний пароль. Залишилось спроб: %1.").arg(remaining));
        return;
    }

    // 3 fails — show separate reset dialog with no further questions
    m_status->setText(QString::fromUtf8("Вичерпано всі спроби."));
    m_confirmBtn->setEnabled(false);
    m_passwordEdit->setEnabled(false);

    ResetPasswordDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        // Password has been reset; unlock the application
        accept();
    }
}

void LockScreenWindow::keyPressEvent(QKeyEvent* event)
{
    // Block Escape — user must enter password or reset it
    if (event->key() == Qt::Key_Escape) {
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

void LockScreenWindow::closeEvent(QCloseEvent* event)
{
    // Prevent closing the lock screen by [X] / Alt+F4
    event->ignore();
}
