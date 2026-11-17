#include "core/services/orders/CashAdvanceService.h"
#include "core/services/orders/CashBankService.h"
#include "core/services/orders/CashAccountMovement.h"
#include "core/services/orders/CashTreasury.h"
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>
#include <QDir>
#include <QDialog>
#include <QFrame>
#include <QLabel>
#include <QSqlQuery>
#include <QSqlError>
#include <QStackedWidget>
#include <QToolButton>
#include <QTableWidget>
#include "ui/main/MainWindow.h"
#include "core/services/ActivityAuditService.h"
#include "core/services/orders/ClientReceiptReader.h"
#include "core/services/orders/OrderClosureJournal.h"

void checkFinanceHeaderActions(QSqlDatabase db) {
    QString cashError;
    QVERIFY2(OrderClosureJournal::ensureSchema(db,&cashError),qPrintable(cashError));
    QSqlQuery receipt(db);
    receipt.prepare("INSERT INTO client_payments(payment_uid,request_uid,order_uid,amount_cents,method,paid_on,actor,recorded_ms) VALUES('ui-cash-record','ui-cash-request','ui-order',18725,'cash',?,'ui-operator',1)");
    receipt.addBindValue(QDate::currentDate().toString(Qt::ISODate));
    QVERIFY2(receipt.exec(),qPrintable(receipt.lastError().text()));
    ActivityAuditService::setCurrentActor({"owner","Owner","owner","test"});
    MainWindow window;
    window.resize(1280, 800);
    window.show();
    auto* header = window.findChild<QFrame*>("top_pill");
    auto* pages = window.findChild<QStackedWidget*>("pages");
    QVERIFY(header && pages);
    auto* rate = header->findChild<QToolButton*>("headerExchangeRate");
    auto* statements = header->findChild<QToolButton*>("headerStatements");
    QVERIFY(rate && statements);
    auto* logo = header->findChild<QWidget*>("animated_logo");
    QVERIFY(logo);
    QCOMPARE(logo->accessibleName(), QStringLiteral("CarService PointG"));
    QCOMPARE(rate->text(), QStringLiteral("Курс"));
    QCOMPARE(statements->text(), QStringLiteral("Виписки"));
    for (int index : {1, 3, 9}) {
        pages->setCurrentIndex(index);
        QCoreApplication::processEvents();
        for (auto* button : {rate, statements}) {
            QVERIFY(button->isVisibleTo(&window));
            QCOMPARE(button->isEnabled(), button == statements);
            QVERIFY(!button->icon().isNull());
            QVERIFY(button->toolTip().contains(button->text()));
            QTRY_VERIFY2(header->rect().contains(QRect(button->mapTo(header,QPoint()),button->size())),
                         qPrintable(QString("%1 in %2x%3 at %4,%5 size %6x%7").arg(button->text()).arg(header->width())
                         .arg(header->height()).arg(button->mapTo(header,QPoint()).x()).arg(button->mapTo(header,QPoint()).y())
                         .arg(button->width()).arg(button->height())));
            button->click();
            QCOMPARE(pages->currentIndex(), index);
        }
        auto* dialog = window.findChild<QDialog*>("headerStatementsWindow");
        QVERIFY(dialog && dialog->isVisible());
        statements->click();
        QCOMPARE(window.findChildren<QDialog*>("headerStatementsWindow").size(), 1);
        QCOMPARE(window.findChild<QDialog*>("headerStatementsWindow"), dialog);
        QVERIFY(logo->isVisibleTo(&window));
        const QRect brand(logo->mapTo(header, QPoint()), logo->size());
        QVERIFY(header->rect().contains(brand));
        QVERIFY(brand.width() > 0);
        if (index == 1) {
            auto* title = header->findChild<QLabel*>("shellPageTitle");
            QVERIFY(title && title->isVisible() && title->text() == QStringLiteral("Поточні наряди"));
            auto* tile = pages->currentWidget()->findChild<QWidget*>("currentOrderActionTile3");
            QVERIFY(tile);
            const auto difference = [&] {
                return logo->mapToGlobal(logo->rect().topRight()).x() -
                       tile->mapToGlobal(tile->rect().topRight()).x();
            };
            QTRY_VERIFY2(qAbs(difference()) <= 1,qPrintable(QString::number(difference())));
            const auto captures = qEnvironmentVariable("CS_HEADER_SIZE_CAPTURES");
            if (!captures.isEmpty()) QVERIFY(window.grab().save(QDir(captures).filePath("orders.png")));
        }
        QVERIFY(brand.right() < rate->mapTo(header, QPoint()).x());
        QVERIFY(rate->mapTo(header, rate->rect().topRight()).x() < statements->mapTo(header,QPoint()).x());
    }
    const QStringList headerNames{"headerWarehouse", "headerExpenses", "headerInventoryBalance",
                                  "headerExchangeRate", "headerCash", "headerStatements"};
    const QString captures = qEnvironmentVariable("CS_HEADER_SIZE_CAPTURES");
    for (const auto& size : {QSize(1280,720), QSize(1366,768), QSize(1920,1080)}) {
        window.resize(size);
        QCoreApplication::processEvents();
        int edge = logo->mapTo(header, logo->rect().topRight()).x();
        for (const auto& name : headerNames) {
            auto* button = header->findChild<QToolButton*>(name);
            QVERIFY2(button && button->isVisibleTo(&window), qPrintable(name));
            QVERIFY(!button->icon().isNull());
            const QRect bounds(button->mapTo(header, QPoint()), button->size());
            QVERIFY2(header->rect().contains(bounds) && bounds.left() > edge, qPrintable(name));
            edge = bounds.right();
        }
        auto* cash = header->findChild<QToolButton*>("headerCash");
        QVERIFY(cash->isEnabled());
        cash->click();
        QTRY_COMPARE(pages->currentWidget()->objectName(),QStringLiteral("cashJournalPage"));
        auto* journalPage = pages->currentWidget();
        auto* entries = journalPage->findChild<QTableWidget*>("directCashPayments");
        QVERIFY(entries);
        QTRY_VERIFY(entries->rowCount() > 0);
        const auto receipts = entries->findItems(QStringLiteral("ui-order"),Qt::MatchExactly);
        QCOMPARE(receipts.size(),1);
        QCOMPARE(receipts.front()->data(Qt::UserRole).toString(),QStringLiteral("ui-cash-record"));
        QCOMPARE(entries->item(receipts.front()->row(),3)->text(),ClientReceiptReader::money(18725));
        auto* operations = journalPage->findChild<QTableWidget*>("cashOperations");
        QVERIFY(operations);
        QTRY_COMPARE(operations->findItems("ui-order", Qt::MatchExactly).size(), 1);
        QTRY_VERIFY(operations->viewport()->height()>=operations->rowHeight(0));
        auto* balance = journalPage->findChild<QLabel*>("cashTotal");
        QVERIFY(balance);
        QTRY_VERIFY(balance->height() >= balance->heightForWidth(balance->width()));
        cash->click();
        QCOMPARE(pages->currentWidget(),journalPage);
        if (!captures.isEmpty()) QVERIFY(window.grab().save(QDir(captures).filePath(QString("shell-%1.png").arg(size.width()))));
    }
    const QString path = qEnvironmentVariable("CS_HEADER_PREVIEW");
    if (!path.isEmpty()) QVERIFY(header->grab().save(path));
}

