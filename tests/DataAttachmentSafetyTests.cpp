#include <QtTest/QtTest>
#include <QInputDialog>
#include <QTimer>
#include "ui/common/dialogs/InvoiceReviewDialog.h"
#include "ui/tabs/orders/CurrentOrdersPageLocalSupport.h"
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QSettings>
#include <QSqlQuery>
#include <QSqlError>
#include <QRegularExpression>
#include <QFile>
#include <QDir>
#include <QThread>
#include <QNetworkProxy>
#include <QPdfWriter>
#include <QPainter>
#include <QFontDatabase>
#include <QUuid>
#include <QLockFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QDirIterator>
#include "ui/common/dialogs/InvoiceDocumentOpenSupport.h"
#include "ui/tabs/orders/WorkPartsImportedRowsViewModel.h"

#include "app/App.h"
#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/db/sqlite/DbContext.h"
#include "infrastructure/db/sqlite/TransactionRunner.h"
#include "core/services/storage/StorageDirectoriesGuard.h"
#include "core/services/orders/OrderAttachmentService.h"
#include "core/services/orders/CurrentOrdersInvoiceImportService.h"
#include "core/import/InvoiceAttachmentPdfService.h"
#include "core/import/InvoiceImportEngine.h"
#include "core/services/InvoiceEvidenceService.h"
#include "core/services/DebtAnalysisPdfService.h"
#include "ui/tabs/reports/PurchaseDebtsPage.h"
#include <QElapsedTimer>
#include "core/services/StaffAccountService.h"
#include "core/services/AccessControlService.h"
#include "core/services/ActivityAuditService.h"
#include <QTextBrowser>
#include "core/backup/BackupSnapshotService.h"
#include "core/backup/BackupPaths.h"
#include "runtime/AppRuntime.h"

class DataAttachmentSafetyTests final : public QObject {
    Q_OBJECT
    QTemporaryDir scratch;
    QSqlDatabase db;
    QString testDataRoot;
    void sql(const QString& text) {
        QSqlQuery query(db);
        QVERIFY2(query.exec(text), qPrintable(query.lastError().text()));
    }
    int count(const QString& table) {
        QSqlQuery query(db);
        if (!query.exec("SELECT COUNT(*) FROM " + table) || !query.next()) return -1;
        return query.value(0).toInt();
    }
    QString file(const QString& name, const QByteArray& bytes) {
        const QString path = scratch.filePath(name);
        QFile output(path);
        if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size()) return {};
        return path;
    }
    QByteArray bytes(const QString& path) {
        QFile input(path);
        return input.open(QIODevice::ReadOnly) ? input.readAll() : QByteArray{};
    }
    QString pdfFile(const QString& name) {
        const QString path = scratch.filePath(name);
        QPdfWriter writer(path);
        QPainter painter(&writer);
        painter.drawRect(10, 10, 100, 100);
        painter.end();
        return path;
    }
    QString managed(const QString& source, const QString& name = "invoice.pdf") {
        QString error;
        return StorageDirectoriesGuard::copyFileToManagedStorageAs(
            StorageDirectoriesGuard::StorageKind::Invoice, 1, "parts", name, source, &error);
    }
    QList<CurrentOrdersImportServiceRow> rows() {
        CurrentOrdersImportServiceRow row;
        row.orderNumberText = "1"; row.orderedAt = "02.10.26";
        row.name = "Filter"; row.store = "Test store";
        row.oemNumber = "TEST-001"; row.qtyText = "2"; row.priceText = "0";
        auto second = row; second.name = "Oil"; second.oemNumber = "TEST-002"; second.qtyText = "4.5";
        return {row, second};
    }
    void addOrder() {
        sql("INSERT OR IGNORE INTO masters(id,name) VALUES('stage1-master','Test Master')");
        sql("INSERT INTO work_orders(number,order_number,order_year,order_uid,opened_at,status,plate,plate_norm,car,client,master_id) "
            "VALUES(1,1,2026,'stage1-order','02.10.26 10:00:00','in_progress','AA 0001 AA','aa0001aa','Test car','Test client','stage1-master')");
    }
