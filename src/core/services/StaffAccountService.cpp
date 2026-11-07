#include "infrastructure/db/sqlite/Db.h"
#include "core/services/StaffAccountService.h"

#include <QCryptographicHash>
#include <QPasswordDigestor>
#include <QRandomGenerator>
#include <QScopeGuard>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace {

void setError(QString* errorText, const QString& value)
{
    if (errorText) *errorText = value;
}

QString legacyPinHash(const QString& accountId, const QString& pin)
{
    const QByteArray input = QByteArrayLiteral("cs_staff_pin_v1:") + accountId.toUtf8() + QByteArrayLiteral(":") + pin.toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(input, QCryptographicHash::Sha256).toHex());
}

constexpr int pinIterations = 600000;

QString loginEligibility(QSqlDatabase database)
{
    // Restored profiles may retain an active account for an archived worker.
    // Check personnel live; never erase accounts referenced by historical audits.
    const bool hasPersonnel = database.tables().contains(QStringLiteral("masters"));
    const QString linked = hasPersonnel
        ? QStringLiteral("EXISTS(SELECT 1 FROM masters m WHERE m.id=staff_accounts.master_id AND m.active=1)")
        : QStringLiteral("0");
    const QString ownerLinked = hasPersonnel
        ? QStringLiteral("EXISTS(SELECT 1 FROM masters m WHERE m.id=o.master_id AND m.active=1)")
        : QStringLiteral("0");
    return QStringLiteral(
        "active=1 AND (master_id='' OR %1) "
        "AND NOT (id='admin' AND master_id='' AND EXISTS("
        "SELECT 1 FROM staff_accounts o WHERE o.id<>'admin' AND o.active=1 "
        "AND o.role IN ('owner','admin') AND LENGTH(TRIM(o.pin_hash))>0 "
        "AND (o.master_id='' OR %2)))").arg(linked, ownerLinked);
}

QString pinHash(const QString& pin)
{
    QByteArray salt(16, Qt::Uninitialized);
    for (char& byte : salt) byte = static_cast<char>(QRandomGenerator::system()->generate() & 0xff);
    const auto key = QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256, pin.toUtf8(), salt, pinIterations, 32);
    return QStringLiteral("pbkdf2-sha256$%1$%2$%3")
        .arg(pinIterations).arg(QString::fromLatin1(salt.toHex()), QString::fromLatin1(key.toHex()));
}

bool sameBytes(const QByteArray& left, const QByteArray& right)
{
    if (left.size() != right.size()) return false;
    unsigned int difference = 0;
    for (qsizetype i = 0; i < left.size(); ++i)
        difference |= static_cast<unsigned char>(left[i]) ^ static_cast<unsigned char>(right[i]);
    return difference == 0;
}

bool verifyPin(const QString& stored, const QString& account, const QString& pin)
{
    if (!stored.startsWith(QStringLiteral("pbkdf2-sha256$")))
        return stored.size() == 64 && sameBytes(stored.toLatin1(), legacyPinHash(account, pin).toLatin1());
    const auto fields = stored.split('$');
    if (fields.size() != 4) return false;
    bool ok = false;
    const int iterations = fields[1].toInt(&ok);
    if (!ok || iterations < pinIterations || iterations > 2000000) return false;
    const auto salt = QByteArray::fromHex(fields[2].toLatin1());
    const auto expected = QByteArray::fromHex(fields[3].toLatin1());
    if (salt.size() != 16 || expected.size() != 32) return false;
    return sameBytes(expected, QPasswordDigestor::deriveKeyPbkdf2(
        QCryptographicHash::Sha256, pin.toUtf8(), salt, iterations, 32));
}

} // namespace