void checkSupplierCashDocument(QSqlDatabase db) {
        // The backend mode is captured for the lifetime of the process.
        if (!qEnvironmentVariableIsSet("CS_TEST_CASH_SUPPLIER_CHILD")) {
            QProcess child;
            auto env = QProcessEnvironment::systemEnvironment();
            env.insert("CS_TEST_CASH_SUPPLIER_CHILD","1");
            child.setProcessEnvironment(env);
            QTemporaryDir logs;
            QVERIFY(logs.isValid());
            const auto report = logs.filePath("supplier.txt");
            child.start(QCoreApplication::applicationFilePath(),{"supplierCashDocumentIsAtomic","-o",report+",txt"});
            QVERIFY(child.waitForFinished(60000));
            QFile log(report);
            QVERIFY(log.open(QIODevice::ReadOnly));
            const auto output = log.readAll();
            QVERIFY2(child.exitStatus()==QProcess::NormalExit && child.exitCode()==0 &&
                     output.contains("3 passed, 0 failed, 0 skipped") && !output.contains("QCRITICAL"),output.constData());
            return;
        }
        QString error;
        QVERIFY2(PurchasePaymentService::ensureSchema(db,&error),qPrintable(error));
        error = CashAdvanceService::designateSingleWorkstation();
        QVERIFY2(error.isEmpty(),qPrintable(error));
        auto id=[] { return QUuid::createUuid().toString(QUuid::WithoutBraces); };
        const auto bank=id();
        const auto day=QDate::currentDate();
        QVERIFY(CashBankService::post({id(),"account",bank,"QA bank","bank",{},"QA",day}).error.isEmpty());
        QVERIFY(CashAccountMovement::post({id(),{},bank,"Звірено",day,15000}).error.isEmpty());
        const auto debts=CashTreasury::openDebts(error);
        QVERIFY2(error.isEmpty(),qPrintable(error));
        const auto found=std::find_if(debts.cbegin(),debts.cend(),[](const auto& debt){ return debt.source=="part" && debt.id==1; });
        QVERIFY(found!=debts.cend());
        CashTreasury::Command cmd{id(),"supplier",bank,"Оплата придбання",10000,day,*found};
        auto posted=CashTreasury::post(cmd);
        QVERIFY2(posted.error.isEmpty(),qPrintable(posted.error));
        QVERIFY(CashTreasury::post(cmd).replay);
        qint64 balance=0;
        QVERIFY(CashAccountMovement::balance(db,bank,balance).isEmpty());
        QCOMPARE(balance,5000);
        auto updated=PurchasePaymentService::detail(db,"part",1,"order-a",&error);
        QVERIFY2(error.isEmpty(),qPrintable(error));
        QCOMPARE(updated.paidCents,10000);
        cmd.uid=id();cmd.debt=updated;cmd.cents=4000;
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TRIGGER abort_cash_supplier BEFORE INSERT ON cash_treasury_entries BEGIN SELECT RAISE(ABORT,'QA'); END"));
        const auto rejected=CashTreasury::post(cmd);
        QVERIFY2(rejected.error.contains("QA"),qPrintable(rejected.error));
        QCOMPARE(PurchasePaymentService::detail(db,"part",1,"order-a",&error).paidCents,10000);
        QVERIFY(CashAccountMovement::balance(db,bank,balance).isEmpty());
        QCOMPARE(balance,5000);
        QVERIFY(q.exec("SELECT COUNT(*) FROM purchase_payment_events WHERE amount_cents>0") && q.next());
        QCOMPARE(q.value(0).toInt(),1);
    }
