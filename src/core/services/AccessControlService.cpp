#include "core/util/DateFormats.h"
#include "core/services/AccessControlService.h"
#include "core/services/ActivityAuditService.h"
#include "infrastructure/db/sqlite/Db.h"

#include "core/services/ActivityAuditService.h"

#include <QHash>
#include <QCryptographicHash>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QScopeGuard>

namespace {

void setError(QString* errorText, const QString& text)
{
    if (errorText) *errorText = text;
}

QDateTime parseDatabaseDateTime(const QString& value)
{
    QDateTime parsed = DateFormats::parseDateTime(value);
    if (parsed.isValid()) return parsed;
    return QDateTime::fromString(value, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

const QVector<AccessPermissionDefinition>& definitions()
{
    static const QVector<AccessPermissionDefinition> values = {
        {QStringLiteral("nav.planning"), QString::fromUtf8("Розділи"), QString::fromUtf8("Планування")},
        {QStringLiteral("nav.intake"), QString::fromUtf8("Розділи"), QString::fromUtf8("Приймання авто")},
        {QStringLiteral("nav.new_order"), QString::fromUtf8("Розділи"), QString::fromUtf8("Створення нового наряду")},
        {QStringLiteral("nav.current_orders"), QString::fromUtf8("Розділи"), QString::fromUtf8("Поточні наряди")},
        {QStringLiteral("nav.parts"), QString::fromUtf8("Розділи"), QString::fromUtf8("Замовлення запчастин")},
        {QStringLiteral("nav.history"), QString::fromUtf8("Розділи"), QString::fromUtf8("База виконаних нарядів")},
        {QStringLiteral("nav.reports"), QString::fromUtf8("Розділи"), QString::fromUtf8("Звіти")},
        {QStringLiteral("nav.clients"), QString::fromUtf8("Розділи"), QString::fromUtf8("Клієнти")},
        {QStringLiteral("nav.inventory"), QString::fromUtf8("Розділи"), QString::fromUtf8("Склад СТО")},
        {QStringLiteral("nav.catalog"), QString::fromUtf8("Розділи"), QString::fromUtf8("Каталог")},
        {QStringLiteral("nav.statistics"), QString::fromUtf8("Розділи"), QString::fromUtf8("Статистика")},
        {QStringLiteral("nav.settings"), QString::fromUtf8("Розділи"), QString::fromUtf8("Налаштування")},
        {QStringLiteral("orders.create"), QString::fromUtf8("Операції"), QString::fromUtf8("Створювати наряди")},
        {QStringLiteral("orders.edit"), QString::fromUtf8("Операції"), QString::fromUtf8("Редагувати наряди")},
        {QStringLiteral("orders.close"), QString::fromUtf8("Операції"), QString::fromUtf8("Закривати наряди")},
        {QStringLiteral("parts.order"), QString::fromUtf8("Операції"), QString::fromUtf8("Замовляти запчастини")},
        {QStringLiteral("parts.install"), QString::fromUtf8("Операції"), QString::fromUtf8("Фіксувати встановлення запчастин")},
        {QStringLiteral("inventory.purchase"), QString::fromUtf8("Операції"), QString::fromUtf8("Додавати придбання матеріалів")},
        {QStringLiteral("inventory.delete"), QString::fromUtf8("Операції"), QString::fromUtf8("Видаляти придбання матеріалів")},
        {QStringLiteral("access.manage"), QString::fromUtf8("Безпека"), QString::fromUtf8("Керувати користувачами, ролями та правами")},
        {QStringLiteral("security.manage"), QString::fromUtf8("Безпека"), QString::fromUtf8("Змінювати налаштування безпеки")},
        {QStringLiteral("mobile.pair"), QString::fromUtf8("Синхронізація"), QString::fromUtf8("Прив'язувати свій Android-телефон")}
    };
    return values;
}

const QVector<AccessRoleDefinition>& defaultRoles()
{
    static const QVector<AccessRoleDefinition> values = {
        {QStringLiteral("owner"), QString::fromUtf8("Власник"), QString::fromUtf8("Повний контроль СТО, ролей і політик доступу."), true, true},
        {QStringLiteral("administrator"), QString::fromUtf8("Адміністратор"), QString::fromUtf8("Операційне адміністрування без зміни політик доступу."), true, true},
        {QStringLiteral("manager"), QString::fromUtf8("Менеджер"), QString::fromUtf8("Приймання, наряди, клієнти, запчастини та звіти."), true, true},
        {QStringLiteral("master"), QString::fromUtf8("Майстер"), QString::fromUtf8("Виконання робіт і робота з призначеними нарядами."), true, true},
        {QStringLiteral("parts_specialist"), QString::fromUtf8("Комірник / запчастини"), QString::fromUtf8("Замовлення, надходження та складський облік."), true, true},
        {QStringLiteral("viewer"), QString::fromUtf8("Перегляд"), QString::fromUtf8("Лише перегляд історії, клієнтів і звітів."), true, true}
    };
    return values;
}

QSet<QString> permissions(const std::initializer_list<const char*>& values)
{
    QSet<QString> result;
    for (const char* value : values) result.insert(QString::fromLatin1(value));
    return result;
}

QSet<QString> defaultGrants(const QString& roleId)
{
    if (roleId == QStringLiteral("owner")) {
        QSet<QString> all;
        for (const auto& permission : definitions()) all.insert(permission.key);
        return all;
    }
    if (roleId == QStringLiteral("administrator")) {
        return permissions({"nav.planning", "nav.intake", "nav.new_order", "nav.current_orders", "nav.parts", "nav.history",
                            "nav.reports", "nav.clients", "nav.inventory", "nav.catalog", "nav.statistics", "nav.settings",
                            "orders.create", "orders.edit", "orders.close", "parts.order", "parts.install",
                            "inventory.purchase", "inventory.delete", "security.manage"});
    }
    if (roleId == QStringLiteral("manager")) {
        return permissions({"nav.planning", "nav.intake", "nav.new_order", "nav.current_orders", "nav.parts", "nav.history",
                            "nav.reports", "nav.clients", "nav.statistics", "orders.create", "orders.edit", "orders.close",
                            "parts.order", "parts.install"});
    }
    if (roleId == QStringLiteral("master")) {
        return permissions({"nav.planning", "nav.current_orders", "nav.parts", "nav.history",
                            "orders.create", "orders.edit", "parts.install", "mobile.pair"});
    }
    if (roleId == QStringLiteral("parts_specialist")) {
        return permissions({"nav.current_orders", "nav.parts", "nav.history", "nav.inventory", "nav.catalog",
                            "parts.order", "parts.install", "inventory.purchase"});
    }
    if (roleId == QStringLiteral("viewer")) {
        return permissions({"nav.history", "nav.reports", "nav.clients"});
    }
    return {};
}

bool roleExists(QSqlDatabase database, const QString& roleId)
{
    QSqlQuery query(database);
    query.prepare(QStringLiteral("SELECT 1 FROM access_roles WHERE id=? AND active=1 LIMIT 1"));
    query.addBindValue(roleId.trimmed());
    return query.exec() && query.next();
}

bool canManage(QSqlDatabase database)
{
    return AccessControlService::currentUserHasPermission(database, QStringLiteral("access.manage"));
}

bool actorMatchesActiveAccount(QSqlDatabase database)
{
    const auto actor = ActivityAuditService::currentActor();
    if (!actor.isValid() || !database.isValid() || !database.isOpen()) return false;
    QSqlQuery query(database);
    query.prepare(QStringLiteral("SELECT a.role FROM staff_accounts a JOIN access_roles r ON r.id=a.role "
                                 "WHERE a.id=? AND a.active=1 AND r.active=1"));
    query.addBindValue(actor.accountId);
    return query.exec() && query.next() && query.value(0).toString() == actor.role;
}

bool audit(QSqlDatabase database, const QString& action, const QString& title, const QString& entityId, const QString& subject)
{
    QString ignored;
    return ActivityAuditService::record(database,
                                        QStringLiteral("access"),
                                        action,
                                        title,
                                        0,
                                        QStringLiteral("staff_account"),
                                        entityId,
                                        subject,
                                        QStringLiteral("{}"),
                                        &ignored);
}

} // namespace

namespace AccessControlService {

bool currentActorIsMaster()
{
    return ActivityAuditService::currentActor().role.trimmed().compare(QStringLiteral("master"), Qt::CaseInsensitive) == 0;
}

QString currentScopeKey()
{
    const auto actor = ActivityAuditService::currentActor();
    if (!actor.isValid()) return QStringLiteral("anonymous");
    QSqlQuery query(Database::db());
    query.prepare(QStringLiteral("SELECT a.role,a.master_id FROM staff_accounts a JOIN access_roles r ON r.id=a.role "
                                 "WHERE a.id=? AND a.active=1 AND r.active=1"));
    query.addBindValue(actor.accountId);
    const QString accountScope = query.exec() && query.next()
        ? query.value(0).toString() + QLatin1Char('|') + query.value(1).toString()
        : QStringLiteral("revoked");
    QByteArray grants;
    QSqlQuery permissionsQuery(Database::db());
    permissionsQuery.prepare(QStringLiteral("SELECT permission_key,granted FROM access_role_permissions WHERE role_id=? ORDER BY permission_key"));
    permissionsQuery.addBindValue(actor.role);
    if (permissionsQuery.exec()) {
        while (permissionsQuery.next()) {
            grants += permissionsQuery.value(0).toString().toUtf8() + ':'
                + permissionsQuery.value(1).toString().toUtf8() + '\n';
        }
    } else {
        grants = "unavailable";
    }
    return actor.accountId + QLatin1Char('|') + actor.role + QLatin1Char('|') + actor.sessionId
        + QLatin1Char('|') + accountScope + QLatin1Char('|')
        + QString::fromLatin1(QCryptographicHash::hash(grants, QCryptographicHash::Sha256).toHex());
}

QString currentMasterId(QSqlDatabase database)
{
    if (!actorMatchesActiveAccount(database)) return {};
    QSqlQuery q(database);
    q.prepare(QStringLiteral("SELECT master_id FROM staff_accounts WHERE id=? AND active=1"));
    q.addBindValue(ActivityAuditService::currentActor().accountId);
    return q.exec() && q.next() ? q.value(0).toString().trimmed() : QString();
}

QString currentOrderPredicate(QSqlDatabase database, const QString& alias)
{
    if (!actorMatchesActiveAccount(database)) return QStringLiteral("0=1");
    if (!currentActorIsMaster()) return QStringLiteral("1=1");
    QString master = currentMasterId(database);
    if (master.isEmpty()) return QStringLiteral("0=1");
    master.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QStringLiteral("%1.master_id='%2'").arg(alias, master);
}

bool canAccessOrder(QSqlDatabase database, int internalNumber)
{
    if (!actorMatchesActiveAccount(database)) return false;
    if (!currentActorIsMaster()) return true;
    const QString source = database.tables(QSql::AllTables).contains(QStringLiteral("router_work_orders"))
        ? QStringLiteral("router_work_orders") : QStringLiteral("work_orders");
    QSqlQuery q(database);
    q.prepare(QStringLiteral("SELECT 1 FROM %1 o WHERE o.number=? AND (%2)")
                  .arg(source, currentOrderPredicate(database, QStringLiteral("o"))));
    q.addBindValue(internalNumber);
    return q.exec() && q.next();
}

OrderScope masterOrderScope(QSqlDatabase database, const QString& accountId)
{
    OrderScope scope;
    scope.restricted = true;
    QSqlQuery account(database);
    account.prepare(QStringLiteral("SELECT master_id FROM staff_accounts WHERE id=? AND active=1"));
    account.addBindValue(accountId.trimmed());
    if (!account.exec() || !account.next()) return scope;
    const QString masterId = account.value(0).toString().trimmed();
    if (masterId.isEmpty()) return scope;
    QSqlQuery orders(database);
    const QString source = database.tables(QSql::AllTables).contains(QStringLiteral("router_work_orders"))
        ? QStringLiteral("router_work_orders") : QStringLiteral("work_orders");
    orders.prepare(QStringLiteral("SELECT order_uid,number FROM %1 WHERE master_id=?").arg(source));
    orders.addBindValue(masterId);
    if (!orders.exec()) return scope;
    while (orders.next()) {
        const QString uid = orders.value(0).toString().trimmed();
        if (!uid.isEmpty()) scope.uids.insert(uid);
        scope.legacyNumbers.insert(orders.value(1).toInt());
    }
    return scope;
}

OrderScope currentOrderScope(QSqlDatabase database)
{
    if (!actorMatchesActiveAccount(database)) {
        OrderScope denied;
        denied.restricted = true;
        return denied;
    }
    return currentActorIsMaster() ? masterOrderScope(database, ActivityAuditService::currentActor().accountId) : OrderScope{};
}

bool ensureSchema(QSqlDatabase database, QString* errorText)
{
    if (!database.isValid()) {
        setError(errorText, QString::fromUtf8("Немає з'єднання з базою даних прав доступу."));
        return false;
    }
    if (!database.isOpen() && !database.open()) {
        setError(errorText, database.lastError().text());
        return false;
    }

    QSqlQuery query(database);
    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS access_roles ("
            "id TEXT PRIMARY KEY,"
            "title TEXT NOT NULL,"
            "description TEXT NOT NULL DEFAULT '',"
            "built_in INTEGER NOT NULL DEFAULT 0,"
            "active INTEGER NOT NULL DEFAULT 1,"
            "created_at TEXT NOT NULL DEFAULT (datetime('now'))"
            ")"))
        || !query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS access_role_permissions ("
            "role_id TEXT NOT NULL,"
            "permission_key TEXT NOT NULL,"
            "granted INTEGER NOT NULL DEFAULT 1,"
            "PRIMARY KEY(role_id, permission_key),"
            "FOREIGN KEY(role_id) REFERENCES access_roles(id) ON DELETE CASCADE"
            ")"))
        || !query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS access_role_permission_profiles ("
            "role_id TEXT PRIMARY KEY,"
            "initialized_at TEXT NOT NULL DEFAULT (datetime('now')),"
            "FOREIGN KEY(role_id) REFERENCES access_roles(id) ON DELETE CASCADE"
            ")"))
        || !query.exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_access_role_permissions_role "
            "ON access_role_permissions(role_id, granted)"))) {
        setError(errorText, query.lastError().text());
        return false;
    }

    for (const AccessRoleDefinition& role : defaultRoles()) {
        QSqlQuery insertRole(database);
        insertRole.prepare(QStringLiteral(
            "INSERT OR IGNORE INTO access_roles(id,title,description,built_in,active) VALUES(?,?,?,?,1)"));
        insertRole.addBindValue(role.id);
        insertRole.addBindValue(role.title);
        insertRole.addBindValue(role.description);
        insertRole.addBindValue(role.builtIn ? 1 : 0);
        if (!insertRole.exec()) {
            setError(errorText, insertRole.lastError().text());
            return false;
        }

        QSqlQuery profileCheck(database);
        profileCheck.prepare(QStringLiteral("SELECT 1 FROM access_role_permission_profiles WHERE role_id=?"));
        profileCheck.addBindValue(role.id);
        if (!profileCheck.exec()) {
            setError(errorText, profileCheck.lastError().text());
            return false;
        }
        if (profileCheck.next()) {
            // Adds self-service phone pairing to the default Master role only.
            // INSERT OR IGNORE preserves an explicit owner revocation (granted=0).
            if (role.id == QStringLiteral("master")) {
                QSqlQuery grantMobilePairing(database);
                grantMobilePairing.prepare(QStringLiteral(
                    "INSERT OR IGNORE INTO access_role_permissions(role_id,permission_key,granted) VALUES(?,?,1)"));
                grantMobilePairing.addBindValue(role.id);
                grantMobilePairing.addBindValue(QStringLiteral("mobile.pair"));
                if (!grantMobilePairing.exec()) {
                    setError(errorText, grantMobilePairing.lastError().text());
                    return false;
                }
            }
            continue;
        }

        for (const QString& permission : defaultGrants(role.id)) {
            QSqlQuery insertPermission(database);
            insertPermission.prepare(QStringLiteral(
                "INSERT OR IGNORE INTO access_role_permissions(role_id,permission_key,granted) VALUES(?,?,1)"));
            insertPermission.addBindValue(role.id);
            insertPermission.addBindValue(permission);
            if (!insertPermission.exec()) {
                setError(errorText, insertPermission.lastError().text());
                return false;
            }
        }
        QSqlQuery profileInsert(database);
        profileInsert.prepare(QStringLiteral(
            "INSERT INTO access_role_permission_profiles(role_id) VALUES(?)"));
        profileInsert.addBindValue(role.id);
        if (!profileInsert.exec()) {
            setError(errorText, profileInsert.lastError().text());
            return false;
        }
    }

    if (!query.exec(QStringLiteral("UPDATE staff_accounts SET role='owner' WHERE id='admin' AND role='admin'"))
        || !query.exec(QStringLiteral("UPDATE staff_accounts SET role='administrator' WHERE role='admin'"))) {
        setError(errorText, query.lastError().text());
        return false;
    }

    QSqlQuery normalize(database);
    if (!normalize.exec(QStringLiteral(
            "UPDATE staff_accounts SET role='master' "
            "WHERE TRIM(COALESCE(role,''))='' OR NOT EXISTS "
            "(SELECT 1 FROM access_roles r WHERE r.id=staff_accounts.role AND r.active=1)"))) {
        setError(errorText, normalize.lastError().text());
        return false;
    }
    return true;
}

