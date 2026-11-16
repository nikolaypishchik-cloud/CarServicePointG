#include <QtTest>
#include <QComboBox>
#include <QDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTemporaryDir>
#include "app/App.h"
#include "core/services/AccessControlService.h"
#include "core/services/ActivityAuditService.h"
#include "core/services/orders/CashAdvanceService.h"
#include "core/services/orders/CashOpeningService.h"
#include "core/services/orders/ClientReceiptReader.h"
#include "core/services/orders/OrderClosureJournal.h"
#include "infrastructure/db/sqlite/Db.h"
#include "ui/main/CashJournalPage.h"

class CashAdvanceScreenTests : public QObject {
    Q_OBJECT
private slots:
    void postAndReplayFromScreen() {
        QTemporaryDir work;
        QVERIFY(work.isValid());
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("CarServiceStage2Regression");
        QCoreApplication::setApplicationName("Safety-Advance-"+QUuid::createUuid().toString(QUuid::Id128));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,work.path());
        QString error;
        QVERIFY2(Database::prepareRuntime(&error),qPrintable(error));
        QVERIFY2(Database::openPrepared(&error),qPrintable(error));
        auto db=Database::db();
        QSqlQuery q(db);
        for(const auto& sql:QStringList{
            "CREATE TABLE settings(key TEXT PRIMARY KEY,value TEXT)",
            "CREATE TABLE staff_accounts(id TEXT PRIMARY KEY,master_id TEXT DEFAULT '',display_name TEXT,role TEXT,active INTEGER)",
            "INSERT INTO staff_accounts VALUES('worker','','Operator','owner',1)",
            "CREATE TABLE crm_clients(client_uid TEXT PRIMARY KEY,full_name TEXT)",
            "INSERT INTO crm_clients VALUES('client','QA')",
            "CREATE TABLE work_orders(number INTEGER,order_uid TEXT PRIMARY KEY,client_uid TEXT,car TEXT,discount REAL,discount_mode TEXT)",
            "INSERT INTO work_orders VALUES(15,'order','client','QA car',0,'uah')",
            "CREATE TABLE work_order_services(order_uid TEXT,order_number INTEGER,qty INTEGER,price INTEGER,amount INTEGER)",
            "CREATE TABLE work_order_parts(order_uid TEXT,order_number INTEGER,qty INTEGER,price INTEGER,amount INTEGER)",
            "INSERT INTO work_order_services VALUES('order',15,1,200,200)"})
            QVERIFY2(q.exec(sql),qPrintable(q.lastError().text()));
        QVERIFY(Database::setSetting("settings.backend.offline",true));
        QVERIFY(AccessControlService::ensureSchema(db,&error));
        QVERIFY(ActivityAuditService::ensureSchema(db,&error));
        ActivityAuditService::setCurrentActor({"worker","Operator","owner","test"});
        QVERIFY(OrderClosureJournal::ensureSchema(db,&error));
        QVERIFY(ClientReceiptReader::beginTrackingNewOrder(db,"order",&error));
        const auto designated=CashAdvanceService::designateSingleWorkstation();
        QVERIFY2(designated.isEmpty(),qPrintable(designated));
        CashJournalPage page(nullptr);
        page.show();
        auto operate=[&](bool allocation,bool useCash=false) {
            page.findChild<QPushButton*>(allocation?"allocateAdvance":"receiveAdvance")->click();
            auto* form=page.findChild<QDialog*>("cashAdvanceForm");
            QVERIFY(form);
            form->findChild<QComboBox*>("advanceClient")->setCurrentIndex(1);
            if(allocation) {
                form->findChild<QComboBox*>("advanceSource")->setCurrentIndex(1);
                form->findChild<QComboBox*>("advanceOrder")->setCurrentIndex(1);
            } else form->findChild<QComboBox*>("advanceMethod")->setCurrentIndex(useCash?1:3);
            form->findChild<QLineEdit*>("advanceAmount")->setText(allocation?"40,01":"100,00");
            auto* post=form->findChild<QPushButton*>("advancePost");
            auto* status=form->findChild<QLabel*>("advanceStatus");
            post->click();
            QVERIFY2(status->text().startsWith(QStringLiteral("Проведено")),qPrintable(status->text()));
            post->click();
            QVERIFY2(status->text().startsWith(QStringLiteral("Уже проведено")),qPrintable(status->text()));
            form->close();
            QTRY_VERIFY(!page.findChild<QDialog*>("cashAdvanceForm"));
        };
        operate(false);
        operate(true);
        QVERIFY(q.exec("SELECT COUNT(*),SUM(CASE WHEN source_uid IS NULL THEN cents ELSE 0 END),"
                       "SUM(CASE WHEN source_uid IS NULL THEN cents ELSE -cents END) FROM cash_advance_events") && q.next());
        QCOMPARE(q.value(0).toInt(),2);
        QCOMPARE(q.value(1).toLongLong(),10000);
        QCOMPARE(q.value(2).toLongLong(),5999);
        QVERIFY(q.exec("SELECT COUNT(*),SUM(amount_cents) FROM client_payments WHERE order_uid='order'") && q.next());
        QCOMPARE(q.value(0).toInt(),1);
        QCOMPARE(q.value(1).toLongLong(),4001);
        auto* displayed=page.findChild<QTableWidget*>("postedAdvances");
        QVERIFY(displayed);
        QCOMPARE(displayed->rowCount(),2);
        auto allocations=displayed->findItems(QStringLiteral("Зарахування"),Qt::MatchExactly);
        QCOMPARE(allocations.size(),1);
        QCOMPARE(displayed->item(allocations.front()->row(),3)->text(),ClientReceiptReader::money(0));
        QVERIFY(q.exec("SELECT uid,source_uid FROM cash_advance_events WHERE source_uid IS NOT NULL") && q.next());
        const QString allocationUid=q.value(0).toString(), sourceUid=q.value(1).toString();
        q.finish();
        CashAdvanceService::Command retry{allocationUid,"client","order",sourceUid,"allocation",{},QDate::currentDate(),4002};
        QVERIFY(!CashAdvanceService::post(retry).error.isEmpty());
        retry.requestUid=QUuid::createUuid().toString(QUuid::WithoutBraces);
        retry.cents=6000;
        QVERIFY(!CashAdvanceService::post(retry).error.isEmpty());
        auto cash=retry;
        cash.advanceUid.clear(); cash.method="cash"; cash.cents=1;
        QVERIFY(!CashAdvanceService::post(cash).error.isEmpty());
        QVERIFY(q.exec("SELECT COUNT(*) FROM cash_advance_events") && q.next());
        QCOMPARE(q.value(0).toInt(),2);
        q.finish();
        const auto choices=CashAdvanceService::advances("client");
        QVERIFY2(choices.error.isEmpty(),qPrintable(choices.error));
        QCOMPARE(choices.rows.size(),1);
        QVERIFY(choices.rows.front().label.contains("59,99"));
        CashAdvanceService::Command valid{QUuid::createUuid().toString(QUuid::WithoutBraces),"client","order",
            sourceUid,"allocation",{},QDate::currentDate(),2000};
        QVERIFY(q.exec("CREATE TRIGGER fail_allocation BEFORE INSERT ON client_payments "
                       "BEGIN SELECT RAISE(ABORT,'fixture write failure'); END"));
        QVERIFY(!CashAdvanceService::post(valid).error.isEmpty());
        QVERIFY(q.exec("SELECT COUNT(*) FROM cash_advance_events") && q.next());
        QCOMPARE(q.value(0).toInt(),2);
        q.finish();
        QVERIFY(q.exec("DROP TRIGGER fail_allocation"));
        auto posted=CashAdvanceService::post(valid);
        QVERIFY2(posted.error.isEmpty() && !posted.replay,qPrintable(posted.error));
        posted=CashAdvanceService::post(valid);
        QVERIFY2(posted.error.isEmpty() && posted.replay,qPrintable(posted.error));
        QVERIFY(q.exec("SELECT COUNT(*),SUM(amount_cents) FROM client_payments") && q.next());
        QCOMPARE(q.value(0).toInt(),2);
        QCOMPARE(q.value(1).toLongLong(),6001);
        q.finish();
        const auto day=QDate::currentDate();
        const auto direct=ClientReceiptReader::readJournal(db,day,day);
        QVERIFY(direct.available && direct.error.isEmpty());
        QCOMPARE(direct.directPaymentsCents,0);
        using Action=CashOpeningService::Action;
        auto shift=[&](Action action,qint64 amount,QString reason={}) {
            return CashOpeningService::apply({QUuid::createUuid().toString(QUuid::WithoutBraces),action,day,amount,reason});
        };
        QVERIFY(shift(Action::Initial,50000).recorded);
        QVERIFY(!shift(Action::Initial,50000).recorded);
        const auto mismatch=shift(Action::Count,49000,"перераховано");
        QVERIFY(mismatch.recorded && !mismatch.opened);
        QVERIFY(!CashAdvanceService::post(cash).error.isEmpty());
        QVERIFY(q.exec("SELECT SUM(delta) FROM cash_opening_events") && q.next());
        QCOMPARE(q.value(0).toLongLong(),50000);
        q.finish();
        QVERIFY(!shift(Action::Adjustment,49000).recorded);
        QVERIFY(shift(Action::Adjustment,49000,"рішення після перевірки").recorded);
        QVERIFY(shift(Action::Count,49000).opened);
        operate(false,true);
        auto firstCash=CashAdvanceService::post(cash);
        QVERIFY2(firstCash.error.isEmpty(),qPrintable(firstCash.error));
        QVERIFY(CashAdvanceService::post(cash).replay);
        QVERIFY(q.exec("SELECT COUNT(*),SUM(cents) FROM cash_advance_events WHERE method='cash' AND shift_uid IS NOT NULL") && q.next());
        QCOMPARE(q.value(0).toInt(),2);
        QCOMPARE(q.value(1).toLongLong(),10001);
    }
};
QTEST_MAIN(CashAdvanceScreenTests)
#include "CashAdvanceScreenTests.moc"
