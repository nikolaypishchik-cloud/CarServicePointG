#pragma once

#include "core/services/StaffAccountService.h"

#include <QDialog>
#include <QVector>
class QAction;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

class StaffSessionDialog : public QDialog
{
    Q_OBJECT
public:
    explicit StaffSessionDialog(QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* event) override;

private slots:
    void updatePinMode();
    void signIn();

private:
    void loadAccounts();

    QVector<StaffAccount> m_accounts;
    QComboBox* m_account = nullptr;
    QLineEdit* m_pin = nullptr;
    QLineEdit* m_confirmPin = nullptr;
    QAction* m_pinVisibilityToggle = nullptr;
    QAction* m_confirmPinVisibilityToggle = nullptr;
    QLabel* m_pinHint = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_signIn = nullptr;
};
