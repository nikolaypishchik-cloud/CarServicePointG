#include <QtTest>
#include <QApplication>
#include <QDateEdit>
#include <QDialog>
#include <QFileDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QPrintPreviewWidget>
#include <QPushButton>
#include <QSqlError>
#include <QSqlQuery>
#include <QTableWidget>
#include <QTabWidget>
#include "core/crm/CrmPageReadService.h"
#include <QTemporaryDir>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include "core/services/ActivityAuditService.h"
#include "ui/main/MainWindow.h"

void checkStatementApprovalAndPreview(QSqlDatabase db) {
    QSqlQuery q(db);
    QVERIFY(q.exec("ALTER TABLE work_orders ADD COLUMN client_uid TEXT"));
    QVERIFY(q.exec("UPDATE work_orders SET client_uid='client-a' WHERE number=1"));
    QVERIFY(q.exec("CREATE TABLE crm_clients(id INTEGER,client_uid TEXT,full_name TEXT,phone TEXT,updated_at TEXT)"));
    QVERIFY(q.exec("CREATE TABLE crm_vehicles(id INTEGER,vehicle_uid TEXT,client_uid TEXT,brand TEXT,model TEXT,plate TEXT,is_primary INTEGER,updated_at TEXT)"));
    QVERIFY(q.exec("INSERT INTO crm_clients VALUES(1,'client-a','QA client','','')"));
    QString error;
    QVERIFY2(ActivityAuditService::ensureSchema(db,&error),qPrintable(error));
    QJsonObject decision{{"client_uid","client-a"},{"order_uid","order-a"},{"status","approved"},
                         {"decision_at",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
    q.prepare("INSERT INTO activity_audit_events(category,action,title,entity_type,entity_id,payload_json) "
              "VALUES('crm','decision','QA','estimate_approval','approval-a',?)");
    q.addBindValue(QString::fromUtf8(QJsonDocument(QJsonObject{{"after",decision}}).toJson()));
    QVERIFY2(q.exec(),qPrintable(q.lastError().text()));
    ActivityAuditService::setCurrentActor({"owner","Owner","owner","test"});
    MainWindow window;
    auto* header=window.findChild<QToolButton*>("headerStatements");
    QVERIFY(header && header->isEnabled());
    header->click();
    auto* dialog=window.findChild<QDialog*>("headerStatementsWindow");
    QVERIFY(dialog);
    auto* table=dialog->findChild<QTableWidget*>("statementApprovals");
    QVERIFY(table);
    auto button=[&](const QString& text)->QPushButton* {
        for (auto* item:dialog->findChildren<QPushButton*>()) if(item->text()==text) return item;
        return nullptr;
    };
    auto* generate=button(QStringLiteral("Сформувати"));
    auto* open=button(QStringLiteral("Переглянути наряд"));
    auto* pdf=button(QStringLiteral("PDF"));
    QVERIFY(generate && open && pdf && !pdf->isEnabled());
    generate->click();
    QCOMPARE(table->rowCount(),1);
    table->setCurrentCell(0,0);
    QVERIFY(open->isEnabled());
    open->click();
    auto* card=dialog->findChild<QDialog*>("statementOrderCard");
    QVERIFY(card && card->isVisible());
    QCOMPARE(card->windowTitle(),QStringLiteral("Наряд №1"));
    QStringList values;
    for (auto* field:card->findChildren<QLabel*>()) values.append(field->text());
    QVERIFY(values.contains("order-a"));
    QVERIFY(values.contains("AA1234AA"));
    card->close();
    QVERIFY(pdf->isEnabled());
    QVERIFY(q.exec("UPDATE activity_audit_events SET payload_json='{}' WHERE entity_id='approval-a'"));
    QTemporaryDir output;
    QVERIFY(output.isValid());
    const auto path=output.filePath("statement.pdf");
    pdf->click();
    auto* pages=dialog->findChild<QPrintPreviewWidget*>();
    QVERIFY(pages && pages->isVisible());
    QVERIFY(pages->pageCount()>0);
    auto* preview=qobject_cast<QDialog*>(pages->window());
    QVERIFY(preview);
    auto* document=preview->findChild<QTextDocument*>(QString(),Qt::FindDirectChildrenOnly);
    QVERIFY(document && document->toPlainText().contains("order-a"));
    QVERIFY(document->toPlainText().contains("QA client"));
    QVERIFY(!QFileInfo::exists(path));
    auto* save=button(QStringLiteral("Зберегти PDF"));
    QVERIFY(save);
    const bool nativeDisabled=QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,true);
    save->click();
    auto* files=preview->findChild<QFileDialog*>();
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,nativeDisabled);
    QVERIFY(files);
    files->selectFile(path);
    QVERIFY(QMetaObject::invokeMethod(files,"accept",Qt::DirectConnection));
    QFile saved(path);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QVERIFY(saved.read(5)=="%PDF-");
    const auto evidence=qEnvironmentVariable("CS_STATEMENT_PDF_EVIDENCE");
    if (!evidence.isEmpty()) QVERIFY(QFile::copy(path,evidence));
    preview->close();
    auto* date=dialog->findChild<QDateEdit*>();
    QVERIFY(date);
    date->setDate(date->date().addDays(-1));
    QVERIFY(!pdf->isEnabled());

    QVERIFY(q.exec("ALTER TABLE work_orders ADD COLUMN vehicle_uid TEXT"));
    QVERIFY(q.exec("UPDATE work_orders SET vehicle_uid='vehicle-a' WHERE number=1"));
    QVERIFY(q.exec("ALTER TABLE work_order_parts ADD COLUMN part_uid TEXT"));
    QVERIFY(q.exec("CREATE VIEW IF NOT EXISTS router_work_order_parts AS SELECT * FROM work_order_parts"));
    QVERIFY(q.exec("INSERT INTO work_order_parts(id,order_number,order_uid,part_uid,name) VALUES"
                   "(81,1,'order-a','part-81','Filter'),(82,1,'order-a','part-82','Filter')"));
    const auto day=QDate::currentDate();
    q.prepare("UPDATE work_order_parts SET installed_at=?,qty=5 WHERE id=82");
    q.addBindValue(day.toString(Qt::ISODate));
    QVERIFY(q.exec());
    error.clear();
    const auto beforeLedger=CrmPageReadService::statementPartHistory("client-a","vehicle-a",day,day,&error);
    QVERIFY2(error.isEmpty(),qPrintable(error));
    QCOMPARE(beforeLedger.size(),1);
    QCOMPARE(beforeLedger[0][9].toString(),QStringLiteral("installed"));
    QVERIFY(beforeLedger[0][2].isNull());
    QVERIFY(q.exec("CREATE TABLE workshop_spare_movements(id INTEGER PRIMARY KEY,part_id INTEGER,order_number INTEGER,"
                   "moved_on TEXT,operation TEXT,quantity REAL,actor TEXT)"));
    q.prepare("INSERT INTO workshop_spare_movements VALUES(11,81,1,?,'issue',1,'QA'),(12,81,1,?,'issue',2,'QA'),"
              "(13,82,1,?,'issue',1,''),(14,999,1,?,'issue',1,'QA'),(15,81,1,?,'receipt',9,'QA')");
    for (int i=0;i<5;++i) q.addBindValue(day.toString(Qt::ISODate));
    QVERIFY2(q.exec(),qPrintable(q.lastError().text()));
    QVERIFY(q.exec("PRAGMA query_only=ON"));
    for (const auto& vehicle : {QStringLiteral("vehicle-a"),QStringLiteral("other")}) {
        error.clear();
        const auto events=CrmPageReadService::statementPartHistory("client-a",vehicle,day,day,&error);
        QVERIFY2(error.isEmpty(),qPrintable(error));
        QCOMPARE(events.size(),vehicle=="vehicle-a"?4:0);
    }
    auto* issues=dialog->findChild<QTableWidget*>("statementIssues");
    auto* tabs=dialog->findChild<QTabWidget*>();
    QVERIFY(issues && tabs);
    tabs->setCurrentWidget(issues);
    generate->click();
    QCOMPARE(issues->rowCount(),4);
    QCOMPARE(issues->item(0,0)->text(),day.toString(Qt::ISODate));
    QCOMPARE(issues->item(0,5)->text(),QStringLiteral("part-81"));
    QCOMPARE(issues->item(1,2)->text(),QStringLiteral("2"));
    QCOMPARE(issues->item(1,6)->text(),QStringLiteral("Складська видача №12"));
    QCOMPARE(issues->item(2,5)->text(),QStringLiteral("part-82"));
    QCOMPARE(issues->item(2,3)->text(),QStringLiteral("Немає даних"));
    QCOMPARE(issues->item(3,5)->text(),QStringLiteral("part-82"));
    QCOMPARE(issues->item(3,0)->text(),day.toString(Qt::ISODate));
    QCOMPARE(issues->item(3,2)->text(),QStringLiteral("Немає даних"));
    QCOMPARE(issues->item(3,3)->text(),QStringLiteral("Немає даних"));
    QCOMPARE(issues->item(3,9)->text(),QStringLiteral("Встановлено"));
    QVERIFY(pdf->isEnabled());
    QVERIFY(q.exec("PRAGMA query_only=OFF"));
}
