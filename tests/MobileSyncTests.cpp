#include <QtTest/QtTest>
#include <QSqlQuery>
#include <QSqlError>
#include "core/cloudsync/WorkOrderSyncApplyService.h"
#include "core/cloudsync/CloudSyncInboxService.h"
#include "core/cloudsync/RemoteMutationApplyService.h"
#include "core/services/orders/OrderStatusService.h"
#include "core/cloudsync/CloudSyncCredentials.h"
#include "core/cloudsync/CloudSyncSettings.h"
#include "core/cloudsync/SyncEventPolicy.h"
#include "core/cloudsync/SyncBatchIsolation.h"
#include "core/cloudsync/CloudSyncOutboxService.h"
#include "core/cloudsync/CloudSyncSchemaService.h"
#include "core/cloudsync/ConflictResolverService.h"
#include "core/backup/BackupSnapshotService.h"
#include "core/backup/BackupPaths.h"
#include "core/util/OrderClosureMarker.h"
#include "infrastructure/db/sqlite/DbSettingsCacheSupport.h"
#include <QTemporaryDir>
#include <QFile>
#include <QRegularExpression>

class MobileSyncTests : public QObject {
    Q_OBJECT
    QSqlDatabase db;
    // These are required diagnostics of deliberate rollback scenarios, not a log filter.
    // QtTest fails when either expected message is absent; other critical errors remain visible.
    void expectRollback(const QString& reason) {
        QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(
            "^DB_TRANSACTION_CONTEXT phase= rollback_ok  transactionId= [0-9a-f-]{36}  owner= TransactionRunner  operationId= tx_runner_[0-9]+  connection= carservice_main  threadId= [0-9]+  error= step_failed$"));
        QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(
            "^DB_TRANSACTION step_failed_rollback connection= carservice_main  transactionId= [0-9a-f-]{36}  rollbackOk= true  error= "
            + QRegularExpression::escape(reason) + "$"));
    }
