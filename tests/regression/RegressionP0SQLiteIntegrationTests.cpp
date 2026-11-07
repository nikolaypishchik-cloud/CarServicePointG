#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <QRegularExpression>

#include "TestSourceAssertions.h"
#include "infrastructure/db/sqlite/TransactionRunner.h"

namespace {

QString uniqueConnectionName(const QString& prefix)
{
    return prefix + QLatin1Char('_') + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QSqlDatabase openSqlite(const QString& fileName, const QString& connectionName)
{
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    db.setDatabaseName(fileName);
    if (!db.open()) qFatal("%s", qPrintable(db.lastError().text()));
    QSqlQuery pragma(db);
    if (!pragma.exec(QStringLiteral("PRAGMA foreign_keys=ON"))) qFatal("%s", qPrintable(pragma.lastError().text()));
    if (!pragma.exec(QStringLiteral("PRAGMA busy_timeout=50"))) qFatal("%s", qPrintable(pragma.lastError().text()));
    return db;
}

void execOk(QSqlDatabase db, const QString& sql)
{
    QSqlQuery q(db);
    QVERIFY2(q.exec(sql), qPrintable(q.lastError().text() + QStringLiteral(" SQL=") + sql));
}

int scalarInt(QSqlDatabase db, const QString& sql)
{
    QSqlQuery q(db);
    if (!q.exec(sql)) qFatal("%s", qPrintable(q.lastError().text() + QStringLiteral(" SQL=") + sql));
    if (!q.next()) qFatal("Expected scalar row");
    return q.value(0).toInt();
}

void prepareOrderTables(QSqlDatabase db)
{
    execOk(db, QStringLiteral("CREATE TABLE work_orders(number INTEGER PRIMARY KEY, order_uid TEXT NOT NULL, works_sum REAL DEFAULT 0, parts_sum REAL DEFAULT 0)"));
    execOk(db, QStringLiteral("CREATE TABLE work_order_services(id INTEGER PRIMARY KEY AUTOINCREMENT, order_uid TEXT NOT NULL, title TEXT, amount REAL DEFAULT 0)"));
    execOk(db, QStringLiteral("CREATE TABLE work_order_parts(id INTEGER PRIMARY KEY AUTOINCREMENT, order_uid TEXT NOT NULL, imported INTEGER DEFAULT 0, name TEXT, ordered_at TEXT, delivery_date TEXT, invoice_date TEXT, amount REAL DEFAULT 0)"));
    execOk(db, QStringLiteral("INSERT INTO work_orders(number, order_uid) VALUES(1, 'order-1')"));
}

} // namespace

class RegressionP0SQLiteIntegrationTests final : public QObject {
    Q_OBJECT
private slots:
    void repositorySideEffectsStayOutsidePersistenceLayer();
    void postCommitServiceOwnsCacheAndMutationLogSideEffects();
    void batchSaveWorksAndPartsCommitsAtomically();
    void rollbackOnAttachmentStageFailureLeavesDirtyDataUncommitted();
    void concurrentRefreshDuringSaveKeepsSingleTransactionBoundary();
    void databaseLockedSimulationReportsFailure();
    void repeatedOpenEditSaveImportedPartsKeepsDatesExplicit();
};

void RegressionP0SQLiteIntegrationTests::repositorySideEffectsStayOutsidePersistenceLayer()
{
    const QString partRepo = cs::tests::readSourceFile("src/core/repository/PartRepository.cpp");
    const QString serviceRepo = cs::tests::readSourceFile("src/core/repository/ServiceRepository.cpp");
    const QString partMutation = cs::tests::readSourceFile("src/core/services/orders/PartMutationService.cpp");

    QVERIFY2(!partRepo.contains(QStringLiteral("LegacyMutationLogSupport"))
                 && !serviceRepo.contains(QStringLiteral("LegacyMutationLogSupport")),
             "P0 p6: repositories must not write mutation logs; post-commit application service owns that side-effect.");
    QVERIFY2(!partRepo.contains(QStringLiteral("PartsQueryService::invalidateCache"))
                 && !partMutation.contains(QStringLiteral("PartsQueryService::invalidateCache")),
             "P0 p6: repository/low-level mutation code must not invalidate UI/query cache inside persistence calls.");
}

void RegressionP0SQLiteIntegrationTests::postCommitServiceOwnsCacheAndMutationLogSideEffects()
{
    const QString postCommit = cs::tests::readSourceFile("src/core/services/MutationPostCommitService.cpp");
    const QString executor = cs::tests::readSourceFile("src/core/services/MutationExecutor.cpp");

    QVERIFY2(postCommit.contains(QStringLiteral("runAfterCommit"))
                 && postCommit.contains(QStringLiteral("LegacyMutationLogSupport::logMutation"))
                 && postCommit.contains(QStringLiteral("SnapshotCacheInvalidator::invalidateOrdersAndParts")),
             "P0 p6: cache invalidation and mutation logging must be centralized after successful commit.");
    QVERIFY2(executor.contains(QStringLiteral("if (result.ok)"))
                 && executor.contains(QStringLiteral("MutationPostCommitService::runAfterCommit")),
             "P0 p6: post-commit side-effects must run only after TransactionRunner has committed successfully.");
}

void RegressionP0SQLiteIntegrationTests::batchSaveWorksAndPartsCommitsAtomically()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString conn = uniqueConnectionName(QStringLiteral("p0_batch"));
    QSqlDatabase db = openSqlite(dir.filePath(QStringLiteral("batch.db")), conn);
    prepareOrderTables(db);

