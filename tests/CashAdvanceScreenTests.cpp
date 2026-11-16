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
#include <QTabWidget>
#include "core/services/orders/CashOutflow.h"
#include "core/services/orders/CashBankService.h"
#include <QTemporaryDir>
#include "app/App.h"
#include "core/services/AccessControlService.h"
#include "core/services/ActivityAuditService.h"
#include "core/services/orders/CashAdvanceService.h"
#include "core/services/orders/CashOpeningService.h"
#include "core/services/orders/CashShiftService.h"
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
        q.finish();
        using Step=CashShiftService::Action;
        auto id=[] {return QUuid::createUuid().toString(QUuid::WithoutBraces);};
        auto prior=CashShiftService::current().uid;
        auto transition=[&](Step action,qint64 count,QString target={},QString reason={}){
            return CashShiftService::apply({id(),prior,action,count,target,reason});
        };
        QVERIFY(q.exec("INSERT INTO staff_accounts VALUES('next','','Second operator','administrator',1)"));
        QVERIFY(transition(Step::Freeze,0).recorded);
        cash.requestUid=id();
        QVERIFY(!CashAdvanceService::post(cash).error.isEmpty());
        QVERIFY(transition(Step::Close,59000,{},"незбіг").recorded);
        QCOMPARE(CashShiftService::current().phase,"closing");
        auto handOverButton=page.findChild<QPushButton*>("cashShift");
        QVERIFY(handOverButton);
        handOverButton->click();
        auto* handover=page.findChild<QDialog*>("cashShiftForm");
        QVERIFY(handover);
        auto* actions=handover->findChild<QComboBox*>("shiftAction");
        auto* recipients=handover->findChild<QComboBox*>("shiftRecipient");
        actions->setCurrentIndex(actions->findData(int(Step::HandOver)));
        recipients->setCurrentIndex(recipients->findData("next"));
        handover->findChild<QLineEdit*>("shiftCount")->setText("590,01");
        handover->findChild<QLineEdit*>("shiftReason")->setText("повторний перерахунок");
        handover->findChild<QPushButton*>("shiftSubmit")->click();
        QCOMPARE(CashShiftService::current().phase,"handover");
        handover->close();
        QTRY_VERIFY(!page.findChild<QDialog*>("cashShiftForm"));
        QVERIFY(!transition(Step::Accept,59001).recorded);
        ActivityAuditService::setCurrentActor({"next","Second operator","administrator","test"});
        CashShiftService::Command accept{id(),prior,Step::Accept,59001,{},{}};
        QVERIFY(q.exec("CREATE TRIGGER fail_successor BEFORE INSERT ON cash_shifts BEGIN SELECT RAISE(ABORT,'fixture'); END"));
        QVERIFY(!CashShiftService::apply(accept).recorded);
        QCOMPARE(CashShiftService::current().phase,"handover");
        QVERIFY(q.exec("DROP TRIGGER fail_successor"));
        auto accepted=CashShiftService::apply(accept);
        QVERIFY2(accepted.recorded,qPrintable(accepted.message));
        QVERIFY(CashShiftService::apply(accept).recorded);
        QVERIFY(!transition(Step::Accept,59001).recorded);
        prior=CashShiftService::current().uid;
        QVERIFY(transition(Step::Freeze,0).recorded);
        QVERIFY(transition(Step::Close,59001).recorded);
        QVERIFY(transition(Step::OpenNext,59001).recorded);
        QVERIFY(q.exec("SELECT COUNT(*),MIN(opening_cents) FROM cash_shifts WHERE state='open'") && q.next());
        QCOMPARE(q.value(0).toInt(),1);
        QCOMPARE(q.value(1).toLongLong(),59001);
        QVERIFY(q.exec("SELECT COUNT(*),SUM(amount_cents) FROM client_payments") && q.next());
        QCOMPARE(q.value(0).toInt(),2);
        QCOMPARE(q.value(1).toLongLong(),6001);
        QVERIFY(q.exec("SELECT COUNT(*),SUM(cents) FROM cash_advance_events WHERE method='cash'") && q.next());
        QCOMPARE(q.value(0).toInt(),2);
        QCOMPARE(q.value(1).toLongLong(),10001);
        q.finish();
        // Direct card receipt and cash expense exercise different sources of funds.
        q.prepare("INSERT INTO client_payments VALUES('card-source','original','order',9000,'card',?,'worker',1)");
        q.addBindValue(day.toString(Qt::ISODate));
        QVERIFY(q.exec());
        page.refresh();
        auto throughForm=[&](QString button,QString input){
            auto* trigger=page.findChild<QPushButton*>(button);
            QVERIFY(trigger && trigger->isEnabled());
            trigger->click();
            auto* dialog=page.findChild<QDialog*>("outflowDialog");
            QVERIFY(dialog);
            dialog->findChild<QLineEdit*>("outflowAmount")->setText(input);
            if(button=="postExpense")dialog->findChild<QLineEdit*>("outflowCategory")->setText("Матеріали");
            dialog->findChild<QLineEdit*>("outflowReason")->setText("QA");
            auto* submit=dialog->findChild<QPushButton*>("outflowPost");
            submit->click();
            auto* status=dialog->findChild<QLabel*>("outflowStatus");
            QCOMPARE(status->text(),QStringLiteral("Проведено."));
            QVERIFY(!submit->isEnabled());
            dialog->close();
            QTRY_VERIFY(!page.findChild<QDialog*>("outflowDialog"));
        };
        auto* receipts=page.findChild<QTableWidget*>("directCashPayments");
        QCOMPARE(receipts->rowCount(),1);
        receipts->selectRow(0);
        throughForm("refundReceipt","30,00");
        throughForm("postExpense","40,00");
        for(const auto& kind:QStringList{"expense","payment"}){
            q.prepare("SELECT uid,source,cents,category FROM cash_outflows WHERE kind=?");
            q.addBindValue(kind);
            QVERIFY(q.exec() && q.next());
            CashOutflow::Command cmd{q.value(0).toString(),kind,q.value(1).toString(),"QA",day,q.value(2).toLongLong(),q.value(3).toString()};
            q.finish();
            QVERIFY(CashOutflow::post(cmd).replay);
            cmd.cents++;
            QVERIFY(!CashOutflow::post(cmd).error.isEmpty());
        }
        QVERIFY(!CashOutflow::post({id(),"expense",{},"QA",day,55002,"Матеріали"}).error.isEmpty());
        QVERIFY(!CashOutflow::post({id(),"payment","card-source","QA",day,6001}).error.isEmpty());
        const auto totals=ClientReceiptReader::read(db,{"order"});
        QCOMPARE(totals.centsByOrder.value("order"),12001);
        const auto history=ClientReceiptReader::readPeriod(db,"order",day,day);
        QVERIFY(history.error.isEmpty());
        QVERIFY(std::any_of(history.payments.begin(),history.payments.end(),[](const auto& item){return item.method=="refund" && item.cents==-3000;}));
        auto advanceRefund=CashOutflow::Command{id(),"advance",sourceUid,"QA",day,3999};
        auto refunded=CashOutflow::post(advanceRefund);
        QVERIFY2(refunded.error.isEmpty(),qPrintable(refunded.error));
        QVERIFY(CashOutflow::post(advanceRefund).replay);
        valid.requestUid=id();
        valid.cents=1;
        QVERIFY(!CashAdvanceService::post(valid).error.isEmpty());
        advanceRefund.uid=id();
        QVERIFY(!CashOutflow::post(advanceRefund).error.isEmpty());
        QVERIFY(q.exec("SELECT COUNT(*),SUM(cents) FROM cash_outflows") && q.next());
        QCOMPARE(q.value(0).toInt(),3);
        QCOMPARE(q.value(1).toLongLong(),10999);
        q.finish();
        QVERIFY(q.exec("UPDATE access_role_permissions SET granted=0 WHERE role_id='administrator' AND permission_key='cash.expense'"));
        QVERIFY(!CashOutflow::allowed(true));
        QVERIFY(!CashOutflow::post({id(),"expense",{},"QA",day,1,"Матеріали"}).error.isEmpty());
        QVERIFY(CashOutflow::allowed(false));
        page.refresh();
        page.findChild<QTabWidget*>()->setCurrentIndex(1);
        QVERIFY(q.exec("SELECT uid FROM cash_advance_events WHERE method='cash' AND cents=10000") && q.next());
        const auto originalCash=q.value(0).toString();
        q.finish();
        for(int row=0;row<displayed->rowCount();++row)
            if(displayed->item(row,0)->data(Qt::UserRole).toString()==originalCash)displayed->selectRow(row);
        throughForm("refundReceipt","21,01");
        QVERIFY(q.exec("SELECT COUNT(*),SUM(cents) FROM cash_outflows WHERE shift_uid IS NOT NULL") && q.next());
        QCOMPARE(q.value(0).toInt(),2);
        QCOMPARE(q.value(1).toLongLong(),6101);
        q.finish();
        QVERIFY(q.exec("SELECT uid FROM cash_outflows WHERE kind='advance' AND cents=2101") && q.next());
        const auto mistaken=q.value(0).toString();
        q.finish();
        auto reverse=CashOutflow::Command{id(),"reverse",mistaken,"Помилка введення",day,2100};
        QVERIFY(!CashOutflow::post(reverse).error.isEmpty());
        reverse.cents=2101;
        auto* tabs=page.findChild<QTabWidget*>();
        tabs->setCurrentIndex(2);
        auto* outflows=qobject_cast<QTableWidget*>(tabs->currentWidget());
        QVERIFY(outflows);
        for(int row=0;row<outflows->rowCount();++row)
            if(outflows->item(row,0)->data(Qt::UserRole).toString()==mistaken)outflows->selectRow(row);
        page.findChild<QPushButton*>("reverseRefund")->click();
        auto* dialog=page.findChild<QDialog*>("outflowDialog");
        QVERIFY(dialog);
        QCOMPARE(dialog->findChild<QLineEdit*>("outflowAmount")->text(),"21.01");
        dialog->findChild<QLineEdit*>("outflowReason")->setText(reverse.reason);
        dialog->findChild<QPushButton*>("outflowPost")->click();
        QCOMPARE(dialog->findChild<QLabel*>("outflowStatus")->text(),QStringLiteral("Проведено."));
        QVERIFY(!CashOutflow::post(reverse).error.isEmpty());
        q.prepare("SELECT uid FROM cash_outflows WHERE reversed_uid=?");q.addBindValue(mistaken);
        QVERIFY(q.exec() && q.next());reverse.uid=q.value(0).toString();q.finish();
        QVERIFY(CashOutflow::post(reverse).replay);
        auto atomic=CashOutflow::Command{id(),"advance",originalCash,"QA",day,1};
        QVERIFY(q.exec("CREATE TRIGGER failed_outflow_audit BEFORE INSERT ON activity_audit_events BEGIN SELECT RAISE(ABORT,'fixture'); END"));
        QVERIFY(!CashOutflow::post(atomic).error.isEmpty());
        QVERIFY(q.exec("DROP TRIGGER failed_outflow_audit"));
        auto recovered=CashOutflow::post(atomic);
        QVERIFY(recovered.error.isEmpty() && !recovered.replay);
        QVERIFY(CashOutflow::post(atomic).replay);
        prior=CashShiftService::current().uid;
        QVERIFY(transition(Step::Freeze,0).recorded);
        QVERIFY(transition(Step::Close,55000).recorded);
        QCOMPARE(CashShiftService::current().phase,"closed");
        ActivityAuditService::setCurrentActor({"worker","Operator","owner","test"});
        QVERIFY(q.exec("INSERT INTO staff_accounts VALUES('delegated','','Responsible','manager',1)"));
        QVERIFY(q.exec("PRAGMA query_only=ON"));
        const auto summary=CashOutflow::totals(day,day);
        QVERIFY2(summary.error.isEmpty(),qPrintable(summary.error));
        QCOMPARE(summary.received,29001);
        QCOMPARE(summary.refunded,9101);
        QCOMPARE(summary.net,22001);
        QCOMPARE(summary.freeAdvances,10000);
        const auto earlier=CashOutflow::totals(day.addDays(-1),day.addDays(-1));
        QCOMPARE(earlier.received,0);
        QCOMPARE(earlier.freeAdvances,summary.freeAdvances);
        QCOMPARE(earlier.allNet,summary.net);
        QVERIFY(q.exec("PRAGMA query_only=OFF"));
        const auto bank=id();
        using Bank=CashBankService::Command;
        QVERIFY(CashBankService::post(Bank{id(),"account",bank,"QA bank","bank",{},"Налаштування",day}).error.isEmpty());
        const auto bankPayment=id();
        q.prepare("INSERT INTO client_payments VALUES(?,?,'order',100000,'card',?,'worker',1)");
        q.addBindValue(bankPayment);q.addBindValue(id());q.addBindValue(day.toString(Qt::ISODate));
        QVERIFY(q.exec());
        auto beforeBank=ClientReceiptReader::read(db,{"order"});
        Bank credit{id(),"credit",bank,"QA-82",{},{},"Виписка",day,98000,2000,0};
        auto credited=CashBankService::post(credit);
        QVERIFY2(credited.error.isEmpty(),qPrintable(credited.error));
        QVERIFY(CashBankService::post(credit).replay);
        credit.uid=id();
        QVERIFY(!CashBankService::post(credit).error.isEmpty());
        Bank match{id(),"match",bank,"QA-82","payment",bankPayment,"Звірено з чеком",day,0,0,100000};
        auto matched=CashBankService::post(match);
        QVERIFY2(matched.error.isEmpty(),qPrintable(matched.error));
        QVERIFY(CashBankService::post(match).replay);
        match.uid=id();QVERIFY(!CashBankService::post(match).error.isEmpty());
        auto creditedRows=CashBankService::credits(error);
        QVERIFY2(error.isEmpty(),qPrintable(error));
        QCOMPARE(creditedRows.size(),1);
        QCOMPARE(creditedRows.front().unmatched,0);
        QCOMPARE(creditedRows.front().net,98000);
        QCOMPARE(creditedRows.front().fee,2000);
        QCOMPARE(ClientReceiptReader::read(db,{"order"}).centsByOrder,beforeBank.centsByOrder);
        auto grants=AccessControlService::grantedPermissions(db,"manager",&error);
        grants.unite({"cash.view","cash.receive","cash.shift","cash.configure"});
        QVERIFY2(AccessControlService::setRolePermissions(db,"manager",grants,&error),qPrintable(error));
        QVERIFY(CashShiftService::recipients().contains({"delegated","Responsible"}));
        ActivityAuditService::setCurrentActor({"delegated","Responsible","manager","test"});
        QVERIFY(CashAdvanceService::designateSingleWorkstation().isEmpty());
        QVERIFY(transition(Step::OpenNext,55000).recorded);
        cash.requestUid=id();
        QVERIFY(CashAdvanceService::post(cash).error.isEmpty());
        QVERIFY(!CashAdvanceService::postingAuthorityError("cash.bank").isEmpty());
        QVERIFY(!CashAdvanceService::postingAuthorityError("cash.allocate").isEmpty());
        QVERIFY(!CashAdvanceService::postingAuthorityError("cash.reconcile").isEmpty());
        CashJournalPage delegatedPage(nullptr);
        QVERIFY(delegatedPage.findChild<QPushButton*>("receiveAdvance")->isEnabled());
        QVERIFY(!delegatedPage.findChild<QPushButton*>("allocateAdvance")->isEnabled());
        ActivityAuditService::setCurrentActor({"worker","Operator","owner","test"});
        grants.remove("cash.receive");
        QVERIFY(AccessControlService::setRolePermissions(db,"manager",grants,&error));
        QVERIFY(AccessControlService::ensureSchema(db,&error));
        ActivityAuditService::setCurrentActor({"delegated","Responsible","manager","test"});
        cash.requestUid=id();
        QVERIFY(!CashAdvanceService::post(cash).error.isEmpty());
    }
};
QTEST_MAIN(CashAdvanceScreenTests)
#include "CashAdvanceScreenTests.moc"
