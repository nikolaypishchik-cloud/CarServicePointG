#include "ui/tabs/reports/ReportsPage.h"
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

static void saveStatementPreview(QDialog* preview,QPushButton* save,const QString& path,const char* evidenceVariable) {
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
    const auto evidence=qEnvironmentVariable(evidenceVariable);
    if (!evidence.isEmpty()) QVERIFY(QFile::copy(path,evidence));
}

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
    saveStatementPreview(preview,save,path,"CS_STATEMENT_PDF_EVIDENCE");
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
    QVERIFY(q.exec("ALTER TABLE workshop_spare_movements ADD COLUMN spare_id INTEGER"));
    QVERIFY(q.exec("UPDATE workshop_spare_movements SET spare_id=101 WHERE id IN (11,12,15)"));
    QVERIFY(q.exec("UPDATE workshop_spare_movements SET moved_on='2026-10-09' WHERE id=15"));
    QVERIFY(q.exec("UPDATE workshop_spare_movements SET spare_id=102 WHERE id=13"));
    QVERIFY(q.exec("CREATE TABLE workshop_spare_orders(id INTEGER PRIMARY KEY,name TEXT,ordered_on TEXT,expected_on TEXT)"));
    QVERIFY(q.exec("INSERT INTO workshop_spare_orders VALUES(101,'Filter','2026-10-01','2026-10-08'),"
                   "(102,'Filter','2026-10-02','2026-10-12')"));
    QVERIFY(q.exec("INSERT INTO workshop_spare_movements(id,spare_id,moved_on,operation,quantity,actor) VALUES"
                   "(16,101,'2026-10-06','receipt',2,'Receiver'),(17,102,'2026-10-07','receipt',8,'Other')"));
    QVERIFY(q.exec("PRAGMA query_only=ON"));
    const auto receipts=CrmPageReadService::statementPartHistory("client-a","vehicle-a",
        QDate(2026,10,6),QDate(2026,10,9),&error);
    QVERIFY2(error.isEmpty(),qPrintable(error));
    QList<int> receiptIds;
    for (const auto& row:receipts) {
        if (row[9].toString()!="batch_receipt") continue;
        receiptIds.append(row[6].toInt());
        QVERIFY(row[2].isNull());
        QCOMPARE(row[5].toString(),row[6].toInt()==17 ? QStringLiteral("part-82") : QStringLiteral("part-81"));
    }
    QCOMPARE(receiptIds,QList<int>({16,17,15}));
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
    issues->setCurrentCell(0,0);
    auto* showSource=button(QStringLiteral("Первинна видача"));
    QVERIFY(showSource && showSource->isVisibleTo(dialog));
    showSource->click();
    auto* source=dialog->findChild<QDialog*>("statementIssueSource");
    QVERIFY(source && source->isVisible());
    QCOMPARE(source->windowTitle(),QStringLiteral("Складська видача №11"));
    QStringList labels;
    for (auto* label:source->findChildren<QLabel*>()) labels.append(label->text());
    QVERIFY(labels.contains("2026-10-08"));
    QVERIFY(labels.contains(QStringLiteral("Очікуване надходження (план)")));
    auto* received=source->findChild<QTableWidget*>();
    QVERIFY(received);
    QCOMPARE(received->rowCount(),2);
    QCOMPARE(received->item(0,0)->text(),QStringLiteral("16"));
    QCOMPARE(received->item(0,2)->text(),QStringLiteral("2"));
    QCOMPARE(received->item(1,0)->text(),QStringLiteral("15"));
    QCOMPARE(received->item(1,2)->text(),QStringLiteral("9"));
    source->close();
    const auto savedFrom=date->date();
    date->setDate(QDate(2026,10,1));
    generate->click();
    int receiptRow=-1;
    for (int i=0;i<issues->rowCount();++i)
        if (issues->item(i,6)->text()==QStringLiteral("Надходження партії №16")) receiptRow=i;
    QVERIFY(receiptRow>=0);
    QCOMPARE(issues->item(receiptRow,2)->text(),QStringLiteral("не встановлено"));
    issues->setCurrentCell(receiptRow,0);
    QCOMPARE(showSource->text(),QStringLiteral("Первинне надходження"));
    showSource->click();
    auto* batch=dialog->findChild<QDialog*>("statementBatchReceipt");
    QVERIFY(batch && batch->isVisible());
    QCOMPARE(batch->windowTitle(),QStringLiteral("Надходження партії №16"));
    QStringList batchFields;
    for (const auto* label:batch->findChildren<QLabel*>()) batchFields.append(label->text());
    for (const QString& value:QStringList{"2026-10-06","Receiver","2","Кількість партії","не встановлено"})
        QVERIFY(batchFields.contains(value));
    batch->close();
    date->setDate(savedFrom);
    QVERIFY(!CrmPageReadService::statementIssueSource("other","order-a","part-81",11).error.isEmpty());
    QVERIFY(!CrmPageReadService::statementIssueSource("client-a","order-a","part-82",11).error.isEmpty());
    QVERIFY(q.exec("PRAGMA query_only=OFF"));
    QVERIFY(q.exec("CREATE TABLE client_payments(payment_uid TEXT,order_uid TEXT,paid_on TEXT,recorded_ms INTEGER,method TEXT,amount_cents INTEGER)"));
    q.prepare("INSERT INTO client_payments VALUES('receiptx','order-a',?,1,'cash',12500)");
    q.addBindValue(day.toString(Qt::ISODate));
    QVERIFY(q.exec());
    QVERIFY(q.exec("CREATE TABLE order_closure_commands(request_uid TEXT,order_uid TEXT,state TEXT,response_json TEXT)"));
    QJsonObject order{{"order_uid","order-a"},{"closed_at",day.toString(Qt::ISODate)+"T12:00:00+02:00"}};
    QJsonObject closure{{"order_uid","order-a"},{"order",order},{"total_cents",45000}};
    q.prepare("INSERT INTO order_closure_commands VALUES('close-a','order-a','applied',?)");
    q.addBindValue(QString::fromUtf8(QJsonDocument(closure).toJson()));
    QVERIFY(q.exec());
    q.prepare("INSERT INTO client_payments VALUES('prior','order-a',?,1,'cash',2500)");
    q.addBindValue(date->date().addDays(-1).toString(Qt::ISODate));
    QVERIFY(q.exec());
    QVERIFY(q.exec("ALTER TABLE work_orders ADD COLUMN discount REAL DEFAULT 0"));
    QVERIFY(q.exec("ALTER TABLE work_orders ADD COLUMN discount_mode TEXT DEFAULT 'uah'"));
    QVERIFY(q.exec("CREATE TABLE IF NOT EXISTS work_order_services(order_uid TEXT,order_number INTEGER,qty REAL,price REAL,amount REAL)"));
    QVERIFY(q.exec("CREATE VIEW IF NOT EXISTS router_work_order_services AS SELECT * FROM work_order_services"));
    q.prepare("INSERT INTO work_orders(number,order_number,order_uid,client_uid,car,status,opened_at) "
              "VALUES(72,72,'pending-a','client-a','QA pending','open',?),(73,73,'future-a','client-a','Future','open',?)");
    q.addBindValue(day.toString(Qt::ISODate)); q.addBindValue(day.addDays(1).toString(Qt::ISODate));
    QVERIFY(q.exec());
    QVERIFY(q.exec("INSERT INTO work_order_services(order_uid,order_number,qty,price,amount) VALUES('pending-a',72,2,392.5,785)"));
    auto* financial=dialog->findChild<QTableWidget*>("statementFinances");
    QVERIFY(financial);
    tabs->setCurrentWidget(financial);
    generate->click();
    QCOMPARE(financial->rowCount(),3);
    QCOMPARE(financial->item(2,1)->data(Qt::UserRole).toString(),QStringLiteral("pending-a"));
    QCOMPARE(financial->item(2,7)->text(),QStringLiteral("785,00 грн"));
    for (int c:{4,5,6}) QVERIFY(financial->item(2,c)->text().isEmpty());
    QCOMPARE(financial->item(0,4)->text(),QStringLiteral("450,00 грн"));
    QCOMPARE(financial->item(0,6)->text(),QStringLiteral("425,00 грн (борг)"));
    QCOMPARE(financial->item(1,3)->data(Qt::UserRole).toString(),QStringLiteral("receiptx"));
    QCOMPARE(financial->item(1,5)->text(),QStringLiteral("125,00 грн"));
    QCOMPARE(financial->item(1,6)->text(),QStringLiteral("300,00 грн (борг)"));
    QVERIFY(q.exec("ALTER TABLE client_payments ADD COLUMN actor TEXT"));
    QVERIFY(q.exec("UPDATE client_payments SET actor='QA receipt operator' WHERE payment_uid='receiptx'"));
    financial->setCurrentCell(1,0);
    QCOMPARE(open->text(),QStringLiteral("Переглянути оплату"));
    open->click();
    auto* receipt=dialog->findChild<QDialog*>("statementPaymentCard");
    QVERIFY(receipt && receipt->isVisible());
    QStringList receiptValues;
    for (auto* label:receipt->findChildren<QLabel*>()) receiptValues.append(label->text());
    for (const QString& value:QStringList{"receiptx","order-a","125,00 грн","QA receipt operator"})
        QVERIFY(receiptValues.contains(value));
    receipt->close();
    QVERIFY(CrmPageReadService::statementPayment("another-client","order-a","receiptx",&error).isEmpty());
    QVERIFY(CrmPageReadService::statementPayment("client-a","another-order","receiptx",&error).isEmpty());
    auto* totals=dialog->findChild<QLabel*>("statementFinancialTotals");
    QVERIFY(totals && totals->isVisibleTo(dialog));
    const auto generated=totals->text();
    QVERIFY(generated.contains("На початок -25,00 грн (аванс за журналом)"));
    QVERIFY(generated.contains("На кінець 300,00 грн (борг)"));
    QVERIFY(q.exec("UPDATE client_payments SET amount_cents=99999 WHERE payment_uid='receiptx'"));
    QVERIFY(q.exec("UPDATE work_orders SET client_uid='another-client' WHERE order_uid='order-a'"));
    financial->setCurrentCell(0,0);
    open->click();
    QCOMPARE(totals->text(),generated);
    pdf->click();
    auto* financialPreview=qobject_cast<QDialog*>(QApplication::activeModalWidget());
    QVERIFY(financialPreview && financialPreview!=preview);
    auto* snapshot=financialPreview->findChild<QTextDocument*>(QString(),Qt::FindDirectChildrenOnly);
    QVERIFY(snapshot && snapshot->toPlainText().contains(generated));
    QVERIFY(!snapshot->toPlainText().contains("999,99"));
    QPushButton* exportButton=nullptr;
    for (auto* item:financialPreview->findChildren<QPushButton*>())
        if (item->text()==QStringLiteral("Зберегти PDF")) exportButton=item;
    QVERIFY(exportButton);
    saveStatementPreview(financialPreview,exportButton,output.filePath("financial.pdf"),"CS_FINANCIAL_PDF_EVIDENCE");
    financialPreview->close();
}

