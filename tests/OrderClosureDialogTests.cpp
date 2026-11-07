#include <QtTest>
#include <QLineEdit>
#include <QPushButton>
#include "ui/common/dialogs/OrderClosureDialog.h"
#include "core/cloudsync/OrderPartIssueEvidence.h"
#include "core/services/orders/OrderClosureJournal.h"
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QSqlRecord>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QSqlError>

class OrderClosureDialogTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        const auto root=QFileInfo(qEnvironmentVariable("CS_TEST_DATA_ROOT")).canonicalFilePath();
        QVERIFY2(!root.isEmpty(),"Explicit isolated test root required");
        QVERIFY(QFileInfo(QDir::tempPath()).canonicalFilePath().startsWith(root+"/"));
        qInfo()<<"ISOLATION_VERIFIED"<<root;
    }
    void journalRecovery_data() {
        QTest::addColumn<QString>("dataset");
        QTest::newRow("baseline") << QString("baseline");
        QTest::newRow("scaled5") << QString("scaled5");
    }
    void journalRecovery() {
        QFETCH(QString,dataset);
        QTemporaryDir temp(QDir(qEnvironmentVariable("CS_TEST_DATA_ROOT")).filePath("closure-XXXXXX"));QVERIFY(temp.isValid());
        const auto path=temp.filePath("workspace.db");
        // Synthetic baseline and 5x fixture: no production database or credentials.
        {
            auto fixture=QSqlDatabase::addDatabase("QSQLITE","closure-fixture");fixture.setDatabaseName(path);QVERIFY(fixture.open());
            QSqlQuery q(fixture);
            QVERIFY(q.exec("CREATE TABLE work_orders(number INTEGER PRIMARY KEY,order_uid TEXT UNIQUE,status TEXT,closed_at TEXT)"));
            QVERIFY(q.exec("CREATE TABLE work_order_parts(id INTEGER PRIMARY KEY,part_uid TEXT UNIQUE,order_uid TEXT,installed_at TEXT)"));
            for(const auto& table:QStringList{"purchase_payment_balances","purchase_payment_events","purchase_payment_terms","workshop_spare_payment_allocations","workshop_spare_movements"})
                QVERIFY(q.exec("CREATE TABLE "+table+"(id INTEGER PRIMARY KEY,amount_cents INTEGER)"));
            QVERIFY(fixture.transaction());
            const int rows=dataset=="scaled5"?500:100;
            for(int i=1;i<=rows;++i) {
                QVERIFY(q.exec(QString("INSERT INTO work_orders VALUES(%1,'order-%1','open','')").arg(i)));
                QVERIFY(q.exec(QString("INSERT INTO work_order_parts VALUES(%1,'part-%1','order-%1','')").arg(i)));
                for(const auto& table:QStringList{"purchase_payment_balances","purchase_payment_events","purchase_payment_terms","workshop_spare_payment_allocations","workshop_spare_movements"})
                    QVERIFY(q.exec(QString("INSERT INTO %1 VALUES(%2,300000)").arg(table).arg(i)));
            }
            QVERIFY(fixture.commit());fixture.close();
        }
        QSqlDatabase::removeDatabase("closure-fixture");
        const QString connection="closure-journal-test";
        {
            auto db=QSqlDatabase::addDatabase("QSQLITE",connection);db.setDatabaseName(path);QVERIFY(db.open());
            QString error;QVERIFY2(OrderClosureJournal::ensureSchema(db,&error),qPrintable(error));
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT o.order_uid,p.part_uid FROM work_orders o JOIN work_order_parts p ON p.order_uid=o.order_uid WHERE o.order_uid<>'' AND p.part_uid<>'' LIMIT 1"));QVERIFY(q.next());
            const auto uid=q.value(0).toString(),part=q.value(1).toString();q.finish();
            auto scalar=[&](const QString& sql){QSqlQuery s(db);if(!s.exec(sql)||!s.next())return QVariant();return s.value(0);};
            auto suppliers=[&]{QByteArray bytes;for(const auto& table:QStringList{"purchase_payment_balances","purchase_payment_events","purchase_payment_terms","workshop_spare_payment_allocations","workshop_spare_movements"}) {
                if(!db.tables().contains(table))continue;QSqlQuery s(db);if(!s.exec("SELECT * FROM "+table+" ORDER BY rowid"))return QByteArray("error");
                while(s.next())for(int i=0;i<s.record().count();++i){bytes+=s.value(i).toString().toUtf8();bytes+='\0';}}
                return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);};
            const auto before=suppliers();QVERIFY(before!="error");
            const QString requestId="test-closure-"+dataset;
            const QJsonObject request{{"request_uid",requestId},{"order_uid",uid},{"expected_revision","test"}};
            QVERIFY2(OrderClosureJournal::remember(db,request,&error),qPrintable(error));
            const QJsonObject payment{{"payment_uid","test-receipt-"+dataset},{"request_uid",requestId},{"order_uid",uid},{"amount_cents",500000},{"method","cash"},{"paid_on","2026-10-28"},{"actor","test-owner"},{"recorded_ms",1000}};
            const QJsonObject response{{"schema_version",1},{"ok",true},{"request_uid",requestId},{"order_uid",uid},{"status","closed"},
                {"order",QJsonObject{{"order_uid",uid},{"closed_at","2026-10-28T10:00:00+02:00"}}},
                {"payments",QJsonArray{payment}},{"received_cents",500000},
                {"installed_parts",QJsonArray{QJsonObject{{"part_uid",part},{"order_uid",uid},{"installed_at","2026-10-28"}}}}};
            const auto orderBefore=scalar("SELECT status||coalesce(closed_at,'') FROM work_orders WHERE order_uid='"+uid+"'");
            QVERIFY(q.exec("CREATE TRIGGER fail_closure BEFORE UPDATE ON work_orders BEGIN SELECT RAISE(ABORT,'injected write failure'); END"));
            QVERIFY(!OrderClosureJournal::apply(db,response,&error));
            QCOMPARE(scalar("SELECT COUNT(*) FROM client_payments").toInt(),0);
            QCOMPARE(scalar("SELECT status||coalesce(closed_at,'') FROM work_orders WHERE order_uid='"+uid+"'"),orderBefore);
            QCOMPARE(suppliers(),before);
            QVERIFY(q.exec("DROP TRIGGER fail_closure"));db.close();QVERIFY(db.open());
            error.clear();QCOMPARE(OrderClosureJournal::pending(db,uid,&error),request);
            QFile input(temp.filePath("response.json"));QVERIFY(input.open(QIODevice::WriteOnly));input.write(QJsonDocument(response).toJson());input.close();
            for(const auto& phase:{QString("before_commit"),QString("after_commit")}) {
                db.close();
                QProcess child;auto env=QProcessEnvironment::systemEnvironment();env.insert("CS_CLOSURE_CRASH",phase);child.setProcessEnvironment(env);
                child.start(QCoreApplication::applicationFilePath(),{"--crash-child",path,input.fileName()});
                QVERIFY(child.waitForStarted(5000));
                qInfo()<<"RECOVERY_CHILD_STARTED"<<dataset<<phase<<child.processId();
                QVERIFY(child.waitForFinished(30000));
                qInfo()<<"RECOVERY_CHILD_FINISHED"<<dataset<<phase<<child.exitCode()<<child.readAllStandardOutput()<<child.readAllStandardError();
                QCOMPARE(child.exitCode(),87);
                QVERIFY(db.open());
                QCOMPARE(scalar("PRAGMA integrity_check").toString(),QString("ok"));
                QCOMPARE(suppliers(),before);
                if(phase=="before_commit") {
                    QCOMPARE(scalar("SELECT COUNT(*) FROM client_payments").toInt(),0);
                    QCOMPARE(scalar("SELECT status||coalesce(closed_at,'') FROM work_orders WHERE order_uid='"+uid+"'"),orderBefore);
                    QCOMPARE(OrderClosureJournal::pending(db,uid,&error),request);
                }
            }
            db.close();QVERIFY(db.open());
            QVERIFY2(OrderClosureJournal::apply(db,response,&error),qPrintable(error));
            QCOMPARE(scalar("SELECT COUNT(*) FROM client_payments").toInt(),1);
            QCOMPARE(scalar("SELECT SUM(amount_cents) FROM client_payments").toLongLong(),qint64(500000));
            QCOMPARE(scalar("SELECT status FROM work_orders WHERE order_uid='"+uid+"'").toString(),QString("closed"));
            QCOMPARE(scalar("SELECT installed_at FROM work_order_parts WHERE part_uid='"+part+"'").toString(),QString("2026-10-28"));
            QVERIFY(OrderClosureJournal::pending(db,uid,&error).isEmpty());
            QCOMPARE(suppliers(),before);
            QVERIFY(q.exec("UPDATE work_orders SET status='open' WHERE order_uid='"+uid+"'"));
            QVERIFY(OrderClosureJournal::apply(db,response,&error));
            QCOMPARE(scalar("SELECT status FROM work_orders WHERE order_uid='"+uid+"'").toString(),QString("open"));
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
    }
    void issueEvidenceRequiresMatchingActualMovement() {
        const QString connection = "closure-issue-test";
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(":memory:");
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("CREATE TABLE work_order_parts(id INTEGER,part_uid TEXT,order_uid TEXT,order_number INTEGER)"));
            QVERIFY(q.exec("INSERT INTO work_order_parts VALUES(1,'P','A',42),(2,'Q','A',42)"));
            const QJsonArray rows{QJsonObject{{"part_uid","P"},{"order_uid","A"}},
                                  QJsonObject{{"part_uid","Q"},{"order_uid","A"}}};
            QVERIFY(OrderPartIssueEvidence::attach(db,rows)[0].toObject()["closure_issue_evidence"].isNull());
            QVERIFY(q.exec("CREATE TABLE workshop_spare_movements(id INTEGER,part_id INTEGER,order_number INTEGER,operation TEXT,quantity REAL)"));
            QVERIFY(q.exec("INSERT INTO workshop_spare_movements VALUES(10,1,42,'issue',2),(11,2,99,'issue',1),(12,2,42,'receipt',1)"));
            const auto annotated = OrderPartIssueEvidence::attach(db, rows);
            const auto proof = annotated[0].toObject()["closure_issue_evidence"].toObject();
            QCOMPARE(proof["issued_quantity"].toDouble(), 2.0);
            QCOMPARE(proof["movement_ids"].toArray().size(), 1);
            QVERIFY(annotated[1].toObject()["closure_issue_evidence"].isNull());
            QVERIFY(q.exec("DELETE FROM workshop_spare_movements WHERE id=10"));
            QVERIFY(OrderPartIssueEvidence::attach(db,annotated)[0].toObject()["closure_issue_evidence"].isNull());
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
    }
    void blankPaymentUsesOutstandingBalance() {
        OrderClosureDialog dialog({"536", "revision", 3450000, 100005, {}});
        auto* amount = dialog.findChild<QLineEdit*>("receivedNow");
        QVERIFY(amount->text().isEmpty());
        dialog.findChild<QPushButton*>("closeWithPayment")->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(amount->text(), QString("33499,95"));
        QCOMPARE(dialog.choice().amountCents, qint64(3349995));
    }
    void emptyDefaultAndPartialPayment() {
        OrderClosureDialog dialog({"42", "revision", 500000, 100000, {}});
        auto* amount = dialog.findChild<QLineEdit*>("receivedNow");
        auto* pay = dialog.findChild<QPushButton*>("closeWithPayment");
        QVERIFY(amount->text().isEmpty());
        QVERIFY(pay->isEnabled());
        amount->setText("4000,01");
        QVERIFY(!pay->isEnabled());
        amount->setText("1000,50");
        QVERIFY(pay->isEnabled());
        pay->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QVERIFY(dialog.choice().withPayment);
        QCOMPARE(dialog.choice().amountCents, qint64(100050));
        QCOMPARE(dialog.choice().expectedRevision, QString("revision"));
    }
    void noNewPaymentAndFullyPaid() {
        OrderClosureDialog dialog({"42", "revision", 500000, 500000, {}});
        QVERIFY(!dialog.findChild<QLineEdit*>("receivedNow")->isEnabled());
        QVERIFY(!dialog.findChild<QPushButton*>("closeWithPayment")->isEnabled());
        dialog.findChild<QPushButton*>("closeWithoutPayment")->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QVERIFY(!dialog.choice().withPayment);
        QCOMPARE(dialog.choice().amountCents, qint64(0));
    }
    void unresolvedPartsBlockBothActions() {
        OrderClosureDialog dialog({"42", "revision", 500000, 0, {"Не виданий фільтр"}});
        QVERIFY(!dialog.findChild<QPushButton*>("closeWithPayment")->isEnabled());
        QVERIFY(!dialog.findChild<QPushButton*>("closeWithoutPayment")->isEnabled());
        dialog.findChild<QPushButton*>("cancelClosure")->click();
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
        QVERIFY(dialog.choice().expectedRevision.isEmpty());
    }
    void escapeAndWindowCloseDiscardIntent() {
        for (bool escape : {true, false}) {
            OrderClosureDialog dialog({"42", "revision", 500000, 0, {}});
            dialog.show();
            dialog.findChild<QLineEdit*>("receivedNow")->setText("5000");
            if (escape) QTest::keyClick(&dialog, Qt::Key_Escape);
            else dialog.close();
            QCOMPARE(dialog.result(), int(QDialog::Rejected));
            QCOMPARE(dialog.choice().amountCents, qint64(0));
        }
    }
};
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    const auto root=QFileInfo(qEnvironmentVariable("CS_TEST_DATA_ROOT")).canonicalFilePath();
    if(root.isEmpty())return 9;
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("CarServiceClosureIsolated");
    QCoreApplication::setApplicationName("ClosureRecoveryTest");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root);
    if(argc==4 && QString::fromLocal8Bit(argv[1])=="--crash-child") {
        if(!QFileInfo(QString::fromLocal8Bit(argv[2])).canonicalFilePath().startsWith(root+"/")
            || !QFileInfo(QString::fromLocal8Bit(argv[3])).canonicalFilePath().startsWith(root+"/"))return 10;
        auto db=QSqlDatabase::addDatabase("QSQLITE");db.setDatabaseName(QString::fromLocal8Bit(argv[2]));
        if(!db.open())return 2;
        QFile input(QString::fromLocal8Bit(argv[3]));if(!input.open(QIODevice::ReadOnly))return 3;
        QString error;return OrderClosureJournal::apply(db,QJsonDocument::fromJson(input.readAll()).object(),&error)?0:4;
    }
    OrderClosureDialogTests test;return QTest::qExec(&test,argc,argv);
}
#include "OrderClosureDialogTests.moc"
