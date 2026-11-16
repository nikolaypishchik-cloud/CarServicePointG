#include <QtTest>
#include <QDir>
#include <QDialog>
#include <QFrame>
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
            QVERIFY(header->rect().contains(QRect(button->mapTo(header,QPoint()),button->size())));
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
        QVERIFY(brand.left() < header->width() / 10);
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
        auto* entries = journalPage->findChild<QTableWidget*>();
        QVERIFY(entries);
        QTRY_VERIFY(entries->rowCount() > 0);
        const auto receipts = entries->findItems(QStringLiteral("ui-order"),Qt::MatchExactly);
        QCOMPARE(receipts.size(),1);
        QCOMPARE(receipts.front()->data(Qt::UserRole).toString(),QStringLiteral("ui-cash-record"));
        QCOMPARE(entries->item(receipts.front()->row(),3)->text(),ClientReceiptReader::money(18725));
        cash->click();
        QCOMPARE(pages->currentWidget(),journalPage);
        if (!captures.isEmpty()) QVERIFY(window.grab().save(QDir(captures).filePath(QString("shell-%1.png").arg(size.width()))));
    }
    const QString path = qEnvironmentVariable("CS_HEADER_PREVIEW");
    if (!path.isEmpty()) QVERIFY(header->grab().save(path));
}
