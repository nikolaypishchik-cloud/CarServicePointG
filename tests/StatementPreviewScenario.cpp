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
    auto* table=dialog->findChild<QTableWidget*>();
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
}