private slots:
    void initTestCase() {
        QVERIFY(scratch.isValid());
        // The offscreen Qt platform does not discover the Windows font folder.
        // Keep PDF text assertions meaningful by loading the production fonts.
        for (const auto& name : QStringList{"times.ttf", "timesbd.ttf", "timesi.ttf", "timesbi.ttf", "arial.ttf", "arialbd.ttf"}) {
            const auto path = qEnvironmentVariable("WINDIR") + "/Fonts/" + name;
            QVERIFY2(QFile::exists(path), qPrintable(path));
            QVERIFY(QFontDatabase::addApplicationFont(path) >= 0);
        }
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("CarServiceStage1Tests");
        QCoreApplication::setApplicationName("Safety-" + QUuid::createUuid().toString(QUuid::Id128));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, scratch.path());
        const QString root=QFileInfo(qEnvironmentVariable("CS_TEST_DATA_ROOT")).canonicalFilePath();
        QVERIFY2(!qEnvironmentVariable("CS_TEST_DATA_ROOT").isEmpty() && !root.isEmpty(), "An explicit isolated test root is required");
        QVERIFY(QFileInfo(scratch.path()).canonicalFilePath().startsWith(root + "/"));
        QVERIFY(QDir::setCurrent(scratch.path()));
        testDataRoot = App::dataDir();
        QVERIFY(QFileInfo(testDataRoot).canonicalFilePath().startsWith(root + "/"));
        QVERIFY(QFileInfo(QSettings().fileName()).absoluteFilePath().startsWith(root + "/"));
        for(const auto& path : {AppRuntime::activeDbFilePath(),AppRuntime::archiveDbFilePath(),AppRuntime::registryDbFilePath()})
            QVERIFY2(QFileInfo(path).absoluteFilePath().startsWith(root + "/"),qPrintable(path));
        qputenv("CS_ELIT_DISABLE_STARTUP","1");
        QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::HttpProxy,"127.0.0.1",9));
        qInfo()<<"ISOLATION_VERIFIED"<<root<<scratch.path()<<testDataRoot<<QSettings().fileName();
        QString runtimeError;
        QVERIFY2(Database::prepareRuntime(&runtimeError), qPrintable(runtimeError));
        QVERIFY2(Database::openPrepared(&runtimeError), qPrintable(runtimeError));
        db = Database::db();
        QVERIFY(Database::ensureSchema());
        QVERIFY(StaffAccountService::ensureSchema(db));
        QVERIFY(AccessControlService::ensureSchema(db));
        sql("INSERT OR IGNORE INTO staff_accounts(id,display_name,role) VALUES('evidence-owner','Test Owner','owner')");
        QVERIFY(Database::setSetting("settings.int.directories.invoices_dir", scratch.filePath("invoices")));
        QVERIFY(Database::setSetting("settings.int.autobackup.dir", scratch.filePath("backups")));
        QVERIFY(Database::setSetting("settings.int.directories.order_photos_dir", scratch.filePath("photos")));
        QVERIFY(Database::setSetting("settings.sync.enabled", "0"));
    }
    void init() {
        ActivityAuditService::setCurrentActor({"evidence-owner","Test Owner","owner","test"});
        sql("DROP TRIGGER IF EXISTS fail_evidence");
        if (db.tables().contains("invoice_part_evidence")) sql("DELETE FROM invoice_part_evidence");
        sql("DELETE FROM work_order_part_attachments");
        sql("DELETE FROM work_order_service_attachments");
        sql("DELETE FROM work_order_parts");
        sql("DELETE FROM work_order_services");
        sql("DELETE FROM work_orders");
        sql("DELETE FROM settings WHERE key LIKE 'invoice_import.v1.%'");
    }
    void sameSizeDifferentFilesRemainDistinct() {
        const QString first = managed(file("source-a.pdf", "first document"), "collision.pdf");
        const QString second = managed(file("source-b.pdf", "other document"), "collision.pdf");
        QVERIFY(!first.isEmpty()); QVERIFY(!second.isEmpty()); QVERIFY(first != second);
        QCOMPARE(bytes(first), QByteArray("first document"));
        QCOMPARE(bytes(second), QByteArray("other document"));
        QCOMPARE(managed(scratch.filePath("source-b.pdf"), "collision.pdf"), second);
    }
    void lockedDestinationRetainsPreviousFile() {
        const QString old = managed(file("old.pdf", "old data"), "locked.pdf");
        QVERIFY(!old.isEmpty());
        QLockFile lock(old + ".lock"); QVERIFY(lock.tryLock());
        QVERIFY(managed(file("new.pdf", "new document"), "locked.pdf").isEmpty());
        QCOMPARE(bytes(old), QByteArray("old data"));
    }
    void callbacksRunOnlyAfterCommit() {
        bool called = false;
        // Required diagnostics of this deliberately rejected transaction.
        // ignoreMessage fails the test if absent; all unrelated critical messages remain fatal to acceptance.
        QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(
            "^DB_TRANSACTION_CONTEXT phase= rollback_ok  transactionId= [0-9a-f-]{36}  owner= TransactionRunner  operationId= tx_runner_[0-9]+  connection= carservice_main  threadId= [0-9]+  error= step_failed$"));
        QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(
            "^DB_TRANSACTION step_failed_rollback connection= carservice_main  transactionId= [0-9a-f-]{36}  rollbackOk= true  error= begin$"));
        auto rolledBack = TransactionRunner::run(db, "begin", "commit", [&](QString*) {
            if (!TransactionContext::deferUntilCommit(db, [&] { called = true; })) return true;
            return false;
        });
        QVERIFY(!rolledBack.ok); QVERIFY(!called);
        auto committed = TransactionRunner::run(db, "begin", "commit", [&](QString*) {
            return TransactionContext::deferUntilCommit(db, [&] { called = true; });
        });
        QVERIFY(committed.ok); QVERIFY(called);
    }
    void failedCommitDoesNotDeleteAttachment() {
        addOrder();
        sql("INSERT INTO work_order_parts(id,order_uid,order_number,name) VALUES(1,'stage1-order',1,'Filter')");
        const QString path = managed(file("rollback.pdf", "rollback document"), "rollback.pdf");
        QVERIFY(!path.isEmpty());
        QVERIFY(OrderAttachmentService::setPartAttachment(db, 1, path, "rollback"));
        sql("PRAGMA foreign_keys=ON");
        sql("CREATE TABLE IF NOT EXISTS stage1_deferred(id INTEGER REFERENCES work_orders(number) DEFERRABLE INITIALLY DEFERRED)");
        QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(
            "^DB_TRANSACTION_CONTEXT phase= commit_failed_rollback  transactionId= [0-9a-f-]{36}  owner= TransactionRunner  operationId= tx_runner_[0-9]+  connection= carservice_main  threadId= [0-9]+  error= FOREIGN KEY constraint failed Unable to commit transaction rollbackOk=true$"));
        QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(
            "^DB_TRANSACTION commit_failed connection= carservice_main  transactionId= [0-9a-f-]{36}  error= FOREIGN KEY constraint failed Unable to commit transaction$"));
        const auto result = TransactionRunner::run(db, "begin", "commit", [&](QString* error) {
            QSqlQuery query(db);
            if (!query.exec("INSERT INTO stage1_deferred VALUES(999999)")) return false;
            return OrderAttachmentService::setPartAttachment(db, 1, {}, {}, {}, {}, error);
        });
        QVERIFY(!result.ok);
        QCOMPARE(OrderAttachmentService::partAttachmentPath(db, 1), path);
        QCOMPARE(bytes(path), QByteArray("rollback document"));
    }
    void sharedAttachmentSurvivesUnlink() {
        addOrder();
        sql("INSERT INTO work_order_parts(id,order_uid,order_number,name) VALUES(1,'stage1-order',1,'Filter'),(2,'stage1-order',1,'Oil')");
        const QString path = managed(file("shared.pdf", "shared document"), "shared.pdf");
        QVERIFY(OrderAttachmentService::setPartAttachment(db, 1, path, "shared"));
        QVERIFY(OrderAttachmentService::setPartAttachment(db, 2, path, "shared"));
        const auto result = TransactionRunner::run(db, "begin", "commit", [&](QString* error) {
            return OrderAttachmentService::setPartAttachment(db, 1, {}, {}, {}, {}, error);
        });
        QVERIFY(result.ok); QVERIFY(QFileInfo::exists(path));
        QCOMPARE(OrderAttachmentService::partAttachmentPath(db, 2), path);
        const auto finalUnlink = TransactionRunner::run(db, "begin", "commit", [&](QString* error) {
            return OrderAttachmentService::setPartAttachment(db, 2, {}, {}, {}, {}, error);
        });
        QVERIFY(finalUnlink.ok);
        QVERIFY(!QFileInfo::exists(path));
    }
    void invoiceImportArchiveOwnerRoute() {
        QString error;
        sql("INSERT INTO work_orders(number,order_number,order_year,order_uid,opened_at,closed_at,status,plate,car) VALUES(917,917,2024,'invoice-archive-owner','2024-01-01','2024-02-01','closed','TEST 917','Synthetic archived car')");
        QVERIFY2(Database::refreshOrderRegistry(917,&error),qPrintable(error));
        QVERIFY2(Database::archiveSingleActiveOrderToArchive(917,&error),qPrintable(error));
        QSqlQuery absent(db);QVERIFY(absent.exec("SELECT COUNT(*) FROM work_orders WHERE order_uid='invoice-archive-owner'"));QVERIFY(absent.next());QCOMPARE(absent.value(0).toInt(),0);absent.finish();
        const auto hits=CurrentOrdersInvoiceImportService::findOrderCandidates(917,&error);
        QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(hits.size(),1);QCOMPARE(hits.first().orderUid,QString("invoice-archive-owner"));
        auto input=rows();for(auto& row:input){row.orderNumberText="917";row.priceText="10.25";}
        CurrentOrdersInvoiceAttachmentSpec spec;spec.sourceFilePath=pdfFile("archived-invoice.pdf");spec.invoiceNumber="ARCHIVED-OWNER";
        CurrentOrdersInvoiceImportResult result;
        QVERIFY2(CurrentOrdersInvoiceImportService::importRows(input,spec,&result,&error),qPrintable(error));
        QCOMPARE(result.affectedOrderNumbers,QList<int>{917});QCOMPARE(count("work_order_parts"),2);
        QVERIFY2(CurrentOrdersInvoiceImportService::importRows(input,spec,&result,&error),qPrintable(error));QCOMPARE(count("work_order_parts"),2);
        QSqlQuery owner(db);QVERIFY(owner.exec("SELECT DISTINCT order_uid FROM work_order_parts"));QVERIFY(owner.next());QCOMPARE(owner.value(0).toString(),QString("invoice-archive-owner"));QVERIFY(!owner.next());
    }
    void invoiceImportVisibleOwnerSelectionAndNoDraft() {
        addOrder(); QString error;
        sql("UPDATE work_orders SET order_number=712,order_year=2024 WHERE number=1");
        sql("INSERT INTO work_orders(number,order_number,order_year,order_uid,opened_at,status) VALUES(712,713,2026,'invoice-physical-owner','2026-01-01','in_progress')");
        auto input=rows(); for(auto& row:input) {row.orderNumberText="712";row.priceText="10.25";}
        CurrentOrdersInvoiceAttachmentSpec spec;spec.sourceFilePath=pdfFile("owner-visible.pdf");spec.invoiceNumber="OWNER-1";
        CurrentOrdersInvoiceImportResult result;
        auto hits=CurrentOrdersInvoiceImportService::findOrderCandidates(712,&error);
        QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(hits.size(),1);QCOMPARE(hits.first().orderUid,QString("stage1-order"));
        QVERIFY2(CurrentOrdersInvoiceImportService::importRows(input,spec,&result,&error),qPrintable(error));
        QCOMPARE(result.affectedOrderNumbers,QList<int>{1});QCOMPARE(count("work_order_parts"),2);
        QSqlQuery sums(db);QVERIFY(sums.exec("SELECT SUM(purchase_amount) FROM work_order_parts WHERE order_uid='stage1-order'"));QVERIFY(sums.next());QCOMPARE(qRound64(sums.value(0).toDouble()*100),qint64(6663));
        QVERIFY2(CurrentOrdersInvoiceImportService::importRows(input,spec,&result,&error),qPrintable(error));QCOMPARE(count("work_order_parts"),2);
        for(auto& row:input)row.orderNumberText="991234";
        QVERIFY(!CurrentOrdersInvoiceImportService::importRows(input,spec,&result,&error));QVERIFY(!error.isEmpty());QCOMPARE(count("work_orders"),2);QCOMPARE(count("work_order_parts"),2);
        for(auto& row:input){row.orderNumberText="712";row.orderUid="deleted-owner";}
        QVERIFY(!CurrentOrdersInvoiceImportService::importRows(input,spec,&result,&error));QCOMPARE(count("work_order_parts"),2);
        sql("UPDATE work_orders SET order_number=712 WHERE number=712");
        for(auto& row:input)row.orderUid.clear();
        QVERIFY(!CurrentOrdersInvoiceImportService::importRows(input,spec,&result,&error));QVERIFY(error.contains(QString::fromUtf8("кільком")));
        for(auto& row:input)row.orderUid="stage1-order";
        QVERIFY2(CurrentOrdersInvoiceImportService::importRows(input,spec,&result,&error),qPrintable(error));QCOMPARE(count("work_order_parts"),2);
    }
    void invoiceImportReceiptOwnerAndContentGuards() {
        addOrder();QString error;sql("UPDATE work_orders SET order_number=714 WHERE number=1");
        CurrentOrdersInvoiceAttachmentSpec spec;spec.sourceFilePath=pdfFile("owner-receipt.pdf");spec.invoiceNumber="OWNER-RECEIPT";
        auto input=rows();for(auto& row:input){row.orderUid="stage1-order";row.orderNumberText="714";}
        QVERIFY2(CurrentOrdersInvoiceImportService::importRows(input,spec,nullptr,&error),qPrintable(error));
        QSqlQuery receipt(db);QVERIFY(receipt.exec("SELECT value FROM settings WHERE key LIKE 'invoice_import.v1.%'"));QVERIFY(receipt.next());
        QVERIFY(QJsonDocument::fromJson(receipt.value(0).toString().toUtf8()).isArray());receipt.finish();
        QSqlQuery attachment(db);QVERIFY(attachment.exec("SELECT file_path FROM work_order_part_attachments LIMIT 1"));QVERIFY(attachment.next());const QString path=attachment.value(0).toString();attachment.finish();
        const auto original=bytes(path);
        auto expectRejected=[&] {
            const QString message=QString::fromUtf8("Власник або вкладення попереднього імпорту змінилися. Повтор заблоковано.");
            QTest::ignoreMessage(QtCriticalMsg,QRegularExpression("^DB_TRANSACTION_CONTEXT phase= rollback_ok  transactionId= [0-9a-f-]{36}  owner= TransactionRunner  operationId= tx_runner_[0-9]+  connection= carservice_main  threadId= [0-9]+  error= step_failed$"));
            QTest::ignoreMessage(QtCriticalMsg,QRegularExpression("^DB_TRANSACTION step_failed_rollback connection= carservice_main  transactionId= [0-9a-f-]{36}  rollbackOk= true  error= "+QRegularExpression::escape(message)+"$"));
            QTest::ignoreMessage(QtCriticalMsg,QRegularExpression("^INVOICE_IMPORT_END trace=INV-[0-9]+ status=failed error="+QRegularExpression::escape(message)+"$"));
            QVERIFY(!CurrentOrdersInvoiceImportService::importRows(input,spec,nullptr,&error));QCOMPARE(error,message);QCOMPARE(count("work_order_parts"),2);
        };
        sql("INSERT INTO work_orders(number,order_number,order_year,order_uid,opened_at,status) VALUES(914,714,2025,'invoice-other-owner','2025-01-01','in_progress')");
        for(auto& row:input)row.orderUid="invoice-other-owner";
        expectRejected();QCOMPARE(bytes(path),original);
        for(auto& row:input){row.orderUid="stage1-order";row.orderNumberText="714";}
        {QFile changed(path);QVERIFY(changed.open(QIODevice::WriteOnly|QIODevice::Truncate));QCOMPARE(changed.write("changed attachment"),qint64(18));}
        expectRejected();QCOMPARE(bytes(path),QByteArray("changed attachment"));
        {QFile repaired(path);QVERIFY(repaired.open(QIODevice::WriteOnly|QIODevice::Truncate));QCOMPARE(repaired.write(original),qint64(original.size()));}
        QVERIFY2(CurrentOrdersInvoiceImportService::importRows(input,spec,nullptr,&error),qPrintable(error));QCOMPARE(count("work_order_parts"),2);
        db.close();QVERIFY(db.open());
        QVERIFY2(CurrentOrdersInvoiceImportService::importRows(input,spec,nullptr,&error),qPrintable(error));QCOMPARE(count("work_order_parts"),2);
    }
    void invoiceImportOwnerChangeRollsBack() {
        addOrder();QString error;auto input=rows();input=input.mid(0,1);
        const QString message=QString::fromUtf8("Власник наряду змінився. Імпорт скасовано до запису.");
        QTest::ignoreMessage(QtCriticalMsg,QRegularExpression("^DB_TRANSACTION_CONTEXT phase= rollback_ok  transactionId= [0-9a-f-]{36}  owner= TransactionRunner  operationId= tx_runner_[0-9]+  connection= carservice_main  threadId= [0-9]+  error= step_failed$"));
        QTest::ignoreMessage(QtCriticalMsg,QRegularExpression("^DB_TRANSACTION step_failed_rollback connection= carservice_main  transactionId= [0-9a-f-]{36}  rollbackOk= true  error= "+QRegularExpression::escape(message)+"$"));
        QTest::ignoreMessage(QtCriticalMsg,QRegularExpression("^INVOICE_IMPORT_END trace=INV-[0-9]+ status=failed error="+QRegularExpression::escape(message)+"$"));
        CurrentOrdersInvoiceImportService::AttachmentHandler mutate=[](QSqlDatabase& tx,int,int,QString*) {QSqlQuery q(tx);return q.exec("UPDATE work_orders SET order_uid='changed-in-transaction' WHERE number=1");};
        QVERIFY(!CurrentOrdersInvoiceImportService::importRows(input,mutate,nullptr,&error));QCOMPARE(error,message);QCOMPARE(count("work_order_parts"),0);
        QSqlQuery owner(db);QVERIFY(owner.exec("SELECT order_uid FROM work_orders WHERE number=1"));QVERIFY(owner.next());QCOMPARE(owner.value(0).toString(),QString("stage1-order"));owner.finish();
        QVERIFY2(CurrentOrdersInvoiceImportService::importRows(input,CurrentOrdersInvoiceImportService::AttachmentHandler{},nullptr,&error),qPrintable(error));QCOMPARE(count("work_order_parts"),1);
    }
    void invoiceReviewCsvPdfOwnerSelection() {
        addOrder();QString error;sql("UPDATE work_orders SET order_number=918 WHERE number=1");
        sql("INSERT INTO work_orders(number,order_number,order_year,order_uid,opened_at,status,plate,car) VALUES(915,918,2025,'invoice-form-owner','2025-01-01','in_progress','TEST 915','Synthetic car')");
        InvoiceImport::ImportedPartDraft draft;draft.orderNumberText="918";draft.name="Synthetic part";draft.oemNumber="FORM-1";draft.qtyText="2";draft.priceText="10.25";
        InvoiceImport::ExcelReadResult data;data.documentDate="2026-10-02";data.documentNumber="FORM-CSV";data.canonicalPdfAttachmentPath=pdfFile("form-csv.pdf");
        const QString csv=file("form.csv","Name,Qty,Price\nSynthetic part,2,10.25\n");
        auto form=CurrentOrdersSupport::createInvoiceReview(csv,data,{draft},nullptr);
        bool choiceSeen=false;
        auto choose=[&] {
            QTimer::singleShot(0,form.get(),[&] {
                auto* dialog=qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
                if(!dialog)return;
                const auto labels=dialog->comboBoxItems();
                for(const auto& label:labels)if(label.contains("invoice-form-owner")){choiceSeen=true;dialog->setTextValue(label);dialog->accept();return;}
                dialog->reject();
            });
        };
        choose();QVERIFY2(form->commit(&error),qPrintable(error));QVERIFY(choiceSeen);QCOMPARE(count("work_order_parts"),1);
        choose();QVERIFY2(form->commit(&error),qPrintable(error));QCOMPARE(count("work_order_parts"),1);
        QSqlQuery part(db);QVERIFY(part.exec("SELECT order_uid,purchase_amount FROM work_order_parts"));QVERIFY(part.next());QCOMPARE(part.value(0).toString(),QString("invoice-form-owner"));QCOMPARE(qRound64(part.value(1).toDouble()*100),qint64(2050));part.finish();
        data.documentNumber="FORM-PDF";data.canonicalPdfAttachmentPath.clear();
        form=CurrentOrdersSupport::createInvoiceReview(pdfFile("form-direct.pdf"),data,{draft},nullptr);
        choose();QVERIFY2(form->commit(&error),qPrintable(error));QCOMPARE(count("work_order_parts"),2);
        QTimer::singleShot(0,form.get(),[]{if(auto* dialog=qobject_cast<QInputDialog*>(QApplication::activeModalWidget()))dialog->reject();});
        QVERIFY(!form->commit(&error));QCOMPARE(count("work_order_parts"),2);
        QTimer::singleShot(0,form.get(),[&]{
            auto* dialog=qobject_cast<QInputDialog*>(QApplication::activeModalWidget());if(!dialog)return;
            for(const auto& label:dialog->comboBoxItems())if(label.contains("invoice-form-owner")){
                dialog->setTextValue(label);sql("UPDATE work_orders SET order_uid='changed-form-owner' WHERE number=915");dialog->accept();return;
            }
            dialog->reject();
        });
        QVERIFY(!form->commit(&error));QVERIFY(error.contains(QString::fromUtf8("змінився")));QCOMPARE(count("work_order_parts"),2);
        sql("UPDATE work_orders SET order_uid='invoice-form-owner' WHERE number=915");
        choose();QVERIFY2(form->commit(&error),qPrintable(error));QCOMPARE(count("work_order_parts"),2);
    }
    void invoiceImportIsAtomicAndIdempotent() {
        addOrder();
        CurrentOrdersInvoiceAttachmentSpec spec;
        spec.sourceFilePath = pdfFile("import.pdf");
        spec.displayName = "Import"; spec.invoiceNumber = "STAGE1-1";
        CurrentOrdersInvoiceImportResult imported;
        QString error;
        QVERIFY2(CurrentOrdersInvoiceImportService::importRows(rows(), spec, &imported, &error), qPrintable(error));
        QCOMPARE(count("work_order_parts"), 2); QCOMPARE(count("work_order_part_attachments"), 2);
        QCOMPARE(count("invoice_part_evidence"), 2);
        QCOMPARE(imported.affectedOrderNumbers, QList<int>{1});
        QVERIFY2(CurrentOrdersInvoiceImportService::importRows(rows(), spec, &imported, &error), qPrintable(error));
        QCOMPARE(count("work_order_parts"), 2);
        QCOMPARE(count("invoice_part_evidence"), 2);
        db.close(); QVERIFY(db.open());
        QVERIFY2(CurrentOrdersInvoiceImportService::importRows(rows(), spec, &imported, &error), qPrintable(error));
        QCOMPARE(count("work_order_parts"), 2);
        spec.importOperationId = "intentional-second-import";
        QVERIFY2(CurrentOrdersInvoiceImportService::importRows(rows(), spec, &imported, &error), qPrintable(error));
        QCOMPARE(count("work_order_parts"), 4);
    }
    void invoiceFactsPersistWithoutReparsingAndRemainImmutable() {
        addOrder();
        auto importedRows=rows(); importedRows[0].priceText="100.25"; importedRows[1].priceText="80";
        CurrentOrdersInvoiceAttachmentSpec spec;
        spec.sourceFilePath=pdfFile("evidence.pdf"); spec.invoiceNumber="EVIDENCE-1"; spec.invoiceDate="25.09.26";
        QString error;
        QVERIFY2(CurrentOrdersInvoiceImportService::importRows(importedRows,spec,nullptr,&error),qPrintable(error));
        auto parts=PartsQueryService::loadRows(db,{}, {}, {},&error);
        QCOMPARE(parts.size(),2);
        auto facts=InvoiceEvidenceService::load(db,parts,&error); QCOMPARE(facts.size(),2);
        const auto original=facts.value(InvoiceEvidenceService::key(parts[0].orderUid,parts[0].id));
        QCOMPARE(original.invoiceNumber,QString("EVIDENCE-1")); QCOMPARE(original.invoiceDate,QString("25.09.26"));
        QCOMPARE(original.documentHash.size(),64);
        qint64 total=0; for(const auto& fact:facts) total+=fact.purchaseCents;
        QCOMPARE(total,qint64(56050));
        sql(QString("UPDATE work_order_parts SET purchase_amount=999 WHERE id=%1").arg(parts[0].id));
        QVERIFY(OrderAttachmentService::setPartAttachment(db,parts[0].id,original.filePath,"invoice","EVIDENCE-1","25.09.26",&error,true));
        db.close(); QVERIFY(db.open());
        QCOMPARE(InvoiceEvidenceService::load(db,parts,&error).value(InvoiceEvidenceService::key(parts[0].orderUid,parts[0].id)).purchaseCents,original.purchaseCents);
        PartsQueryService::invalidateCache();
        const auto debts=PurchasePaymentService::debts(db,&error);
        const auto report=PurchaseDebtAnalysisPage::analysisHtml(debts,parts,QDate(2026,10,2),{},0,{},facts);
        QVERIFY(report.contains(QString::fromUtf8("За накладними: 2")));
        QVERIFY(report.contains(QString::fromUtf8("Розбіжність з накладною")));
        const QString screenshots=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
        if(!screenshots.isEmpty()) {
            QVERIFY(QDir().mkpath(screenshots));
            QTextBrowser preview; preview.resize(1500,950); preview.setHtml(report); preview.show();
            QTest::qWait(150);
            QVERIFY(preview.grab().save(QDir(screenshots).filePath("invoice-evidence-analysis.png")));
            QVERIFY2(DebtAnalysisPdfService::save(QDir(screenshots).filePath("invoice-analysis.pdf"),report,&error),qPrintable(error));
        }
        // Reads do not require opening or parsing the file again.
        QVERIFY(QFile::remove(original.filePath));
        QCOMPARE(InvoiceEvidenceService::load(db,parts,&error).size(),2);
        QVector<PartRowSnapshot> wrongScope=parts;
        for(auto& part:wrongScope) part.orderUid="other-order";
        QVERIFY(InvoiceEvidenceService::load(db,wrongScope,&error).isEmpty());
    }
    void debtAnalysisPdfIncludesAllRowsAndRejectsInvalidDestination() {
        QVector<PurchaseDebt> debts;
        for(int i=0;i<105;++i) {
            PurchaseDebt debt; debt.source="part"; debt.id=i+1; debt.orderUid="test-order";
            debt.orderNumber=i+1; debt.supplier="Test store"; debt.name=QString("PDF-ITEM-%1-END").arg(i);
            debt.totalCents=12345; debt.date="25.09.26"; debts.append(debt);
        }
        const auto html=PurchaseDebtAnalysisPage::analysisHtml(debts,{},QDate(2026,10,2),{},-1);
        QVERIFY(html.contains("PDF-ITEM-0-END")); QVERIFY(html.contains("PDF-ITEM-104-END"));
        QVERIFY(!PurchaseDebtAnalysisPage::analysisHtml(debts,{},QDate(2026,10,2)).contains("PDF-ITEM-104-END"));
        QVERIFY(!PurchaseDebtAnalysisPage::analysisHtml(debts,{},QDate(2026,10,2),"Other",-1).contains("PDF-ITEM-0-END"));
        QString error;
        const QString path=scratch.filePath("analysis.pdf");
        QVERIFY2(DebtAnalysisPdfService::save(path,html,&error),qPrintable(error));
        QVERIFY(bytes(path).startsWith("%PDF-")); QVERIFY(bytes(path).size()>1000);
        QVERIFY(!DebtAnalysisPdfService::save(scratch.filePath("missing/analysis.pdf"),html,&error));
        QVERIFY(!error.isEmpty()); QVERIFY(bytes(path).startsWith("%PDF-"));
        const QString qa=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
        if(!qa.isEmpty()) { QVERIFY(QDir().mkpath(qa)); QVERIFY(QFile::copy(path,QDir(qa).filePath("analysis-all-105.pdf"))); }
    }
    void debtFullAndBriefReportsPreservePositiveBalances() {
        QVector<PurchaseDebt> debts;
        const QDate today(2026,10,3);
        for(int i=0;i<12;++i) {
            PurchaseDebt d; d.source="part"; d.id=i+1; d.orderUid="print-test"; d.orderNumber=9+i;
            d.supplier=i<6?QString::fromUtf8("Тестовий магазин А"):QString::fromUtf8("Тестовий магазин Б");
            d.name=QString("PRINT-DETAIL-%1-END").arg(i); d.totalCents=100000; d.date="2026-09-20";
            if(i<6) d.dueDate=today.addDays(-5);
            debts.append(d);
        }
        SupplierDebtReconciliation r; r.supplier=QString::fromUtf8("Магазин лише зі звірки");
        r.actualCents=150000; r.computedCents=0; r.checkedAt="2026-10-03";
        QMap<QString,SupplierDebtReconciliation> facts{{r.supplier,r}};
        const auto full=PurchaseDebtAnalysisPage::analysisHtml(debts,{},today,{},-1,facts,{},-1);
        const auto brief=PurchaseDebtAnalysisPage::analysisHtml(debts,{},today,{},-1,facts,{},3);
        for(const auto& html:{full,brief}) {
            QVERIFY(html.contains(r.supplier));
            QVERIFY(html.contains(QString::fromUtf8("13 500")));
            QVERIFY(!html.contains("<td>0</td>"));
            QVERIFY(!html.contains(QString::fromUtf8("Не визначено")));
            QVERIFY(!html.contains(QString::fromUtf8("не підтверджено")));
            QVERIFY(!html.contains(QString::fromUtf8("вигадані запчастини")));
        }
        QVERIFY(full.contains("PRINT-DETAIL-11-END"));
        QTextDocument compact; compact.setHtml(DebtAnalysisPdfService::compactHtml(full));
        QVERIFY(compact.toPlainText().contains(QString::fromUtf8("Єдиний реєстр боргів")));
        QVERIFY(compact.toPlainText().contains(QString::fromUtf8("Сума за обліком")));
        QVERIFY(!brief.contains("PRINT-DETAIL-"));
        QVERIFY(!brief.contains(QString::fromUtf8("Єдиний реєстр боргів")));
        const auto filtered=PurchaseDebtAnalysisPage::analysisHtml(debts,{},today,debts[0].supplier,-1,facts,{},3);
        QVERIFY(!filtered.contains(debts[11].supplier)); QVERIFY(!filtered.contains(r.supplier));
        QString error;
        const QString qa=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
        for(const auto& report:QList<QPair<QString,QString>>{{"debt-full-print.pdf",full},{"debt-brief-print.pdf",brief}}) {
            const auto path=scratch.filePath(report.first);
            QVERIFY2(DebtAnalysisPdfService::save(path,report.second,&error),qPrintable(error));
            const auto poppler=QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../resources/windows/ocr-layer/poppler/bin/pdftotext.exe");
            QProcess extract; extract.start(poppler,{"-layout","-enc","UTF-8",path,"-"});
            QVERIFY(extract.waitForFinished()); QCOMPARE(extract.exitCode(),0);
            const auto text=QString::fromUtf8(extract.readAllStandardOutput());
            QVERIFY(text.contains("CarService PointG")); QVERIFY(text.contains(r.supplier));
            QCOMPARE(text.contains("PRINT-DETAIL-11-END"),report.first=="debt-full-print.pdf");
            if(report.first=="debt-full-print.pdf") {
                QVERIFY(text.contains(QString::fromUtf8("Єдиний реєстр боргів")));
                QVERIFY(text.contains(QString::fromUtf8("Сума за обліком")));
            }
            if(!qa.isEmpty()) { QVERIFY(QDir().mkpath(qa)); QVERIFY(QFile::copy(path,QDir(qa).filePath(report.first))); }
        }
    }
    void manualAttachmentsAreNotInvoiceEvidenceAndReplacementRevokesEvidence() {
        addOrder();
        sql("INSERT INTO work_order_parts(id,order_uid,order_number,name,qty,purchase_amount) VALUES(1,'stage1-order',1,'Filter',2,200)");
        QString error;
        const QString first=pdfFile("manual-evidence.pdf"), second=pdfFile("replacement-evidence.pdf");
        QVERIFY(OrderAttachmentService::setPartAttachment(db,1,first,"manual","MANUAL","25.09.26",&error));
        PartRowSnapshot part; part.id=1; part.orderUid="stage1-order";
        QVERIFY(InvoiceEvidenceService::load(db,{part},&error).isEmpty());
        QVERIFY(OrderAttachmentService::setPartAttachment(db,1,first,"import","IMPORT","25.09.26",&error,true));
        QCOMPARE(InvoiceEvidenceService::load(db,{part},&error).size(),1);
        QVERIFY(OrderAttachmentService::setPartAttachment(db,1,second,"manual",{}, {},&error));
        QVERIFY(InvoiceEvidenceService::load(db,{part},&error).isEmpty());
        QCOMPARE(count("invoice_part_evidence"),1); // Historical evidence retained.
    }
    void evidenceFailureRollsBackAttachmentEvenWithoutOuterTransaction() {
        addOrder();
        sql("INSERT INTO work_order_parts(id,order_uid,order_number,name,qty,purchase_amount) VALUES(1,'stage1-order',1,'Filter',2,200)");
        QString error;
        const QString first=pdfFile("atomic-evidence.pdf");
        QVERIFY(OrderAttachmentService::setPartAttachment(db,1,first,"first","FIRST","25.09.26",&error,true));
        sql("CREATE TRIGGER fail_evidence BEFORE INSERT ON invoice_part_evidence BEGIN SELECT RAISE(ABORT,'evidence failure'); END");
        QVERIFY(!OrderAttachmentService::setPartAttachment(db,1,pdfFile("atomic-second.pdf"),"second","SECOND","26.09.26",&error,true));
        QVERIFY(!error.isEmpty());
        QCOMPARE(OrderAttachmentService::partAttachmentPath(db,1),first);
        PartRowSnapshot part; part.id=1; part.orderUid="stage1-order";
        QCOMPARE(InvoiceEvidenceService::load(db,{part}).value(InvoiceEvidenceService::key(part.orderUid,1)).invoiceNumber,QString("FIRST"));
        sql("DROP TRIGGER fail_evidence");
    }
    void invoiceEvidenceRequiresAuthorizedOrderScope() {
        addOrder();
        sql("INSERT INTO work_order_parts(id,order_uid,order_number,name,qty,purchase_amount) VALUES(1,'stage1-order',1,'Filter',2,200)");
        QString error;
        QVERIFY(OrderAttachmentService::setPartAttachment(db,1,pdfFile("scoped.pdf"),"invoice","SCOPE","25.09.26",&error,true));
        PartRowSnapshot part; part.id=1; part.orderUid="stage1-order"; part.orderNumber=1;
        sql("INSERT OR IGNORE INTO staff_accounts(id,display_name,role) VALUES('unlinked-master','Test Master','master')");
        ActivityAuditService::setCurrentActor({"unlinked-master","Test Master","master","test"});
        QVERIFY(InvoiceEvidenceService::load(db,{part},&error).isEmpty());
        ActivityAuditService::clearCurrentActor();
        QVERIFY(InvoiceEvidenceService::load(db,{part},&error).isEmpty());
        ActivityAuditService::setCurrentActor({"evidence-owner","Test Owner","owner","test"});
        QCOMPARE(InvoiceEvidenceService::load(db,{part},&error).size(),1);
    }
    void evidenceLookupUsesIndexOnLargeDataset() {
        addOrder();
        sql("INSERT INTO work_order_parts(id,order_uid,order_number,name,qty,purchase_amount) VALUES(1,'stage1-order',1,'Filter',2,200)");
        QString error;
        QVERIFY(OrderAttachmentService::setPartAttachment(db,1,pdfFile("indexed-evidence.pdf"),"invoice","IDX","25.09.26",&error,true));
        sql("WITH RECURSIVE n(i) AS (VALUES(2) UNION ALL SELECT i+1 FROM n WHERE i<100000) INSERT INTO invoice_part_evidence SELECT printf('%064d',i),'synthetic-'||i,i,'uid-'||i,'IDX','25.09.26','Test store','OEM','Filter',2,20000,'test.pdf','02.10.26 10:00:00',1 FROM n");
        QCOMPARE(count("invoice_part_evidence"),100000);
        QSqlQuery plan(db);
        QVERIFY(plan.exec("EXPLAIN QUERY PLAN SELECT purchase_cents FROM invoice_part_evidence WHERE order_uid='stage1-order' AND part_id=1 AND active=1"));
        QVERIFY(plan.next()); QVERIFY(plan.value(3).toString().contains("INDEX"));
        PartRowSnapshot part; part.id=1; part.orderUid="stage1-order";
        QElapsedTimer timer; timer.start();
        for(int i=0;i<100;++i) QCOMPARE(InvoiceEvidenceService::load(db,{part},&error).size(),1);
        qInfo() << "100 indexed invoice lookups, 100000 rows, ms:" << timer.elapsed();
        QVERIFY2(error.isEmpty(),qPrintable(error));
    }
    void invoiceEvidenceBatchesRemainScopedAndDoNotDropBoundaryRows() {
        addOrder();
        sql("INSERT INTO work_order_parts(id,order_uid,order_number,name,qty,purchase_amount) VALUES(1,'stage1-order',1,'Filter',2,200)");
        QString error;
        QVERIFY(OrderAttachmentService::setPartAttachment(db,1,pdfFile("batch-evidence.pdf"),"invoice","BATCH","25.09.26",&error,true));
        sql("WITH RECURSIVE n(i) AS (VALUES(2) UNION ALL SELECT i+1 FROM n WHERE i<402) INSERT INTO invoice_part_evidence SELECT printf('%064d',i),'synthetic-'||i,i,'uid-'||i,'BATCH','25.09.26','Store','OEM','Filter',2,20000,'test.pdf','02.10.26 10:00:00',1 FROM n");
        QVector<PartRowSnapshot> parts;
        for(int i=1;i<=401;++i) {
            PartRowSnapshot part; part.id=i; part.orderUid=i==1?"stage1-order":QString("synthetic-%1").arg(i);
            parts.append(part);
        }
        const auto evidence=InvoiceEvidenceService::load(db,parts,&error);
        QVERIFY2(error.isEmpty(),qPrintable(error)); QCOMPARE(evidence.size(),401);
        for(int i:{200,201,400,401}) QVERIFY(evidence.contains(InvoiceEvidenceService::key(QString("synthetic-%1").arg(i),i)));
        QVERIFY(!evidence.contains(InvoiceEvidenceService::key("synthetic-402",402)));
        ActivityAuditService::clearCurrentActor();
        QVERIFY(InvoiceEvidenceService::load(db,parts,&error).isEmpty());
    }
    void archiveReferencesRetainFiles() {
        const QString path = managed(file("archive-source.pdf", "archived document"), "archived.pdf");
        QVERIFY(!path.isEmpty());
        const QString archiveDir = QDir(AppRuntime::dataDir()).filePath("archive_shards");
        QVERIFY(QDir().mkpath(archiveDir));
        const QString archivePath = QDir(archiveDir).filePath("stage1-archive.db");
        {
            auto archive = QSqlDatabase::addDatabase("QSQLITE", "stage1_archive");
            archive.setDatabaseName(archivePath); QVERIFY(archive.open());
            QSqlQuery query(archive);
            QVERIFY(query.exec("CREATE TABLE attachments(file_path TEXT)"));
            query.prepare("INSERT INTO attachments VALUES(?)"); query.addBindValue(path); QVERIFY(query.exec());
            archive.close();
        }
        QSqlDatabase::removeDatabase("stage1_archive");
        OrderAttachmentService::removeUnreferencedManagedFile(db, path);
        QVERIFY(QFileInfo::exists(path));
        QVERIFY(QFile::remove(archivePath));
    }
    void metadataFailureRollsBackWholeInvoice() {
        addOrder();
        sql("CREATE TRIGGER stage1_fail_attachment BEFORE INSERT ON work_order_part_attachments "
            "WHEN (SELECT COUNT(*) FROM work_order_part_attachments)>0 BEGIN SELECT RAISE(ABORT,'synthetic failure'); END");
        CurrentOrdersInvoiceAttachmentSpec spec;
        spec.sourceFilePath = pdfFile("failed-import.pdf"); spec.invoiceNumber = "STAGE1-FAIL";
        CurrentOrdersInvoiceImportResult imported; QString error;
        QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(
            "^DB_TRANSACTION_CONTEXT phase= rollback_ok  transactionId= [0-9a-f-]{36}  owner= TransactionRunner  operationId= tx_runner_[0-9]+  connection= carservice_main  threadId= [0-9]+  error= step_failed$"));
        QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(
            "^DB_TRANSACTION step_failed_rollback connection= carservice_main  transactionId= [0-9a-f-]{36}  rollbackOk= true  error= synthetic failure Unable to fetch row$"));
        QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(
            "^INVOICE_IMPORT_END trace=INV-[0-9]+ status=failed error=synthetic failure Unable to fetch row$"));
        QVERIFY(!CurrentOrdersInvoiceImportService::importRows(rows(), spec, &imported, &error));
        QVERIFY(error.contains("synthetic failure"));
        QCOMPARE(count("work_order_parts"), 0); QCOMPARE(count("work_order_part_attachments"), 0);
        QVERIFY(imported.affectedOrderNumbers.isEmpty());
        sql("DROP TRIGGER stage1_fail_attachment");
        QVERIFY2(CurrentOrdersInvoiceImportService::importRows(rows(), spec, &imported, &error), qPrintable(error));
        QCOMPARE(count("work_order_parts"), 2);
    }
    void missingSourceDoesNotInsertRows() {
        addOrder(); CurrentOrdersInvoiceAttachmentSpec spec;
        spec.sourceFilePath = scratch.filePath("not-found.pdf"); QString error;
        QVERIFY(!CurrentOrdersInvoiceImportService::importRows(rows(), spec, nullptr, &error));
        QCOMPARE(count("work_order_parts"), 0);
    }
    void truncatedSourceDoesNotInsertRows() {
        addOrder(); CurrentOrdersInvoiceAttachmentSpec spec;
        spec.sourceFilePath = file("truncated-source.pdf", "%PDF-truncated"); QString error;
        QVERIFY(!CurrentOrdersInvoiceImportService::importRows(rows(), spec, nullptr, &error));
        QCOMPARE(count("work_order_parts"), 0);
    }
    void pdfCacheRejectsTruncationAndRebuilds() {
        const QString source = file("invoice.csv", "Name,Qty,Price\nFilter,2,100\n");
        InvoiceImport::ExcelReadResult data;
        data.rows = {{"OEM", "Name", "Qty", "Price", "Amount"}, {"TEST1", "Filter", "2", "100", "200"}};
        QString pdf, error;
        auto render = [&] {
            QThread* thread = QThread::create([&] {
                pdf = InvoiceImport::ensureInvoicePdfAttachmentFromRows(source, data, "TEST-PDF", "02.10.26", "Test", &error);
            });
            thread->start(); const bool finished = thread->wait(30000);
            if (finished) delete thread;
            return finished;
        };
        QVERIFY(render()); QVERIFY2(!pdf.isEmpty(), qPrintable(error));
        const QByteArray original = bytes(pdf); QVERIFY(original.startsWith("%PDF-"));
        QFile broken(pdf); QVERIFY(broken.open(QIODevice::WriteOnly)); broken.write("%PDF-truncated"); broken.close();
        QVERIFY(render()); QVERIFY2(!pdf.isEmpty(), qPrintable(error));
        QVERIFY(bytes(pdf).size() > 1000); QVERIFY(bytes(pdf).trimmed().endsWith("%%EOF"));
        const QString oldPath = pdf;
        file("invoice.csv", "Name,Qty,Price\nOtherx,2,100\n");
        QVERIFY(render()); QVERIFY2(!pdf.isEmpty(), qPrintable(error)); QVERIFY(pdf != oldPath);
    }
    void invalidPdfRejected() {
        const QString path = file("invalid.pdf", "%PDF-1.4\nnot a PDF\nstartxref\n0\n%%EOF\n");
        bool valid = true; QString error;
        auto* thread = QThread::create([&] { valid = InvoiceImport::validateInvoicePdf(path, &error); });
        thread->start(); QVERIFY(thread->wait(30000)); delete thread;
        QVERIFY(!valid); QVERIFY(!error.isEmpty());
    }
    void changedMappingAndMultiplePagesUseFreshCache() {
        const QString source = file("large.csv", "large synthetic invoice");
        InvoiceImport::ExcelReadResult data;
        data.rows = {{"OEM", "Name", "Qty", "Price", "Amount"}};
        for (int i = 0; i < 180; ++i) data.rows.append({QString::number(i), "Filter", "2", "100", "200"});
        QString pdf, error;
        auto render = [&] {
            auto* thread = QThread::create([&] { pdf = InvoiceImport::ensureInvoicePdfAttachmentFromRows(
                source, data, "MULTI", "02.10.26", "Test", &error); });
            thread->start(); const bool done = thread->wait(30000);
            if (done) delete thread;
            return done;
        };
        QVERIFY(render()); QVERIFY2(!pdf.isEmpty(), qPrintable(error));
        const QString previous = pdf;
        QVERIFY(bytes(pdf).count("/Type /Page\n") > 1 || bytes(pdf).count("/Type /Page ") > 1);
        data.rows[1][1] = "Other filter";
        QVERIFY(render()); QVERIFY2(!pdf.isEmpty(), qPrintable(error)); QVERIFY(pdf != previous);
    }
    void guiConversionNeverReturnsSpreadsheet() {
        const QString source = file("gui.csv", "Name,Qty,Price\nFilter,2,100\n");
        QString error;
        QVERIFY(InvoiceImport::ensureInvoicePdfAttachment(source, {}, {}, {}, &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(QFileInfo::exists(source));
    }
    void spreadsheetBatchRequiresPreparedPdf() {
        WorkPartsDialogExcelSupport::ImportedPartDraft draft;
        draft.name = "Filter"; draft.qtyText = "2"; draft.priceText = "100";
        InvoiceImport::ExcelReadResult data;
        const auto missing = WorkPartsImportedRowsViewModel::buildBatch(1, {draft}, data, "invoice.xlsx", {});
        QVERIFY(!missing.isValid());
        const auto ready = WorkPartsImportedRowsViewModel::buildBatch(1, {draft}, data, "invoice.xlsx", "invoice.pdf");
        QVERIFY(ready.isValid());
        QCOMPARE(ready.rows.first().attachmentSource, QStringLiteral("invoice.pdf"));
    }
    void documentOpenValidatesWithoutBlockingOrDuplicating() {
        QWidget owner;
        const QString source = pdfFile("open-valid.pdf");
        int callbacks = 0; QString opened, error;
        auto callback = [&](const QString& path, const QString& detail) {
            ++callbacks; opened = path; error = detail;
        };
        InvoiceDocumentOpenSupport::prepareAsync(&owner, source, {}, {}, {}, callback);
        QCOMPARE(callbacks, 0);
        InvoiceDocumentOpenSupport::prepareAsync(&owner, source, {}, {}, {}, callback);
        QTRY_COMPARE_WITH_TIMEOUT(callbacks, 1, 30000);
        QCOMPARE(opened, source); QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!owner.property("csInvoiceOpenPending").toBool());
        InvoiceDocumentOpenSupport::prepareAsync(&owner, file("open-broken.pdf", "%PDF-broken"), {}, {}, {}, callback);
        QTRY_COMPARE_WITH_TIMEOUT(callbacks, 2, 30000);
        QVERIFY(opened.isEmpty()); QVERIFY(!error.isEmpty());
    }
    void destroyedDocumentOwnerDoesNotReceiveResult() {
        int callbacks = 0;
        auto* owner = new QWidget;
        InvoiceDocumentOpenSupport::prepareAsync(owner, pdfFile("owner.pdf"), {}, {}, {},
            [&](const QString&, const QString&) { ++callbacks; });
        delete owner;
        QTest::qWait(500);
        QCOMPARE(callbacks, 0);
    }
    void legacySpreadsheetOpensPdfAndKeepsOriginal_data() {
        QTest::addColumn<QString>("fixture");
        QTest::newRow("csv") << QString();
        QTest::newRow("xlsx") << QStringLiteral("avtonova_d_excel_vat.xlsx");
        QTest::newRow("xls") << QStringLiteral("unik_trade_excel_vat.xls");
    }
    void legacySpreadsheetOpensPdfAndKeepsOriginal() {
        QFETCH(QString, fixture);
        QWidget owner;
        QByteArray original = QString::fromUtf8("OEM;Найменування;Кількість;Ціна;Сума\nTEST-001;Фільтр;2;100;200\n").toUtf8();
        if (!fixture.isEmpty()) {
            original = bytes(QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()).filePath("fixtures/invoices/" + fixture));
            QVERIFY(!original.isEmpty());
        }
        const QString source = file(fixture.isEmpty() ? "legacy.csv" : fixture, original);
        bool finished = false; QString opened, error;
        InvoiceDocumentOpenSupport::prepareAsync(&owner, source, "LEGACY", "02.10.26", "Test",
            [&](const QString& path, const QString& detail) { finished = true; opened = path; error = detail; });
        QTRY_VERIFY_WITH_TIMEOUT(finished, 30000);
        QVERIFY2(!opened.isEmpty(), qPrintable(error));
        QCOMPARE(QFileInfo(opened).suffix(), QStringLiteral("pdf"));
        QVERIFY(bytes(opened).startsWith("%PDF-"));
        QCOMPARE(bytes(source), original);
    }
    void canonicalCurrencyAndAmounts_data() {
        QTest::addColumn<bool>("currency");
        QTest::newRow("without-currency") << false;
        QTest::newRow("currency-on-last-page") << true;
    }
    void canonicalCurrencyAndAmounts() {
        QFETCH(bool, currency);
        const QString source = file(currency ? "currency.csv" : "uah.csv", "synthetic invoice");
        InvoiceImport::ExcelReadResult data;
        data.rows = {{"OEM", QString::fromUtf8("Найменування"), QString::fromUtf8("Кількість"),
                      QString::fromUtf8("Ціна"), QString::fromUtf8("Сума"), "EURO"}};
        for (int i = 0; i < 150; ++i)
            data.rows.append({QString::number(i), QString::fromUtf8("Фільтр масляный"), "2.5", "10.10", "25.25",
                              currency && i == 149 ? "1.50" : ""});
        QString pdf, error;
        auto* thread = QThread::create([&] { pdf = InvoiceImport::ensureInvoicePdfAttachmentFromRows(
            source, data, "STAGE3", "02.10.26", "Test", &error); });
        thread->start(); QVERIFY(thread->wait(30000)); delete thread;
        QVERIFY2(!pdf.isEmpty(), qPrintable(error));
        const QString layer = qEnvironmentVariable("CS_PDF_OCR_LAYER");
        QDirIterator tools(layer, {"pdftotext.exe"}, QDir::Files, QDirIterator::Subdirectories);
        QVERIFY2(tools.hasNext(), "Bundled pdftotext is required for PDF content checks");
        QProcess process; process.start(tools.next(), {"-layout", "-enc", "UTF-8", pdf, "-"});
        QVERIFY(process.waitForFinished(30000)); QCOMPARE(process.exitCode(), 0);
        const QString text = QString::fromUtf8(process.readAllStandardOutput());
        QCOMPARE(text.contains("EURO"), currency);
        QVERIFY(text.contains(QString::fromUtf8("Фільтр масляный")));
        QVERIFY(text.contains("25.25") || text.contains("25,25"));
        QVERIFY(text.contains("3787.50") || text.contains("3 787.50") || text.contains("3 787,50"));
        QVERIFY(text.count(QChar('\f')) > 1);
        const QString qa = qEnvironmentVariable("CS_STAGE3_PDF_QA_DIR");
        if (!qa.isEmpty()) {
            QVERIFY(QDir().mkpath(qa));
            QVERIFY(QFile::copy(pdf, QDir(qa).filePath(currency ? "currency.pdf" : "uah.pdf")));
        }
    }
    void backupRehearsalIsDatabaseOnly() {
        addOrder();
        sql("INSERT INTO work_order_parts(id,order_uid,order_number,name) VALUES(1,'stage1-order',1,'Filter')");
        const QString path = managed(file("backup-doc.pdf", "document outside SQLite"), "backup-doc.pdf");
        QVERIFY(OrderAttachmentService::setPartAttachment(db, 1, path, "backup"));
        SnapshotJobContext context; context.source = "stage1_diagnostic";
        context.event.eventId = QUuid::createUuid().toString();
        SnapshotMetadata metadata; QString error;
        QVERIFY2(BackupSnapshotService::createSnapshot(context, &metadata, &error), qPrintable(error));
        QVERIFY2(BackupSnapshotService::rehearseRestore(metadata.snapshotPath, &error), qPrintable(error));
        QVERIFY(QFileInfo::exists(path));
        QTemporaryDir restoreDir;
        QVERIFY(restoreDir.isValid());
        const QString independent = restoreDir.filePath("independent-restore.db");
        QVERIFY(QFile::copy(metadata.snapshotPath, independent));
        {
            auto restored = QSqlDatabase::addDatabase("QSQLITE", "stage1_restore");
            restored.setDatabaseName(independent); restored.setConnectOptions("QSQLITE_OPEN_READONLY"); QVERIFY(restored.open());
            QSqlQuery query(restored); QVERIFY(query.exec("SELECT file_path FROM work_order_part_attachments")); QVERIFY(query.next());
            QCOMPARE(query.value(0).toString(), path);
            QVERIFY(!QFileInfo::exists(QDir(QFileInfo(independent).absolutePath()).filePath(QFileInfo(path).fileName())));
            restored.close();
        }
        QSqlDatabase::removeDatabase("stage1_restore");
    }
    void cleanupTestCase() {
        db.close(); db = {}; QSqlDatabase::removeDatabase("carservice_main");
        for (const auto& name : QSqlDatabase::connectionNames()) {
            { auto connection = QSqlDatabase::database(name, false); connection.close(); }
            QSqlDatabase::removeDatabase(name);
        }
        const QString root=QFileInfo(qEnvironmentVariable("CS_TEST_DATA_ROOT")).canonicalFilePath();
        QVERIFY(!root.isEmpty());
        QVERIFY(QFileInfo(testDataRoot).canonicalFilePath().startsWith(root + "/") && QFileInfo(testDataRoot).fileName().startsWith("Safety-"));
        QVERIFY(QDir(testDataRoot).removeRecursively());
    }
};

QTEST_MAIN(DataAttachmentSafetyTests)
#include "DataAttachmentSafetyTests.moc"
