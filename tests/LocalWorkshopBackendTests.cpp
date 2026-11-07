#include <QtTest>
#include <QApplication>
#include <QTemporaryDir>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QJsonDocument>
#include <QProcess>
#include <QCryptographicHash>
#include <QCheckBox>
#include <QSettings>
#include <QStandardPaths>
#include <QElapsedTimer>
#include <QFontDatabase>
#include "core/cloudsync/CloudSyncSettings.h"
#include "core/cloudsync/CloudSyncClient.h"
#include "infrastructure/db/sqlite/Db.h"
#include "ui/tabs/cloudsync/CloudSyncPage.h"
#include "core/services/orders/LocalWorkshopBackend.h"
#include "core/services/orders/OrderClosureJournal.h"
#include "core/services/orders/BaySnapshot.h"
#include "core/services/AccessControlService.h"
#include "core/services/ActivityAuditService.h"

namespace {
QVariant scalar(QSqlDatabase db,const QString& sql){QSqlQuery q(db);if(!q.exec(sql)||!q.next())return {};return q.value(0);}
void owner(){ActivityAuditService::setCurrentActor({"local-test-owner","Owner","owner","test"});}
bool login(QSqlDatabase db) {
    QSqlQuery q(db);
    if(!db.tables().contains("staff_accounts") && !q.exec("CREATE TABLE staff_accounts(id TEXT PRIMARY KEY,master_id TEXT DEFAULT '',display_name TEXT DEFAULT '',role TEXT,active INTEGER DEFAULT 1)"))return false;
    if(!q.exec("INSERT OR REPLACE INTO staff_accounts(id,display_name,role,active) VALUES('local-test-owner','Test owner','owner',1)")){qWarning()<<q.lastError();return false;}
    QString error;if(!AccessControlService::ensureSchema(db,&error))return false;owner();return true;
}
QJsonObject command(QSqlDatabase db,const QString& uid,qint64 amount=-1) {
    const auto p=LocalWorkshopBackend::request(db,"closure-preview",{{"order_uid",uid}});
    QJsonObject c{{"request_uid",QUuid::createUuid().toString(QUuid::WithoutBraces)},{"order_uid",uid},{"expected_revision",p.body["revision"]},{"release_bay",true},{"local_authority",true},{"payment",QJsonValue::Null}};
    if(amount>=0)c["payment"]=QJsonObject{{"amount_cents",amount},{"method","cash"},{"paid_on",QDate::currentDate().toString(Qt::ISODate)}};
    return c;
}
LocalWorkshopBackend::Reply action(QSqlDatabase db,const QString& endpoint,QJsonObject p={}) {
    p["request_uid"]=QUuid::createUuid().toString(QUuid::WithoutBraces);return LocalWorkshopBackend::request(db,endpoint,p);
}
QByteArray suppliers(QSqlDatabase db) {
    QByteArray bytes;for(const auto& table:QStringList{"purchase_payment_balances","purchase_payment_events","purchase_payment_terms","workshop_spare_payment_allocations","workshop_spare_movements"}) {
        if(!db.tables().contains(table))continue;QSqlQuery q(db);if(!q.exec("SELECT * FROM "+table+" ORDER BY rowid"))return "error";
        while(q.next())for(int i=0;i<q.record().count();++i){bytes+=q.value(i).toString().toUtf8();bytes+='\0';}
    }return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);
}
}
class LocalWorkshopBackendTests:public QObject {
    Q_OBJECT
    QTemporaryDir dir;QSqlDatabase db;QString name;
private slots:
    void init() {
        name=QUuid::createUuid().toString();db=QSqlDatabase::addDatabase("QSQLITE",name);db.setDatabaseName(dir.filePath(name+".db"));QVERIFY(db.open());
        QSqlQuery q(db);
        for(const auto& sql:QStringList{
            "CREATE TABLE work_orders(number INTEGER PRIMARY KEY,order_uid TEXT,status TEXT,discount REAL DEFAULT 0,closed_at TEXT)",
            "INSERT INTO work_orders VALUES(1,'o1','open',0,''),(2,'o2','open',0,'')",
            "CREATE TABLE work_order_parts(id INTEGER PRIMARY KEY,part_uid TEXT,order_uid TEXT,order_number INTEGER,name TEXT,qty REAL,price REAL,installed_at TEXT,status TEXT)",
            "INSERT INTO work_order_parts VALUES(1,'p1','o1',1,'Part',1,3000,'','active')",
            "CREATE TABLE work_order_services(id INTEGER PRIMARY KEY,service_uid TEXT,order_uid TEXT,qty REAL,price REAL)",
            "INSERT INTO work_order_services VALUES(1,'s1','o1',1,2000)",
            "CREATE TABLE workshop_spare_movements(id INTEGER PRIMARY KEY,part_id INTEGER,order_number INTEGER,operation TEXT,quantity REAL)",
            "INSERT INTO workshop_spare_movements VALUES(1,1,1,'issue',1)",
            "CREATE TABLE purchase_payment_balances(purchase_key TEXT,paid_cents INTEGER)",
            "INSERT INTO purchase_payment_balances VALUES('supplier-invoice',0)"})QVERIFY2(q.exec(sql),qPrintable(q.lastError().text()));
        QVERIFY(login(db));
    }
    void cleanup(){ActivityAuditService::clearCurrentActor();db.close();db={};QSqlDatabase::removeDatabase(name);}
    void payments_data(){QTest::addColumn<qint64>("amount");QTest::newRow("none")<<qint64(-1);QTest::newRow("partial")<<qint64(200000);QTest::newRow("full")<<qint64(500000);}
    void payments() {
        QFETCH(qint64,amount);const auto before=suppliers(db);const auto c=command(db,"o1",amount);
        // Preview/cancel has no financial or order side effects.
        QCOMPARE(scalar(db,"SELECT status FROM work_orders WHERE number=1").toString(),QString("open"));
        QCOMPARE(scalar(db,"SELECT COUNT(*) FROM client_payments").toInt(),0);
        const auto result=LocalWorkshopBackend::request(db,"closure-commit",c);QVERIFY2(result.ok,qPrintable(QJsonDocument(result.body).toJson()));
        QCOMPARE(result.body["balance_cents"].toInteger(),qint64(500000-qMax(qint64(0),amount)));
        QCOMPARE(scalar(db,"SELECT status FROM work_orders WHERE number=1").toString(),QString("closed"));
        QVERIFY(!scalar(db,"SELECT installed_at FROM work_order_parts WHERE id=1").toString().isEmpty());
        QCOMPARE(scalar(db,"SELECT COUNT(*) FROM client_payments").toInt(),amount<0?0:1);
        QCOMPARE(suppliers(db),before);
        QCOMPARE(LocalWorkshopBackend::request(db,"closure-commit",c).body,result.body);
        QSqlQuery q(db);QVERIFY(q.exec("UPDATE work_orders SET status='open' WHERE number=1"));
        QCOMPARE(LocalWorkshopBackend::request(db,"closure-commit",c).body,result.body);
        QCOMPARE(scalar(db,"SELECT status FROM work_orders WHERE number=1").toString(),QString("open"));
        // Reopening keeps prior receipt; a new closure only adds the remainder.
        const auto again=command(db,"o1",amount==500000?-1:500000-qMax(qint64(0),amount));
        const auto paid=LocalWorkshopBackend::request(db,"closure-commit",again);QVERIFY(paid.ok);QCOMPARE(paid.body["balance_cents"].toInteger(),qint64(0));
    }
    void conflictAndFailures() {
        QVERIFY(action(db,"provision").ok);
        QVERIFY(action(db,"confirm-occupied",{{"order_uid","o1"},{"target_state","on_bay"},{"target_bay_uid","1"},{"expected_version",0}}).ok);
        QTest::qWait(2);
        auto c=command(db,"o1",500000);QSqlQuery q(db);QVERIFY(q.exec("UPDATE work_order_services SET price=2100 WHERE id=1"));
        QCOMPARE(LocalWorkshopBackend::request(db,"closure-commit",c).status,409);
        QCOMPARE(scalar(db,"SELECT COUNT(*) FROM client_payments").toInt(),0);
        c=command(db,"o1",600000);QCOMPARE(LocalWorkshopBackend::request(db,"closure-commit",c).status,422);
        c=command(db,"o1",500000);
        QVERIFY(q.exec("CREATE TRIGGER fail_local BEFORE INSERT ON local_workshop_commands BEGIN SELECT RAISE(ABORT,'injected failure after payment'); END"));
        QCOMPARE(LocalWorkshopBackend::request(db,"closure-commit",c).status,500);
        QCOMPARE(scalar(db,"SELECT COUNT(*) FROM client_payments").toInt(),0);
        QCOMPARE(scalar(db,"SELECT status FROM work_orders WHERE number=1").toString(),QString("open"));
        QCOMPARE(scalar(db,"SELECT installed_at FROM work_order_parts WHERE id=1").toString(),QString());
        QCOMPARE(LocalWorkshopBackend::request(db,"snapshot").body["states"].toArray()[0].toObject()["state"].toString(),QString("on_bay"));
        QVERIFY(q.exec("DROP TRIGGER fail_local"));QVERIFY(LocalWorkshopBackend::request(db,"closure-commit",c).ok);
        c["payment"]=QJsonValue::Null;QCOMPARE(LocalWorkshopBackend::request(db,"closure-commit",c).status,409);
    }
    void parts_data(){QTest::addColumn<QString>("sql");QTest::addColumn<bool>("allowed");
        QTest::newRow("unissued")<<QString("DELETE FROM workshop_spare_movements")<<false;
        QTest::newRow("returned")<<QString("UPDATE work_order_parts SET status='returned'")<<false;
        QTest::newRow("cancelled")<<QString("UPDATE work_order_parts SET status='cancelled'")<<false;
        QTest::newRow("installed")<<QString("UPDATE work_order_parts SET installed_at='2026-10-01'")<<true;
        QTest::newRow("none")<<QString("DELETE FROM work_order_parts")<<true;}
    void parts(){QFETCH(QString,sql);QFETCH(bool,allowed);QSqlQuery q(db);QVERIFY(q.exec(sql));auto r=LocalWorkshopBackend::request(db,"closure-commit",command(db,"o1"));QCOMPARE(r.ok,allowed);}
    void fractionalUnitCost() {
        QSqlQuery q(db);QVERIFY(q.exec("UPDATE work_order_parts SET qty=300,price=500.0/300.0"));QVERIFY(q.exec("DELETE FROM work_order_services"));
        QCOMPARE(LocalWorkshopBackend::request(db,"closure-preview",{{"order_uid","o1"}}).body["total_cents"].toInteger(),qint64(50000));
        QVERIFY(q.exec("UPDATE work_order_parts SET qty=1,price=2.675"));
        QCOMPARE(LocalWorkshopBackend::request(db,"closure-preview",{{"order_uid","o1"}}).body["total_cents"].toInteger(),qint64(268));
    }
    void explicitDiscountChangesClosureAndRejectsStaleConfirmation() {
        QSqlQuery q(db);
        QVERIFY(q.exec("ALTER TABLE work_orders ADD COLUMN discount_mode TEXT DEFAULT 'legacy'"));
        QVERIFY(q.exec("UPDATE work_orders SET discount=50,discount_mode='percent' WHERE number=1"));
        auto preview=LocalWorkshopBackend::request(db,"closure-preview",{{"order_uid","o1"}});
        QVERIFY(preview.ok);QCOMPARE(preview.body["total_cents"].toInteger(),qint64(400000));
        auto old=command(db,"o1",-1);
        QVERIFY(q.exec("UPDATE work_orders SET discount_mode='uah' WHERE number=1"));
        QCOMPARE(LocalWorkshopBackend::request(db,"closure-commit",old).status,409);
        preview=LocalWorkshopBackend::request(db,"closure-preview",{{"order_uid","o1"}});
        QCOMPARE(preview.body["total_cents"].toInteger(),qint64(495000));
        const auto before=suppliers(db);
        QVERIFY(LocalWorkshopBackend::request(db,"closure-commit",command(db,"o1",495000)).ok);
        QCOMPARE(suppliers(db),before);
    }
    void bays() {
        QVERIFY(action(db,"provision").ok);
        auto snap=LocalWorkshopBackend::request(db,"snapshot").body;QVERIFY(BaySnapshot::parse(snap).error.isEmpty());QCOMPARE(snap["bays"].toArray().size(),3);
        QVERIFY(action(db,"confirm-occupied",{{"order_uid","o1"},{"target_state","on_bay"},{"target_bay_uid","1"},{"expected_version",0}}).ok);
        QVERIFY(!LocalWorkshopBackend::deletionBlockReason(db,"o1").isEmpty());
        QVERIFY(action(db,"confirm-free",{{"bay_uid","2"}}).ok);
        QVERIFY(!action(db,"change",{{"order_uid","o2"},{"target_state","on_bay"},{"target_bay_uid","1"},{"expected_version",0}}).ok);
        QVERIFY(!action(db,"change",{{"order_uid","o1"},{"target_state","on_bay"},{"target_bay_uid","2"},{"expected_version",0}}).ok);
        QTest::qWait(2);QVERIFY(action(db,"change",{{"order_uid","o1"},{"target_state","on_bay"},{"target_bay_uid","2"},{"expected_version",1}}).ok);
        QTest::qWait(2);auto c=command(db,"o1",500000);QVERIFY(LocalWorkshopBackend::request(db,"closure-commit",c).ok);
        snap=LocalWorkshopBackend::request(db,"snapshot").body;QVERIFY(BaySnapshot::parse(snap).error.isEmpty());
        QCOMPARE(snap["states"].toArray()[0].toObject()["state"].toString(),QString("off_bay"));QCOMPARE(snap["intervals"].toArray().size(),2);
        QVERIFY(LocalWorkshopBackend::deletionBlockReason(db,"o1").isEmpty());
        // Corrections require revision + reason and preserve a visible audit trail.
        auto interval=snap["intervals"].toArray()[0].toObject();
        QVERIFY(action(db,"correct-interval",{{"interval_uid",interval["uid"]},{"expected_revision",interval["revision"]},{"bay_uid",interval["bay_uid"]},{"begin_ms",interval["begin_ms"]},{"end_ms",interval["end_ms"]},{"reason","Verified times"}}).ok);
        auto b=snap["bays"].toArray()[0].toObject();
        QVERIFY(action(db,"correct-calendar",{{"bay_uid","1"},{"expected_revision",b["calendar_revision"]},{"calendars",b["calendars"]},{"reason","Verified calendar"}}).ok);
        snap=LocalWorkshopBackend::request(db,"snapshot").body;QJsonObject o;for(auto v:snap["orders"].toArray())if(v.toObject()["uid"]=="o2")o=v.toObject();
        QVERIFY(action(db,"close-orders",{{"target_status","cancelled"},{"orders",QJsonArray{QJsonObject{{"order_uid","o2"},{"expected_order_revision",o["revision"]},{"expected_version",0},{"release",false}}}}}).ok);
        QCOMPARE(scalar(db,"SELECT status FROM work_orders WHERE number=2").toString(),QString("cancelled"));
        ActivityAuditService::clearCurrentActor();QCOMPARE(LocalWorkshopBackend::request(db,"snapshot").status,403);
    }
    void offlineSwitchAndNetworkGate() {
        const auto path=db.databaseName();db.close();
        QProcess child;child.start(QCoreApplication::applicationFilePath(),{"--offline-switch",path});QVERIFY(child.waitForFinished(30000));
        QVERIFY2(child.exitCode()==0,child.readAllStandardError().constData());
        child.start(QCoreApplication::applicationFilePath(),{"--offline-restart",path});QVERIFY(child.waitForFinished(30000));
        QVERIFY2(child.exitCode()==0,child.readAllStandardError().constData());QVERIFY(db.open());
    }
    void crashRecovery_data(){QTest::addColumn<QString>("dataset");QTest::newRow("baseline")<<QString("baseline");QTest::newRow("scaled5")<<QString("scaled5");}
    void crashRecovery() {
        QFETCH(QString,dataset);QTemporaryDir temp(QCoreApplication::applicationDirPath()+"/local-backend-XXXXXX");QVERIFY(temp.isValid());
        const auto original=QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../performance-20261026/"+dataset+"/CarService_workspace.db");const auto path=temp.filePath("workspace.db");QVERIFY(QFile::copy(original,path));
        const auto conn="copy-"+dataset;
        {
            auto copy=QSqlDatabase::addDatabase("QSQLITE",conn);copy.setDatabaseName(path);QVERIFY(copy.open());QVERIFY(login(copy));QSqlQuery q(copy);
            QVERIFY(q.exec("SELECT o.order_uid FROM work_orders o JOIN work_order_parts p ON p.order_uid=o.order_uid WHERE o.order_uid<>'' AND p.price>0 AND p.qty>0 LIMIT 1"));QVERIFY(q.next());const auto uid=q.value(0).toString();q.finish();
            q.prepare("UPDATE work_orders SET status='open' WHERE order_uid=?");q.addBindValue(uid);QVERIFY(q.exec());
            q.prepare("UPDATE work_order_parts SET installed_at='2026-10-01' WHERE order_uid=?");q.addBindValue(uid);QVERIFY(q.exec());
            const auto before=suppliers(copy);const auto c=command(copy,uid,1);QString error;QVERIFY(OrderClosureJournal::remember(copy,c,&error));
            const auto paymentsBefore=scalar(copy,"SELECT COUNT(*) FROM client_payments").toInt();
            QFile input(temp.filePath("request.json"));QVERIFY(input.open(QIODevice::WriteOnly));input.write(QJsonDocument(c).toJson());input.close();
            for(const auto& phase:{QString("before_commit"),QString("after_commit")}) {
                copy.close();QProcess child;auto env=QProcessEnvironment::systemEnvironment();env.insert("CS_LOCAL_CRASH",phase);child.setProcessEnvironment(env);
                child.start(QCoreApplication::applicationFilePath(),{"--crash-child",path,input.fileName()});QVERIFY(child.waitForFinished(30000));QCOMPARE(child.exitCode(),87);
                QVERIFY(copy.open());QCOMPARE(scalar(copy,"PRAGMA integrity_check").toString(),QString("ok"));QCOMPARE(suppliers(copy),before);
                if(phase=="before_commit"){QCOMPARE(OrderClosureJournal::pending(copy,uid,&error),c);QCOMPARE(scalar(copy,"SELECT COUNT(*) FROM client_payments").toInt(),paymentsBefore);}
                else QCOMPARE(scalar(copy,"SELECT COUNT(*) FROM client_payments").toInt(),paymentsBefore+1);
            }
            const auto r=LocalWorkshopBackend::request(copy,"closure-commit",c);QVERIFY2(r.ok,qPrintable(QJsonDocument(r.body).toJson()));
            QCOMPARE(scalar(copy,"SELECT COUNT(*) FROM local_workshop_commands").toInt(),1);QCOMPARE(suppliers(copy),before);copy.close();
        }QSqlDatabase::removeDatabase(conn);
    }
};
int main(int argc,char** argv) {
    QApplication app(argc,argv);const auto args=app.arguments();
    if(args.size()==3 && args[1].startsWith("--offline-")) {
        const auto fontId=QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR")+"/Fonts/times.ttf");
        if(fontId>=0)QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(fontId).first(),11));
        QStandardPaths::setTestModeEnabled(true);QCoreApplication::setOrganizationName("CarServiceTests");QCoreApplication::setApplicationName("Offline-"+QUuid::createUuid().toString(QUuid::Id128));
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,QFileInfo(args[2]).absolutePath());
        auto db=QSqlDatabase::addDatabase("QSQLITE","carservice_main");db.setDatabaseName(args[2]);if(!db.open() || !login(db))return 10;
        QSqlQuery q(db);if(!q.exec("CREATE TABLE IF NOT EXISTS settings(key TEXT PRIMARY KEY,value TEXT NOT NULL)"))return 11;
        if(args[1]=="--offline-switch") {
            if(CloudSyncSettings::localBackendEnabled())return 12;
            auto settings=CloudSyncSettings::load();settings.apiToken="test-token-not-a-credential";settings.enabled=true;settings.offlineMode=true;QString error;
            if(!CloudSyncSettings::save(settings,&error)){qWarning()<<error;return 13;}
            if(CloudSyncSettings::localBackendEnabled() || CloudSyncSettings::serverRequestsAllowed() || CloudSyncSettings::load().enabled)return 14;
        } else {
            if(!CloudSyncSettings::localBackendEnabled() || CloudSyncSettings::serverRequestsAllowed())return 15;
            CloudSyncPage page;page.show();QApplication::processEvents();
            auto* check=page.findChild<QCheckBox*>("offlineBackendMode");if(!check || !check->isChecked())return 16;
            const auto screenshot=qEnvironmentVariable("CS_OFFLINE_SCREENSHOT");if(!screenshot.isEmpty()){page.resize(1100,780);QApplication::processEvents();if(!page.grab().save(screenshot))return 20;}
            if(!LocalWorkshopBackend::request(db,"closure-preview",{{"order_uid","o1"}}).ok)return 17;
            if(!LocalWorkshopBackend::request(db,"closure-commit",command(db,"o1",500000)).ok)return 18;
            auto settings=CloudSyncSettings::load();settings.offlineMode=false;QString error;
            if(CloudSyncSettings::save(settings,&error) || error.isEmpty())return 19;
        }
        QString error;QElapsedTimer timer;timer.start();if(CloudSyncClient::testConnection(&error) || error.isEmpty() || timer.elapsed()>1000)return 21;
        return 0;
    }
    if(args.size()==4 && args[1]=="--crash-child") {
        auto db=QSqlDatabase::addDatabase("QSQLITE","child");db.setDatabaseName(args[2]);if(!db.open() || !login(db))return 2;
        QFile input(args[3]);if(!input.open(QIODevice::ReadOnly))return 3;
        auto r=LocalWorkshopBackend::request(db,"closure-commit",QJsonDocument::fromJson(input.readAll()).object());
        if(!r.ok)qWarning()<<r.body;return r.ok?0:4;
    }
    LocalWorkshopBackendTests test;return QTest::qExec(&test,argc,argv);
}
#include "LocalWorkshopBackendTests.moc"
