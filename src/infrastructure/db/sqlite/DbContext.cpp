#include "infrastructure/db/sqlite/DbContext.h"

#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/db/sqlite/DbFacadeSupport.h"
#include "core/diagnostics/RuntimeLoggingControl.h"

#include <QDebug>
#include <QSet>
#include <QHash>
#include <QSqlError>
#include <QThread>
#include <QUuid>

namespace {
thread_local QSet<QString> activeTransactionConnections;
thread_local QHash<QString, TransactionContext*> transactionOwners;

QString threadIdText()
{
    return QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()));
}

QString normalizedConnectionName(const QSqlDatabase& db)
{
    const QString name = db.connectionName().trimmed();
    return name.isEmpty() ? QStringLiteral("<default>") : name;
}

QString normalizedError(const QString& candidate, const QString& fallback)
{
    const QString value = candidate.trimmed();
    return value.isEmpty() ? fallback : value;
}

void writeDbContextLog(const QString& phase,
                       const QString& contextId,
                       const QString& operation,
                       const QString& connectionName,
                       const QString& errorText = QString())
{
    if (errorText.trimmed().isEmpty()) {
        if (RuntimeLoggingControl::deepDebugEnabled()) {
            qInfo().noquote() << QStringLiteral("DB_CONTEXT phase=") << phase
                              << QStringLiteral(" contextId=") << contextId
                              << QStringLiteral(" operation=") << operation
                              << QStringLiteral(" connection=") << connectionName
                              << QStringLiteral(" threadId=") << threadIdText();
        }
        return;
    }
    qCritical().noquote() << QStringLiteral("DB_CONTEXT phase=") << phase
                          << QStringLiteral(" contextId=") << contextId
                          << QStringLiteral(" operation=") << operation
                          << QStringLiteral(" connection=") << connectionName
                          << QStringLiteral(" threadId=") << threadIdText()
                          << QStringLiteral(" error=") << errorText;
}

void writeTransactionContextLog(const QString& phase,
                                const QString& transactionId,
                                const QString& owner,
                                const QString& operationId,
                                const QString& connectionName,
                                const QString& errorText = QString())
{
    if (errorText.trimmed().isEmpty()) {
        if (RuntimeLoggingControl::deepDebugEnabled()) {
            qInfo().noquote() << QStringLiteral("DB_TRANSACTION_CONTEXT phase=") << phase
                              << QStringLiteral(" transactionId=") << transactionId
                              << QStringLiteral(" owner=") << owner
                              << QStringLiteral(" operationId=") << operationId
                              << QStringLiteral(" connection=") << connectionName
                              << QStringLiteral(" threadId=") << threadIdText();
        }
        return;
    }
    qCritical().noquote() << QStringLiteral("DB_TRANSACTION_CONTEXT phase=") << phase
                          << QStringLiteral(" transactionId=") << transactionId
                          << QStringLiteral(" owner=") << owner
                          << QStringLiteral(" operationId=") << operationId
                          << QStringLiteral(" connection=") << connectionName
                          << QStringLiteral(" threadId=") << threadIdText()
                          << QStringLiteral(" error=") << errorText;
}
} // namespace

DbContext::Result DbContext::openDefault(const QString& operation, QString* errorText)
{
    Result result;
    result.operation = operation.trimmed().isEmpty() ? QStringLiteral("default") : operation.trimmed();
    result.contextId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    result.connectionName = currentConnectionName();

    QString dbError;
    result.db = Database::db(&dbError);
    result.connectionName = result.db.isValid() ? normalizedConnectionName(result.db) : result.connectionName;

    if (!result.db.isValid() || !result.db.isOpen()) {
        result.errorText = normalizedError(dbError, QStringLiteral("Default SQLite connection is not open"));
        if (errorText) *errorText = result.errorText;
        writeDbContextLog(QStringLiteral("open_default_failed"), result.contextId, result.operation, result.connectionName, result.errorText);
        return result;
    }

    result.ok = true;
    writeDbContextLog(QStringLiteral("open_default_ok"), result.contextId, result.operation, result.connectionName);
    return result;
}

