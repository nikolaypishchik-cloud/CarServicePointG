#include "infrastructure/db/sqlite/TransactionRunner.h"
#include "core/diagnostics/RuntimeLoggingControl.h"

#include "infrastructure/db/sqlite/DbContext.h"

#include <QDebug>
#include <QThread>

namespace {
QString txConnectionLabel(const QSqlDatabase& db)
{
    const QString name = db.connectionName().trimmed();
    return name.isEmpty() ? QStringLiteral("<default>") : name;
}
}

namespace TransactionRunner {

Result run(QSqlDatabase db,
           const QString& beginErrorFallback,
           const QString& commitErrorFallback,
           const Step& step)
{
    Result result;
    const QString connectionLabel = txConnectionLabel(db);
    const QString operationId = QStringLiteral("tx_runner_%1").arg(QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId())));
    if (RuntimeLoggingControl::deepDebugEnabled()) qInfo().noquote() << QStringLiteral("DB_TRANSACTION begin_request connection=") << connectionLabel
                      << QStringLiteral(" threadId=") << QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()))
                      << QStringLiteral(" operationId=") << operationId;

    TransactionContext tx(db, QStringLiteral("TransactionRunner"), operationId);
    QString txError;
    if (!tx.begin(beginErrorFallback, &txError)) {
        result.errorText = txError.trimmed().isEmpty() ? beginErrorFallback : txError;
        qCritical().noquote() << QStringLiteral("DB_TRANSACTION begin_failed connection=") << connectionLabel
                              << QStringLiteral(" transactionId=") << tx.transactionId()
                              << QStringLiteral(" error=") << result.errorText;
        return result;
    }

    QString stepError;
    if (!step(&stepError)) {
        QString rollbackError;
        const bool rollbackOk = tx.rollback(QStringLiteral("step_failed"), &rollbackError);
        result.errorText = stepError.trimmed().isEmpty() ? beginErrorFallback : stepError;
        if (!rollbackOk && !rollbackError.trimmed().isEmpty()) {
            result.errorText += QStringLiteral("; rollback: %1").arg(rollbackError);
        }
        qCritical().noquote() << QStringLiteral("DB_TRANSACTION step_failed_rollback connection=") << connectionLabel
                              << QStringLiteral(" transactionId=") << tx.transactionId()
                              << QStringLiteral(" rollbackOk=") << rollbackOk
                              << QStringLiteral(" error=") << result.errorText;
        return result;
    }

    if (!tx.commit(commitErrorFallback, &txError)) {
        result.errorText = txError.trimmed().isEmpty() ? commitErrorFallback : txError;
        qCritical().noquote() << QStringLiteral("DB_TRANSACTION commit_failed connection=") << connectionLabel
                              << QStringLiteral(" transactionId=") << tx.transactionId()
                              << QStringLiteral(" error=") << result.errorText;
        return result;
    }

    if (RuntimeLoggingControl::deepDebugEnabled()) qInfo().noquote() << QStringLiteral("DB_TRANSACTION commit_ok connection=") << connectionLabel
                      << QStringLiteral(" transactionId=") << tx.transactionId();
    result.ok = true;
    return result;
}

} // namespace TransactionRunner