private slots:
    void mobileOrderClosureReachesDesktopWithMarkerAndPreservesServices() {
        QSqlQuery schema(db);
        QVERIFY(schema.exec("ALTER TABLE work_orders ADD COLUMN mobile_state_json TEXT NOT NULL DEFAULT '{}'"));
        QVERIFY(schema.exec("CREATE TABLE settings(key TEXT PRIMARY KEY,value TEXT)"));
        QString error;
        const QString closedAt = QStringLiteral("2026-10-02T21:30:00+03:00");
        const QJsonObject workflow{{"ready_completed_at",closedAt},{"ready_completed_by","m1"},
            {"ready_completed_role","master"},{"ready_completed_source","mobile"}};
        const QJsonObject event{{"event_uid","close-mobile-test"},{"source_device_uid","mobile-panel"},
            {"entity_type","work_order"},{"entity_uid","parent-A"},{"operation","update"},
            {"payload",QJsonObject{{"order_uid","parent-A"},{"number",19},{"status","completed"},
                {"closed_at",closedAt},{"workflow",workflow}}}};
        QVERIFY2(CloudSyncInboxService::enqueueEvent(db,event,&error),qPrintable(error));
        auto result = RemoteMutationApplyService::applyPendingEvents(db,100,&error);
        QCOMPARE(result.applied,1); QCOMPARE(result.conflicts,0);
        QSqlQuery q(db);
        QVERIFY(q.exec("SELECT status,closed_at,mobile_state_json FROM work_orders WHERE order_uid='parent-A'"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(),QStringLiteral("completed"));
        const QString state=q.value(2).toString();
        QCOMPARE(OrderClosureMarker::label(q.value(0).toString(),q.value(1).toString(),state),QString::fromUtf8("Закрито майстром"));
        QCOMPARE(OrderClosureMarker::label("open",closedAt,state),QString());
        QCOMPARE(OrderClosureMarker::label("closed","2026-10-03T21:30:00+03:00",state),QString());
        QCOMPARE(OrderClosureMarker::label("closed",closedAt,"bad json"),QString());
        auto ownerWorkflow=workflow; ownerWorkflow["ready_completed_role"]="owner";
        QCOMPARE(OrderClosureMarker::label("closed",closedAt,QString::fromUtf8(QJsonDocument(QJsonObject{{"workflow",ownerWorkflow}}).toJson())),QString());
        QVERIFY(q.exec("SELECT title,status FROM work_order_services WHERE service_uid='service-A'"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QStringLiteral("Oil"));
        QCOMPARE(q.value(1).toString(),QStringLiteral("open"));
        QVERIFY2(CloudSyncInboxService::enqueueEvent(db,event,&error),qPrintable(error));
        result=RemoteMutationApplyService::applyPendingEvents(db,100,&error);
        QCOMPARE(result.applied,0);
    }
    void completeOrderMembershipIncludesEmptyArraysAndMoreThan200Rows() {
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE settings(key TEXT PRIMARY KEY,value TEXT)"));
        QVERIFY(q.exec("INSERT INTO settings VALUES('settings.cloudsync.capture_outbox_while_disabled','1')"));
        QVERIFY(q.exec("CREATE TABLE work_order_parts(id INTEGER PRIMARY KEY,part_uid TEXT,order_uid TEXT)"));
        QVERIFY(q.exec("DROP TABLE work_order_services"));
        QVERIFY(q.exec("CREATE TABLE work_order_services(id INTEGER PRIMARY KEY,service_uid TEXT,order_uid TEXT,qty REAL,price REAL)"));
        QVERIFY(q.exec("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE x<201) INSERT INTO work_order_services SELECT x,'svc-'||x,'parent-A',1,10 FROM n"));
        QString error;QVERIFY2(CloudSyncSchemaService::ensureSchema(db,&error),qPrintable(error));
        MutationResult mutation;mutation.ok=true;mutation.sourceModule="Test";mutation.entityType="work_order";mutation.entityId="parent-A";mutation.operation="upsert";mutation.changedTables={"work_orders"};
        QVERIFY2(CloudSyncOutboxService::appendMutation(db,mutation,&error),qPrintable(error));
        QVERIFY(q.exec("SELECT json_extract(payload_json,'$.related.complete_order_rows_v1'),json_array_length(payload_json,'$.related.services'),json_array_length(payload_json,'$.related.parts') FROM sync_outbox ORDER BY rowid DESC LIMIT 1"));
        QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),1);QCOMPARE(q.value(1).toInt(),201);QCOMPARE(q.value(2).toInt(),0);
        QVERIFY(q.exec("DELETE FROM work_order_services"));
        QVERIFY2(CloudSyncOutboxService::appendMutation(db,mutation,&error),qPrintable(error));
        QVERIFY(q.exec("SELECT json_extract(payload_json,'$.related.complete_order_rows_v1'),json_array_length(payload_json,'$.related.services') FROM sync_outbox ORDER BY rowid DESC LIMIT 1"));
        QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),1);QCOMPARE(q.value(1).toInt(),0);
    }
    void purchasePaymentSnapshotsIncludePartialPaymentsAndDueDate() {
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE settings(key TEXT PRIMARY KEY,value TEXT)"));
        QVERIFY(q.exec("INSERT INTO settings VALUES('settings.cloudsync.capture_outbox_while_disabled','1')"));
        QVERIFY(q.exec("CREATE TABLE work_order_parts(id INTEGER PRIMARY KEY,part_uid TEXT,order_uid TEXT,qty REAL,purchase_price REAL,purchase_amount REAL,paid INTEGER)"));
        QVERIFY(q.exec("INSERT INTO work_order_parts VALUES(1,'part-payment','parent-A',3,125.5,0,0)"));
        QVERIFY(q.exec("ALTER TABLE work_order_parts ADD COLUMN price REAL DEFAULT 555"));
        QVERIFY(q.exec("CREATE TABLE purchase_payment_balances(payment_key TEXT PRIMARY KEY,paid_cents INTEGER)"));
        QVERIFY(q.exec("CREATE TABLE purchase_payment_terms(payment_key TEXT PRIMARY KEY,due_date TEXT)"));
        QVERIFY(q.exec("INSERT INTO purchase_payment_balances VALUES('part:1:parent-A',10000)"));
        QVERIFY(q.exec("INSERT INTO purchase_payment_terms VALUES('part:1:parent-A','30.09.26')"));
        QString error;
        QVERIFY2(CloudSyncSchemaService::ensureSchema(db,&error),qPrintable(error));
        for(int i=0;i<2;++i) QVERIFY2(CloudSyncOutboxService::enqueuePurchasePaymentSnapshots(db,&error),qPrintable(error));
        QVERIFY(q.exec("SELECT entity_type,entity_uid,json_extract(payload_json,'$.payload.purchase_payment.total_cents'),json_extract(payload_json,'$.payload.purchase_payment.paid_cents'),json_extract(payload_json,'$.payload.purchase_payment.due_date'),json_extract(payload_json,'$.payload.price') FROM sync_outbox WHERE json_extract(payload_json,'$.source_module')='PurchasePaymentSync'"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString("work_order_part"));
        QCOMPARE(q.value(1).toString(),QString("part-payment"));
        QCOMPARE(q.value(2).toLongLong(),37650); QCOMPARE(q.value(3).toLongLong(),10000);
        QCOMPARE(q.value(4).toString(),QString("2026-09-30")); QCOMPARE(q.value(5).toDouble(),555.0); QVERIFY(!q.next());
        QVERIFY(q.exec("UPDATE purchase_payment_balances SET paid_cents=37650"));
        QVERIFY2(CloudSyncOutboxService::enqueuePurchasePaymentSnapshots(db,&error),qPrintable(error));
        QVERIFY(q.exec("SELECT count(*) FROM sync_outbox WHERE json_extract(payload_json,'$.source_module')='PurchasePaymentSync'"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),2);
    }
    void childIdDoesNotSelectUnrelatedOrderNumber() {
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE settings(key TEXT PRIMARY KEY,value TEXT)"));
        QVERIFY(q.exec("INSERT INTO settings VALUES('settings.cloudsync.capture_outbox_while_disabled','1')"));
        QVERIFY(q.exec("CREATE TABLE work_order_parts(id INTEGER PRIMARY KEY,part_uid TEXT,order_uid TEXT,order_number INTEGER,title TEXT,qty REAL)"));
        QVERIFY(q.exec("INSERT INTO work_order_parts VALUES(20,'part-collision','parent-A',19,'Oil',5)"));
        QString error;
        QVERIFY2(CloudSyncSchemaService::ensureSchema(db,&error),qPrintable(error));
        MutationResult mutation;
        mutation.ok=true; mutation.entityType="work_order_part"; mutation.entityId="20";
        mutation.operation="update"; mutation.changedTables={"work_order_parts","work_orders"};
        QVERIFY2(CloudSyncOutboxService::appendMutation(db,mutation,&error),qPrintable(error));
        QVERIFY(q.exec("SELECT entity_uid,json_extract(payload_json,'$.related.parts[0].part_uid') FROM sync_outbox"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString("parent-A"));
        QCOMPARE(q.value(1).toString(),QString("part-collision"));
        QVERIFY(!q.next());
    }
    void appliedChangesAreReportedAlongsideConflicts() {
        QSqlQuery q(db); QVERIFY(q.exec("CREATE TABLE settings(key TEXT PRIMARY KEY,value TEXT)"));
        QString error;
        QVERIFY(CloudSyncInboxService::enqueueEvent(db, {
            {"event_uid","bad-before-good"},{"source_device_uid","other"},
            {"entity_type","unknown_business"},{"entity_uid","unknown"},{"operation","upsert"},
            {"payload",QJsonObject{{"name","Keep"}}}},&error));
        QVERIFY(CloudSyncInboxService::enqueueEvent(db, {
            {"event_uid","completion-after-conflict"},{"source_device_uid","mobile-panel"},
            {"entity_type","work_order_service"},{"entity_uid","service-A"},{"operation","update"},
            {"payload",QJsonObject{{"order_uid","parent-A"},{"status","completed"}}}},&error));
        const auto stats=RemoteMutationApplyService::applyPendingEvents(db,100,&error);
        QCOMPARE(stats.conflicts,1); QCOMPARE(stats.applied,1);
        QVERIFY(q.exec("SELECT status FROM work_order_services WHERE service_uid='service-A'"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString("completed"));
    }
    void conflictDoesNotBlockOtherBatchEvents() {
        QJsonArray accepted, rejected;
        QVERIFY(!SyncBatchIsolation::deliver(QJsonArray{1,2,3,4,5}, [&](const QJsonArray& batch) {
            if (batch.contains(3)) return 409;
            for (const auto& item:batch) accepted.append(item);
            return 200;
        }, [&](const QJsonArray& batch) { for (const auto& item:batch) rejected.append(item); }));
        QCOMPARE(accepted,QJsonArray({1,2,4,5})); QCOMPARE(rejected,QJsonArray({3}));
        int calls=0;
        QVERIFY(!SyncBatchIsolation::deliver(QJsonArray{1,2,3}, [&](const QJsonArray&) { ++calls; return 401; },
            [](const QJsonArray&) {}));
        QCOMPARE(calls,1);
    }
    void metadataPolicyNeverDiscardsBusinessPayload() {
        QJsonObject event{{"payload_source","mutation_metadata_only"},{"payload",QJsonObject{}}};
        QVERIFY(SyncEventPolicy::isLocalMetadata("schema",event));
        QVERIFY(!SyncEventPolicy::isLocalMetadata("work_order",event));
        QVERIFY(!SyncEventPolicy::isLocalMetadata("unknown_business",event));
        event.insert("payload",QJsonObject{{"name","Oil"}});
        QVERIFY(!SyncEventPolicy::isLocalMetadata("order",event));
        event.insert("payload",QJsonObject{});
        event.insert("related",QJsonObject{{"parts",QJsonArray{QJsonObject{{"part_uid","part-1"}}}}});
        QVERIFY(!SyncEventPolicy::isLocalMetadata("order",event));
    }
    void legacyDraftAndAdvisorMetadataAreRetainedWithoutApplying() {
        QSqlQuery q(db); QVERIFY(q.exec("CREATE TABLE settings(key TEXT PRIMARY KEY,value TEXT)"));
        QString error;
        QJsonObject draft{{"event_uid","empty-draft"},{"source_device_uid","other"},
            {"entity_type","work_order"},{"entity_uid","-1"},{"operation","upsert"},
            {"source_module","OrderWorkspaceService"},{"payload_complete",false},
            {"payload_source","mutation_metadata_only"},{"payload",QJsonObject{}},{"related",QJsonObject{}}};
        QVERIFY(SyncEventPolicy::isLocalMetadata("work_order",draft));
        auto business=draft; business.insert("entity_uid","real-order");
        QVERIFY(!SyncEventPolicy::isLocalMetadata("work_order",business));
        business=draft; business.insert("payload",QJsonObject{{"opened_at","2026-10-02"}});
        QVERIFY(!SyncEventPolicy::isLocalMetadata("work_order",business));
        business=draft; business.insert("data",QJsonObject{{"number",12}});
        QVERIFY(!SyncEventPolicy::isLocalMetadata("work_order",business));
        business=draft; business.insert("related",QJsonObject{{"parts",QJsonArray{QJsonObject{{"name","Oil"}}}}});
        QVERIFY(!SyncEventPolicy::isLocalMetadata("work_order",business));
        QVERIFY(CloudSyncInboxService::enqueueEvent(db,draft,&error));
        auto advisor=draft;
        advisor.insert("event_uid","advisor-audit"); advisor.insert("entity_type","repair_ai_approved_parts");
        advisor.insert("entity_uid","parent-A");
        QVERIFY(CloudSyncInboxService::enqueueEvent(db,advisor,&error));
        QVERIFY(CloudSyncInboxService::markStatus(db,"empty-draft","failed","Old SQL failure",&error));
        QString conflict;
        QVERIFY(ConflictResolverService::recordConflict(db,"repair_ai_approved_parts","parent-A",{},advisor,
            "unsupported_entity_type",&conflict,&error));
        QVERIFY(CloudSyncOutboxService::retireLegacyMetadata(db,&error));
        QVERIFY(CloudSyncOutboxService::retireLegacyMetadata(db,&error));
        QCOMPARE(ConflictResolverService::pendingCount(db),0);
        QVERIFY(q.exec("SELECT count(*) FROM sync_inbox WHERE status='ignored_metadata'"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),2);
        QVERIFY(q.exec("SELECT count(*) FROM work_orders")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),2);
    }
    void missingParentErrorIsActionableAndOtherEventsContinue() {
        QSqlQuery q(db); QVERIFY(q.exec("CREATE TABLE settings(key TEXT PRIMARY KEY,value TEXT)"));
        QString error;
        for(const auto& parent: {QString("missing-parent"),QString("parent-A")}) {
            QVERIFY(CloudSyncInboxService::enqueueEvent(db,{
                {"event_uid",parent},{"source_device_uid","mobile-panel"},
                {"entity_type","work_order_service"},{"entity_uid",parent=="parent-A" ? "service-A" : "orphan-service"},
                {"operation","update"},{"payload",QJsonObject{{"order_uid",parent},{"status","completed"}}}},&error));
        }
        expectRollback(QString::fromUtf8("Наряд для роботи ще не синхронізовано."));
        const auto stats=RemoteMutationApplyService::applyPendingEvents(db,100,&error);
        QCOMPARE(stats.failed,1); QCOMPARE(stats.applied,1);
        QVERIFY(error.contains("orphan-service"));
        QVERIFY(error.contains(QString::fromUtf8("Наряд для роботи ще не синхронізовано")));
    }
    void metadataIsRetainedAndAcknowledgedWithoutConflict() {
        QSqlQuery q(db); QVERIFY(q.exec("CREATE TABLE settings(key TEXT PRIMARY KEY,value TEXT)"));
        QString error;
        QVERIFY(CloudSyncInboxService::enqueueEvent(db, {
            {"event_uid","metadata-test"},{"source_device_uid","old-desktop"},
            {"entity_type","schema"},{"entity_uid","catalog"},{"operation","rebuild"},
            {"payload_source","mutation_metadata_only"},{"payload",QJsonObject{}}},&error));
        const auto stats=RemoteMutationApplyService::applyPendingEvents(db,100,&error);
        QCOMPARE(stats.applied,1); QCOMPARE(stats.conflicts,0);
        const auto ready=CloudSyncInboxService::unackedAppliedEvents(db,100,&error);
        QCOMPARE(ready.size(),1); QCOMPARE(ready[0].status,QString("ignored_metadata"));
        QVERIFY2(CloudSyncOutboxService::retireLegacyMetadata(db,&error),qPrintable(error));
        QCOMPARE(CloudSyncInboxService::unackedAppliedEvents(db,100,&error).size(),1);
    }
    void unsupportedBusinessEventDoesNotStayApplying() {
        QSqlQuery q(db); QVERIFY(q.exec("CREATE TABLE settings(key TEXT PRIMARY KEY,value TEXT)"));
        QString error;
        QVERIFY(CloudSyncInboxService::enqueueEvent(db, {
            {"event_uid","unknown-test"},{"source_device_uid","other"},
            {"entity_type","unknown_business"},{"entity_uid","item-1"},{"operation","upsert"},
            {"payload",QJsonObject{{"name","Retain this"}}}},&error));
        const auto stats=RemoteMutationApplyService::applyPendingEvents(db,100,&error);
        QCOMPARE(stats.conflicts,1);
        QVERIFY(q.exec("SELECT status FROM sync_inbox")); QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(),QString("conflict"));
    }
    void orderNumberCollisionDoesNotOverwriteOrLoop() {
        QSqlQuery q(db); QVERIFY(q.exec("CREATE TABLE settings(key TEXT PRIMARY KEY,value TEXT)"));
        QString error;
        QVERIFY(CloudSyncInboxService::enqueueEvent(db, {
            {"event_uid","collision-test"},{"source_device_uid","other"},
            {"entity_type","work_order"},{"entity_uid","different-uid"},{"operation","upsert"},
            {"payload",QJsonObject{{"order_uid","different-uid"},{"number",19},{"status","closed"}}}},&error));
        const auto stats=RemoteMutationApplyService::applyPendingEvents(db,100,&error);
        QCOMPARE(stats.conflicts,1);
        QVERIFY(q.exec("SELECT status FROM work_orders WHERE order_uid='parent-A'")); QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(),QString("open"));
        QCOMPARE(RemoteMutationApplyService::applyPendingEvents(db,100,&error).conflicts,0);
    }
    void tokenEncryptionRoundTripAndMigration() {
#ifdef Q_OS_WIN
        QString error;
        const QString plain = QStringLiteral("synthetic-token-not-a-real-credential");
        const QString encrypted = CloudSyncCredentials::protectToken(plain, &error);
        QVERIFY2(encrypted.startsWith("dpapi:v1:"), qPrintable(error));
        QVERIFY(!encrypted.contains(plain));
        QCOMPARE(CloudSyncCredentials::unprotectToken(encrypted, &error), plain);
        QVERIFY(CloudSyncCredentials::unprotectToken("dpapi:v1:broken", &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE settings(key TEXT PRIMARY KEY,value TEXT)"));
        q.prepare("INSERT INTO settings VALUES('settings.cloudsync.api_token',?)");
        q.addBindValue(plain); QVERIFY(q.exec());
        QCOMPARE(CloudSyncSettings::load().apiToken, plain);
        QVERIFY(q.exec("SELECT value FROM settings WHERE key='settings.cloudsync.api_token'"));
        QVERIFY(q.next()); QVERIFY(q.value(0).toString().startsWith("dpapi:v1:"));
        QCOMPARE(CloudSyncSettings::load().apiToken, plain);
#endif
    }
    void restoreRehearsalIsIsolatedAndRejectsCorruption() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString snapshot = dir.filePath("snapshot.db");
        QSqlQuery q(db);
        q.prepare("VACUUM INTO ?"); q.addBindValue(snapshot); QVERIFY(q.exec());
        QString error;
        QVERIFY2(BackupSnapshotService::rehearseRestore(snapshot, &error), qPrintable(error));
        QVERIFY(!BackupPaths::isIndependentStorage(snapshot, dir.path(), &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(q.exec("SELECT COUNT(*) FROM work_orders")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 2);
        QFile broken(dir.filePath("broken.db")); QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("not a sqlite database"); broken.close();
        QVERIFY(!BackupSnapshotService::rehearseRestore(broken.fileName(), &error));
        const QString missing = dir.filePath("missing.db");
        QVERIFY(!BackupSnapshotService::validateSnapshot(missing, &error));
        QVERIFY(!QFile::exists(missing));
    }
    void init() {
        DbSettingsCacheSupport::clear();
        db = QSqlDatabase::addDatabase("QSQLITE", "carservice_main");
        db.setDatabaseName(":memory:");
        QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE work_orders(order_uid TEXT PRIMARY KEY,number INTEGER,status TEXT,car_year INTEGER,closed_at TEXT)"));
        QVERIFY(q.exec("INSERT INTO work_orders VALUES('parent-A',19,'open',2014,''),('parent-B',20,'open',2015,'')"));
        QVERIFY(q.exec("CREATE TABLE work_order_services(service_uid TEXT PRIMARY KEY,order_uid TEXT,order_number INTEGER,title TEXT,status TEXT,completed_at TEXT,completed_by TEXT)"));
        QVERIFY(q.exec("INSERT INTO work_order_services VALUES('service-A','parent-A',19,'Oil','open','','')"));
    }
    void cleanup() {
        db.close(); db = {}; QSqlDatabase::removeDatabase("carservice_main");
    }
    void completionResolvesLocalOrderNumberByUid() {
        QString error;
        QVERIFY2(WorkOrderSyncApplyService::upsertServiceDirect(db,"service-A",
            {{"order_uid","parent-A"},{"order_number",999},{"status","completed"},{"completed_by","m1"}}, &error), qPrintable(error));
        QSqlQuery q(db);
        QVERIFY(q.exec("SELECT order_number,status,title FROM work_order_services WHERE service_uid='service-A'"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),19);
        QCOMPARE(q.value(1).toString(),QString("completed")); QCOMPARE(q.value(2).toString(),QString("Oil"));
    }
    void mobileManualServiceKeepsItsAuthorAndRetryIsIdempotent() {
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE masters(id TEXT PRIMARY KEY,name TEXT)"));
        QVERIFY(q.exec("INSERT INTO masters VALUES('m2','Master 2')"));
        QVERIFY(q.exec("DROP TABLE work_order_services"));
        QVERIFY(q.exec("CREATE TABLE work_order_services(id INTEGER PRIMARY KEY AUTOINCREMENT,service_uid TEXT UNIQUE,order_uid TEXT,order_number INTEGER,title TEXT,status TEXT)"));
        QVERIFY(q.exec("CREATE TABLE service_masters(service_id INTEGER,master_id TEXT,share_percent REAL,role TEXT,master_name_closed TEXT,PRIMARY KEY(service_id,master_id))"));
        QVariantMap fields{{"order_uid","parent-A"},{"order_number",999},{"title",QString::fromUtf8("Заміна моторної оливи")},
            {"source","mobile_manual"},{"assigned_to","m2"},{"status","open"}};
        QString error;
        for (int retry=0; retry<2; ++retry)
            QVERIFY2(WorkOrderSyncApplyService::upsertServiceDirect(db,"new-mobile",fields,&error),qPrintable(error));
        QVERIFY(q.exec("SELECT s.order_number,s.title,sm.master_id,sm.share_percent FROM work_order_services s JOIN service_masters sm ON sm.service_id=s.id WHERE s.service_uid='new-mobile'"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),19);
        QCOMPARE(q.value(1).toString(),QString::fromUtf8("Заміна моторної оливи"));
        QCOMPARE(q.value(2).toString(),QString("m2")); QCOMPARE(q.value(3).toDouble(),100.0);
        QVERIFY(!q.next());
    }
    void cannotMoveServiceToAnotherOrder() {
        QString error;
        QVERIFY(!WorkOrderSyncApplyService::upsertServiceDirect(db,"service-A",{{"order_uid","parent-B"},{"status","completed"}}, &error));
        QVERIFY(!error.isEmpty());
    }
    void contentCannotReopenTerminalOrPlannedOrder() {
        for (const QString& status : {QString("completed"), QString("closed"), QString("paid"),
                                      QString("archived"), QString("deleted"), QString("planned")}) {
            QSqlQuery q(db);
            q.prepare("UPDATE work_orders SET status=? WHERE number=19");
            q.addBindValue(status); QVERIFY(q.exec());
            QString error;
            QVERIFY2(OrderStatusService::promoteToInProgressIfNeeded(db,19,&error),qPrintable(error));
            QVERIFY(q.exec("SELECT status FROM work_orders WHERE number=19"));
            QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),status);
        }
    }
    void closedDateAndVehicleYearArePreserved() {
        QString error;
        QVERIFY(WorkOrderSyncApplyService::upsertWorkOrderDirect(db,"parent-A",{{"car_year",2019},{"closed_at","2026-09-21T12:00:00"}}, &error));
        QSqlQuery q(db); QVERIFY(q.exec("SELECT car_year,closed_at FROM work_orders WHERE order_uid='parent-A'"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),2019); QVERIFY(!q.value(1).toString().isEmpty());
    }
    void planningEventReachesCalendar() {
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE settings(key TEXT PRIMARY KEY,value TEXT)"));
        QVERIFY(q.exec("CREATE TABLE planning_calendar(day TEXT PRIMARY KEY,note TEXT)"));
        const QString note = QString::fromUtf8("09:00 AA 1111 AA | Audi Q7 | 2014 | Client | 0501234567 | Oil | Master | normal | Note | Пробіг: 125000 км | Джерело: мобільний застосунок | Майстер ID: m1");
        QString error;
        QVERIFY(CloudSyncInboxService::enqueueEvent(db, {
            {"event_uid","planning-test"}, {"source_device_uid","mobile-panel"},
            {"entity_type","planning_day"}, {"entity_uid","2030-10-01"}, {"operation","upsert"},
            {"payload",QJsonObject{{"day","2030-10-01"},{"note",note}}}}, &error));
        const auto result = RemoteMutationApplyService::applyPendingEvents(db,100,&error);
        if (result.applied != 1 && q.exec("SELECT apply_error FROM sync_inbox") && q.next())
            error += q.value(0).toString();
        QVERIFY2(result.applied == 1,qPrintable(error));
        QVERIFY(q.exec("SELECT note FROM planning_calendar WHERE day='01.10.30'"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),note);
    }
    void rejectedCalendarPushBecomesResolvableConflict_data() {
        QTest::addColumn<QString>("choice");
        QTest::newRow("desktop") << QString("keep_local");
        QTest::newRow("server") << QString("accept_remote");
        QTest::newRow("rollback") << QString("rollback");
        QTest::newRow("inactive") << QString("inactive");
        QTest::newRow("legacy-iso") << QString("legacy");
    }
    void rejectedCalendarPushBecomesResolvableConflict() {
        QFETCH(QString,choice);
        QSqlQuery q(db); QString error;
        QVERIFY(q.exec("CREATE TABLE settings(key TEXT PRIMARY KEY,value TEXT)"));
        QVERIFY(q.exec("INSERT INTO settings VALUES('settings.cloudsync.capture_outbox_while_disabled','1')"));
        QVERIFY(q.exec("CREATE TABLE planning_calendar(day TEXT PRIMARY KEY,note TEXT)"));
        QVERIFY(q.exec("INSERT INTO planning_calendar VALUES('28.05.26','10:00 Desktop')"));
        if(choice=="legacy") QVERIFY(q.exec("UPDATE planning_calendar SET day='2026-05-28'"));
        QVERIFY2(CloudSyncSchemaService::ensureSchema(db,&error),qPrintable(error));
        MutationResult mutation; mutation.ok=true; mutation.entityType="planning_day"; mutation.entityId="2026-05-28";
        mutation.operation="update"; mutation.changedTables={"planning_calendar"};
        QVERIFY2(CloudSyncOutboxService::appendMutation(db,mutation,&error),qPrintable(error));
        QVERIFY(q.exec(choice=="inactive" ? "UPDATE sync_outbox SET status='local_only'" : "UPDATE sync_outbox SET status='failed',last_error='HTTP 409 Conflict'"));
        QVERIFY(CloudSyncInboxService::enqueueEvent(db,{{"event_uid","calendar-conflict"},{"source_device_uid","mobile-panel"},
            {"entity_type","planning_day"},{"entity_uid","2026-05-28"},{"operation","upsert"},
            {"payload",QJsonObject{{"day","2026-05-28"},{"note","10:00 Server"}}}},&error));
        auto applied=RemoteMutationApplyService::applyPendingEvents(db,100,&error);
        if(choice=="inactive") { QCOMPARE(applied.applied,1); QCOMPARE(applied.failed,0); return; }
        QCOMPARE(applied.failed,0); QCOMPARE(applied.conflicts,1);
        QVERIFY(q.exec("SELECT conflict_uid FROM sync_conflicts WHERE resolution='pending'")); QVERIFY(q.next());
        const QString conflict=q.value(0).toString();
        if(choice=="rollback") {
            QVERIFY(q.exec("CREATE TRIGGER fail_resolve BEFORE UPDATE OF resolution ON sync_conflicts WHEN NEW.resolution!='pending' BEGIN SELECT RAISE(ABORT,'test rollback'); END"));
            expectRollback(QString::fromUtf8("Конфлікт уже змінено. Оновіть список."));
            QVERIFY(!ConflictResolverService::resolveConflict(db,conflict,"accept_remote","test",&error));
            QVERIFY(q.exec("SELECT note FROM planning_calendar")); QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString("10:00 Desktop"));
            QVERIFY(q.exec("SELECT status FROM sync_outbox")); QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString("failed")); return;
        }
        if(choice=="legacy") choice="keep_local";
        QVERIFY2(ConflictResolverService::resolveConflict(db,conflict,choice,"test",&error),qPrintable(error));
        QVERIFY(q.exec("SELECT note FROM planning_calendar")); QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(),choice=="keep_local"?QString("10:00 Desktop"):QString("10:00 Server"));
        QVERIFY(q.exec("SELECT count(*) FROM sync_outbox WHERE status='failed'")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),0);
        if(choice=="keep_local") {
            QVERIFY(q.exec("SELECT json_extract(payload_json,'$.payload.base_note') FROM sync_outbox WHERE status='pending'"));
            QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString("10:00 Server"));
        }
        QCOMPARE(ConflictResolverService::pendingCount(db),0);
        QCOMPARE(RemoteMutationApplyService::applyPendingEvents(db,100,&error).failed,0);
    }
    void domainChangeRollsBackWhenReceiptCannotBeSaved() {
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE settings(key TEXT PRIMARY KEY,value TEXT)"));
        QVERIFY(q.exec("CREATE TABLE planning_calendar(day TEXT PRIMARY KEY,note TEXT)"));
        QString error;
        QVERIFY(CloudSyncInboxService::enqueueEvent(db, {
            {"event_uid","receipt-failure"}, {"source_device_uid","mobile-panel"},
            {"entity_type","planning_day"}, {"entity_uid","2030-10-01"}, {"operation","upsert"},
            {"payload",QJsonObject{{"day","2030-10-01"},{"note","09:00 Car"}}}}, &error));
        QVERIFY(q.exec("CREATE TRIGGER fail_receipt BEFORE UPDATE OF status ON sync_inbox "
                       "WHEN NEW.status='applied' BEGIN SELECT RAISE(ABORT,'receipt write failed'); END"));
        expectRollback(QStringLiteral("receipt write failed Unable to fetch row"));
        auto result = RemoteMutationApplyService::applyPendingEvents(db,100,&error);
        QCOMPARE(result.applied,0);
        QCOMPARE(result.failed,1);
        QVERIFY(q.exec("SELECT COUNT(*) FROM planning_calendar"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),0);
        QVERIFY(q.exec("DROP TRIGGER fail_receipt"));
        result = RemoteMutationApplyService::applyPendingEvents(db,100,&error);
        QCOMPARE(result.applied,1);
        result = RemoteMutationApplyService::applyPendingEvents(db,100,&error);
        QCOMPARE(result.applied,0);
        QVERIFY(q.exec("SELECT COUNT(*) FROM planning_calendar"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),1);
    }
};
QTEST_MAIN(MobileSyncTests)
#include "MobileSyncTests.moc"