DbContext::Result DbContext::fromConnectionName(const QString& connectionName,
                                                const QString& operation,
                                                QString* errorText)
{
    Result result;
    result.operation = operation.trimmed().isEmpty() ? QStringLiteral("active_transaction") : operation.trimmed();
    result.contextId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    result.connectionName = connectionName.trimmed();

    if (result.connectionName.isEmpty()) {
        result.errorText = QStringLiteral("SQLite connectionName is empty; refusing implicit Database::db() fallback");
        if (errorText) *errorText = result.errorText;
        writeDbContextLog(QStringLiteral("connection_name_missing"), result.contextId, result.operation, QStringLiteral("<empty>"), result.errorText);
        return result;
    }

    if (!QSqlDatabase::contains(result.connectionName)) {
        result.errorText = QStringLiteral("SQLite connection is not registered: %1").arg(result.connectionName);
        if (errorText) *errorText = result.errorText;
        writeDbContextLog(QStringLiteral("connection_not_found"), result.contextId, result.operation, result.connectionName, result.errorText);
        return result;
    }

    result.db = QSqlDatabase::database(result.connectionName, false);
    if (!result.db.isValid()) {
        result.errorText = QStringLiteral("SQLite connection handle is invalid: %1").arg(result.connectionName);
        if (errorText) *errorText = result.errorText;
        writeDbContextLog(QStringLiteral("connection_invalid"), result.contextId, result.operation, result.connectionName, result.errorText);
        return result;
    }

    if (!result.db.isOpen() && !result.db.open()) {
        result.errorText = normalizedError(result.db.lastError().text(), QStringLiteral("SQLite connection cannot be opened: %1").arg(result.connectionName));
        if (errorText) *errorText = result.errorText;
        writeDbContextLog(QStringLiteral("connection_open_failed"), result.contextId, result.operation, result.connectionName, result.errorText);
        return result;
    }

    result.ok = true;
    writeDbContextLog(QStringLiteral("connection_ok"), result.contextId, result.operation, result.connectionName);
    return result;
}

TransactionContext::TransactionContext(QSqlDatabase database,
                                       QString owner,
                                       QString operationId)
    : m_database(database)
    , m_owner(owner.trimmed().isEmpty() ? QStringLiteral("TransactionContext") : owner.trimmed())
    , m_operationId(operationId.trimmed())
    , m_transactionId(QUuid::createUuid().toString(QUuid::WithoutBraces))
    , m_connectionName(normalizedConnectionName(m_database))
{
}

TransactionContext::~TransactionContext()
{
    if (!m_active) return;
    QString ignored;
    rollback(QStringLiteral("destructor_rollback_unfinished_transaction"), &ignored);
}

bool TransactionContext::begin(const QString& beginErrorFallback, QString* errorText)
{
    if (!m_database.isValid() || (!m_database.isOpen() && !m_database.open())) {
        const QString detail = normalizedError(m_database.lastError().text(), beginErrorFallback);
        if (errorText) *errorText = detail;
        writeTransactionContextLog(QStringLiteral("begin_open_failed"), m_transactionId, m_owner, m_operationId, m_connectionName, detail);
        return false;
    }

    m_connectionName = normalizedConnectionName(m_database);
    if (activeTransactionConnections.contains(m_connectionName)) {
        const QString detail = QStringLiteral("Nested SQLite transaction is prohibited without explicit transaction owner: %1").arg(m_connectionName);
        if (errorText) *errorText = detail;
        writeTransactionContextLog(QStringLiteral("nested_rejected"), m_transactionId, m_owner, m_operationId, m_connectionName, detail);
        return false;
    }

    if (!m_database.transaction()) {
        const QString detail = normalizedError(m_database.lastError().text(), beginErrorFallback);
        if (errorText) *errorText = detail;
        writeTransactionContextLog(QStringLiteral("begin_failed"), m_transactionId, m_owner, m_operationId, m_connectionName, detail);
        return false;
    }

    activeTransactionConnections.insert(m_connectionName);
    transactionOwners.insert(m_connectionName, this);
    m_registered = true;
    m_active = true;
    writeTransactionContextLog(QStringLiteral("begin_ok"), m_transactionId, m_owner, m_operationId, m_connectionName);
    return true;
}