QVector<AccessPermissionDefinition> permissionDefinitions()
{
    return definitions();
}

QVector<AccessRoleDefinition> roles(QSqlDatabase database, QString* errorText)
{
    QVector<AccessRoleDefinition> rows;
    if (!ensureSchema(database, errorText)) return rows;

    QSqlQuery query(database);
    if (!query.exec(QStringLiteral(
            "SELECT id,title,description,built_in,active FROM access_roles "
            "WHERE active=1 ORDER BY CASE WHEN id='owner' THEN 0 ELSE 1 END, built_in DESC, title COLLATE NOCASE"))) {
        setError(errorText, query.lastError().text());
        return rows;
    }
    while (query.next()) {
        rows.append({query.value(0).toString(), query.value(1).toString(), query.value(2).toString(),
                     query.value(3).toInt() != 0, query.value(4).toInt() != 0});
    }
    return rows;
}

QVector<AccessAccountRow> accounts(QSqlDatabase database, QString* errorText)
{
    QVector<AccessAccountRow> rows;
    if (!ensureSchema(database, errorText)) return rows;

    QSqlQuery query(database);
    if (!query.exec(QStringLiteral(
            "SELECT a.id,a.master_id,a.display_name,a.role,COALESCE(r.title,a.role),a.active,"
            "CASE WHEN LENGTH(TRIM(a.pin_hash))>0 THEN 1 ELSE 0 END,a.updated_at,"
            "COALESCE((SELECT MAX(s.logged_in_at) FROM staff_login_sessions s WHERE s.account_id=a.id),'') "
            "FROM staff_accounts a LEFT JOIN access_roles r ON r.id=a.role WHERE a.active=1 "
            "ORDER BY CASE WHEN a.role='owner' THEN 0 ELSE 1 END,a.display_name COLLATE NOCASE"))) {
        setError(errorText, query.lastError().text());
        return rows;
    }
    while (query.next()) {
        AccessAccountRow row;
        row.id = query.value(0).toString();
        row.masterId = query.value(1).toString();
        row.displayName = query.value(2).toString();
        row.roleId = query.value(3).toString();
        row.roleTitle = query.value(4).toString();
        row.active = query.value(5).toInt() != 0;
        row.pinConfigured = query.value(6).toInt() != 0;
        row.pinConfiguredAt = row.pinConfigured ? parseDatabaseDateTime(query.value(7).toString()) : QDateTime();
        row.lastLoginAt = parseDatabaseDateTime(query.value(8).toString());
        rows.append(row);
    }
    return rows;
}