namespace StaffAccountService {

bool ensureSchema(QSqlDatabase database, QString* errorText)
{
    if (!database.isValid()) {
        setError(errorText, QString::fromUtf8("Немає з'єднання з базою даних облікових записів."));
        return false;
    }
    if (!database.isOpen() && !database.open()) {
        setError(errorText, database.lastError().text());
        return false;
    }

    const bool bootstrapAccounts=!database.tables().contains(QStringLiteral("staff_accounts"));
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS staff_accounts ("
            "id TEXT PRIMARY KEY,"
            "master_id TEXT NOT NULL DEFAULT '',"
            "display_name TEXT NOT NULL,"
            "role TEXT NOT NULL DEFAULT 'master',"
            "pin_hash TEXT NOT NULL DEFAULT '',"
            "active INTEGER NOT NULL DEFAULT 1,"
            "created_at TEXT NOT NULL DEFAULT (datetime('now')),"
            "updated_at TEXT NOT NULL DEFAULT (datetime('now'))"
            ")"))
        || !query.exec(QStringLiteral(
            "CREATE UNIQUE INDEX IF NOT EXISTS idx_staff_accounts_master_id "
            "ON staff_accounts(master_id) WHERE master_id<>''"))
        || !query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS staff_login_sessions ("
            "id TEXT PRIMARY KEY,"
            "account_id TEXT NOT NULL,"
            "account_name TEXT NOT NULL,"
            "role TEXT NOT NULL,"
            "logged_in_at TEXT NOT NULL DEFAULT (datetime('now')),"
            "logged_out_at TEXT NOT NULL DEFAULT '',"
            "session_kind TEXT NOT NULL DEFAULT 'interactive'"
            ")"))
        || !query.exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_staff_login_sessions_account "
            "ON staff_login_sessions(account_id, logged_in_at DESC)"))) {
        setError(errorText, query.lastError().text());
        return false;
    }

    if (bootstrapAccounts && (!query.exec(QStringLiteral(
            "INSERT OR IGNORE INTO staff_accounts(id, master_id, display_name, role, active) "
            "VALUES('admin','','Адміністратор','admin',1)"))
        || !query.exec(QStringLiteral(
            "INSERT OR IGNORE INTO staff_accounts(id, master_id, display_name, role, active) "
            "SELECT 'master:' || id, id, name, COALESCE(NULLIF(TRIM(role),''),'master'), active "
            "FROM masters WHERE TRIM(COALESCE(id,''))<>''")))) {
        setError(errorText, query.lastError().text());
        return false;
    }

    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS staff_login_attempts ("
            "account_id TEXT PRIMARY KEY, failures INTEGER NOT NULL DEFAULT 0, "
            "locked_until INTEGER NOT NULL DEFAULT 0)"))) {
        setError(errorText, query.lastError().text());
        return false;
    }
    return ActivityAuditService::ensureSchema(database, errorText);
}

QVector<StaffAccount> accounts(QSqlDatabase database, QString* errorText)
{
    QVector<StaffAccount> rows;
    if (!ensureSchema(database, errorText)) return rows;

    QSqlQuery query(database);
    if (!query.exec(QStringLiteral(
            "SELECT id, master_id, display_name, role, active, CASE WHEN LENGTH(TRIM(pin_hash))>0 THEN 1 ELSE 0 END "
            "FROM staff_accounts WHERE %1 ORDER BY CASE WHEN role='admin' THEN 0 ELSE 1 END, display_name COLLATE NOCASE")
            .arg(loginEligibility(database)))) {
        setError(errorText, query.lastError().text());
        return rows;
    }
    while (query.next()) {
        rows.append({query.value(0).toString(), query.value(1).toString(), query.value(2).toString(),
                     query.value(3).toString(), query.value(4).toInt() != 0, query.value(5).toInt() != 0});
    }
    return rows;
}

bool setPin(QSqlDatabase database, const QString& accountId, const QString& pin, QString* errorText)
{
    setError(errorText, {});
    if (!ensureSchema(database, errorText)) return false;
    if (pin.size() < 4 || pin.size() > 256) {
        setError(errorText, QString::fromUtf8("PIN має містити від 4 до 256 символів."));
        return false;
    }
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "UPDATE staff_accounts SET pin_hash=?, updated_at=datetime('now') "
        "WHERE id=? AND (%1) AND TRIM(COALESCE(pin_hash,''))=''").arg(loginEligibility(database)));
    query.addBindValue(pinHash(pin));
    query.addBindValue(accountId.trimmed());
    if (!query.exec()) {
        setError(errorText, query.lastError().text());
        return false;
    }
    if (query.numRowsAffected() != 1) {
        setError(errorText, QString::fromUtf8("PIN для цього облікового запису вже встановлено або запис неактивний."));
        return false;
    }
    return true;
}