bool TransactionContext::commit(const QString& commitErrorFallback, QString* errorText)
{
    if (!m_active) {
        const QString detail = QStringLiteral("SQLite transaction is not active; commit refused: %1").arg(m_connectionName);
        if (errorText) *errorText = detail;
        writeTransactionContextLog(QStringLiteral("commit_without_active_tx"), m_transactionId, m_owner, m_operationId, m_connectionName, detail);
        return false;
    }

    if (!m_database.commit()) {
        const QString detail = normalizedError(m_database.lastError().text(), commitErrorFallback);
        const bool rollbackOk = m_database.rollback();
        if (m_registered) activeTransactionConnections.remove(m_connectionName);
        transactionOwners.remove(m_connectionName);
        m_afterCommit.clear();
        m_afterCommitKeys.clear();
        m_registered = false;
        m_active = false;
        if (errorText) *errorText = detail;
        writeTransactionContextLog(QStringLiteral("commit_failed_rollback"), m_transactionId, m_owner, m_operationId, m_connectionName,
                                   QStringLiteral("%1 rollbackOk=%2").arg(detail, rollbackOk ? QStringLiteral("true") : QStringLiteral("false")));
        return false;
    }

    if (m_registered) activeTransactionConnections.remove(m_connectionName);
    transactionOwners.remove(m_connectionName);
    m_registered = false;
    m_active = false;
    writeTransactionContextLog(QStringLiteral("commit_ok"), m_transactionId, m_owner, m_operationId, m_connectionName);
    const auto actions = std::move(m_afterCommit);
    m_afterCommitKeys.clear();
    for (const auto& action : actions) {
        try { action(); }
        catch (...) { qWarning() << "Post-commit cleanup failed; committed data retained"; }
    }
    return true;
}

bool TransactionContext::rollback(const QString& reason, QString* errorText)
{
    if (!m_active) return true;
    const bool rollbackOk = m_database.rollback();
    if (m_registered) activeTransactionConnections.remove(m_connectionName);
    transactionOwners.remove(m_connectionName);
    m_afterCommit.clear();
    m_afterCommitKeys.clear();
    m_registered = false;
    m_active = false;
    if (!rollbackOk) {
        const QString detail = normalizedError(m_database.lastError().text(), QStringLiteral("SQLite rollback failed"));
        if (errorText) *errorText = detail;
        writeTransactionContextLog(QStringLiteral("rollback_failed"), m_transactionId, m_owner, m_operationId, m_connectionName,
                                   QStringLiteral("%1 reason=%2").arg(detail, reason));
        return false;
    }
    writeTransactionContextLog(QStringLiteral("rollback_ok"), m_transactionId, m_owner, m_operationId, m_connectionName, reason);
    return true;
}

QSqlDatabase& TransactionContext::database()
{
    return m_database;
}

const QString& TransactionContext::connectionName() const
{
    return m_connectionName;
}

const QString& TransactionContext::transactionId() const
{
    return m_transactionId;
}

bool TransactionContext::isActive() const
{
    return m_active;
}

bool TransactionContext::hasActiveTransaction(QSqlDatabase database)
{
    const auto owner = transactionOwners.value(normalizedConnectionName(database), nullptr);
    return database.isValid() && database.isOpen() && owner && owner->isActive();
}

bool TransactionContext::deferUntilCommit(QSqlDatabase database, std::function<void()> action, const QString& key)
{
    auto* owner = transactionOwners.value(normalizedConnectionName(database), nullptr);
    if (!owner || !owner->m_active) return false;
    if (!key.isEmpty() && owner->m_afterCommitKeys.contains(key)) return true;
    if (!key.isEmpty()) owner->m_afterCommitKeys.insert(key);
    owner->m_afterCommit.append(std::move(action));
    return true;
}
