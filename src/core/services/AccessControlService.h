#pragma once

#include <QDateTime>
#include <QSet>
#include <QString>
#include <QVector>

class QSqlDatabase;

struct AccessPermissionDefinition
{
    QString key;
    QString category;
    QString title;
};

struct AccessRoleDefinition
{
    QString id;
    QString title;
    QString description;
    bool builtIn = false;
    bool active = true;
};

struct AccessAccountRow
{
    QString id;
    QString masterId;
    QString displayName;
    QString roleId;
    QString roleTitle;
    bool active = true;
    bool pinConfigured = false;
    QDateTime pinConfiguredAt;
    QDateTime lastLoginAt;
};

namespace AccessControlService {


struct OrderScope {
    bool restricted = false;
    QSet<QString> uids;
    QSet<int> legacyNumbers;
    bool contains(const QString& uid, int number) const {
        return !restricted || (uid.trimmed().isEmpty() ? legacyNumbers.contains(number) : uids.contains(uid.trimmed()));
    }
};
bool currentActorIsMaster();
QString currentScopeKey();
QString currentMasterId(QSqlDatabase database);
QString currentOrderPredicate(QSqlDatabase database, const QString& alias);
bool canAccessOrder(QSqlDatabase database, int internalNumber);
OrderScope masterOrderScope(QSqlDatabase database, const QString& accountId);
OrderScope currentOrderScope(QSqlDatabase database);
OrderScope currentOrderScope();

bool ensureSchema(QSqlDatabase database, QString* errorText = nullptr);

QVector<AccessPermissionDefinition> permissionDefinitions();
QVector<AccessRoleDefinition> roles(QSqlDatabase database, QString* errorText = nullptr);
QVector<AccessAccountRow> accounts(QSqlDatabase database, QString* errorText = nullptr);
QSet<QString> grantedPermissions(QSqlDatabase database, const QString& roleId, QString* errorText = nullptr);

bool setRolePermissions(QSqlDatabase database, const QString& roleId, const QSet<QString>& permissionKeys, QString* errorText = nullptr);
bool createRole(QSqlDatabase database, const QString& title, const QString& description, QString* roleId = nullptr, QString* errorText = nullptr);
bool deleteCustomRole(QSqlDatabase database, const QString& roleId, QString* errorText = nullptr);
bool assignRole(QSqlDatabase database, const QString& accountId, const QString& roleId, QString* errorText = nullptr);
bool saveAccount(QSqlDatabase database, const QString& accountId, const QString& name, const QString& roleId, QString* errorText = nullptr);
bool deactivateAccount(QSqlDatabase database, const QString& accountId, QString* errorText = nullptr);
bool resetAccountPin(QSqlDatabase database, const QString& accountId, QString* errorText = nullptr);

bool hasPermission(QSqlDatabase database, const QString& roleId, const QString& permissionKey);
bool currentUserHasPermission(QSqlDatabase database, const QString& permissionKey);

// Application entry points select the connection; UI never owns a transaction.
QString currentMasterId();
bool canAccessOrder(int internalNumber);
bool currentUserHasPermission(const QString& permissionKey);
QVector<AccessRoleDefinition> roles(QString* errorText = nullptr);
QVector<AccessAccountRow> accounts(QString* errorText = nullptr);
QSet<QString> grantedPermissions(const QString& roleId, QString* errorText = nullptr);
bool saveAccount(const QString& accountId, const QString& name, const QString& roleId, QString* errorText);
bool deactivateAccount(const QString& accountId, QString* errorText);
bool resetAccountPin(const QString& accountId, QString* errorText);
bool createRole(const QString& title, const QString& description, QString* roleId, QString* errorText);
bool deleteCustomRole(const QString& roleId, QString* errorText);
bool setRolePermissions(const QString& roleId, const QSet<QString>& permissions, QString* errorText);
struct AccountEdit {
    AccessAccountRow expected;
    QString name, roleId;
};
bool saveAccounts(const QVector<AccountEdit>& edits, bool* changed, QString* errorText);

} // namespace AccessControlService