QSet<QString> grantedPermissions(QSqlDatabase database, const QString& roleId, QString* errorText)
{
    QSet<QString> values;
    if (!ensureSchema(database, errorText)) return values;
    if (roleId == QStringLiteral("owner")) {
        for (const auto& permission : definitions()) values.insert(permission.key);
        return values;
    }

    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "SELECT permission_key FROM access_role_permissions WHERE role_id=? AND granted=1"));
    query.addBindValue(roleId.trimmed());
    if (!query.exec()) {
        setError(errorText, query.lastError().text());
        return values;
    }
    while (query.next()) values.insert(query.value(0).toString());
    return values;
}

bool setRolePermissions(QSqlDatabase database, const QString& roleId, const QSet<QString>& permissionKeys, QString* errorText)
{
    if (!ensureSchema(database, errorText)) return false;
    if (!canManage(database)) {
        setError(errorText, QString::fromUtf8("Лише власник може змінювати права доступу."));
        return false;
    }
    if (roleId == QStringLiteral("owner")) {
        setError(errorText, QString::fromUtf8("Права власника є повними та не можуть бути обмежені."));
        return false;
    }
    if (!roleExists(database, roleId)) {
        setError(errorText, QString::fromUtf8("Роль не знайдено."));
        return false;
    }

    QSet<QString> validKeys;
    for (const auto& definition : definitions()) validKeys.insert(definition.key);
    for (const QString& key : permissionKeys) {
        if (!validKeys.contains(key)) {
            setError(errorText, QString::fromUtf8("Невідомий дозвіл: %1").arg(key));
            return false;
        }
    }

    if (!database.transaction()) {
        setError(errorText, database.lastError().text());
        return false;
    }
    QSqlQuery remove(database);
    remove.prepare(QStringLiteral("DELETE FROM access_role_permissions WHERE role_id=?"));
    remove.addBindValue(roleId);
    if (!remove.exec()) {
        database.rollback();
        setError(errorText, remove.lastError().text());
        return false;
    }
    for (const QString& key : permissionKeys) {
        QSqlQuery insert(database);
        insert.prepare(QStringLiteral(
            "INSERT INTO access_role_permissions(role_id,permission_key,granted) VALUES(?,?,1)"));
        insert.addBindValue(roleId);
        insert.addBindValue(key);
        if (!insert.exec()) {
            database.rollback();
            setError(errorText, insert.lastError().text());
            return false;
        }
    }
    QSqlQuery profile(database);
    profile.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO access_role_permission_profiles(role_id) VALUES(?)"));
    profile.addBindValue(roleId);
    if (!profile.exec()) {
        database.rollback();
        setError(errorText, profile.lastError().text());
        return false;
    }
    if (!database.commit()) {
        setError(errorText, database.lastError().text());
        return false;
    }
    audit(database, QStringLiteral("role_permissions_update"), QString::fromUtf8("Оновлено права ролі"), roleId, roleId);
    return true;
}