void checkWeeklyReportScreen(QSqlDatabase db) {
    QSqlQuery q(db);
    QVERIFY(q.exec("DROP VIEW IF EXISTS router_work_orders"));
    QVERIFY(q.exec("CREATE TABLE IF NOT EXISTS masters(id INTEGER, name TEXT)"));
    QVERIFY2(q.exec("CREATE VIEW router_work_orders AS SELECT 10 AS number, 10 AS order_number,"
        "'2025-01-08' AS opened_at,'' AS closed_at,'Car' AS car,'M' AS master_name_closed,"
        "NULL AS master_id,100 AS works_sum,20 AS parts_sum UNION ALL "
        "SELECT 9,9,'2025-01-01','','B','M',NULL,30,30 UNION ALL "
        "SELECT 8,8,'2025-01-02','','C','M',NULL,900,900 UNION ALL "
        "SELECT 7,7,'2024-12-25','','D','M',NULL,0,0"),qPrintable(q.lastError().text()));
    ReportsPage page;
    page.resize(1280,800);
    page.show();
    auto* from=page.findChild<QDateEdit*>("dashboardFrom");
    auto* to=page.findChild<QDateEdit*>("dashboardTo");
    QVERIFY(from && to);
    from->setDate(QDate(2025,1,6));
    to->setDate(QDate(2025,1,8));
    auto text=[&]() -> QString {
        for (auto* label:page.findChildren<QLabel*>())
            if (label->isVisible() && label->text().contains("30.12.2024")) return label->text();
        return {};
    };
    QTRY_VERIFY_WITH_TIMEOUT(!text().isEmpty(),15000);
    const auto weeks=text().split('\n');
    QCOMPARE(weeks.size(),4);
    QVERIFY(weeks[0].contains("01.01.2025") && weeks[0].contains("100")
        && weeks[0].contains('%') && !weeks[0].contains("900"));
    QVERIFY(weeks[1].contains("25.12.2024") && weeks[1].contains("Зміна: —"));
    QVERIFY(weeks[2].contains("18.12.2024") && weeks[2].contains("немає нарядів"));
    QVERIFY(weeks[3].contains("11.12.2024") && weeks[3].contains("немає нарядів"));
    const auto output=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
    if (!output.isEmpty()) QVERIFY(page.grab().save(QDir(output).filePath("four-weeks.png")));
}
