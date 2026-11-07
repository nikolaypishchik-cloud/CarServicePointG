#pragma once

#include <QWidget>
#include <QVector>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTreeWidget;
struct AccessAccountRow;
struct AccessRoleDefinition;

class AccessControlPage : public QWidget
{
public:
    explicit AccessControlPage(QWidget* parent = nullptr);

private:
    void buildUi();
    void reloadAll(const QString& preferredRoleId = {});
    void reloadAccounts();
    void reloadRoles(const QString& preferredRoleId = {});
    void loadRolePermissions();
    void saveAccountRoles();
    void resetAccountPin(int row);
    void createRole();
    void deleteSelectedRole();
    void saveRolePermissions();
    void setStatus(const QString& text, bool error = false);

    QVector<AccessAccountRow> m_accounts;
    QVector<AccessRoleDefinition> m_roles;
    QTableWidget* m_accountsTable = nullptr;
    QComboBox* m_roleSelector = nullptr;
    QTreeWidget* m_permissionsTree = nullptr;
    QLineEdit* m_newRoleName = nullptr;
    QLineEdit* m_newRoleDescription = nullptr;
    QLabel* m_roleDescription = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_saveAccountsButton = nullptr;
    QPushButton* m_savePermissionsButton = nullptr;
    QPushButton* m_deleteRoleButton = nullptr;
    bool m_canManage = false;
};