bool createRole(QSqlDatabase database, const QString& title, const QString& description, QString* roleId, QString* errorText)
{
    if (!ensureSchema(database, errorText)) return false;
    if (!canManage(database)) {
        setError(errorText, QString::fromUtf8("Лише власник може створювати ролі."));
        return false;
    }
    if (title.trimmed().size() < 2) {
        setError(errorText, QString::fromUtf8("Вкажіть назву ролі щонайменше з 2 символів."));
        return false;
    }

    const QString generatedId = QStringLiteral("custom:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "INSERT INTO access_roles(id,title,description,built_in,active) VALUES(?,?,?,0,1)"));
    query.addBindValue(generatedId);
    query.addBindValue(title.trimmed());
    query.addBindValue(description.trimmed());
    if (!query.exec()) {
        setError(errorText, query.lastError().text());
        return false;
    }
    QSqlQuery profile(database);
    profile.prepare(QStringLiteral(
        "INSERT INTO access_role_permission_profiles(role_id) VALUES(?)"));
    profile.addBindValue(generatedId);
    if (!profile.exec()) {
        setError(errorText, profile.lastError().text());
        return false;
    }
    audit(database, QStringLiteral("role_create"), QString::fromUtf8("Створено роль: %1").arg(title.trimmed()), generatedId, title.trimmed());
    if (roleId) *roleId = generatedId;
    return true;
}

bool deleteCustomRole(QSqlDatabase database, const QString& roleId, QString* errorText)
{
    if (!ensureSchema(database, errorText)) return false;
    if (!canManage(database)) {
        setError(errorText, QString::fromUtf8("Лише власник може видаляти ролі."));
        return false;
    }

    QSqlQuery used(database);
    used.prepare(QStringLiteral("SELECT COUNT(*) FROM staff_accounts WHERE role=?"));
    used.addBindValue(roleId);
    if (!used.exec() || !used.next()) {
        setError(errorText, used.lastError().text());
        return false;
    }
    if (used.value(0).toInt() > 0) {
        setError(errorText, QString::fromUtf8("Роль призначена працівникам і не може бути видалена."));
        return false;
    }

    QSqlQuery removeProfile(database);
    removeProfile.prepare(QStringLiteral("DELETE FROM access_role_permission_profiles WHERE role_id=?"));
    removeProfile.addBindValue(roleId);
    if (!removeProfile.exec()) {
        setError(errorText, removeProfile.lastError().text());
        return false;
    }

    QSqlQuery query(database);
    query.prepare(QStringLiteral("DELETE FROM access_roles WHERE id=? AND built_in=0"));
    query.addBindValue(roleId);
    if (!query.exec()) {
        setError(errorText, query.lastError().text());
        return false;
    }
    if (query.numRowsAffected() != 1) {
        setError(errorText, QString::fromUtf8("Можна видалити лише користувацьку роль."));
        return false;
    }
    audit(database, QStringLiteral("role_delete"), QString::fromUtf8("Видалено користувацьку роль"), roleId, roleId);
    return true;
}

bool assignRole(QSqlDatabase database, const QString& accountId, const QString& roleId, QString* errorText)
{
    if (!ensureSchema(database, errorText)) return false;
    if (!canManage(database)) {
        setError(errorText, QString::fromUtf8("Лише власник може призначати ролі."));
        return false;
    }
    if (!roleExists(database, roleId)) {
        setError(errorText, QString::fromUtf8("Обрана роль не існує або неактивна."));
        return false;
    }
    if (accountId == QStringLiteral("admin") && roleId != QStringLiteral("owner")) {
        setError(errorText, QString::fromUtf8("Роль власника не можна зняти з основного облікового запису."));
        return false;
    }

    QSqlQuery query(database);
    query.prepare(QStringLiteral("UPDATE staff_accounts SET role=?,updated_at=datetime('now') WHERE id=?"));
    query.addBindValue(roleId);
    query.addBindValue(accountId);
    if (!query.exec()) {
        setError(errorText, query.lastError().text());
        return false;
    }
    if (query.numRowsAffected() != 1) {
        setError(errorText, QString::fromUtf8("Обліковий запис не знайдено."));
        return false;
    }
    audit(database, QStringLiteral("role_assign"), QString::fromUtf8("Призначено роль працівнику"), accountId, roleId);
    return true;
}

bool saveAccount(QSqlDatabase database, const QString& accountId, const QString& name, const QString& roleId, QString* errorText)
{
    if(!ensureSchema(database,errorText))return false;
    if(!canManage(database) || name.trimmed().isEmpty() || !roleExists(database,roleId)) {
        setError(errorText,QString::fromUtf8("Потрібні права власника, ім’я працівника та чинна роль."));return false;
    }
    if((accountId=="admin" || accountId==ActivityAuditService::currentActor().accountId) && roleId!="owner") {
        setError(errorText,QString::fromUtf8("Не можна зняти роль власника з поточного або основного облікового запису."));return false;
    }
    QSqlQuery query(database);
    const auto id=accountId.isEmpty()?QUuid::createUuid().toString(QUuid::WithoutBraces):accountId;
    if(accountId.isEmpty())query.prepare("INSERT INTO staff_accounts(id,display_name,role,master_id,active) VALUES(?,?,?,'',1)");
    else query.prepare("UPDATE staff_accounts SET display_name=?,role=?,updated_at=datetime('now') WHERE id=? AND active=1");
    if(accountId.isEmpty()) {query.addBindValue(id);query.addBindValue(name.trimmed());query.addBindValue(roleId);}
    else {query.addBindValue(name.trimmed());query.addBindValue(roleId);query.addBindValue(id);}
    if(!query.exec() || query.numRowsAffected()!=1) {setError(errorText,query.lastError().isValid()?query.lastError().text():QString::fromUtf8("Працівника не знайдено."));return false;}
    audit(database,"account_save",QString::fromUtf8("Збережено обліковий запис працівника"),id,name.trimmed());return true;
}

bool deactivateAccount(QSqlDatabase database, const QString& accountId, QString* errorText)
{
    if(!ensureSchema(database,errorText))return false;
    if(!canManage(database) || accountId=="admin" || accountId==ActivityAuditService::currentActor().accountId) {
        setError(errorText,QString::fromUtf8("Не можна видалити поточного або основного власника. Керування доступне власнику."));return false;
    }
    QSqlQuery query(database);
    query.prepare("UPDATE staff_accounts SET active=0,updated_at=datetime('now') WHERE id=? AND active=1");query.addBindValue(accountId);
    if(!query.exec() || query.numRowsAffected()!=1) {setError(errorText,QString::fromUtf8("Не вдалося видалити обліковий запис."));return false;}
    audit(database,"account_deactivate",QString::fromUtf8("Видалено працівника з активного обліку"),accountId,accountId);return true;
}

bool resetAccountPin(QSqlDatabase database, const QString& accountId, QString* errorText)
{
    if (!ensureSchema(database, errorText)) return false;
    if (!canManage(database)) {
        setError(errorText, QString::fromUtf8("Лише власник може скидати персональні PIN."));
        return false;
    }
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "UPDATE staff_accounts SET pin_hash='',updated_at=datetime('now') WHERE id=?"));
    query.addBindValue(accountId);
    if (!query.exec()) {
        setError(errorText, query.lastError().text());
        return false;
    }
    if (query.numRowsAffected() != 1) {
        setError(errorText, QString::fromUtf8("Обліковий запис не знайдено."));
        return false;
    }
    audit(database, QStringLiteral("pin_reset"), QString::fromUtf8("Скинуто персональний PIN"), accountId, accountId);
    return true;
}