    const auto result = TransactionRunner::run(db, QStringLiteral("begin batch"), QStringLiteral("commit batch"), [&](QString* errorText) {
        Q_UNUSED(errorText);
        execOk(db, QStringLiteral("INSERT INTO work_order_services(order_uid, title, amount) VALUES('order-1','Діагностика',500)"));
        execOk(db, QStringLiteral("INSERT INTO work_order_parts(order_uid, imported, name, ordered_at, delivery_date, invoice_date, amount) VALUES('order-1',1,'Фільтр','2026-04-20','','2026-04-20',700)"));
        execOk(db, QStringLiteral("UPDATE work_orders SET works_sum=(SELECT SUM(amount) FROM work_order_services WHERE order_uid='order-1'), parts_sum=(SELECT SUM(amount) FROM work_order_parts WHERE order_uid='order-1') WHERE order_uid='order-1'"));
        return true;
    });

    QVERIFY2(result.ok, qPrintable(result.errorText));
    QCOMPARE(scalarInt(db, QStringLiteral("SELECT COUNT(*) FROM work_order_services")), 1);
    QCOMPARE(scalarInt(db, QStringLiteral("SELECT COUNT(*) FROM work_order_parts")), 1);
    QCOMPARE(scalarInt(db, QStringLiteral("SELECT works_sum + parts_sum FROM work_orders WHERE order_uid='order-1'")), 1200);

    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(conn);
}

void RegressionP0SQLiteIntegrationTests::rollbackOnAttachmentStageFailureLeavesDirtyDataUncommitted()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString conn = uniqueConnectionName(QStringLiteral("p0_rollback"));
    QSqlDatabase db = openSqlite(dir.filePath(QStringLiteral("rollback.db")), conn);
    prepareOrderTables(db);

    QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(QStringLiteral(
        "^DB_TRANSACTION_CONTEXT phase= rollback_ok  transactionId= [0-9a-f-]{36}  owner= TransactionRunner  operationId= tx_runner_[0-9]+  connection= %1  threadId= [0-9]+  error= step_failed$").arg(QRegularExpression::escape(conn))));
    QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(QStringLiteral(
        "^DB_TRANSACTION step_failed_rollback connection= %1  transactionId= [0-9a-f-]{36}  rollbackOk= true  error= attachment_stage_failed$").arg(QRegularExpression::escape(conn))));
    const auto result = TransactionRunner::run(db, QStringLiteral("begin attachment rollback"), QStringLiteral("commit attachment rollback"), [&](QString* errorText) {
        execOk(db, QStringLiteral("INSERT INTO work_order_parts(order_uid, imported, name, amount) VALUES('order-1',1,'Накладна PDF',100)"));
        if (errorText) *errorText = QStringLiteral("attachment_stage_failed");
        return false;
    });

    QVERIFY(!result.ok);
    QVERIFY2(result.errorText.contains(QStringLiteral("attachment_stage_failed")), qPrintable(result.errorText));
    QCOMPARE(scalarInt(db, QStringLiteral("SELECT COUNT(*) FROM work_order_parts")), 0);

    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(conn);
}

