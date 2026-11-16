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
#include "core/crm/CrmFinancialService.h"
#include "core/services/orders/ClientReceiptReader.h"
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
    void periodSeparatesPriorReceiptsAndPartialPayments() {
        const QDate day(2026,11,15);
        QVERIFY(!ClientReceiptReader::readPeriod(db,"o1",day,day).available);
        QString error;
        QVERIFY2(OrderClosureJournal::ensureSchema(db,&error),qPrintable(error));
        QSqlQuery q(db);
        QVERIFY(q.exec("INSERT INTO client_payments VALUES "
                       "('p0','r0','o1',100,'cash','2026-11-14','a',5),"
                       "('p1','r1','o1',200,'card','2026-11-15','a',2),"
                       "('p2','r2','o1',200,'cash','2026-11-15','a',3),"
                       "('p3','r3','o1',300,'cash','2026-11-16','a',1),"
                       "('p4','r4','o2',400,'cash','2026-11-15','a',4)"));
        QVERIFY(q.exec("PRAGMA query_only=1"));
        const auto result=ClientReceiptReader::readPeriod(db,"o1",day,day);
        QVERIFY2(result.error.isEmpty(),qPrintable(result.error));
        QVERIFY(result.available);
        QCOMPARE(result.receivedBefore,100);
        QCOMPARE(result.payments.size(),2);
        QCOMPARE(result.payments[0].uid,QStringLiteral("p1"));
        QCOMPARE(result.payments[1].uid,QStringLiteral("p2"));
        QCOMPARE(result.payments[0].cents+result.payments[1].cents,400);
    }
    void cashJournalRequiresAuthorizedActiveAccount() {
        const auto day = QDate::currentDate();
        QString error;
        QSqlQuery q(db);
        QVERIFY(q.exec("INSERT INTO staff_accounts(id,role,active) VALUES('cash-admin','administrator',1),('cash-viewer','viewer',1)"));
        ActivityAuditService::setCurrentActor({"cash-admin","Admin","administrator","test"});
        QVERIFY(ClientReceiptReader::readJournal(db,day,day).error.isEmpty());
        ActivityAuditService::setCurrentActor({"cash-viewer","Viewer","viewer","test"});
        QVERIFY(!ClientReceiptReader::readJournal(db,day,day).error.isEmpty());
        owner();
        auto viewerGrants = AccessControlService::grantedPermissions(db,"viewer",&error);
        viewerGrants.insert("access.manage");
        QVERIFY(AccessControlService::setRolePermissions(db,"viewer",viewerGrants,&error));
        ActivityAuditService::setCurrentActor({"cash-viewer","Viewer","viewer","test"});
        QVERIFY(!AccessControlService::assignRole(db,"cash-viewer","administrator",&error));
        QVERIFY(!AccessControlService::saveAccount(db,{},"New admin","administrator",&error));
        viewerGrants.insert("cash.view");
        QVERIFY(!AccessControlService::setRolePermissions(db,"viewer",viewerGrants,&error));
        owner();
        QVERIFY(AccessControlService::setRolePermissions(db,"viewer",viewerGrants,&error));
        ActivityAuditService::setCurrentActor({"cash-viewer","Viewer","viewer","test"});
        QVERIFY(ClientReceiptReader::readJournal(db,day,day).error.isEmpty());
        owner();
        auto grants = AccessControlService::grantedPermissions(db,"administrator",&error);
        QVERIFY(grants.remove("cash.view"));
        QVERIFY2(AccessControlService::setRolePermissions(db,"administrator",grants,&error),qPrintable(error));
        QVERIFY(AccessControlService::ensureSchema(db,&error));
        ActivityAuditService::setCurrentActor({"cash-admin","Admin","administrator","test"});
        QVERIFY(!ClientReceiptReader::readJournal(db,day,day).error.isEmpty());
        ActivityAuditService::clearCurrentActor();
        QVERIFY(!ClientReceiptReader::readJournal(db,day,day).error.isEmpty());
    }
    void cashJournalUsesRecordedReceiptsAcrossOrders() {
        const QDate day(2026,11,15);
        const auto missing = ClientReceiptReader::readJournal(db,day,day);
        QVERIFY(missing.error.isEmpty());
        QVERIFY(!missing.available);
        QString error;
        QVERIFY2(OrderClosureJournal::ensureSchema(db,&error),qPrintable(error));
        QSqlQuery q(db);
        QVERIFY(q.exec("INSERT INTO client_payments VALUES "
                       "('a','r1','o1',325,'card','2026-11-15','staff-a',11),"
                       "('b','r2','o2',210,'cash','2026-11-15','staff-b',20),"
                       "('c','r3','o1',100,'cash','2026-11-14','staff-a',10)"));
        QVERIFY(q.exec("PRAGMA query_only=1"));
        const auto result = ClientReceiptReader::readJournal(db,day,day);
        QVERIFY2(result.error.isEmpty(),qPrintable(result.error));
        QVERIFY(result.available);
        QCOMPARE(result.directPaymentsCents,535);
        QCOMPARE(result.entries.size(),2);
        QCOMPARE(result.entries.front().payment.uid,QStringLiteral("b"));
        QCOMPARE(result.entries.front().orderUid,QStringLiteral("o2"));
        QCOMPARE(result.entries.front().actor,QStringLiteral("staff-b"));
        // The cash journal and order history are views of the same payment.
        const auto repeatedJournal = ClientReceiptReader::readJournal(db,day,day);
        const auto orderHistory = ClientReceiptReader::readPeriod(db,"o2",day,day);
        QVERIFY(repeatedJournal.error.isEmpty() && orderHistory.error.isEmpty());
        QCOMPARE(repeatedJournal.entries.size(),result.entries.size());
        QCOMPARE(repeatedJournal.directPaymentsCents,result.directPaymentsCents);
        QCOMPARE(orderHistory.payments.size(),1);
        QCOMPARE(orderHistory.payments.front().uid,result.entries.front().payment.uid);
        QCOMPARE(orderHistory.payments.front().cents,result.entries.front().payment.cents);
        QVERIFY(q.exec("SELECT COUNT(*) FROM client_payments"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(),3);
        const auto empty = ClientReceiptReader::readJournal(db,day.addDays(1),day.addDays(1));
        QVERIFY(empty.available && empty.error.isEmpty() && empty.entries.isEmpty());
        QCOMPARE(empty.directPaymentsCents,0);
    }

    void closureChargeUsesKyivClosureDate() {
        QString error;
        QVERIFY2(OrderClosureJournal::ensureSchema(db,&error),qPrintable(error));
        QSqlQuery query(db);
        query.prepare("INSERT INTO order_closure_commands VALUES(?,?,?,?,'applied')");
        const QJsonObject order{{"order_uid","o1"},{"opened_at","2026-10-12"},{"closed_at","2026-10-31T22:30:00Z"}};
        const QJsonObject response{{"order_uid","o1"},{"order",order},{"total_cents",37505}};
        query.addBindValue("c1"); query.addBindValue("o1"); query.addBindValue("{}");
        query.addBindValue(QString::fromUtf8(QJsonDocument(response).toJson()));
        QVERIFY2(query.exec(),qPrintable(query.lastError().text()));
        QVERIFY(query.exec("PRAGMA query_only=1"));
        for (int month : {10,11,12}) {
            const QDate start(2026,month,1);
            const auto result=CrmFinancialService::readClosureCharge(db,"o1",start,start.addMonths(1).addDays(-1));
            QVERIFY2(result.error.isEmpty(),qPrintable(result.error));
            QCOMPARE(result.requestUid,QStringLiteral("c1"));
            QCOMPARE(result.closedOn,QDate(2026,11,1));
            QCOMPARE(result.priorCents,month==12?37505:0);
            QCOMPARE(result.periodCents,month==11?37505:0);
            const auto open=CrmFinancialService::readClosureCharge(db,"o2",start,start.addMonths(1).addDays(-1));
            QVERIFY(open.error.isEmpty() && open.requestUid.isEmpty());
            QCOMPARE(open.periodCents,0);
            QCOMPARE(open.priorCents,0);
        }
    }
    void journalBalancesCarryBetweenMonths() {
        QString error;
        QVERIFY2(OrderClosureJournal::ensureSchema(db,&error),qPrintable(error));
        QSqlQuery q(db);
        const QJsonObject order{{"order_uid","o1"},{"closed_at","2026-11-02T12:00:00+02:00"}};
        const QJsonObject response{{"order_uid","o1"},{"order",order},{"total_cents",50000}};
        q.prepare("INSERT INTO order_closure_commands VALUES('close1','o1','{}',?,'applied')");
        q.addBindValue(QString::fromUtf8(QJsonDocument(response).toJson()));
        QVERIFY(q.exec());
        QVERIFY(q.exec("INSERT INTO client_payments VALUES "
                       "('p1','r1','o1',10000,'cash','2026-10-30','a',1),"
                       "('p2','r2','o1',20000,'card','2026-11-03','a',2),"
                       "('p3','r3','o2',90000,'cash','2026-11-03','a',3)"));
        QVERIFY(q.exec("PRAGMA query_only=1"));
        const auto month=[&](int m) {
            QDate day(2026,m,1);
            return CrmFinancialService::readOrderJournal(db,"o1",day,day.addMonths(1).addDays(-1));
        };
        const auto october=month(10),november=month(11),december=month(12);
        QVERIFY2(october.error.isEmpty(),qPrintable(october.error));
        QVERIFY2(november.error.isEmpty(),qPrintable(november.error));
        QVERIFY2(december.error.isEmpty(),qPrintable(december.error));
        QCOMPARE(october.opening,0); QCOMPARE(october.charged,0); QCOMPARE(october.paid,10000);
        QCOMPARE(october.closing,-10000);
        QCOMPARE(november.opening,october.closing);
        QCOMPARE(november.charged,50000); QCOMPARE(november.paid,20000);
        QCOMPARE(november.entries.size(),2);
        QCOMPARE(november.entries[0].sourceUid,QStringLiteral("close1"));
        QCOMPARE(november.entries[0].balance,40000);
        QCOMPARE(november.entries[1].sourceUid,QStringLiteral("p2"));
        QCOMPARE(november.entries[1].balance,20000);
        QCOMPARE(november.closing,20000);
        QCOMPARE(december.opening,november.closing); QCOMPARE(december.closing,december.opening);
        QVERIFY(december.entries.isEmpty());
    }
    void clientJournalFiltersAndMergesOrders() {
        QString error;
        QVERIFY2(OrderClosureJournal::ensureSchema(db,&error),qPrintable(error));
        QSqlQuery q(db);
        for (const auto& column:{"client_uid TEXT","vehicle_uid TEXT","car TEXT","order_number INTEGER","opened_at TEXT","discount_mode TEXT"})
            QVERIFY(q.exec("ALTER TABLE work_orders ADD COLUMN "+QString::fromLatin1(column)));
        QVERIFY(q.exec("ALTER TABLE work_order_parts ADD COLUMN amount REAL"));
        QVERIFY(q.exec("ALTER TABLE work_order_services ADD COLUMN amount REAL"));
        QVERIFY(q.exec("ALTER TABLE work_order_services ADD COLUMN order_number INTEGER"));
        QVERIFY(q.exec("UPDATE work_orders SET opened_at='2026-10-03'"));
        QVERIFY(q.exec("UPDATE work_orders SET client_uid='A',vehicle_uid=order_uid,order_number=42"));
        QVERIFY(q.exec("INSERT INTO work_orders(number,order_uid,client_uid,vehicle_uid,status) VALUES(3,'foreign','B','o2','open')"));
        QVERIFY(q.exec("UPDATE work_orders SET status='closed' WHERE order_uid='o2'"));
        QVERIFY(q.exec("INSERT INTO client_payments VALUES('p1','r1','o2',1000,'cash','2026-10-31','a',1),"
                       "('p2','r2','o2',2000,'card','2026-11-03','a',2),('p3','r3','o1',3000,'card','2026-11-02','a',3),"
                       "('p4','r4','foreign',9000,'cash','2026-11-01','a',4)"));
        QVERIFY(q.exec("PRAGMA query_only=1"));
        const QDate from(2026,11,1), through(2026,11,30);
        const auto both=CrmFinancialService::readClientJournal(db,"A",{},from,through);
        QVERIFY2(both.error.isEmpty(),qPrintable(both.error));
        QCOMPARE(both.preliminary.size(),1);
        const auto pending=both.preliminary.first();
        QVERIFY2(pending.error.isEmpty(),qPrintable(pending.error));
        QCOMPARE(pending.uid,QStringLiteral("o1")); QCOMPARE(pending.estimatedCents,500000);
        QCOMPARE(pending.openedOn,QDate(2026,10,3));
        QCOMPARE(both.opening,-1000); QCOMPARE(both.paid,5000); QCOMPARE(both.closing,-6000);
        QCOMPARE(both.unverifiedClosures,QList<int>{42});
        QCOMPARE(both.entries.size(),2);
        QCOMPARE(both.entries[0].orderUid,QStringLiteral("o1")); QCOMPARE(both.entries[0].event.balance,-4000);
        QCOMPARE(both.entries[1].orderUid,QStringLiteral("o2")); QCOMPARE(both.entries[1].event.balance,-6000);
        const auto one=CrmFinancialService::readClientJournal(db,"A","o2",from,through);
        QVERIFY2(one.error.isEmpty(),qPrintable(one.error));
        QCOMPARE(one.entries.size(),1); QCOMPARE(one.entries[0].event.sourceUid,QStringLiteral("p2"));
        QCOMPARE(one.closing,-3000);
    }
    void payments_data(){QTest::addColumn<qint64>("amount");QTest::newRow("none")<<qint64(-1);QTest::newRow("partial")<<qint64(200000);QTest::newRow("full")<<qint64(500000);}
    void payments() {
        QFETCH(qint64,amount);const auto before=suppliers(db);const auto c=command(db,"o1",amount);
        // Preview/cancel has no financial or order side effects.
        QCOMPARE(scalar(db,"SELECT status FROM work_orders WHERE number=1").toString(),QString("open"));
        QCOMPARE(scalar(db,"SELECT COUNT(*) FROM client_payments").toInt(),0);
        const auto result=LocalWorkshopBackend::request(db,"closure-commit",c);QVERIFY2(result.ok,qPrintable(QJsonDocument(result.body).toJson()));
        QCOMPARE(result.body["balance_cents"].toInteger(),qint64(500000-qMax(qint64(0),amount)));
        const auto closedDay=QDate::fromString(result.body["order"].toObject()["closed_at"].toString().left(10),Qt::ISODate);
        const auto charge=CrmFinancialService::readClosureCharge(db,"o1",closedDay,closedDay);
        QVERIFY2(charge.error.isEmpty(),qPrintable(charge.error));
        QCOMPARE(charge.requestUid,c["request_uid"].toString());
        QCOMPARE(charge.periodCents,qint64(500000));
        QCOMPARE(charge.priorCents,qint64(0));
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
    void managedCashReceiptRequiresOperatorAndOpenShift() {
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE cash_shifts(shift_uid TEXT PRIMARY KEY,state TEXT,operator_uid TEXT)"));
        QVERIFY(q.exec("INSERT INTO cash_shifts VALUES('shift','closing','local-test-owner')"));
        auto pay=command(db,"o1",1050);
        QVERIFY(!LocalWorkshopBackend::request(db,"closure-commit",pay).ok);
        QCOMPARE(scalar(db,"SELECT COUNT(*) FROM client_payments").toInt(),0);
        QVERIFY(q.exec("UPDATE cash_shifts SET state='open',operator_uid='other'"));
        QVERIFY(!LocalWorkshopBackend::request(db,"closure-commit",pay).ok);
        QVERIFY(q.exec("UPDATE cash_shifts SET operator_uid='local-test-owner'"));
        const auto posted=LocalWorkshopBackend::request(db,"closure-commit",pay);
        QVERIFY2(posted.ok,qPrintable(QJsonDocument(posted.body).toJson()));
        QCOMPARE(LocalWorkshopBackend::request(db,"closure-commit",pay).body,posted.body);
        QCOMPARE(scalar(db,"SELECT SUM(amount_cents) FROM client_payments JOIN cash_shift_receipts USING(payment_uid) WHERE shift_uid='shift'").toInt(),1050);
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
