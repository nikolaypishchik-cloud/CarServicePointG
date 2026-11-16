#include <QtTest>
#include <QComboBox>
#include <QDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTabWidget>
#include "core/services/orders/CashOutflow.h"
#include "core/services/orders/CashBankService.h"
#include "core/services/orders/CashOperations.h"
#include "core/services/orders/CashAccountMovement.h"
#include "core/services/orders/CashTreasury.h"
#include "core/services/orders/CashDeskPolicy.h"
#include "core/services/orders/CashReport.h"
#include <QTemporaryDir>
#include "app/App.h"
#include "core/backup/BackupSnapshotService.h"
#include "core/services/AccessControlService.h"
#include "core/services/ActivityAuditService.h"
#include "core/services/orders/CashAdvanceService.h"
#include "core/services/orders/CashOpeningService.h"
#include "core/services/orders/CashShiftService.h"
#include "core/services/orders/ClientReceiptReader.h"
#include "core/services/orders/OrderClosureJournal.h"
#include "infrastructure/db/sqlite/Db.h"
#include "ui/main/CashJournalPage.h"
#include "ui/main/CashOperationsWidget.h"

class CashAdvanceScreenTests : public QObject {
    Q_OBJECT
private slots:
    void postAndReplayFromScreen() {
        // The headless Windows plugin does not enumerate the system fonts.
        const auto fontFile=qEnvironmentVariable("WINDIR")+"/Fonts/arial.ttf";
        if(QFileInfo::exists(fontFile)){
            const int fontId=QFontDatabase::addApplicationFont(fontFile);
            QVERIFY(fontId>=0);
            QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(fontId).first(),10));
        }
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
        CashDeskPolicy::Policy policy{QUuid::createUuid().toString(),"worker","Test branch","Europe/Kyiv","Тестове налаштування",{8,0},{18,0},0};
        error=CashDeskPolicy::save(policy);QVERIFY2(error.isEmpty(),qPrintable(error));
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
        const auto firstShiftId=CashShiftService::current().uid;
        QCOMPARE(CashDeskPolicy::latest(db,error,firstShiftId).uid,policy.uid);
        auto nextPolicy=policy;nextPolicy.uid=QUuid::createUuid().toString();nextPolicy.start=QTime(9,0);
        QVERIFY(CashDeskPolicy::save(nextPolicy).isEmpty());
        QCOMPARE(CashDeskPolicy::latest(db,error,firstShiftId).start,QTime(8,0));
        QCOMPARE(CashDeskPolicy::latest(db,error).start,QTime(9,0));
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
        // An unlinked receipt at the next shift's opening belongs to its reconciliation window.
        QVERIFY(q.exec("SAVEPOINT unlinked_receipt"));
        QVERIFY(q.exec("INSERT INTO client_payments SELECT 'pending-cash','pending-request','order',100,'cash',"
                       "date('now'),'worker',opened_ms FROM cash_shifts WHERE state='open'"));
        qint64 archivedBalance=0;
        QVERIFY(CashShiftService::balance(db,prior,archivedBalance).isEmpty());
        QCOMPARE(archivedBalance,59001);
        qint64 currentDrawer=0;
        QVERIFY(CashShiftService::currentBalance(currentDrawer).contains(QStringLiteral("звірка")));
        QVERIFY(q.exec("ROLLBACK TO unlinked_receipt"));
        QVERIFY(q.exec("RELEASE unlinked_receipt"));
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
        page.findChild<QTabWidget*>()->setCurrentWidget(receipts);
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
        page.findChild<QTabWidget*>()->setCurrentIndex(2);
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
        tabs->setCurrentIndex(3);
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
        page.findChild<QPushButton*>("receiveOrderPayment")->click();
        auto* payForm=page.findChild<QDialog*>("cashAdvanceForm");
        QVERIFY(payForm);
        payForm->findChild<QComboBox*>("advanceClient")->setCurrentIndex(1);
        payForm->findChild<QComboBox*>("advanceOrder")->setCurrentIndex(1);
        payForm->findChild<QComboBox*>("advanceMethod")->setCurrentIndex(2);
        payForm->findChild<QLineEdit*>("advanceAmount")->setText("10,50");
        auto* postPayment=payForm->findChild<QPushButton*>("advancePost");
        postPayment->click();
        auto* paymentNotice=payForm->findChild<QLabel*>("advanceStatus");
        QVERIFY2(paymentNotice->text().startsWith(QStringLiteral("Проведено")),qPrintable(paymentNotice->text()));
        postPayment->click();
        QVERIFY(paymentNotice->text().startsWith(QStringLiteral("Уже проведено")));
        QVERIFY(q.exec("SELECT COUNT(*),SUM(amount_cents) FROM cash_payment_details JOIN client_payments USING(payment_uid)") && q.next());
        QCOMPARE(q.value(0).toInt(),1);QCOMPARE(q.value(1).toLongLong(),1050);q.finish();
        payForm->close();
        QVERIFY(!CashOutflow::post({id(),"void_advance",sourceUid,"Перевірка",day,10000}).error.isEmpty());
        auto voidAllocation=CashOutflow::Command{id(),"void_allocation",allocationUid,"Виправлення зарахування",day,4001};
        auto voided=CashOutflow::post(voidAllocation);
        QVERIFY2(voided.error.isEmpty(),qPrintable(voided.error));
        QVERIFY(CashOutflow::post(voidAllocation).replay);
        auto adjusted=CashOutflow::totals(day,day);
        QCOMPARE(adjusted.freeAdvances,summary.freeAdvances+4001);
        QCOMPARE(adjusted.net,summary.net+1050);
        QVERIFY(q.exec("SELECT payment_uid FROM cash_payment_details") && q.next());
        const auto directUid=q.value(0).toString();q.finish();
        const auto recordedPayment = CashOperations::read(day, day, directUid, "payment", {}, 0);
        QVERIFY2(recordedPayment.error.isEmpty(), qPrintable(recordedPayment.error));
        QCOMPARE(recordedPayment.rows.size(), 1);
        QVERIFY(!recordedPayment.rows.front().name.isEmpty());
        QCOMPARE(recordedPayment.rows.front().amount, 1050);
        auto voidPayment=CashOutflow::Command{id(),"void_payment",directUid,"Помилка оплати",day,1050};
        QVERIFY(CashOutflow::post(voidPayment).error.isEmpty());
        QVERIFY(CashOutflow::post(voidPayment).replay);
        QCOMPARE(CashOutflow::totals(day,day).net,summary.net);
        QVERIFY(!CashOutflow::post({id(),"payment",directUid,"QA",day,1}).error.isEmpty());
        const auto bank=id();
        using Bank=CashBankService::Command;
        QVERIFY(CashBankService::post(Bank{id(),"account",bank,"QA bank","bank",{},"Налаштування",day}).error.isEmpty());
        Bank channel{id(),"channel",bank,"QA terminal",{},"Фактична комісія за випискою","Договір",day};
        QVERIFY(CashBankService::post(channel).error.isEmpty());
        QVERIFY(CashBankService::post(channel).replay);
        auto duplicateChannel=channel;duplicateChannel.uid=id();
        QVERIFY(!CashBankService::post(duplicateChannel).error.isEmpty());
        QCOMPARE(CashBankService::channels(error).size(),1);
        const auto bankPayment=id();
        q.prepare("INSERT INTO client_payments VALUES(?,?,'order',100000,'card',?,'worker',1)");
        q.addBindValue(bankPayment);q.addBindValue(id());q.addBindValue(day.toString(Qt::ISODate));
        QVERIFY(q.exec());
        auto beforeBank=ClientReceiptReader::read(db,{"order"});
        Bank credit{id(),"credit",bank,"QA-82","channel",channel.uid,"Виписка",day,98000,2000,0};
        auto badSource=credit;badSource.source=id();
        QVERIFY(!CashBankService::post(badSource).error.isEmpty());
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
        QCOMPARE(creditedRows.front().channelName,channel.reference);
        QCOMPARE(ClientReceiptReader::read(db,{"order"}).centsByOrder,beforeBank.centsByOrder);
        const auto searchBank=CashOperations::read(day,day,"QA-82","match","transfer",0);
        QVERIFY2(searchBank.error.isEmpty(),qPrintable(searchBank.error));
        QCOMPARE(searchBank.rows.size(),1);QCOMPARE(searchBank.rows.front().net,0);
        QCOMPARE(searchBank.rows.front().amount,100000);
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
        const auto safe=id();
        QVERIFY(CashBankService::post(Bank{id(),"account",safe,"QA сейф","safe",{},"Окремий рахунок",day}).error.isEmpty());
        using Move=CashAccountMovement::Command;
        QVERIFY(CashAccountMovement::post(Move{id(),{},safe,"Перерахунок",day,0}).error.isEmpty());
        Move move{id(),"primary-cash",safe,"Інкасація",day,12345};
        QVERIFY(!CashAccountMovement::post(move).error.isEmpty());
        grants.insert("cash.transfer");
        QVERIFY(AccessControlService::setRolePermissions(db,"manager",grants,&error));
        ActivityAuditService::setCurrentActor({"delegated","Responsible","manager","test"});
        const auto moved=CashAccountMovement::post(move);
        QVERIFY2(moved.error.isEmpty(),qPrintable(moved.error));
        QVERIFY(CashAccountMovement::post(move).replay);
        qint64 safeBalance=0,drawerBalance=0;
        QVERIFY(CashAccountMovement::balance(db,safe,safeBalance).isEmpty());
        QVERIFY(CashShiftService::currentBalance(drawerBalance).isEmpty());
        QCOMPARE(safeBalance,12345);QCOMPARE(drawerBalance,55001-12345);
        const auto emptySafe=id();
        Bank newSafe{id(),"account",emptySafe,"QA архів","safe",{},"Створено",day};
        ActivityAuditService::setCurrentActor({"worker","Operator","owner","test"});
        QVERIFY(CashBankService::post(newSafe).error.isEmpty());
        QVERIFY(CashAccountMovement::post({id(),{},emptySafe,"Нуль за перерахунком",day,0}).error.isEmpty());
        QVERIFY(!CashBankService::post({id(),"archive",safe,safe,{},{},"Наявний залишок",day}).error.isEmpty());
        QVERIFY(!CashBankService::post({id(),"block","primary-cash","primary-cash",{},{},"Відкрита зміна",day}).error.isEmpty());
        Bank archive{id(),"archive",emptySafe,emptySafe,{},{},"Виведено з обліку",day};
        QVERIFY(CashBankService::post(archive).error.isEmpty());
        QVERIFY(CashBankService::post(archive).replay);
        QVERIFY(!CashAccountMovement::post({id(),safe,emptySafe,"Архівний рахунок",day,1}).error.isEmpty());
        QVERIFY(!CashBankService::accounts(error,true).isEmpty());
        ActivityAuditService::setCurrentActor({"delegated","Responsible","manager","test"});
        const auto priorClientNet = CashOutflow::totals(day,day).net;
        CashTreasury::Command deposit{id(),"deposit","primary-cash","Розмінні кошти",2400,day,{}};
        const auto otherConnection=id();
        {
            auto other=QSqlDatabase::addDatabase("QSQLITE",otherConnection);
            other.setDatabaseName(db.databaseName());QVERIFY(other.open());
            QSqlQuery held(other);
            QVERIFY(held.exec("BEGIN IMMEDIATE"));
            QVERIFY(q.exec("PRAGMA busy_timeout=25"));
            QVERIFY(!CashTreasury::post(deposit).error.isEmpty());
            QVERIFY(held.exec("ROLLBACK"));
            QVERIFY(q.exec("PRAGMA busy_timeout=5000"));
            other.close();
        }
        QSqlDatabase::removeDatabase(otherConnection);
        auto receipt = CashTreasury::post(deposit);
        QVERIFY2(receipt.error.isEmpty(),qPrintable(receipt.error));
        QVERIFY(CashTreasury::post(deposit).replay);
        QVERIFY(CashShiftService::currentBalance(drawerBalance).isEmpty());
        QCOMPARE(drawerBalance,55001-12345+2400);
        CashTreasury::Command withdraw{id(),"withdrawal","primary-cash","Вилучення розміну",2400,day,{}};
        QVERIFY(CashTreasury::post(withdraw).error.isEmpty());
        QVERIFY(CashTreasury::post(withdraw).replay);
        QVERIFY(CashShiftService::currentBalance(drawerBalance).isEmpty());
        QCOMPARE(drawerBalance,55001-12345);
        QCOMPARE(CashOutflow::totals(day,day).net,priorClientNet);
        QVERIFY(!CashAccountMovement::post({id(),safe,"primary-cash","Більше за наявне",day,12346}).error.isEmpty());
        ActivityAuditService::setCurrentActor({"worker","Operator","owner","test"});
        QVERIFY(q.exec("WITH RECURSIVE n(i) AS (VALUES(1) UNION ALL SELECT i+1 FROM n WHERE i<103) "
                       "INSERT INTO client_payments SELECT 'export-'||i,'export-request-'||i,'test-export-'||i,100,'card',date('now','localtime'),'qa',i FROM n"));
        const auto screenRows=CashOperations::read(day,day,"test-export","payment","card",0);
        QVERIFY2(screenRows.error.isEmpty(),qPrintable(screenRows.error));
        QCOMPARE(screenRows.rows.size(),100);
        QVERIFY(screenRows.more);
        const auto exportRows=CashOperations::exportSelection(day,day,"test-export","payment","card");
        QVERIFY2(exportRows.error.isEmpty(),qPrintable(exportRows.error));
        QCOMPARE(exportRows.rows.size(),103);
        const auto acquiring=CashReport::read(CashReport::Type::Acquiring,day,day);
        QVERIFY2(acquiring.error.isEmpty(),qPrintable(acquiring.error));
        QVERIFY(acquiring.totals.join(" ").contains(ClientReceiptReader::money(98000)));
        const auto transferReport=CashReport::read(CashReport::Type::Transfers,day,day);
        QVERIFY2(transferReport.error.isEmpty(),qPrintable(transferReport.error));
        QCOMPARE(transferReport.rows.size(),1);
        QCOMPARE(transferReport.rows.front().at(5),ClientReceiptReader::money(12345));
        const auto shiftReport=CashReport::read(CashReport::Type::Shift,day,day,CashShiftService::current().uid);
        QVERIFY2(shiftReport.error.isEmpty(),qPrintable(shiftReport.error));
        QVERIFY(shiftReport.totals.join(" ").contains(ClientReceiptReader::money(drawerBalance)));
        const auto discrepancies=CashReport::read(CashReport::Type::Discrepancies,day,day);
        QVERIFY2(discrepancies.error.isEmpty(),qPrintable(discrepancies.error));
        QVERIFY(!discrepancies.rows.isEmpty());
        const auto movements=CashReport::read(CashReport::Type::Account,day,day,safe);
        QVERIFY2(movements.error.isEmpty(),qPrintable(movements.error));
        QVERIFY(movements.totals.join(" ").contains(ClientReceiptReader::money(12345)));
        const auto periodReport=CashReport::read(CashReport::Type::Period,day,day);
        QVERIFY2(periodReport.error.isEmpty(),qPrintable(periodReport.error));
        QVERIFY(std::any_of(periodReport.rows.cbegin(),periodReport.rows.cend(),[&](const QStringList& row){
            return row.at(1).endsWith(safe) && row.at(2)==ClientReceiptReader::money(safeBalance);
        }));
        const auto exported=qEnvironmentVariable("CS_CASH_PDF_SAMPLE",work.filePath("cash-journal.pdf"));
        error=writeCashJournalPdf(exported,day,day,"test-export","payment","card");
        QVERIFY2(error.isEmpty(),qPrintable(error));
        QVERIFY(QFileInfo(exported).size()>0);
        const auto backupRoot=work.filePath("cash-backups");
        QVERIFY(QDir().mkpath(backupRoot));
        QVERIFY(Database::setSetting("settings.int.autobackup.dir",backupRoot));
        SnapshotJobContext job;
        job.event.entityType="cash";job.event.operationType="test_snapshot";
        SnapshotMetadata meta;
        QVERIFY2(BackupSnapshotService::createSnapshot(job,&meta,&error),qPrintable(error));
        QVERIFY(QFileInfo(meta.snapshotPath).absoluteFilePath().startsWith(backupRoot+"/"));
        QVERIFY2(BackupSnapshotService::rehearseRestore(meta.snapshotPath,&error),qPrintable(error));
        const auto restoredDb=work.filePath("restored-cash.db");
        QVERIFY(QFile::copy(meta.snapshotPath,restoredDb));
        const auto restoreId=id();
        {
            auto restored=QSqlDatabase::addDatabase("QSQLITE",restoreId);
            restored.setConnectOptions("QSQLITE_OPEN_READONLY");restored.setDatabaseName(restoredDb);
            QVERIFY(restored.open());
            qint64 recoveredDrawer=0,recoveredSafe=0;
            QVERIFY(CashShiftService::balance(restored,CashShiftService::current().uid,recoveredDrawer).isEmpty());
            QVERIFY(CashAccountMovement::balance(restored,safe,recoveredSafe).isEmpty());
            QCOMPARE(recoveredDrawer,drawerBalance);QCOMPARE(recoveredSafe,safeBalance);
            for(const auto& table:db.tables()){
                if(!table.startsWith("cash_") && table!="client_payments" && table!="activity_audit_events")continue;
                QSqlQuery original(db),copy(restored);
                const QString sql="SELECT * FROM main."+table+" ORDER BY rowid";
                QVERIFY(original.exec(sql) && copy.exec(sql));
                while(original.next()){
                    QVERIFY(copy.next());
                    for(int col=0;col<original.record().count();++col)QCOMPARE(copy.value(col),original.value(col));
                }
                QVERIFY(!copy.next());
            }
            restored.close();
        }
        QSqlDatabase::removeDatabase(restoreId);
        grants.remove("cash.receive");
        QVERIFY(AccessControlService::setRolePermissions(db,"manager",grants,&error));
        QVERIFY(AccessControlService::ensureSchema(db,&error));
        ActivityAuditService::setCurrentActor({"delegated","Responsible","manager","test"});
        cash.requestUid=id();
        QVERIFY(!CashAdvanceService::post(cash).error.isEmpty());
        QVERIFY(!CashOperations::exportSelection(day,day,{}, {}, {}).error.isEmpty());
    }
};
QTEST_MAIN(CashAdvanceScreenTests)
#include "CashAdvanceScreenTests.moc"