void RegressionP0SQLiteIntegrationTests::concurrentRefreshDuringSaveKeepsSingleTransactionBoundary()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath(QStringLiteral("refresh.db"));
    const QString writeConn = uniqueConnectionName(QStringLiteral("p0_refresh_w"));
    const QString readConn = uniqueConnectionName(QStringLiteral("p0_refresh_r"));
    QSqlDatabase writer = openSqlite(dbPath, writeConn);
    QSqlDatabase reader = openSqlite(dbPath, readConn);
    prepareOrderTables(writer);

    int visibleDuringTransaction = -1;
    const auto result = TransactionRunner::run(writer, QStringLiteral("begin refresh-save"), QStringLiteral("commit refresh-save"), [&](QString* errorText) {
        Q_UNUSED(errorText);
        execOk(writer, QStringLiteral("INSERT INTO work_order_parts(order_uid, imported, name, amount) VALUES('order-1',1,'Підшипник',250)"));
        visibleDuringTransaction = scalarInt(reader, QStringLiteral("SELECT COUNT(*) FROM work_order_parts"));
        return true;
    });

    QVERIFY2(result.ok, qPrintable(result.errorText));
    QCOMPARE(visibleDuringTransaction, 0);
    QCOMPARE(scalarInt(reader, QStringLiteral("SELECT COUNT(*) FROM work_order_parts")), 1);

    writer.close();
    reader.close();
    writer = QSqlDatabase();
    reader = QSqlDatabase();
    QSqlDatabase::removeDatabase(writeConn);
    QSqlDatabase::removeDatabase(readConn);
}

void RegressionP0SQLiteIntegrationTests::databaseLockedSimulationReportsFailure()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath(QStringLiteral("locked.db"));
    const QString lockConn = uniqueConnectionName(QStringLiteral("p0_lock_a"));
    const QString saveConn = uniqueConnectionName(QStringLiteral("p0_lock_b"));
    QSqlDatabase locker = openSqlite(dbPath, lockConn);
    QSqlDatabase saver = openSqlite(dbPath, saveConn);
    prepareOrderTables(locker);

    QVERIFY2(locker.transaction(), qPrintable(locker.lastError().text()));
    execOk(locker, QStringLiteral("INSERT INTO work_order_parts(order_uid, imported, name, amount) VALUES('order-1',1,'lock-holder',1)"));

    {
    QSqlQuery q(saver);
    q.prepare(QStringLiteral("INSERT INTO work_order_parts(order_uid, imported, name, amount) VALUES('order-1',1,'blocked-save',2)"));
    const bool ok = q.exec();
    QVERIFY2(!ok, "Second writer must report a SQLite lock instead of silently succeeding during lock simulation.");
    QVERIFY2(q.lastError().text().contains(QStringLiteral("locked"), Qt::CaseInsensitive)
                 || q.lastError().text().contains(QStringLiteral("busy"), Qt::CaseInsensitive),
             qPrintable(q.lastError().text()));
    }

    QVERIFY(locker.rollback());
    QCOMPARE(scalarInt(saver, QStringLiteral("SELECT COUNT(*) FROM work_order_parts")), 0);

    locker.close();
    saver.close();
    locker = QSqlDatabase();
    saver = QSqlDatabase();
    QSqlDatabase::removeDatabase(lockConn);
    QSqlDatabase::removeDatabase(saveConn);
}

void RegressionP0SQLiteIntegrationTests::repeatedOpenEditSaveImportedPartsKeepsDatesExplicit()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString conn = uniqueConnectionName(QStringLiteral("p0_repeated"));
    QSqlDatabase db = openSqlite(dir.filePath(QStringLiteral("repeated.db")), conn);
    prepareOrderTables(db);
    execOk(db, QStringLiteral("INSERT INTO work_order_parts(order_uid, imported, name, ordered_at, delivery_date, invoice_date, amount) VALUES('order-1',1,'Імпортована позиція','','','2026-04-22',100)"));

    for (int i = 0; i < 5; ++i) {
        const auto result = TransactionRunner::run(db, QStringLiteral("begin imported edit"), QStringLiteral("commit imported edit"), [&](QString* errorText) {
            Q_UNUSED(errorText);
            execOk(db, QStringLiteral("UPDATE work_order_parts SET name='Імпортована позиція edited', delivery_date='' WHERE imported=1 AND order_uid='order-1'"));
            return true;
        });
        QVERIFY2(result.ok, qPrintable(result.errorText));
    }

    {
    QSqlQuery q(db);
    QVERIFY(q.exec(QStringLiteral("SELECT ordered_at, delivery_date, invoice_date FROM work_order_parts WHERE imported=1")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(), QString());
    QCOMPARE(q.value(1).toString(), QString());
    QCOMPARE(q.value(2).toString(), QStringLiteral("2026-04-22"));
    }

    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(conn);
}

QTEST_MAIN(RegressionP0SQLiteIntegrationTests)
#include "RegressionP0SQLiteIntegrationTests.moc"
