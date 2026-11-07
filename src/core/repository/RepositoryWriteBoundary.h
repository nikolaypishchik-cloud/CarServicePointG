#pragma once
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QUuid>

// A nested savepoint also preserves the caller's surrounding transaction.
class RepositoryWriteBoundary final {
public:
    RepositoryWriteBoundary(QSqlDatabase database, QString* error)
        : db(database), errorText(error), name("repository_write_" + QUuid::createUuid().toString(QUuid::Id128))
    {
        QSqlQuery q(db);
        active = q.exec("SAVEPOINT " + name);
        if (!active && errorText) *errorText = q.lastError().text();
    }
    RepositoryWriteBoundary(const RepositoryWriteBoundary&) = delete;
    RepositoryWriteBoundary& operator=(const RepositoryWriteBoundary&) = delete;
    ~RepositoryWriteBoundary()
    {
        if (!active) return;
        QSqlQuery q(db);
        if (!q.exec("ROLLBACK TO " + name)) {
            reportRollbackError(q.lastError().text());
            return; // Never release (and possibly commit) after a failed rollback.
        }
        if (!q.exec("RELEASE " + name)) reportRollbackError(q.lastError().text());
    }
    bool started() const { return active; }
    bool finish()
    {
        QSqlQuery q(db);
        if (!q.exec("RELEASE " + name)) {
            if (errorText) *errorText = q.lastError().text();
            return false;
        }
        active = false;
        return true;
    }
private:
    void reportRollbackError(const QString& detail)
    {
        if (errorText) *errorText += " | Repository rollback failed: " + detail;
        qCritical().noquote() << "REPOSITORY_WRITE_ROLLBACK_FAILED" << detail;
    }
    QSqlDatabase db;
    QString* errorText;
    QString name;
    bool active = false;
};