bool hasPermission(QSqlDatabase database, const QString& roleId, const QString& permissionKey)
{
    if (!database.isValid() || !database.isOpen() || !roleExists(database, roleId)) return false;
    if (roleId == QStringLiteral("owner")) return true;
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "SELECT granted FROM access_role_permissions WHERE role_id=? AND permission_key=? LIMIT 1"));
    query.addBindValue(roleId.trimmed());
    query.addBindValue(permissionKey.trimmed());
    return query.exec() && query.next() && query.value(0).toInt() != 0;
}

bool currentUserHasPermission(QSqlDatabase database, const QString& permissionKey)
{
    if (!actorMatchesActiveAccount(database)) return false;
    const ActivityAuditActor actor = ActivityAuditService::currentActor();
    // Master navigation must not expose unscoped administrative datasets, even
    // when a legacy role configuration granted an additional navigation entry.
    if (actor.role.compare(QStringLiteral("master"), Qt::CaseInsensitive) == 0
        && permissionKey.startsWith(QStringLiteral("nav."))
        && !defaultGrants(QStringLiteral("master")).contains(permissionKey)) return false;
    return actor.isValid() && hasPermission(database, actor.role, permissionKey);
}

bool currentUserHasPermission(const QString& key) { return currentUserHasPermission(Database::db(),key); }
QVector<AccessRoleDefinition> roles(QString* error) { return roles(Database::db(),error); }
QVector<AccessAccountRow> accounts(QString* error) { return accounts(Database::db(),error); }
QSet<QString> grantedPermissions(const QString& role,QString* error) { return grantedPermissions(Database::db(),role,error); }
bool saveAccount(const QString& id,const QString& name,const QString& role,QString* error) { return saveAccount(Database::db(),id,name,role,error); }
bool deactivateAccount(const QString& id,QString* error) { return deactivateAccount(Database::db(),id,error); }
bool resetAccountPin(const QString& id,QString* error) { return resetAccountPin(Database::db(),id,error); }
bool createRole(const QString& title,const QString& description,QString* id,QString* error) { return createRole(Database::db(),title,description,id,error); }
bool deleteCustomRole(const QString& id,QString* error) { return deleteCustomRole(Database::db(),id,error); }
bool setRolePermissions(const QString& id,const QSet<QString>& permissions,QString* error) { return setRolePermissions(Database::db(),id,permissions,error); }

