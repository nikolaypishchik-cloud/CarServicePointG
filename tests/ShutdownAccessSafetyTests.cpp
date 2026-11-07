#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QSettings>
#include <QSqlQuery>
#include <QSqlError>
#include <QSignalSpy>
#include <QDir>
#include <QUuid>
#include <QThread>

#include "app/App.h"
#include "core/cloudsync/CloudSyncWorker.h"
#include "core/cloudsync/CloudSyncClient.h"
#include "core/cloudsync/RemoteMutationApplyService.h"
#include "core/cloudsync/CloudSyncInboxService.h"
#include <QJsonObject>
#include "core/services/AccessControlService.h"
#include "core/services/ActivityAuditService.h"
#include "core/services/StaffAccountService.h"
#include "core/services/orders/OrderDialogCoordinator.h"
#include "core/services/planning/PlanningRepository.h"
#include "TestSourceAssertions.h"
#include "infrastructure/db/sqlite/Db.h"
#include "runtime/RuntimeJobDispatcher.h"

class ShutdownAccessSafetyTests final : public QObject {
    Q_OBJECT
    QTemporaryDir scratch;
    QSqlDatabase db;
    QString testDataRoot;
    void sql(const QString& statement) {
        QSqlQuery q(db);
        QVERIFY2(q.exec(statement), qPrintable(q.lastError().text()));
    }
    void actor(const QString& id, const QString& role) {
        ActivityAuditService::setCurrentActor({id, "Synthetic user", role, "test-session"});
    }
private slots:
    void initTestCase() {
        QVERIFY(scratch.isValid());
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("CarServiceStage2Tests");
        QCoreApplication::setApplicationName("Safety-" + QUuid::createUuid().toString(QUuid::Id128));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, scratch.path());
        testDataRoot = App::dataDir();
        QString runtimeError;
        QVERIFY2(Database::prepareRuntime(&runtimeError), qPrintable(runtimeError));
        QVERIFY2(Database::openPrepared(&runtimeError), qPrintable(runtimeError));
        db = Database::db();
        QVERIFY(Database::ensureSchema());
        QString error;
        QVERIFY2(StaffAccountService::ensureSchema(db, &error), qPrintable(error));
        QVERIFY2(AccessControlService::ensureSchema(db, &error), qPrintable(error));
        QVERIFY(Database::setSetting("settings.cloudsync.enabled", "0"));
        QVERIFY(Database::setSetting("cloudsync.enabled", "0"));
        QVERIFY(Database::setSetting("settings.cloudsync.api_token", ""));
        sql("INSERT INTO masters(id,name) VALUES('s2-m1','Test one'),('s2-m2','Test two')");
        sql("INSERT INTO staff_accounts(id,master_id,display_name,role,active) VALUES"
            "('s2-owner','','Owner','owner',1),('s2-master','s2-m1','Master','master',1)");
        sql("INSERT INTO work_orders(number,order_number,order_year,order_uid,opened_at,status,plate,plate_norm,car,client,master_id) VALUES"
            "(1,1,2026,'s2-order-1','02.10.26 10:00:00','open','AA 0001 AA','aa0001aa','Test','Test','s2-m1'),"
            "(2,2,2026,'s2-order-2','02.10.26 11:00:00','open','AA 0002 AA','aa0002aa','Test','Test','s2-m2')");
    }
    void init() {
        RuntimeJob::RuntimeJobDispatcher::instance().leaveShutdown();
        sql("UPDATE staff_accounts SET active=1,role='owner',master_id='' WHERE id='s2-owner'");
        sql("UPDATE staff_accounts SET active=1,role='master',master_id='s2-m1' WHERE id='s2-master'");
        sql("UPDATE access_roles SET active=1 WHERE id IN ('owner','master')");
        sql("UPDATE access_role_permissions SET granted=1 WHERE role_id='master' AND permission_key='nav.planning'");
        ActivityAuditService::clearCurrentActor();
    }
    void activeRolesAndOwnOrdersRemainAvailable() {
        actor("s2-owner", "owner");
        QVERIFY(AccessControlService::currentUserHasPermission(db, "access.manage"));
        QVERIFY(AccessControlService::canAccessOrder(db, 2));
        QVERIFY(!AccessControlService::currentOrderScope(db).restricted);
        actor("s2-master", "master");
        QVERIFY(AccessControlService::currentUserHasPermission(db, "orders.edit"));
        QVERIFY(!AccessControlService::currentUserHasPermission(db, "access.manage"));
        QVERIFY(AccessControlService::canAccessOrder(db, 1));
        QVERIFY(!AccessControlService::canAccessOrder(db, 2));
        const auto scope = AccessControlService::currentOrderScope(db);
        QVERIFY(scope.contains("s2-order-1", 1));
        QVERIFY(!scope.contains("s2-order-2", 2));
    }
    void inactiveAndDeletedAccountsFailClosed() {
        actor("s2-owner", "owner");
        sql("UPDATE staff_accounts SET active=0 WHERE id='s2-owner'");
        QVERIFY(!AccessControlService::currentUserHasPermission(db, "access.manage"));
        QVERIFY(!AccessControlService::canAccessOrder(db, 1));
        QVERIFY(!AccessControlService::currentOrderScope(db).contains("s2-order-1", 1));
        QCOMPARE(AccessControlService::currentOrderPredicate(db, "o"), QString("0=1"));
        actor("nonexistent", "owner");
        QVERIFY(!AccessControlService::currentUserHasPermission(db, "access.manage"));
    }
    void staleRoleDoesNotRetainOwnerAccess() {
        actor("s2-owner", "owner");
        sql("UPDATE staff_accounts SET role='master',master_id='s2-m2' WHERE id='s2-owner'");
        QVERIFY(!AccessControlService::currentUserHasPermission(db, "access.manage"));
        QVERIFY(!AccessControlService::canAccessOrder(db, 2));
        QVERIFY(!AccessControlService::currentOrderScope(db).contains("s2-order-2", 2));
        actor("s2-owner", "master");
        QVERIFY(AccessControlService::canAccessOrder(db, 2));
        QVERIFY(!AccessControlService::canAccessOrder(db, 1));
    }
    void scopeKeysInvalidatePendingResultsWhenAccessChanges() {
        actor("s2-master", "master");
        const QString original = AccessControlService::currentScopeKey();
        sql("UPDATE staff_accounts SET master_id='s2-m2' WHERE id='s2-master'");
        const QString reassigned = AccessControlService::currentScopeKey();
        QVERIFY(original != reassigned);
        QVERIFY(!AccessControlService::canAccessOrder(db, 1));
        QVERIFY(AccessControlService::canAccessOrder(db, 2));
        sql("UPDATE staff_accounts SET active=0 WHERE id='s2-master'");
        QVERIFY(reassigned != AccessControlService::currentScopeKey());
    }
    void anonymousAndSqlErrorsFailClosed() {
        QVERIFY(!AccessControlService::currentUserHasPermission(db, "orders.edit"));
        QVERIFY(!AccessControlService::canAccessOrder(db, 1));
        actor("s2-owner", "owner");
        QSqlDatabase invalid;
        QVERIFY(!AccessControlService::currentUserHasPermission(invalid, "access.manage"));
        QVERIFY(!AccessControlService::canAccessOrder(invalid, 1));
    }
    void disabledRolesCannotGrantPermissions() {
        actor("s2-owner", "owner");
        sql("UPDATE access_roles SET active=0 WHERE id='owner'");
        QVERIFY(!AccessControlService::currentUserHasPermission(db, "access.manage"));
    }
    void deniedOrderWritesDoNotChangeBusinessData() {
        OrderWorkspaceInput input;
        input.number = 2;
        input.masterId = "s2-m1";
        input.client = "Must not be stored";
        actor("s2-master", "master");
        auto result = OrderDialogCoordinator::saveEditableOrder(input, 2);
        QVERIFY(!result.ok); QVERIFY(!result.error.isEmpty());
        input.number = 1; input.masterId = "s2-m2";
        QVERIFY(!OrderDialogCoordinator::saveEditableOrder(input, 1).ok);
        QVERIFY(!OrderDialogCoordinator::createDraftEditableOrder(input, 3).ok);
        input.masterId = "s2-m1"; input.status = "closed";
        QVERIFY(!OrderDialogCoordinator::saveEditableOrder(input, 1).ok);
        actor("s2-owner", "owner");
        sql("UPDATE staff_accounts SET active=0 WHERE id='s2-owner'");
        QVERIFY(!OrderDialogCoordinator::saveEditableOrder(input, 1).ok);
        QSqlQuery query(db);
        QVERIFY(query.exec("SELECT COUNT(*) FROM work_orders WHERE client='Must not be stored' OR status='closed'"));
        QVERIFY(query.next()); QCOMPARE(query.value(0).toInt(), 0);
        QVERIFY(query.exec("SELECT COUNT(*) FROM work_orders"));
        QVERIFY(query.next()); QCOMPARE(query.value(0).toInt(), 2);
    }
    void authorizationDoesNotInitializeSchema() {
        {
            auto empty = QSqlDatabase::addDatabase("QSQLITE", "stage2_empty");
            empty.setDatabaseName(":memory:");
            QVERIFY(empty.open());
            QVERIFY(!AccessControlService::hasPermission(empty, "owner", "access.manage"));
            QVERIFY(empty.tables().isEmpty());
            empty.close();
        }
        QSqlDatabase::removeDatabase("stage2_empty");
    }
    void revokedGrantsInvalidatePendingResults() {
        actor("s2-master", "master");
        const QString before = AccessControlService::currentScopeKey();
        QVERIFY(AccessControlService::currentUserHasPermission(db, "nav.planning"));
        sql("UPDATE access_role_permissions SET granted=0 WHERE role_id='master' AND permission_key='nav.planning'");
        QVERIFY(!AccessControlService::currentUserHasPermission(db, "nav.planning"));
        QVERIFY(before != AccessControlService::currentScopeKey());
    }
    void revokedPlanningAccessHidesDataAndRejectsWrites() {
        sql("CREATE TABLE IF NOT EXISTS planning_calendar(day TEXT PRIMARY KEY,note TEXT)");
        sql("INSERT OR REPLACE INTO planning_calendar(day,note) VALUES('02.10.26','10:00 Synthetic appointment')");
        actor("s2-owner", "owner");
        PlanningRepository repository;
        const QDate day(2026, 10, 2);
        QVERIFY(!repository.visibleDayMap(day).isEmpty());
        sql("UPDATE staff_accounts SET active=0 WHERE id='s2-owner'");
        QVERIFY(repository.visibleDayMap(day).isEmpty());
        QVERIFY(repository.visibleMonthMaps(2026, 10).isEmpty());
        repository.setVisibleDayMap(day, {});
        QVERIFY(!repository.getDayMap(day).isEmpty());
    }
    void navigationGuardsRemainWired() {
        cs::tests::requireContains("src/ui/main/MainWindowAccessControl.cpp", "case 2: return QStringLiteral(\"nav.settings\")");
        cs::tests::requireContains("src/ui/main/MainWindowAccessControl.cpp", "if (permissionKey.isEmpty()) return false");
        cs::tests::requireContains("src/ui/main/MainWindowAccessControl.cpp", "m_stack->setVisible(false)");
        cs::tests::requireContains("src/ui/main/MainWindow.cpp", "refreshAccessControl();");
    }
    void shutdownRejectsNewAndManualSyncBeforeDatabaseAccess() {
        auto& dispatcher = RuntimeJob::RuntimeJobDispatcher::instance();
        dispatcher.enterShutdown("stage2-test");
        CloudSyncWorker worker;
        QSignalSpy started(&worker, &CloudSyncWorker::syncStarted);
        worker.start(); worker.syncNow();
        QCOMPARE(started.count(), 0);
        QString error;
        QVERIFY(!CloudSyncClient::syncNow(&error));
        QVERIFY(!error.isEmpty());
        QVERIFY(worker.stopAndWait(100));
    }
    void cancelledInboxRemainsQueuedAndCanBeResumed() {
        QString error;
        const QJsonObject event{{"event_uid", "s2-interrupted"}, {"source_device_uid", "synthetic-device"},
                               {"entity_type", "schema"}, {"entity_uid", "synthetic-schema"},
                               {"operation", "update"}, {"payload_source", "mutation_metadata_only"},
                               {"payload", QJsonObject{}}};
        QVERIFY2(CloudSyncInboxService::enqueueEvent(db, event, &error), qPrintable(error));
        auto& dispatcher = RuntimeJob::RuntimeJobDispatcher::instance();
        dispatcher.enterShutdown("stage2-inbox");
        const auto cancelled = RemoteMutationApplyService::applyPendingEvents(db, 10, &error);
        QCOMPARE(cancelled.applied, 0); QCOMPARE(cancelled.failed, 0);
        QVERIFY(!error.isEmpty());
        QSqlQuery query(db);
        QVERIFY(query.exec("SELECT status FROM sync_inbox WHERE event_uid='s2-interrupted'"));
        QVERIFY(query.next()); QCOMPARE(query.value(0).toString(), QString("pending"));
        query.finish();
        dispatcher.leaveShutdown(); error.clear();
        const auto resumed = RemoteMutationApplyService::applyPendingEvents(db, 10, &error);
        QCOMPARE(resumed.applied, 1); QCOMPARE(resumed.failed, 0);
        QVERIFY(query.exec("SELECT status FROM sync_inbox WHERE event_uid='s2-interrupted'"));
        QVERIFY(query.next()); QCOMPARE(query.value(0).toString(), QString("ignored_metadata"));
    }
    void stoppedWorkerDoesNotRunDelayedRequests() {
        CloudSyncWorker worker;
        QSignalSpy started(&worker, &CloudSyncWorker::syncStarted);
        worker.start(); worker.stop(); worker.syncNow();
        QTest::qWait(1100);
        QCOMPARE(started.count(), 0);
        QVERIFY(worker.stopAndWait(100));
    }
    void cancellationDrainsThreadAndDropsLateUiCallbacks() {
        CloudSyncWorker worker;
        QSignalSpy started(&worker, &CloudSyncWorker::syncStarted);
        QSignalSpy finished(&worker, &CloudSyncWorker::syncFinished);
        QSignalSpy received(&worker, &CloudSyncWorker::dataReceived);
        worker.syncNow(); worker.syncNow();
        QCOMPARE(started.count(), 1);
        QVERIFY(worker.stopAndWait(5000));
        QTest::qWait(100);
        QCOMPARE(finished.count(), 0);
        QCOMPARE(received.count(), 0);
        worker.syncNow();
        QCOMPARE(started.count(), 1);
    }
    void destroyingWorkerDrainsThread() {
        auto* worker = new CloudSyncWorker;
        worker->syncNow();
        delete worker;
        QTest::qWait(100);
    }
    void cleanupTestCase() {
        RuntimeJob::RuntimeJobDispatcher::instance().leaveShutdown();
        ActivityAuditService::clearCurrentActor();
        db.close(); db = {}; QSqlDatabase::removeDatabase("carservice_main");
        for (const auto& name : QSqlDatabase::connectionNames()) {
            { auto connection = QSqlDatabase::database(name, false); connection.close(); }
            QSqlDatabase::removeDatabase(name);
        }
        const QString root = QFileInfo(qEnvironmentVariable("CS_TEST_DATA_ROOT")).canonicalFilePath();
        QVERIFY(!root.isEmpty());
        QVERIFY(QFileInfo(testDataRoot).canonicalFilePath().startsWith(root + "/")
                && QFileInfo(testDataRoot).fileName().startsWith("Safety-"));
        QVERIFY(QDir(testDataRoot).removeRecursively());
    }
};

QTEST_MAIN(ShutdownAccessSafetyTests)
#include "ShutdownAccessSafetyTests.moc"
