#pragma once

#include <QSqlDatabase>
#include <QSet>
#include <QString>
#include <functional>
#include <QList>

class DbContext
{
public:
    struct Result {
        bool ok = false;
        QSqlDatabase db;
        QString connectionName;
        QString operation;
        QString contextId;
        QString errorText;
    };

    static Result openDefault(const QString& operation, QString* errorText = nullptr);
    static Result fromConnectionName(const QString& connectionName,
                                     const QString& operation,
                                     QString* errorText = nullptr);
};

class TransactionContext
{
public:
    TransactionContext(QSqlDatabase database,
                       QString owner,
                       QString operationId = QString());
    ~TransactionContext();

    bool begin(const QString& beginErrorFallback, QString* errorText = nullptr);
    bool commit(const QString& commitErrorFallback, QString* errorText = nullptr);
    bool rollback(const QString& reason, QString* errorText = nullptr);

    QSqlDatabase& database();
    const QString& connectionName() const;
    const QString& transactionId() const;
    bool isActive() const;
    static bool hasActiveTransaction(QSqlDatabase database);
    static bool deferUntilCommit(QSqlDatabase database, std::function<void()> action, const QString& key = QString());

private:
    QSqlDatabase m_database;
    QString m_owner;
    QString m_operationId;
    QString m_transactionId;
    QString m_connectionName;
    bool m_active = false;
    bool m_registered = false;
    QList<std::function<void()>> m_afterCommit;
    QSet<QString> m_afterCommitKeys;
};
