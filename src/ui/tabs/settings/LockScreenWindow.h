#pragma once

#include <QDialog>
#include <QPointer>

class QLabel;
class QLineEdit;
class QPushButton;
class QProcess;

// Full-screen modal lock screen.
//
// Shown:
//   • on application startup when password protection is enabled
//   • by IdleLockMonitor when the auto-lock timeout elapses
//
// Behaviour:
//   • optionally launches a Windows screensaver (chosen in settings) before
//     showing the password card; the card appears as soon as the screensaver
//     exits (any user input ends the screensaver, just like in Windows)
//   • after MAX_ATTEMPTS wrong attempts a separate ResetPasswordDialog
//     appears with a single "Скинути пароль" button — resetting clears
//     the password and unlocks the application
//
// Returns QDialog::Accepted on successful unlock (correct password or reset).
class LockScreenWindow : public QDialog
{
    Q_OBJECT
public:
    static constexpr int MAX_ATTEMPTS = 3;

    explicit LockScreenWindow(QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onConfirm();

private:
    void buildUi();
    void launchScreensaverIfConfigured();
    void revealCard();

    int          m_failedAttempts    = 0;
    bool         m_screensaverDone   = false;
    QPointer<QProcess> m_screensaverProc;

    QLineEdit*   m_passwordEdit = nullptr;
    QPushButton* m_confirmBtn   = nullptr;
    QLabel*      m_status       = nullptr;
    QWidget*     m_card         = nullptr;
};