bool saveAccounts(const QVector<AccountEdit>& edits,bool* changed,QString* error)
{
    if(error) error->clear();
    if(changed) *changed=false;
    auto db=Database::db();
    if(!currentUserHasPermission(db,QStringLiteral("access.manage"))) {
        setError(error,QString::fromUtf8("Недостатньо прав для збереження працівників."));return false;
    }
    QSqlQuery transaction(db);
    if(!transaction.exec("SAVEPOINT save_staff_accounts")) {setError(error,transaction.lastError().text());return false;}
    bool committed=false;
    const auto rollback=qScopeGuard([&] {
        if(committed) return;
        QString failure;
        if(!transaction.exec("ROLLBACK TO save_staff_accounts")) failure=transaction.lastError().text();
        if(!transaction.exec("RELEASE save_staff_accounts")) failure+=" "+transaction.lastError().text();
        if(!failure.isEmpty() && error) *error+=QString::fromUtf8(" Помилка відкату: ")+failure;
    });
    QString diagnostic;
    const auto current=accounts(db,&diagnostic);
    if(!diagnostic.isEmpty()) {setError(error,diagnostic);return false;}
    QHash<QString,AccessAccountRow> byId;
    for(const auto& account:current) byId.insert(account.id,account);
    QSet<QString> seen;
    // Validate every row before the first write: stale forms must never overwrite
    // a concurrent role change, deactivation or rename.
    for(const auto& edit:edits) {
        const auto found=byId.constFind(edit.expected.id);
        if(seen.contains(edit.expected.id) || found==byId.cend() || found->displayName!=edit.expected.displayName
            || found->roleId!=edit.expected.roleId || found->active!=edit.expected.active) {
            setError(error,QString::fromUtf8("Дані працівників змінилися. Оновіть форму й повторіть зміни."));return false;
        }
        seen.insert(edit.expected.id);
    }
    bool anyChanged=false;
    for(const auto& edit:edits) {
        if(edit.expected.displayName==edit.name.trimmed() && edit.expected.roleId==edit.roleId) continue;
        if(!saveAccount(db,edit.expected.id,edit.name,edit.roleId,&diagnostic)) {setError(error,diagnostic);return false;}
        anyChanged=true;
    }
    if(!transaction.exec("RELEASE save_staff_accounts")) {setError(error,transaction.lastError().text());return false;}
    committed=true;
    if(changed) *changed=anyChanged;
    return true;
}

QString currentMasterId() { return currentMasterId(Database::db()); }
bool canAccessOrder(int internalNumber) { return canAccessOrder(Database::db(),internalNumber); }

} // namespace AccessControlService

namespace AccessControlService {
OrderScope currentOrderScope() { return currentOrderScope(Database::db()); }
}