bool signIn(QSqlDatabase database,
            const QString& accountId,
            const QString& pin,
            ActivityAuditActor* actor,
            QString* errorText)
{
    setError(errorText, {});
    if (!ensureSchema(database, errorText)) return false;

    // A write lock serializes attempts across processes; the persistent counter
    // survives restarting the application. No confirmed session precedes commit.
    QSqlQuery transaction(database);
    if (!transaction.exec(QStringLiteral("SAVEPOINT staff_signin"))) {
        setError(errorText, transaction.lastError().text());
        return false;
    }
    bool released = false;
    const auto rollback = qScopeGuard([&] {
        if (!released) {
            transaction.exec(QStringLiteral("ROLLBACK TO staff_signin"));
            transaction.exec(QStringLiteral("RELEASE staff_signin"));
        }
    });
    auto commit = [&] {
        released = transaction.exec(QStringLiteral("RELEASE staff_signin"));
        if (!released) setError(errorText, transaction.lastError().text());
        return released;
    };
    QSqlQuery attempts(database);
    attempts.prepare(QStringLiteral("INSERT OR IGNORE INTO staff_login_attempts(account_id) VALUES(?)"));
    attempts.addBindValue(accountId.trimmed());
    if (!attempts.exec()) { setError(errorText, attempts.lastError().text()); return false; }
    attempts.prepare(QStringLiteral("SELECT failures,locked_until FROM staff_login_attempts WHERE account_id=?"));
    attempts.addBindValue(accountId.trimmed());
    if (!attempts.exec() || !attempts.next()) { setError(errorText, attempts.lastError().text()); return false; }
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const qint64 lockedUntil = attempts.value(1).toLongLong();
    const int failures = lockedUntil > 0 && lockedUntil <= now ? 0 : attempts.value(0).toInt();
    attempts.finish();
    if (lockedUntil > now) {
        setError(errorText, QString::fromUtf8("Забагато невдалих спроб. Повторіть вхід через %1 с.").arg(lockedUntil - now));
        return false;
    }

    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "SELECT display_name, role, pin_hash FROM staff_accounts WHERE id=? AND (%1) LIMIT 1")
        .arg(loginEligibility(database)));
    query.addBindValue(accountId.trimmed());
    if (!query.exec() || !query.next()) {
        setError(errorText, query.lastError().isValid() ? query.lastError().text() : QString::fromUtf8("Активний обліковий запис не знайдено."));
        return false;
    }
    const QString storedHash = query.value(2).toString();
    if (pin.size() > 256 || storedHash.isEmpty() || !verifyPin(storedHash, accountId.trimmed(), pin)) {
        query.finish();
        attempts.prepare(QStringLiteral("UPDATE staff_login_attempts SET failures=?,locked_until=? WHERE account_id=?"));
        attempts.addBindValue(failures + 1);
        attempts.addBindValue(failures + 1 >= 5 ? now + 300 : qint64(0));
        attempts.addBindValue(accountId.trimmed());
        if (!attempts.exec()) { setError(errorText, attempts.lastError().text()); return false; }
        if (!commit()) return false;
        setError(errorText, QString::fromUtf8("Невірний персональний PIN."));
        return false;
    }

    ActivityAuditActor authenticated;
    authenticated.accountId = accountId.trimmed();
    authenticated.displayName = query.value(0).toString().trimmed();
    authenticated.role = query.value(1).toString().trimmed();
    authenticated.sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    query.finish();

    if (!storedHash.startsWith(QStringLiteral("pbkdf2-sha256$"))) {
        QSqlQuery migrate(database);
        migrate.prepare(QStringLiteral("UPDATE staff_accounts SET pin_hash=?,updated_at=datetime('now') WHERE id=? AND pin_hash=?"));
        migrate.addBindValue(pinHash(pin));
        migrate.addBindValue(accountId.trimmed());
        migrate.addBindValue(storedHash);
        if (!migrate.exec() || migrate.numRowsAffected() != 1) {
            setError(errorText, QString::fromUtf8("Не вдалося оновити захист PIN."));
            return false;
        }
    }
    attempts.prepare(QStringLiteral("UPDATE staff_login_attempts SET failures=0,locked_until=0 WHERE account_id=?"));
    attempts.addBindValue(accountId.trimmed());
    if (!attempts.exec()) { setError(errorText, attempts.lastError().text()); return false; }

    QSqlQuery session(database);
    session.prepare(QStringLiteral(
        "INSERT INTO staff_login_sessions(id, account_id, account_name, role, session_kind) VALUES(?,?,?,?,?)"));
    session.addBindValue(authenticated.sessionId);
    session.addBindValue(authenticated.accountId);
    session.addBindValue(authenticated.displayName);
    session.addBindValue(authenticated.role);
    session.addBindValue(QStringLiteral("interactive"));
    if (!session.exec()) {
        setError(errorText, session.lastError().text());
        return false;
    }
    if (!commit()) return false;

    // A successful interactive login may replace an already authenticated worker.
    // Close the previous session only after the new credentials and session row are valid.
    if (ActivityAuditService::currentActor().isValid()) {
        closeCurrentSession(database, QStringLiteral("user_switch"));
    }

    ActivityAuditService::setCurrentActor(authenticated);
    QString auditError;
    ActivityAuditService::record(database,
                                 QStringLiteral("session"),
                                 QStringLiteral("login"),
                                 QString::fromUtf8("Вхід у застосунок"),
                                 0,
                                 QStringLiteral("staff_account"),
                                 authenticated.accountId,
                                 authenticated.displayName,
                                 QStringLiteral("{\"source\":\"interactive\"}"),
                                 &auditError);
    if (actor) *actor = authenticated;
    return true;
}

