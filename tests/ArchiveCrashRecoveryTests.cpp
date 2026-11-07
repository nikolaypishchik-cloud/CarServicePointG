#include <QtTest/QtTest>
#include <QStandardPaths>
#include <QSettings>
#include <QProcess>
#include <QSqlQuery>
#include <QSqlError>
#include <QNetworkProxy>
#include <QUuid>
#include "app/App.h"
#include "core/cloudsync/CloudSyncCredentials.h"
#include "ui/common/dialogs/OrderDialog.h"
#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/db/sqlite/DbFacadeSupport.h"
#include "infrastructure/db/sqlite/DbArchiveInternals.h"
#include "infrastructure/db/sqlite/OrderArchiveShardSqlSupport.h"
#include "core/services/ActivityAuditService.h"
#include "core/services/StaffAccountService.h"
#include "core/services/AccessControlService.h"
#include "core/services/orders/OrderClosureJournal.h"

class ArchiveCrashRecoveryTests : public QObject {
    Q_OBJECT
    QString root;
    void runBoundary(const QString& phase, bool retainArchive = false) {
        const QString identity="Safety-archive-"+QUuid::createUuid().toString(QUuid::Id128).left(8);
        for(const auto& mode:{QString("seed"),QString("crash"),QString("resume")}) {
            QProcess process;auto env=QProcessEnvironment::systemEnvironment();
            env.insert("CS_ARCHIVE_ID",identity);env.insert("CS_ARCHIVE_MODE",mode);
            env.insert("CS_ARCHIVE_BOUNDARY",phase);
            if(retainArchive)env.insert("CS_TEST_DELETE_HAS_ARCHIVE","1");
            else env.remove("CS_TEST_DELETE_HAS_ARCHIVE");
            env.insert("CS_ARCHIVE_CRASH",mode=="crash"?phase:QString());
            process.setProcessEnvironment(env);
            const QString report=QDir(root).filePath(identity+"-"+mode+".txt");
            process.start(QCoreApplication::applicationFilePath(),{"worker","-o",report+",txt"});
            QVERIFY(process.waitForStarted(5000));
            QVERIFY2(process.waitForFinished(20000),qPrintable(phase+" "+mode+" timed out"));
            QFile log(report);QVERIFY(log.open(QIODevice::ReadOnly));const auto text=log.readAll();
            QCOMPARE(process.exitCode(),mode=="crash"?87:0);
            if(mode!="crash")QVERIFY2(text.contains("PASS   : ArchiveCrashRecoveryTests::worker()"),text.constData());
        }
    }
private slots:
    void initTestCase() {
        root=QFileInfo(qEnvironmentVariable("CS_TEST_DATA_ROOT")).canonicalFilePath();QVERIFY(!root.isEmpty());
        QStandardPaths::setTestModeEnabled(true);
        auto id=qEnvironmentVariable("CS_ARCHIVE_ID");if(id.isEmpty())id="Safety-archive-parent";
        QVERIFY(id.startsWith("Safety-") && !id.contains('/') && !id.contains('\\'));
        QCoreApplication::setOrganizationName("CarServiceArchiveTests");QCoreApplication::setApplicationName(id);
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root);
        QVERIFY(QFileInfo(App::dataDir()).canonicalFilePath().startsWith(root+"/"));
        QVERIFY(QFileInfo(QSettings().fileName()).absoluteFilePath().startsWith(root+"/"));
        QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::HttpProxy,"127.0.0.1",9));
        qInfo()<<"ISOLATION_VERIFIED"<<root<<App::dataDir();
    }
    void prepared(){runBoundary("prepared");}
    void copyCommitted(){runBoundary("copy_committed");}
    void verified(){runBoundary("verified");}
    void routeCommitted(){runBoundary("route_committed");}
    void routed(){runBoundary("routed");}
    void beforePurgeCommit(){runBoundary("before_purge_commit");}
    void afterPurgeCommit(){runBoundary("after_purge_commit");}
    void deletionSourceCommitted(){runBoundary("deletion_source_committed");}
    void deletionRegistryCommitted(){runBoundary("deletion_registry_committed");}
    void deletionWithHistoricalArchive(){runBoundary("deletion_registry_committed",true);}
    void restoredProfileWithProtectedSettings() {
        const QString identity="Safety-archive-"+QUuid::createUuid().toString(QUuid::Id128).left(8);
        const QString source=QDir(root).filePath("source-"+identity);
        const QString snapshot=QDir(root).filePath("snapshot-"+identity);
        const QString restored=QDir(root).filePath("restored-"+identity);
        QVERIFY(QDir().mkpath(source));
        auto child=[&](const QString& base,const QString& mode){
            QProcess process;auto env=QProcessEnvironment::systemEnvironment();
            env.insert("CS_TEST_DATA_ROOT",base);env.insert("CS_ARCHIVE_ID",identity);
            env.insert("CS_ARCHIVE_MODE",mode);env.remove("CS_ARCHIVE_CRASH");
            process.setProcessEnvironment(env);
            const QString report=QDir(root).filePath(identity+"-"+mode+".txt");
            process.start(QCoreApplication::applicationFilePath(),{"worker","-o",report+",txt"});
            if(!process.waitForFinished(20000))return false;
            QFile log(report);if(!log.open(QIODevice::ReadOnly))return false;
            const auto content=log.readAll();
            if(process.exitCode()!=0)qWarning().noquote()<<content;
            return process.exitCode()==0 && content.contains("PASS   : ArchiveCrashRecoveryTests::worker()");
        };
        QVERIFY(child(source,"seed"));
        const QString script=QString::fromUtf8(__FILE__).replace("tests/ArchiveCrashRecoveryTests.cpp","scripts/profile_snapshot.py").replace("tests\\ArchiveCrashRecoveryTests.cpp","scripts/profile_snapshot.py");
        QVERIFY(QFileInfo::exists(script));
        const QString python=qEnvironmentVariable("CS_TEST_PYTHON",QStandardPaths::findExecutable("python"));
        QVERIFY2(!python.isEmpty() && QFileInfo::exists(python),"Python interpreter is required for profile snapshot/restore");
        for(const auto& args:{QStringList{"-B",script,"snapshot",source,snapshot},QStringList{"-B",script,"restore",snapshot,restored}}){
            QProcess process;process.start(python,args);
            QVERIFY(process.waitForFinished(20000));QVERIFY2(process.exitCode()==0,process.readAllStandardError().constData());
        }
        QVERIFY(QDir().rename(source,source+"-offline"));
        QVERIFY(child(restored,"profile"));
    }
    void worker() {
        const auto mode=qEnvironmentVariable("CS_ARCHIVE_MODE");QVERIFY(!mode.isEmpty());
        auto db=QSqlDatabase::addDatabase("QSQLITE","carservice_main");db.setDatabaseName(App::activeDbFilePath());QVERIFY(db.open());
        QString error;
        const QString boundary=qEnvironmentVariable("CS_ARCHIVE_BOUNDARY");
        const bool deletion=boundary.startsWith("deletion_");
        if(mode=="seed") {
            QVERIFY(Database::ensureSchema());QVERIFY(ensureWorkspaceOrderStateSchema(db));QVERIFY(ensureRouterCacheSchema(db));
            QVERIFY(StaffAccountService::ensureSchema(db,&error));QVERIFY(AccessControlService::ensureSchema(db,&error));
            QSqlQuery q(db);
            QVERIFY(q.exec("INSERT INTO staff_accounts(id,display_name,role,active) VALUES('test-owner','Synthetic owner','owner',1)"));
            for(const auto& sql:{
                "INSERT INTO masters(id,name) VALUES('test-owner','Synthetic')",
                "INSERT INTO work_orders(number,order_uid,order_number,order_year,status,opened_at,closed_at,car,client,plate,plate_norm,master_id) VALUES(1,'crash-order',1,2026,'closed','2026-10-01','2026-10-02','Synthetic','Synthetic','AA 0000 AA','AA0000AA','test-owner')",
                "INSERT INTO work_order_services(id,order_number,order_uid,title,qty,price,amount) VALUES(10,1,'crash-order','Test work',1,10,10)",
                "INSERT INTO work_order_parts(id,order_number,order_uid,name,qty,price,amount,purchase_price,purchase_amount) VALUES(20,1,'crash-order','Test part',2,30,60,20,40)"})
                QVERIFY2(q.exec(sql),qPrintable(q.lastError().text()));
            QVERIFY(OrderClosureJournal::ensureSchema(db,&error));
            QVERIFY2(q.exec("INSERT INTO client_payments VALUES('receipt','request','crash-order',1000,'cash','2026-10-02','test',1)"),qPrintable(q.lastError().text()));
            const QString synthetic="synthetic-profile-password";
            QVERIFY(CloudSyncCredentials::saveOwnerPassword(synthetic,&error));
            const QString protectedToken=CloudSyncCredentials::protectToken("synthetic-profile-token",&error);
            QVERIFY(!protectedToken.isEmpty());
            QSettings settings;settings.setValue("test/protected_token",protectedToken);
            const QString attachment=QDir(App::dataDir()).filePath("synthetic-attachment.txt");
            QFile file(attachment);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write("synthetic attachment"),qint64(20));file.close();
            settings.setValue("test/absolute_attachment",attachment);settings.sync();QCOMPARE(settings.status(),QSettings::NoError);
            if(deletion){
                if(qEnvironmentVariableIsSet("CS_TEST_DELETE_HAS_ARCHIVE")){
                    QSqlQuery archive(db);
                    archive.prepare("VACUUM INTO ?");archive.addBindValue(App::archiveDbFilePath());
                    QVERIFY2(archive.exec(),qPrintable(archive.lastError().text()));
                }
                QVERIFY(ensurePartRegistrySchemaFile());
                QSqlQuery registry(QSqlDatabase::database(dbRegistryConnectionName()));
                QVERIFY(registry.exec("INSERT INTO order_registry(order_number,order_uid,order_year,visible_order_number) VALUES(1,'crash-order',2026,1)"));
                QVERIFY(registry.exec("INSERT INTO part_registry(entry_key,order_number,order_uid,part_uid) VALUES('test-part',1,'crash-order','retained-original')"));
            }
            return;
        }
        if(mode=="profile") {
            ActivityAuditService::setCurrentActor({"test-owner","Test Owner","owner","test"});
            QVERIFY(CloudSyncCredentials::loadOwnerPassword(&error)=="synthetic-profile-password");
            QSettings settings;
            QVERIFY(CloudSyncCredentials::unprotectToken(settings.value("test/protected_token").toString(),&error)=="synthetic-profile-token");
            // Original profile is offline: resolving this proves relocation, not accidental access to source.
            QCOMPARE(settings.value("test/absolute_attachment").toString(),QDir(App::dataDir()).filePath("synthetic-attachment.txt"));
            QVERIFY(QFileInfo::exists(settings.value("test/absolute_attachment").toString()));
            QVERIFY(QFileInfo::exists(QDir(App::dataDir()).filePath("synthetic-attachment.txt")));
            qInfo()<<"RESTORE_REFERENCE_RELOCATED: source offline; restored attachment resolves";
            OrderDialog dialog(nullptr,1);dialog.setReadOnly(true);dialog.show();
            QTRY_VERIFY(dialog.isVisible());QVERIFY(dialog.isEnabled());QVERIFY(dialog.findChild<QWidget*>("orderFormSurface"));QCOMPARE(dialog.recoveryOrderUid(),QString("crash-order"));
            QTest::qWait(150);QVERIFY(dialog.isVisible());
            dialog.grab().save(QDir(root).filePath("restored-order.png"));dialog.hide();
            qInfo()<<"RESTORED_ORDER_UI_OPENED; synthetic DPAPI settings readable on same Windows account";
            return;
        }
        ActivityAuditService::setCurrentActor({"test-owner","Test Owner","owner","test"});
        if(deletion){
            if(mode=="crash"){
                const bool ok=Database::hardDeleteOrder(1,&error);
                QFAIL(qPrintable(QString("Deletion crash point not reached: %1 %2").arg(ok).arg(error)));
            }
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT pending FROM order_deletion_publications WHERE internal_number=1 AND order_uid='crash-order'"));
            QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),1);q.finish();
            QVERIFY(ensurePartRegistrySchemaFile());
            QSqlQuery registry(QSqlDatabase::database(dbRegistryConnectionName()));
            const int expected=boundary=="deletion_source_committed"?1:0;
            for(const auto& table:QStringList{"order_registry","part_registry"}){
                QVERIFY(registry.exec("SELECT COUNT(*) FROM "+table+" WHERE order_uid='crash-order'"));
                QVERIFY(registry.next());QCOMPARE(registry.value(0).toInt(),expected);registry.finish();
            }
            QVERIFY2(Database::runStartupSchemaCheck(&error),qPrintable(error));
            QVERIFY2(Database::runStartupRegistryRefresh(&error),qPrintable(error));
            if(qEnvironmentVariableIsSet("CS_TEST_DELETE_HAS_ARCHIVE")){
                QVERIFY2(rebuildRegistryDerivedState(db,&error),qPrintable(error));
                QVERIFY(forceRebuildRouterCacheFromActiveWorkspace(db));
                QVERIFY(q.exec("SELECT COUNT(*) FROM router_work_orders WHERE order_uid='crash-order'"));
                QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),0);q.finish();
                q.prepare("ATTACH DATABASE ? AS original_archive");q.addBindValue(App::archiveDbFilePath());QVERIFY(q.exec());
                QVERIFY(q.exec("SELECT COUNT(*),SUM(amount) FROM original_archive.work_order_parts WHERE order_uid='crash-order'"));
                QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),1);QCOMPARE(q.value(1).toInt(),60);q.finish();
                const auto changed=QDir(App::dataDir()).filePath("conflicting-archive.db");
                q.prepare("VACUUM original_archive INTO ?");q.addBindValue(changed);QVERIFY(q.exec());q.finish();
                q.prepare("ATTACH DATABASE ? AS conflict_archive");q.addBindValue(changed);QVERIFY(q.exec());q.finish();
                QVERIFY(q.exec("UPDATE conflict_archive.work_orders SET order_number=2 WHERE order_uid='crash-order'"));
                QVERIFY(q.exec("DETACH DATABASE conflict_archive"));
                QVERIFY(!importArchivedRegistryFromDbPath(changed,&error));
                QVERIFY2(error.contains("retired order"),qPrintable(error));
            }
            QVERIFY2(Database::hardDeleteOrder(1,&error),qPrintable(error));
            QVERIFY(q.exec("SELECT pending FROM order_deletion_publications WHERE internal_number=1"));
            QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),0);q.finish();
            QVERIFY(q.exec("SELECT COUNT(*) FROM work_orders"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),0);q.finish();
            QVERIFY(q.exec("SELECT SUM(amount_cents) FROM client_payments WHERE order_uid='crash-order'"));
            QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),1000);q.finish();
            for(const auto& table:QStringList{"order_registry","part_registry"}){
                QVERIFY(registry.exec("SELECT COUNT(*) FROM "+table+" WHERE order_uid='crash-order'"));
                QVERIFY(registry.next());QCOMPARE(registry.value(0).toInt(),0);registry.finish();
            }
            return;
        }
        if(mode=="crash") {
            const bool ok=OrderArchiveShardSqlSupport::archiveSingleActiveOrderToShard(db,1,&error);
            QFAIL(qPrintable(QString("Crash point not reached; result=%1 error=%2").arg(ok).arg(error)));
        }
        QVERIFY2(OrderArchiveShardSqlSupport::pruneActiveClosedOrdersToArchive(db,&error),qPrintable(error));
        QSqlQuery q(db);
        QVERIFY(q.exec("SELECT phase FROM archive_transfer_journal_v1 WHERE order_uid='crash-order'"));QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("complete"));q.finish();
        QVERIFY(q.exec("SELECT COUNT(*) FROM work_orders WHERE number=1"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),0);q.finish();
        QVERIFY(q.exec("SELECT COUNT(*),SUM(amount_cents) FROM client_payments"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),1);QCOMPARE(q.value(1).toLongLong(),qint64(1000));q.finish();
        QVERIFY2(q.exec("ATTACH DATABASE "+sqlQuoteText(archiveShardFilePathForOrder(1))+" AS proof"),qPrintable(q.lastError().text()));
        QVERIFY(q.exec("SELECT COUNT(*),SUM(amount),SUM(purchase_amount) FROM proof.work_order_parts WHERE order_number=1"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),1);QCOMPARE(q.value(1).toDouble(),60.0);QCOMPARE(q.value(2).toDouble(),40.0);q.finish();
        QVERIFY(q.exec("PRAGMA proof.integrity_check"));QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("ok"));q.finish();
        QVERIFY(q.exec("PRAGMA main.integrity_check"));QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("ok"));q.finish();
        QVERIFY(ensureRegistrySchemaFile());auto registry=QSqlDatabase::database(dbRegistryConnectionName());QSqlQuery route(registry);
        QVERIFY(route.exec("SELECT storage_kind,storage_path FROM order_registry WHERE order_uid='crash-order'"));QVERIFY(route.next());QCOMPARE(route.value(0).toString(),QString("archive"));QCOMPARE(route.value(1).toString(),archiveShardFilePathForOrder(1));route.finish();
    }
};
QTEST_MAIN(ArchiveCrashRecoveryTests)
#include "ArchiveCrashRecoveryTests.moc"
