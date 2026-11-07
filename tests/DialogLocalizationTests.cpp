#include <QtTest/QtTest>
#include <QTranslator>
#include <QMessageBox>
#include <QFileDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSqlQuery>
#include <QSqlError>
#include <QStandardPaths>
#include <QUuid>
#include "ui/common/dialogs/MessageBoxUtils.h"
#include "ui/common/dialogs/UkrainianDialogText.h"
#include "core/crm/VehicleIntakeService.h"

class DialogLocalizationTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setApplicationName("Safety-DialogLocalization-" + QUuid::createUuid().toString(QUuid::Id128));
        Q_INIT_RESOURCE(resources);
        qApp->setFont(QFont("Times New Roman",10));
    }
    void sqliteMessageAndButtonsAreUkrainian() {
        QMessageBox box(QMessageBox::Warning,"Error",
            "NOT NULL constraint failed: vehicle_intake.pdf_path Unable to fetch row",
            QMessageBox::Ok | QMessageBox::Cancel);
        applyStandardMessageBoxStyle(&box);
        QCOMPARE(box.windowTitle(),QString::fromUtf8("Помилка"));
        QCOMPARE(box.button(QMessageBox::Ok)->text(),QString::fromUtf8("Гаразд"));
        QCOMPARE(box.button(QMessageBox::Cancel)->text(),QString::fromUtf8("Скасувати"));
        QVERIFY(!box.text().contains("NOT NULL"));
        QVERIFY(!box.text().contains("Unable"));
        QVERIFY(box.text().contains(QString::fromUtf8("шлях до PDF акта приймання")));
        const QString translated=box.text();
        applyStandardMessageBoxStyle(&box);
        QCOMPARE(box.text(),translated);
    }
    void messageBoxesUsePaymentColors() {
        for(auto icon:{QMessageBox::Information,QMessageBox::Warning,QMessageBox::Critical,QMessageBox::Question}) {
            QMessageBox box(icon,QString::fromUtf8("Оплата"),
                QString::fromUtf8("Заборгованість відсутня. Сплачувати нічого не потрібно."),
                icon==QMessageBox::Question ? QMessageBox::Yes|QMessageBox::No : QMessageBox::Ok);
            applyStandardMessageBoxStyle(&box);
            box.show();QTest::qWait(50);
            QCOMPARE(box.palette().color(QPalette::Window),QColor("#202930"));
            for(auto* button:box.buttons()) {
                QVERIFY(button->styleSheet().contains("#5b6672"));
                QVERIFY(button->styleSheet().contains("#84919d"));
                QCOMPARE(button->palette().color(QPalette::ButtonText),QColor("#ffffff"));
            }
            if(icon==QMessageBox::Information && !qEnvironmentVariable("CS_MESSAGE_SCREENSHOT").isEmpty())
                QVERIFY(box.grab().save(qEnvironmentVariable("CS_MESSAGE_SCREENSHOT")));
        }
    }
    void preservesUserDataAndCustomActions() {
        const QString text=QString::fromUtf8("Не знайдено файл C:/Reports/Audi Q7.pdf");
        QCOMPARE(UkrainianDialogText::translate(text),text);
        QMessageBox box(QMessageBox::Question,QString::fromUtf8("Підтвердження"),
            QString::fromUtf8("Закрити наряд?"),QMessageBox::Yes | QMessageBox::No);
        box.button(QMessageBox::Yes)->setText(QString::fromUtf8("Закрити наряд"));
        applyStandardMessageBoxStyle(&box);
        QCOMPARE(box.button(QMessageBox::Yes)->text(),QString::fromUtf8("Закрити наряд"));
        QCOMPARE(box.button(QMessageBox::No)->text(),QString::fromUtf8("Ні"));
    }
    void embeddedQtTranslationCoversStandardDialogs() {
        QTranslator translator;
        QVERIFY(translator.load(":/translations/qtbase_uk.qm"));
        QVERIFY(qApp->installTranslator(&translator));
        QDialogButtonBox buttons(QDialogButtonBox::Cancel | QDialogButtonBox::Save | QDialogButtonBox::Open);
        for(auto* button:buttons.buttons()) {
            QVERIFY2(button->text().contains(QRegularExpression(QString::fromUtf8("[А-Яа-яІіЇїЄєҐґ]"))),qPrintable(button->text()));
        }
        QFileDialog files;
        files.setOption(QFileDialog::DontUseNativeDialog);
        const QString fileNameLabel=files.labelText(QFileDialog::FileName);
        QVERIFY2(fileNameLabel.contains(QRegularExpression(QString::fromUtf8("[А-Яа-яІіЇїЄєҐґ]"))),qPrintable(fileNameLabel));
        qApp->removeTranslator(&translator);
    }
    void newIntakeWithoutPdfOrSignatureSavesAndUpdates() {
        const QString connection="intake-dialog-test";
        {
            auto db=QSqlDatabase::addDatabase("QSQLITE",connection);
            db.setDatabaseName(":memory:"); QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("CREATE TABLE vehicle_intake(intake_uid TEXT PRIMARY KEY,"
                "order_uid TEXT NOT NULL DEFAULT '',client_uid TEXT NOT NULL DEFAULT '',"
                "vehicle_uid TEXT NOT NULL DEFAULT '',mileage INTEGER NOT NULL DEFAULT 0,"
                "fuel_level TEXT NOT NULL DEFAULT '',exterior_state TEXT NOT NULL DEFAULT '',"
                "interior_state TEXT NOT NULL DEFAULT '',checklist_json TEXT NOT NULL DEFAULT '{}',"
                "customer_signature_path TEXT NOT NULL DEFAULT '',pdf_path TEXT NOT NULL DEFAULT '',"
                "sync_status TEXT NOT NULL DEFAULT '',updated_at TEXT NOT NULL DEFAULT '')"));
            QString uid,error;
            QVERIFY2(VehicleIntakeService::saveIntake(db,{{"mileage",120000}},&uid,&error),qPrintable(error));
            QVERIFY(!uid.isEmpty());
            QVERIFY(q.exec("SELECT pdf_path,customer_signature_path,act_number,act_date,accepted_at FROM vehicle_intake"));
            QVERIFY(q.next());
            for(int col=0; col<5; ++col) { QVERIFY(!q.isNull(col)); QCOMPARE(q.value(col).toString(),QString("")); }
            q.finish();
            QVERIFY2(VehicleIntakeService::saveIntake(db,{{"intake_uid",uid},{"pdf_path","C:/Reports/act.pdf"}},nullptr,&error),qPrintable(error));
            QVERIFY(q.exec("SELECT count(*),pdf_path FROM vehicle_intake"));
            QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),1); QCOMPARE(q.value(1).toString(),QString("C:/Reports/act.pdf"));
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
    }
};
QTEST_MAIN(DialogLocalizationTests)
#include "DialogLocalizationTests.moc"