void closeCurrentSession(QSqlDatabase database, const QString& source)
{
    const ActivityAuditActor actor = ActivityAuditService::currentActor();
    if (!actor.isValid() || actor.sessionId.trimmed().isEmpty() || !database.isValid()) {
        ActivityAuditService::clearCurrentActor();
        return;
    }

    const bool userSwitch = source == QStringLiteral("user_switch");
    QString ignored;
    ActivityAuditService::record(database,
                                 QStringLiteral("session"),
                                 QStringLiteral("logout"),
                                 userSwitch
                                     ? QString::fromUtf8("Завершено сеанс для зміни користувача")
                                     : QString::fromUtf8("Вихід із застосунку"),
                                 0,
                                 QStringLiteral("staff_account"),
                                 actor.accountId,
                                 actor.displayName,
                                 QStringLiteral("{\"source\":\"%1\"}").arg(source),
                                 &ignored);
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "UPDATE staff_login_sessions SET logged_out_at=datetime('now') "
        "WHERE id=? AND TRIM(COALESCE(logged_out_at,''))=''"));
    query.addBindValue(actor.sessionId);
    query.exec();
    ActivityAuditService::clearCurrentActor();
}

} // namespace StaffAccountService

namespace StaffAccountService {
QVector<StaffAccount> accounts(QString* errorText) { return accounts(Database::db(),errorText); }
bool setPin(const QString& accountId, const QString& pin, QString* errorText) { return setPin(Database::db(),accountId,pin,errorText); }
bool signIn(const QString& accountId, const QString& pin, ActivityAuditActor* actor, QString* errorText) { return signIn(Database::db(),accountId,pin,actor,errorText); }
QVector<QPair<QString,QString>> orderingMasters() { QVector<QPair<QString,QString>> result; for(const auto& row:Database::masters()) if(row.role=="master") result.append({row.id,row.name}); return result; }
}
