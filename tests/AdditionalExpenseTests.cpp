#include <QtTest/QtTest>
#include "PayrollReadFixture.h"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QFontDatabase>
#include <QFile>
#include <QStandardPaths>
#include <QDateEdit>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScopeGuard>
#include <QSqlError>
#include <QSqlQuery>
#include <QTableView>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include "core/services/AdditionalExpenseService.h"
#include "core/services/ActivityAuditService.h"
#include "core/services/ReportsDataLoader.h"
#include "core/services/ReportsSyncSupport.h"
#include "core/util/FormatUtils.h"
#include "ui/tabs/reports/ReportsPage.h"
#include "ui/theme/ThemeManager.h"

class AdditionalExpenseTests : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    QSqlDatabase db;
private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("CarServiceExpenseTests");
        QCoreApplication::setApplicationName("Safety-ExpenseDialogTest-" + QUuid::createUuid().toString(QUuid::Id128));
        for (const auto& name : {"segoeui.ttf", "times.ttf"}) {
            const auto path=qEnvironmentVariable("WINDIR")+"/Fonts/"+name;
            if (QFile::exists(path)) QVERIFY(QFontDatabase::addApplicationFont(path)>=0);
        }
    }
    void init() {
        ActivityAuditService::clearCurrentActor();
        db = QSqlDatabase::addDatabase("QSQLITE", "carservice_main");
        db.setDatabaseName(dir.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + ".db"));
        QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE settings(key TEXT PRIMARY KEY,value TEXT)"));
        QVERIFY(q.exec("CREATE TABLE expenses(id INTEGER PRIMARY KEY,period_from TEXT,period_to TEXT,"
            "elec_cost REAL,elec_curr REAL,elec_prev REAL,elec_tariff REAL,cleaning REAL,"
            "sw_opendatabot REAL,sw_tecalliance REAL,heating REAL,amortization REAL,rent REAL,other_amount REAL,"
            "cleaning_note TEXT,software_note TEXT,heating_note TEXT,amortization_note TEXT,rent_note TEXT,other_note TEXT)"));
        QString error;
        QVERIFY2(AdditionalExpenseService::ensureSchema(db, &error), qPrintable(error));
        ThemeManager::applySavedTheme();
    }
    void cleanup() {
        ActivityAuditService::clearCurrentActor();
        db.close(); db = {};
        QSqlDatabase::removeDatabase("carservice_main");
    }
    void totalsDatesAndPersistence() {
        QSqlQuery q(db);
        QVERIFY(q.exec("INSERT INTO expenses(id,period_from,period_to,elec_cost,rent,rent_note) "
            "VALUES(1,'2026-09-01','2026-09-30',100.25,300.50,'September rent')"));
        QString error; qint64 id = 0;
        QVERIFY2(AdditionalExpenseService::add(db, QDate(2026,9,17), " Delivery ", "Parcel A", 123.45, &id, &error), qPrintable(error));
        QVERIFY(AdditionalExpenseService::add(db, QDate(2026,9,30), "Trips", "Fuel", 0.01));
        QVERIFY(AdditionalExpenseService::add(db, QDate(2026,10,1), "Trips", "Outside", 500));
        db.close(); QVERIFY(db.open());
        auto s = ReportsDataLoader::buildTotalExpensesSnapshot(db.databaseName(), "2026-09-01", "2026-09-30");
        QVERIFY2(s.error.isEmpty(), qPrintable(s.error));
        QCOMPARE(s.rows.size(), 4);
        QCOMPARE(s.periodicCents, 40075);
        QCOMPARE(s.additionalCents, 12346);
        QVERIFY(s.stats.last().contains(FormatUtils::money(524.21, 2)));
        bool found = false;
        for (const auto& row : s.rows) {
            if (row.expenseId == id) { found = true; QCOMPARE(row.period, QString("17.09.26")); QCOMPARE(row.category, QString("Delivery")); }
        }
        QVERIFY(found);
        QVERIFY(AdditionalExpenseService::categories(db).contains("Delivery"));
        auto empty = ReportsDataLoader::buildTotalExpensesSnapshot(db.databaseName(), "2027-01-01", "2027-01-31");
        QVERIFY(empty.rows.isEmpty()); QCOMPARE(empty.additionalCents, 0);
        QVERIFY(!ReportsDataLoader::buildTotalExpensesSnapshot(db.databaseName(), "2026-09-30", "2026-09-01").error.isEmpty());
        QVERIFY(AdditionalExpenseService::remove(db, id));
        QVERIFY(!AdditionalExpenseService::remove(db, id));
        s = ReportsDataLoader::buildTotalExpensesSnapshot(db.databaseName(), "2026-09-01", "2026-09-30");
        QCOMPARE(s.additionalCents, 1); QCOMPARE(s.periodicCents, 40075);
    }
    void failedReadCannotCreateDatabaseOrReturnPartialMoney() {
        const auto before = QSqlDatabase::connectionNames();
        const auto path = dir.filePath("failed-read.sqlite");
        auto read = [&] { return ReportsDataLoader::buildTotalExpensesSnapshot(path, "2026-01-01", "2026-12-31"); };
        QVERIFY(!read().error.isEmpty());
        QVERIFY(!QFile::exists(path));
        QSqlQuery copy(db);
        copy.prepare("VACUUM INTO ?");
        copy.addBindValue(path);
        QVERIFY2(copy.exec(), qPrintable(copy.lastError().text()));
        const auto connection = QUuid::createUuid().toString();
        {
            auto fixture = QSqlDatabase::addDatabase("QSQLITE", connection);
            fixture.setDatabaseName(path);
            QVERIFY(fixture.open());
            QSqlQuery q(fixture);
            QVERIFY(q.exec("DROP TABLE additional_expenses"));
            QVERIFY(q.exec("INSERT INTO expenses(period_from,period_to,cleaning) VALUES('2026-02-01','2026-02-10',123.45)"));
            const auto oldSchema = read();
            QVERIFY2(oldSchema.error.isEmpty(), qPrintable(oldSchema.error));
            QVERIFY(oldSchema.periodicCents >= 12345);
            QVERIFY(q.exec("CREATE TABLE additional_expenses(id INTEGER)"));
            const auto broken = read();
            QVERIFY(!broken.error.isEmpty());
            QVERIFY(broken.rows.isEmpty());
            QCOMPARE(broken.periodicCents, 0);
            QCOMPARE(broken.additionalCents, 0);
            QCOMPARE(broken.novaPoshtaCents, 0);
        }
        QSqlDatabase::removeDatabase(connection);
        QCOMPARE(QSqlDatabase::connectionNames(), before);
    }

    void payrollReadFailuresAndRecovery() {
        QString error;QVERIFY2(PayrollReadFixture::run(&error),qPrintable(error));
    }
    void payrollUiRecoversAfterReadFailure() {
        QString error;QVERIFY2(PayrollReadFixture::schema(db,&error),qPrintable(error));
        QVERIFY2(PayrollReadFixture::populate(db,&error),qPrintable(error));
        QSqlQuery q(db);QVERIFY(q.exec("ALTER TABLE masters RENAME TO broken_masters"));
        ReportsPage page;page.show();auto* tabs=page.findChild<QTabWidget*>();
        auto* stats=page.findChild<QLabel*>("reportsStatsLabel");QVERIFY(tabs && stats);
        int payroll=-1;for(int i=0;i<tabs->count();++i)if(tabs->tabText(i)==QString::fromUtf8("Зарплата"))payroll=i;
        QVERIFY(payroll>=0);tabs->setCurrentIndex(payroll);
        auto* table=tabs->widget(payroll)->findChild<QTableView*>();QVERIFY(table);
        QTRY_VERIFY_WITH_TIMEOUT(stats->text().contains(QString::fromUtf8("Помилка читання зарплати")),10000);
        QCOMPARE(table->model()->rowCount(),0);
        auto* pdf=page.findChild<QPushButton*>("reportsPdfButton");QVERIFY(pdf);QVERIFY(!pdf->isEnabled());
        QVERIFY(q.exec("ALTER TABLE broken_masters RENAME TO masters"));
        page.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(!stats->text().contains(QString::fromUtf8("Помилка читання зарплати")) && table->model()->rowCount()==2,10000);
        QVERIFY(pdf->isEnabled());
    }
    void expenseFormDistinguishesEmptyPeriodFromReadFailure() {
        const auto before = QSqlDatabase::connectionNames();
        const auto read = [](const QString& path) {
            return ReportsDataLoader::buildExpensesSnapshot(path, "2026-02-01", "2026-02-28");
        };
        const auto missing = dir.filePath("missing-form.sqlite");
        QVERIFY(!read(missing).error.isEmpty());
        QVERIFY(!QFile::exists(missing));
        QCOMPARE(QSqlDatabase::connectionNames(), before);
        QSqlQuery q(db);
        QVERIFY(q.exec("INSERT INTO settings(key,value) VALUES('reports.expenses.electricity_tariff','4.25')"));
        QVERIFY(q.exec("INSERT INTO expenses(period_from,period_to,elec_curr) VALUES('2026-01-01','2026-01-31',378)"));
        const auto empty = read(db.databaseName());
        QVERIFY2(empty.error.isEmpty(), qPrintable(empty.error));
        QVERIFY(!empty.foundRow);
        QCOMPARE(empty.elecPrev, 378.0);
        QCOMPARE(empty.elecTariff, 4.25);
        QVERIFY(q.exec("INSERT INTO expenses(period_from,period_to,cleaning) VALUES('2026-02-01','2026-02-28',28.75)"));
        const auto existing = read(db.databaseName());
        QVERIFY2(existing.error.isEmpty(), qPrintable(existing.error));
        QVERIFY(existing.foundRow);
        QCOMPARE(existing.cleaning, 28.75);
        QVERIFY(q.exec("ALTER TABLE expenses RENAME COLUMN cleaning TO invalid_cleaning"));
        QVERIFY(!read(db.databaseName()).error.isEmpty());
        QCOMPARE(QSqlDatabase::connectionNames(), before);
    }

    void expenseFormRecoversAfterBrokenSchema() {
        QSqlQuery q(db);
        QVERIFY(q.exec("ALTER TABLE expenses RENAME COLUMN cleaning TO invalid_cleaning"));
        ReportsPage page;
        page.show();
        auto* tabs = page.findChild<QTabWidget*>();
        auto* form = page.findChild<QWidget*>("reportsExpensesPage");
        auto* stats = page.findChild<QLabel*>("reportsStatsLabel");
        QVERIFY(tabs && form && stats);
        tabs->setCurrentWidget(form);
        QTRY_VERIFY_WITH_TIMEOUT(stats->text().contains(QStringLiteral("Не вдалося прочитати витрати")), 10000);
        QVERIFY(!form->isEnabled());
        for (auto* field : form->findChildren<QLineEdit*>()) QVERIFY(field->text().isEmpty());
        QVERIFY(q.exec("ALTER TABLE expenses RENAME COLUMN invalid_cleaning TO cleaning"));
        page.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(form->isEnabled(), 10000);
        QVERIFY(!stats->text().contains(QStringLiteral("Не вдалося прочитати витрати")));
    }

    void expenseSavePreservesHistoryAcrossLateFailure() {
        QSqlQuery q(db);
        QVERIFY(q.exec("ALTER TABLE expenses ADD COLUMN elec_kwh REAL"));
        QVERIFY(q.exec("CREATE TABLE electricity_periods(id INTEGER PRIMARY KEY,period_from TEXT,period_to TEXT,"
                       "meter_before REAL,meter_after REAL,consumed_kwh REAL,tariff REAL,amount REAL)"));
        QVERIFY(q.exec("INSERT INTO expenses(id,period_from,period_to,cleaning) VALUES(41,'01.02.2026','28.02.2026',28.75)"));
        QVERIFY(q.exec("INSERT INTO electricity_periods(id,period_from,period_to,amount) VALUES(71,'01.02.26','28.02.26',12)"));
        const auto read = [&] { return ReportsDataLoader::buildExpensesSnapshot(db.databaseName(), "2026-02-01", "2026-02-28"); };
        QVERIFY(read().foundRow);
        QCOMPARE(read().cleaning, 28.75);
        ExpensesSavePayload payload;
        payload.cleaning = 32.25;
        payload.elecCost = 16;
        QString error;
        auto save = [&] { return ReportsSyncSupport::saveExpensesRecord("2026-02-01", "2026-02-28", payload, &error); };
        QVERIFY(q.exec("CREATE TRIGGER fail_power BEFORE UPDATE ON electricity_periods BEGIN SELECT RAISE(ABORT,'late meter error'); END"));
        QVERIFY(db.transaction());
        QVERIFY(q.exec("INSERT INTO settings VALUES('outer','retained')"));
        QVERIFY(!save());
        QVERIFY(error.contains("late meter error"));
        QVERIFY(q.exec("SELECT id,cleaning FROM expenses")); QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(),41); QCOMPARE(q.value(1).toDouble(),28.75); QVERIFY(!q.next()); q.finish();
        QVERIFY(db.commit()); // The savepoint did not roll back the caller's other change.
        QVERIFY(q.exec("SELECT value FROM settings WHERE key='outer'")); QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(), QString("retained")); q.finish();
        QVERIFY(q.exec("DROP TRIGGER fail_power"));
        QVERIFY2(save(), qPrintable(error));
        QVERIFY(q.exec("SELECT id,period_from,cleaning FROM expenses")); QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(),41); QCOMPARE(q.value(1).toString(),QString("01.02.2026"));
        QCOMPARE(q.value(2).toDouble(),32.25); QVERIFY(!q.next()); q.finish();
        QVERIFY(q.exec("SELECT id,amount FROM electricity_periods")); QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(),71); QCOMPARE(q.value(1).toDouble(),16.0); QVERIFY(!q.next()); q.finish();
        QVERIFY(q.exec("SELECT action FROM legacy_mutation_log WHERE entity_name='expenses'")); QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(),QString("update")); QVERIFY(!q.next()); q.finish();
        QVERIFY(q.exec("CREATE TRIGGER fail_audit BEFORE INSERT ON legacy_mutation_log BEGIN SELECT RAISE(ABORT,'audit error'); END"));
        payload.cleaning = 50; payload.elecCost = 21;
        QVERIFY(!save()); QVERIFY(error.contains("audit error"));
        QCOMPARE(read().cleaning,32.25);
        QVERIFY(q.exec("SELECT amount FROM electricity_periods WHERE id=71")); QVERIFY(q.next());
        QCOMPARE(q.value(0).toDouble(),16.0); q.finish();
        QVERIFY(q.exec("INSERT INTO expenses(id,period_from,period_to,cleaning) VALUES(42,'2026-02-01','2026-02-28',19)"));
        QVERIFY(!save()); QVERIFY(error.contains(QStringLiteral("Кілька записів")));
        QVERIFY(q.exec("SELECT COUNT(*) FROM expenses")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),2);
    }

    void validationPermissionsAndRollback() {
        const QDate date(2026,9,17);
        QVERIFY(!AdditionalExpenseService::add(db, date, "", "Parcel", 5));
        QVERIFY(!AdditionalExpenseService::add(db, {}, "Delivery", "Parcel", 5));
        QVERIFY(!AdditionalExpenseService::add(db, date, "Delivery", " ", 5));
        QVERIFY(!AdditionalExpenseService::add(db, date, "Delivery", "Parcel", 0));
        QVERIFY(!AdditionalExpenseService::add(db, date, "Delivery", "Parcel", -5));
        QVERIFY(!AdditionalExpenseService::add(db, date, "Delivery", "Parcel", qQNaN()));
        ActivityAuditService::setCurrentActor({"viewer", "Viewer", "viewer", "test"});
        QVERIFY(!AdditionalExpenseService::add(db, date, "Delivery", "Parcel", 5));
        ActivityAuditService::clearCurrentActor();
        qint64 id=0;
        QVERIFY(AdditionalExpenseService::add(db, date, "Delivery", "Parcel", 5, &id));
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TRIGGER fail_expense_audit BEFORE INSERT ON activity_audit_events "
                       "BEGIN SELECT RAISE(ABORT,'test rollback'); END"));
        QVERIFY(!AdditionalExpenseService::add(db, date, "Delivery", "Another parcel", 6));
        QVERIFY(!AdditionalExpenseService::remove(db, id));
        QVERIFY(q.exec("SELECT COUNT(*),SUM(amount_cents) FROM additional_expenses"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 1); QCOMPARE(q.value(1).toInt(), 500);
    }
    void formAddFilterDeleteAndLayout() {
        ReportsPage page;
        page.resize(1400, 850);
        auto* tabs = page.findChild<QTabWidget*>(); QVERIFY(tabs);
        tabs->setCurrentIndex(6);
        page.show();
        auto* table = page.findChild<QTableView*>("additionalExpensesTable"); QVERIFY(table);
        auto* date = page.findChild<QDateEdit*>("additionalExpenseDate"); QVERIFY(date);
        auto* category = page.findChild<QComboBox*>("additionalExpenseCategory"); QVERIFY(category);
        auto* description = page.findChild<QLineEdit*>("additionalExpenseDescription"); QVERIFY(description);
        auto* amount = page.findChild<QDoubleSpinBox*>("additionalExpenseAmount"); QVERIFY(amount);
        auto* add = page.findChild<QPushButton*>("addAdditionalExpense"); QVERIFY(add);
        auto* remove = page.findChild<QPushButton*>("deleteAdditionalExpense"); QVERIFY(remove);
        QTest::qWait(300);
        QVERIFY(!date->isVisible());
        auto* dialog = page.findChild<QDialog*>("additionalExpensesForm"); QVERIFY(dialog);
        auto* dialogButtons = dialog->findChild<QDialogButtonBox*>(); QVERIFY(dialogButtons);
        QTimer::singleShot(0, [&] {
        QVERIFY(dialog->isVisible());
        QVERIFY(!category->isEditable());
        auto* newCategory=dialog->findChild<QPushButton*>("addExpenseCategory"); QVERIFY(newCategory);
        QTimer::singleShot(0, [] {
            auto* input=qobject_cast<QInputDialog*>(QApplication::activeModalWidget()); QVERIFY(input);
            input->setTextValue("Test category"); input->accept();
        });
        newCategory->click();
        QCOMPARE(category->currentText(), QString("Test category"));
        description->setText(QString::fromUtf8("Доставка запчастин, накладна 12345"));
        date->setDate(QDate::currentDate().addDays(-40));
        amount->setValue(145.75);
        QVERIFY(dialog->grab().save(dir.filePath("additional-expenses-dialog.png")));
        dialogButtons->button(QDialogButtonBox::Save)->click();
        });
        QTest::mouseClick(add, Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(table->model()->index(0,2).data().toString(),
            QString::fromUtf8("Доставка запчастин, накладна 12345"), 10000);
        QCOMPARE(table->model()->columnCount(), 5);
        QCOMPARE(table->model()->index(0,4).data().toString(), FormatUtils::money(145.75, 2));
        QCOMPARE(description->text(), QString());
        auto* banner = page.findChild<QLabel*>("reportsStatsLabel"); QVERIFY(banner);
        QVERIFY(!banner->isVisible());
        QVERIFY(AdditionalExpenseService::categories(db).contains("Test category"));
        // The dashboard owns the visible period controls; reportsFiltersFrame is hidden.
        auto* from=page.findChild<QDateEdit*>("dashboardFrom"); QVERIFY(from);
        QVERIFY(from->isVisible());
        QVERIFY(add->isVisible());
        QCOMPARE(add->mapTo(&page,QPoint()).y(),from->mapTo(&page,QPoint()).y());
        QVERIFY(remove->mapTo(&page,QPoint(remove->width(),0)).x() >= page.width()-15);
        QTimer::singleShot(0, [&] {dialogButtons->button(QDialogButtonBox::Cancel)->click();});
        add->click();
        QSqlQuery count(db); QVERIFY(count.exec("SELECT COUNT(*) FROM additional_expenses"));
        QVERIFY(count.next()); QCOMPARE(count.value(0).toInt(),1);
        QVERIFY(!add->geometry().intersects(remove->geometry()));
        QVERIFY(!description->geometry().intersects(amount->geometry()));
        QVERIFY(page.grab().save(dir.filePath("additional-expenses-page.png")));
        table->setCurrentIndex(table->model()->index(0,0));
        QTRY_VERIFY(remove->isEnabled());
        QTimer::singleShot(0, [] {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                box->button(QMessageBox::No)->click();
        });
        QTest::mouseClick(remove, Qt::LeftButton);
        QCOMPARE(table->model()->index(0,4).data().toString(), FormatUtils::money(145.75,2));
        QTimer::singleShot(0, [] {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                box->button(QMessageBox::Yes)->click();
        });
        QTest::mouseClick(remove, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(table->model()->index(0,0).data().toString().contains(QString::fromUtf8("немає")), 10000);
        QVERIFY(!remove->isEnabled());
        page.close();
        QTest::qWait(100);
    }
};
QTEST_MAIN(AdditionalExpenseTests)
#include "AdditionalExpenseTests.moc"
