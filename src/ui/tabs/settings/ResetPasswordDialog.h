#pragma once

#include <QDialog>

// Simple modal that appears after 3 wrong password attempts on the lock screen.
// Contains a single "Скинути пароль" button — clicking it calls
// PasswordManager::resetPassword() and accepts the dialog.
class ResetPasswordDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ResetPasswordDialog(QWidget* parent = nullptr);
};
