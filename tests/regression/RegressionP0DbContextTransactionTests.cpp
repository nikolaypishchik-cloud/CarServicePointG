#include <QtTest/QtTest>
#include "../TestSourceAssertions.h"

class RegressionP0DbContextTransactionTests final : public QObject {
    Q_OBJECT
private slots:
    void databaseDbExposesErrorReturningOverloadAndStructuredLogs();
    void workPartsMutationsUseDbContextInsteadOfRawDatabaseDb();
    void transactionRunnerUsesTransactionContextBoundary();
};

void RegressionP0DbContextTransactionTests::databaseDbExposesErrorReturningOverloadAndStructuredLogs()
{
    const QString header = cs::tests::readSourceFile("src/infrastructure/db/sqlite/Db.h");
    const QString dbSupport = cs::tests::readSourceFile("src/infrastructure/db/sqlite/DbArchiveRoutingSupport.cpp");
    const QString dbContextHeader = cs::tests::readSourceFile("src/infrastructure/db/sqlite/DbContext.h");
    const QString dbContextImpl = cs::tests::readSourceFile("src/infrastructure/db/sqlite/DbContext.cpp");

    QVERIFY2(header.contains(QStringLiteral("static QSqlDatabase db(QString* errorText)")),
             "Database::db must expose an error-returning overload for callers that cannot accept silent connection failures.");
    QVERIFY2(dbSupport.contains(QStringLiteral("DB_CONNECTION phase=ensure_failed"))
                 && dbSupport.contains(QStringLiteral("caller_legacy_no_error_channel")),
             "Legacy Database::db() callers must get structured logging instead of silently swallowing connection errors.");
    QVERIFY2(dbContextHeader.contains(QStringLiteral("class DbContext"))
                 && dbContextImpl.contains(QStringLiteral("DB_CONTEXT phase=")),
             "P0 p4 requires an explicit DbContext wrapper around SQLite connection acquisition.");
    QVERIFY2(dbContextImpl.contains(QStringLiteral("SQLite connectionName is empty; refusing implicit Database::db() fallback")),
             "A named active transaction context must not silently fall back to global Database::db().");
}

void RegressionP0DbContextTransactionTests::workPartsMutationsUseDbContextInsteadOfRawDatabaseDb()
{
    const QString mutationService = cs::tests::readSourceFile("src/core/services/orders/WorkPartsMutationService.cpp");

    QVERIFY2(mutationService.contains(QStringLiteral("DbContext::openDefault")),
             "WorkPartsMutationService::execute must open the canonical DB through DbContext.");
    QVERIFY2(mutationService.contains(QStringLiteral("DbContext::fromConnectionName")),
             "Batch save substeps must validate and reuse the active TransactionContext connection name.");
    QVERIFY2(!mutationService.contains(QStringLiteral("MutationExecutor::execute(Database::db()")),
             "WorkParts mutation boundary must not mix raw global Database::db() with named transaction connections.");
    QVERIFY2(mutationService.contains(QStringLiteral("canonical_context_open_failed"))
                 && mutationService.contains(QStringLiteral("active_transaction_context_failed")),
             "Connection acquisition failures must be returned to the caller and logged with structured context.");
}

void RegressionP0DbContextTransactionTests::transactionRunnerUsesTransactionContextBoundary()
{
    const QString txHeader = cs::tests::readSourceFile("src/infrastructure/db/sqlite/DbContext.h");
    const QString txImpl = cs::tests::readSourceFile("src/infrastructure/db/sqlite/DbContext.cpp");
    const QString runner = cs::tests::readSourceFile("src/infrastructure/db/sqlite/TransactionRunner.cpp");

    QVERIFY2(txHeader.contains(QStringLiteral("class TransactionContext"))
                 && txImpl.contains(QStringLiteral("DB_TRANSACTION_CONTEXT phase=")),
             "P0 p4 requires a concrete TransactionContext with structured lifecycle logging.");
    QVERIFY2(runner.contains(QStringLiteral("TransactionContext tx"))
                 && runner.contains(QStringLiteral("tx.begin"))
                 && runner.contains(QStringLiteral("tx.commit"))
                 && runner.contains(QStringLiteral("tx.rollback")),
             "TransactionRunner must use TransactionContext as the single transaction lifecycle boundary.");
    QVERIFY2(txImpl.contains(QStringLiteral("Nested SQLite transaction is prohibited"))
                 && txImpl.contains(QStringLiteral("destructor_rollback_unfinished_transaction")),
             "TransactionContext must keep the reentrancy guard and crash-safe rollback semantics.");
}

QTEST_MAIN(RegressionP0DbContextTransactionTests)
#include "RegressionP0DbContextTransactionTests.moc"
