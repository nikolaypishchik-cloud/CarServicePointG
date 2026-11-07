#pragma once

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;

// Modal password-entry dialog.
//
// Entry mode  (default): verifies password; after MAX_ATTEMPTS wrong tries
//             the "Скинути пароль" button appears. Clicking it calls
//             PasswordManager::resetPassword() and accepts the dialog.
//
// Setup mode  (showSetup=true): lets the user define a new password.
//
class PasswordDialog : public QDialog
{
    Q_OBJECT
public:
    static constexpr int MAX_ATTEMPTS = 3;

    explicit PasswordDialog(QWidget* parent = nullptr, bool showSetup = false);

private slots:
    void onConfirm();
    void onReset();

private:
    void buildEntryUi();
    void buildSetupUi();
    void applyThemeStyle();

    bool        m_setupMode   = false;
    int         m_failedAttempts = 0;

    QLabel*      m_statusLabel   = nullptr;
    QLineEdit*   m_passwordEdit  = nullptr;
    QLineEdit*   m_confirmEdit   = nullptr;  // setup mode only
    QPushButton* m_confirmBtn    = nullptr;
    QPushButton* m_resetBtn      = nullptr;  // entry mode only; hidden until MAX_ATTEMPTS
};
