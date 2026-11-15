#include "AdminPayrollIdentityFixture.h"
#include "PayrollGraphProjectionFixture.h"
#include "PayrollIdentityFixture.h"
#include "ui/tabs/reports/ReportsDialogSupport.h"
#include "ui/tabs/reports/ReportsExpensesSupport.h"
#include "PartsPdfIdentityFixture.h"
#include "FinancialPdfReadFixture.h"
#include "StatisticsReadFixture.h"
#include "core/services/statistics/StatisticsReadService.h"
#include <QtTest/QtTest>
#include "PayrollReadFixture.h"
#include "infrastructure/db/sqlite/DbBootstrapEnsureSchema.h"
#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/db/sqlite/DbFacadeSupport.h"
#include "core/services/WorkshopSpareStockService.h"
#include "core/services/PartsQueryService.h"
#include "core/services/ReportsPdfSupport.h"
#include "core/services/ReportsPdfRenderer.h"
#include "core/services/ReportsPdfExpenseSupport.h"
#include "core/services/ReportsDataLoaderLocalSupport.h"
#include "core/services/WorkshopInventoryService.h"
#include "core/services/StaffAccountService.h"
#include "core/services/AccessControlService.h"
#include <QComboBox>
#include <QTabBar>
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
#include "ui/tabs/reports/PartsRegisterView.h"
#include "core/services/PurchasePaymentService.h"
#include "core/services/ReportsPdfPartsSupport.h"
#include "ui/tabs/reports/ReportsPage.h"
#include "ui/tabs/reports/ReportsTableSupport.h"
#include "ui/theme/ThemeManager.h"

class AdditionalExpenseTests : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    QSqlDatabase db;
    void configureFinancialReportDirectory() {
        const auto root=qEnvironmentVariable("CS_TEST_DATA_ROOT",dir.path());
        const auto reports=QDir(root).filePath("financial-pdf-output");
        QVERIFY(QDir().mkpath(reports));
        QVERIFY(Database::setSetting("settings.int.directories.reports_dir",reports));
    }
    void preparePartsPayrollPdfFixture() {
        QString error;QVERIFY(ensureWorkspaceSchema(db));QVERIFY(ensureRouterCacheSchema(db));QVERIFY(rebuildRouterViews(db));
        QVERIFY(WorkshopInventoryService::ensureSchema(db,&error));QVERIFY(WorkshopSpareStockService::ensureSchema(db,&error));
        QVERIFY(StaffAccountService::ensureSchema(db,&error));QVERIFY(AccessControlService::ensureSchema(db,&error));
        QSqlQuery q(db);QVERIFY(q.exec("INSERT INTO masters(id,name,role,admin_percent,active) VALUES('pdf-master','Synthetic master','master',50,1)"));
        QVERIFY(q.exec("INSERT INTO staff_accounts(id,display_name,role,active,master_id) VALUES('pdf-owner','Synthetic owner','owner',1,'pdf-master')"));
        ActivityAuditService::setCurrentActor({"pdf-owner","Synthetic owner","owner","test"});configureFinancialReportDirectory();
        QVERIFY(q.exec("INSERT OR REPLACE INTO settings(key,value) VALUES('payroll_percents','{\"pdf-master\":50}')"));
        QVERIFY(q.exec("INSERT INTO work_orders(number,order_uid,order_number,order_year,opened_at,closed_at,status,plate,plate_norm,car,client,master_id,works_sum,discount_mode) VALUES(11,'pdf-order',7,2026,'2026-01-01','2026-01-02','closed','','','Synthetic','','pdf-master',100.50,'percent')"));
        QVERIFY(q.exec("INSERT INTO work_order_services(id,order_number,order_uid,service_uid,title,qty,price,amount) VALUES(31,11,'pdf-order','pdf-service','Synthetic service',1,100.50,100.50)"));
        QVERIFY(q.exec("INSERT INTO service_masters(service_id,master_id,share_percent) VALUES(31,'pdf-master',100)"));
        QVERIFY(q.exec("INSERT INTO work_order_parts(id,order_number,order_uid,part_uid,name,store,ordered_at,qty,purchase_price,purchase_amount,price,amount,service_id,pricing_calc_version) VALUES(21,11,'pdf-order','pdf-part','Synthetic part','Shop','2026-01-01',1,30.10,30.10,45.25,45.25,31,3)"));
        QVERIFY(q.exec("INSERT INTO expenses(period_from,period_to,cleaning,cleaning_note) VALUES('2026-01-01','2026-01-31',12.10,'Synthetic note')"));
    }
private slots:
    void adminPartsUseOrderUidAcrossReports() {
        preparePartsPayrollPdfFixture();
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TEMP VIEW router_work_order_parts AS "
                       "SELECT 99 AS order_number,order_uid,name,store,qty,amount,purchase_amount,purchase_price,"
                       "markup_percent,manual_markup_override,pricing_calc_version FROM main.router_work_order_parts "
                       "UNION ALL SELECT 11,'different-order','Other','Shop',2,200,100,50,0,0,3"));
        QString error;
        const auto screen = ReportsDataLoader::LocalSupport::collectAdminSalaryTotalsForReports(
            db, "2026-01-01", "2026-01-31", &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const auto pdf = ReportsPdfExpenseSupport::collectAdminSalaryTotals("2026-01-01","2026-01-31",&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(qRound64(screen.purchaseTotal * 100), qRound64(pdf.purchaseTotal * 100));
        QCOMPARE(qRound64(screen.clientTotal * 100), qRound64(pdf.clientTotal * 100));
        QCOMPARE(qRound64(screen.margin * 100), qRound64(pdf.margin * 100));
        QCOMPARE(screen.partsQty, 1.0);
        QCOMPARE(screen.partsQty, pdf.partsQty);
    }
    void adminPartsRejectAmbiguousHistoricalOwner() {
        preparePartsPayrollPdfFixture();
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TEMP TABLE router_work_orders AS SELECT * FROM main.router_work_orders"));
        QVERIFY(q.exec("INSERT INTO temp.router_work_orders(number,order_uid,status,closed_at) "
                       "VALUES(11,'archived-other','closed','2025-12-31')"));
        QVERIFY(q.exec("CREATE TEMP TABLE router_work_order_parts AS SELECT * FROM main.router_work_order_parts"));
        QVERIFY(q.exec("UPDATE temp.router_work_order_parts SET order_uid=NULL"));
        QString screenError, pdfError;
        const auto readScreen = [&] { return ReportsDataLoader::LocalSupport::collectAdminSalaryTotalsForReports(
            db, "2026-01-01", "2026-01-31", &screenError); };
        const auto readPdf = [&] { return ReportsPdfExpenseSupport::collectAdminSalaryTotals(
            "2026-01-01","2026-01-31",&pdfError); };
        auto screen = readScreen();
        auto pdf = readPdf();
        QVERIFY2(screenError.contains("11"), qPrintable(screenError));
        QVERIFY2(pdfError.contains("11"), qPrintable(pdfError));
        QCOMPARE(screen.partsQty, 0.0);
        QCOMPARE(pdf.partsQty, 0.0);
        QVERIFY(q.exec("DELETE FROM temp.router_work_orders WHERE order_uid='archived-other'"));
        screenError.clear();
        screen = readScreen();
        pdf = readPdf();
        QVERIFY2(screenError.isEmpty(), qPrintable(screenError));
        QVERIFY2(pdfError.isEmpty(), qPrintable(pdfError));
        QCOMPARE(qRound64(screen.purchaseTotal * 100), 3010);
        QCOMPARE(qRound64(pdf.purchaseTotal * 100), 3010);
    }
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

    void partsReportSupplierPaymentIdentity() {
        preparePartsPayrollPdfFixture();
        QString error;
        QVERIFY2(PurchasePaymentService::ensureSchema(db, &error), qPrintable(error));
        QSqlQuery q(db);
        const auto tip = [&] {
            const auto report = ReportsDataLoader::buildPartsSnapshot(db.databaseName(), "2026-01-01", "2026-01-31", 0, 0, {}, {});
            return report.rows.size() == 2 ? report.rows.last().tooltip : report.error;
        };
        QVERIFY(q.exec("UPDATE work_order_parts SET paid=0 WHERE id=21"));
        QVERIFY(q.exec("INSERT INTO purchase_payment_balances VALUES('part:21:other-order',3000)"));
        QVERIFY(tip().contains(QStringLiteral("Оплата постачальнику: Не сплачено")));
        QVERIFY(q.exec("INSERT INTO purchase_payment_balances VALUES('part:21:pdf-order',1250)"));
        QVERIFY(tip().contains(QStringLiteral("Оплата постачальнику: Частково сплачено")));
        QVERIFY(q.exec("UPDATE purchase_payment_balances SET paid_cents=3010 WHERE payment_key='part:21:pdf-order'"));
        QVERIFY(tip().contains(QStringLiteral("Оплата постачальнику: Сплачено")));
        QVERIFY(q.exec("CREATE TABLE archived_report_parts AS SELECT * FROM router_work_order_parts"));
        QVERIFY(q.exec("UPDATE archived_report_parts SET part_uid='other-archived-part'"));
        QVERIFY(q.exec("DROP VIEW router_work_order_parts"));
        QVERIFY(q.exec("CREATE VIEW router_work_order_parts AS SELECT * FROM archived_report_parts"));
        QVERIFY(tip().contains(QStringLiteral("Оплата постачальнику: Немає підтверджених даних")));
    }

    void filteredRegisterPdf() {
        preparePartsPayrollPdfFixture();
        PartsRegisterView view(nullptr);
        PartsSnapshot snapshot;
        for (int i = 0; i < 90; ++i) {
            PartsSnapshotRow part;
            part.orderNumber = "7";
            part.oem = QStringLiteral("ITEM%1").arg(i, 3, 10, QChar('0'));
            part.partName = "Filter <A&B>";
            part.store = i == 1 ? "Other" : "Shop";
            part.supplierPayment = i == 2 ? "Unpaid" : "Paid";
            part.clientAmount = "45.25";
            if (i == 3) part.partName = "Excluded";
            snapshot.rows.append(part);
        }
        view.setSnapshot(snapshot);
        view.setActive(true);
        view.setSearch("filter");
        const auto combos = view.findChildren<QComboBox*>();
        QCOMPARE(combos.size(), 2);
        combos[0]->setCurrentText("Shop");
        combos[1]->setCurrentText("Paid");
        auto* table = view.findChild<QTableView*>();
        QVERIFY(table);
        table->selectRow(0);
        const auto selection = view.exportSelection();
        QCOMPARE(selection.rows.size(), 87);
        view.setSearch("different");
        const auto result = ReportsPdfRenderer::renderPartsRegisterPdf(QDate(2026,1,1), QDate(2026,1,31), selection);
        QVERIFY2(result.success, qPrintable(result.errorText));
        QCOMPARE(result.recordCount, 87);
        const auto capture = qEnvironmentVariable("CS_FILTERED_REGISTER_PDF");
        if (!capture.isEmpty()) QVERIFY(QFile::copy(result.path, capture));
    }

    void purchaseRegisterOrderIdentity() {
        preparePartsPayrollPdfFixture();
        PartsRegisterView view(nullptr);
        PartsSnapshot snapshot;
        for (int i = 0; i < 3; ++i) {
            PartsSnapshotRow part;
            part.orderNumber = "71";
            part.orderUid = i == 2 ? "other" : "first";
            part.internalOrderNumber = i == 2 ? 102 : 101;
            part.car = i == 2 ? "Blue" : "Red";
            part.oem = QString("QA%1").arg(i);
            part.installedAt = i == 0 ? "2026-01-01" : "2026-01-02";
            part.supplierPayment = i == 0 ? "Сплачено" : "Частково сплачено";
            snapshot.rows.append(part);
        }
        view.setSnapshot(snapshot);
        view.setActive(true, true);
        auto* orders = view.findChild<QListWidget*>("partsRegisterOrders");
        QVERIFY(orders);
        QCOMPARE(orders->count(), 2);
        auto* dates = view.findChild<QLabel*>("partsRegisterOrderDates");
        auto* table = view.findChild<QTableView*>("partsRegisterTable");
        QVERIFY(dates && table);
        QVERIFY(dates->text().contains(QStringLiteral("Встановлено: див. позиції")));
        QVERIFY(!table->isColumnHidden(1));
        QCOMPARE(table->model()->index(0,1).data().toString(), QString("2026-01-01"));
        QCOMPARE(table->model()->index(1,1).data().toString(), QString("2026-01-02"));
        QVERIFY(orders->item(0)->text().contains(QStringLiteral("Різні стани оплати")));
        QCOMPARE(view.exportSelection().rows.size(), 2);
        orders->setCurrentRow(1);
        QVERIFY(dates->text().contains("2026-01-02"));
        QVERIFY(table->isColumnHidden(1));
        const auto picked = view.exportSelection();
        QCOMPARE(picked.rows.size(), 1);
        QCOMPARE(picked.rows.first()[6], QString("QA2"));
        QVERIFY(picked.orderCaption.contains("Blue"));
        const auto pdf = ReportsPdfRenderer::renderPartsRegisterPdf(QDate(2026,1,1),QDate(2026,1,31),picked);
        QVERIFY2(pdf.success,qPrintable(pdf.errorText));
        QCOMPARE(pdf.recordCount,1);
        const auto capture = qEnvironmentVariable("CS_ORDER_REGISTER_PDF");
        if (!capture.isEmpty()) QVERIFY(QFile::copy(pdf.path,capture));
        view.setSearch("Red");
        QVERIFY(!view.hasExportSelection());
        QVERIFY(dates->text().isEmpty() && dates->isHidden());
        orders->setCurrentRow(0);
        QCOMPARE(view.exportSelection().rows.size(),2);
        view.setActive(true,false);
        QVERIFY(dates->isHidden());
        for (int column = 0; column < 4; ++column) QVERIFY(!table->isColumnHidden(column));
        QCOMPARE(view.exportSelection().rows.size(),2);
    }

    void flatPartsRegister() {
        preparePartsPayrollPdfFixture();
        QSqlQuery q(db);
        const QString today = QDate::currentDate().toString(Qt::ISODate);
        q.prepare("UPDATE work_order_parts SET qty=0.5, part_number='OEM-71', ordered_at=?,paid=1 WHERE id=21");
        q.addBindValue(today);
        QVERIFY(q.exec());
        q.prepare("INSERT INTO work_order_parts(id,order_number,order_uid,part_uid,name,store,ordered_at,qty,purchase_amount,amount,paid,pricing_calc_version) "
                  "VALUES(22,11,'pdf-order','other-item','Other item','Zulu',?,2,10,20,0,3)");
        q.addBindValue(today);
        QVERIFY(q.exec());
        ReportsPage page;
        auto* mode = page.findChild<QTabBar*>("partsReportMode");
        auto* search = page.findChild<QLineEdit*>("partsReportSearch");
        auto* table = page.findChild<QTableView*>("partsRegisterTable");
        QVERIFY(mode && search && table);
        auto* view = dynamic_cast<PartsRegisterView*>(page.findChild<QWidget*>("partsRegisterView"));
        QVERIFY(view);
        page.show();
        auto* tabs = page.findChild<QTabWidget*>();
        QVERIFY(tabs);
        tabs->setCurrentWidget(mode->parentWidget());
        page.refresh();
        QTest::mouseClick(mode, Qt::LeftButton, Qt::NoModifier, mode->tabRect(2).center());
        QVERIFY(!view->isHidden());
        QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(), 2, 10000);
        auto* store = view->findChildren<QComboBox*>().value(0);
        auto* payment = view->findChildren<QComboBox*>().value(1);
        QVERIFY(store && payment);
        search->setFocus();
        for (QWidget* next : QList<QWidget*>{mode, store, payment, table}) {
            QVERIFY(page.focusWidget());
            QTest::keyClick(page.focusWidget(), Qt::Key_Tab);
            QCOMPARE(page.focusWidget(), next);
        }
        store->setCurrentText("Zulu");
        QVERIFY(table->isRowHidden(0) && !table->isRowHidden(1));
        payment->setCurrentText(QStringLiteral("Сплачено"));
        QVERIFY(table->isRowHidden(0) && table->isRowHidden(1));
        store->setCurrentIndex(0);
        QVERIFY(!table->isRowHidden(0) && table->isRowHidden(1));
        payment->setCurrentIndex(0);
        QCOMPARE(table->model()->index(0, 6).data().toString(), QStringLiteral("OEM-71"));
        QCOMPARE(table->model()->index(0, 9).data().toString(), FormatUtils::money(90.50, 2));
        QCOMPARE(table->model()->index(0, 10).data().toString(), FormatUtils::money(45.25, 2));
        for (int index = 0; index < mode->count(); ++index) {
            const auto rect = mode->tabRect(index);
            QVERIFY2(mode->rect().contains(rect), qPrintable(QString("mode %1x%2, tab %3 at %4,%5 size %6x%7")
                .arg(mode->width()).arg(mode->height()).arg(index).arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height())));
            QVERIFY(mode->tabRect(index).width() >= mode->fontMetrics().horizontalAdvance(mode->tabText(index)));
        }
        QVERIFY(!mode->usesScrollButtons());
        const auto modeCapture = qEnvironmentVariable("CS_PARTS_MODE_CAPTURE");
        if (!modeCapture.isEmpty()) QVERIFY(mode->grab().save(modeCapture));
        const QString capture = qEnvironmentVariable("CS_FLAT_REPORT_CAPTURE");
        if (!capture.isEmpty()) QVERIFY(page.grab().save(capture));
        search->setText("no match");
        QVERIFY(table->isRowHidden(0));
        search->setText("oem-71");
        QVERIFY(!table->isRowHidden(0));
        QTest::mouseClick(mode, Qt::LeftButton, Qt::NoModifier, mode->tabRect(1).center());
        auto* orders = view->findChild<QListWidget*>("partsRegisterOrders");
        QVERIFY(orders && !orders->isHidden());
        QCOMPARE(orders->count(), 1);
        QCOMPARE(view->exportSelection().rows.size(), 1);
        QCOMPARE(view->exportSelection().rows.first()[6], QStringLiteral("OEM-71"));
        auto* link = view->findChild<QPushButton*>("partsRegisterOpenOrder");
        QVERIFY(link && link->isVisible());
        payment->setFocus();
        for (QWidget* next : QList<QWidget*>{orders, link, table}) {
            QTest::keyClick(page.focusWidget(), Qt::Key_Tab);
            QCOMPARE(page.focusWidget(), next);
        }
        mode->setFocus();
        QTest::keyClick(mode, Qt::Key_Left);
        QCOMPARE(mode->currentIndex(), 0);
        QVERIFY(view->isHidden());
        auto* collapse = page.findChild<QPushButton*>("partsCollapseAll");
        QVERIFY(collapse && collapse->isEnabled());
        search->setFocus();
        QTest::keyClick(search, Qt::Key_Tab);
        QCOMPARE(page.focusWidget(), collapse);
        QTest::keyClick(collapse, Qt::Key_Tab);
        QCOMPARE(page.focusWidget(), mode);
    }

    void partsQuantityAndUnitPrice() {
        preparePartsPayrollPdfFixture();
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE qty_fixture AS SELECT * FROM router_work_order_parts"));
        QVERIFY(q.exec("DROP VIEW router_work_order_parts"));
        QVERIFY(q.exec("CREATE VIEW router_work_order_parts AS SELECT * FROM qty_fixture"));
        for (const QString& value : {QStringLiteral("NULL"), QStringLiteral("0"), QStringLiteral("0.5")}) {
            QVERIFY(q.exec("UPDATE qty_fixture SET purchase_price=0,qty=" + value));
            const auto report = ReportsDataLoader::buildPartsSnapshot(db.databaseName(), "2026-01-01", "2026-01-31", 0, 0, {}, {});
            QVERIFY2(report.error.isEmpty(), qPrintable(report.error));
            QCOMPARE(report.rows.size(), 2);
            const auto expectedQty = value == "NULL" ? QStringLiteral("—") : FormatUtils::smartQty(value.toDouble());
            QCOMPARE(report.rows.last().qty, expectedQty);
            QString error;
            const auto groups = ReportsPdfSupport::collectPartsPdfGroups("2026-01-01", "2026-01-31", &error);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(groups.value("Shop").size(), 1);
            const auto row = groups.value("Shop").first();
            QCOMPARE(row.qty, expectedQty);
            QCOMPARE(row.purchase, 30.10);
            QCOMPARE(row.clientPrice, 45.25);
            const auto cells = ReportsPdfPartsSupport::buildPartCells(row, 1, true, false);
            QCOMPARE(cells.at(4), value == "0.5" ? FormatUtils::money(60.20, 2) : QStringLiteral("—"));
            QCOMPARE(cells.at(5), expectedQty);
        }
    }

    void partsPdfDependenciesScopeAndRepeat() {
        preparePartsPayrollPdfFixture();QString error;
        const auto groups=ReportsPdfSupport::collectPartsPdfGroups("2026-01-01","2026-01-31",&error);QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(groups.size(),1);
        const auto part=groups.first().first();QCOMPARE(qRound64(part.purchase*100),3010);QCOMPARE(qRound64(part.clientPrice*100),4525);QCOMPARE(qRound64(part.added*100),1515);
        ReportsPdfRenderer::ExportResult result;const auto render=[&]{return ReportsPdfRenderer::renderPartsPdf(QDate(2026,1,1),QDate(2026,1,31),&result);};
        QVERIFY2(render(),qPrintable(result.errorText));QCOMPARE(result.recordCount,1);const auto path=result.path;QSqlQuery q(db);
        for(const auto& table:QStringList{"work_order_part_attachments","workshop_inventory_purchases","workshop_spare_movements","settings"}) {
            const auto good=PayrollReadFixture::bytes(path);QVERIFY(good.startsWith("%PDF"));
            QVERIFY(q.exec("ALTER TABLE "+table+" RENAME TO broken_pdf_dependency"));QVERIFY(!render());QVERIFY(!result.errorText.isEmpty());QCOMPARE(result.recordCount,0);QCOMPARE(PayrollReadFixture::bytes(path),good);
            QVERIFY(q.exec("ALTER TABLE broken_pdf_dependency RENAME TO "+table));QVERIFY2(render(),qPrintable(result.errorText));QCOMPARE(result.recordCount,1);
        }
        const auto good=PayrollReadFixture::bytes(path);QVERIFY(q.exec("UPDATE staff_accounts SET role='master' WHERE id='pdf-owner'"));ActivityAuditService::setCurrentActor({"pdf-owner","Synthetic master","master","test"});QVERIFY(AccessControlService::currentOrderScope(db).restricted);
        QVERIFY(!render());QVERIFY(!result.errorText.isEmpty());QCOMPARE(PayrollReadFixture::bytes(path),good);
        QVERIFY(q.exec("UPDATE staff_accounts SET role='owner' WHERE id='pdf-owner'"));ActivityAuditService::setCurrentActor({"pdf-owner","Synthetic owner","owner","test"});QVERIFY2(render(),qPrintable(result.errorText));
        QVERIFY(ReportsPdfRenderer::renderPartsPdf(QDate(2027,1,1),QDate(2027,1,31),&result));QCOMPARE(result.recordCount,0);
    }
    void payrollGraphProjectionAndIndexReuse() {
        QTemporaryDir sourceDir;QVERIFY(sourceDir.isValid());
        const QString connection=QStringLiteral("payroll-projection-")+QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto source=QSqlDatabase::addDatabase("QSQLITE",connection);source.setDatabaseName(sourceDir.filePath("synthetic.db"));
        const auto cleanup=qScopeGuard([&]{source.close();source={};QSqlDatabase::removeDatabase(connection);});
        QVERIFY(source.open());QString error;
        QVERIFY2(PayrollGraphProjectionFixture::run(source,&error),qPrintable(error));
    }
    void adminPayrollIdentityOwnersAndRepeat() {
        QString error;QVERIFY2(AdminPayrollIdentityFixture::run(&error),qPrintable(error));
    }

    void payrollIdentityOwnersSplitAndRepeat() {
        QString error;QVERIFY2(PayrollIdentityFixture::run(db,&error),qPrintable(error));
        QVERIFY(!ReportsSyncSupport::setPayrollOrderAbsent("m",1,true,&error));QVERIFY(!error.isEmpty());
        QVERIFY(ReportsSyncSupport::payrollWorksTextForMaster("1","m",50,&error).isEmpty());QVERIFY(!error.isEmpty());
        for(int i=0;i<2;++i){const auto snapshot=ReportsDataLoader::buildPayrollSnapshot(db.databaseName(),"2026-01-01","2026-01-31");
            QVERIFY2(snapshot.error.isEmpty(),qPrintable(snapshot.error));double m=-1,n=-1;
            for(const auto& row:snapshot.rows){if(row.masterId=="m"){m=row.accruedValue;QCOMPARE(row.accrued,FormatUtils::money(m,2));}if(row.masterId=="n")n=row.accruedValue;}
            QCOMPARE(qRound64(m*100),qint64(5));QCOMPARE(qRound64(n*100),qint64(2));
        }
    }
    void payrollPdfIdentityRoundingAndFilePreservation() {
        preparePartsPayrollPdfFixture();QString error;QSqlQuery q(db);
        QVERIFY(q.exec("UPDATE masters SET active=0,active_to='2025-12-31' WHERE id<>'pdf-master'"));
        QVERIFY(q.exec("DELETE FROM expenses"));
        QVERIFY(q.exec("UPDATE work_orders SET works_sum=0.10 WHERE number=11"));
        QVERIFY(q.exec("UPDATE work_order_services SET price=0.05,amount=0.05 WHERE id=31"));
        QVERIFY(q.exec("INSERT INTO work_order_services(id,order_number,order_uid,service_uid,title,qty,price,amount) VALUES(32,11,'pdf-order','pdf-service-two','Second tiny work',1,0.05,0.05)"));
        QVERIFY(q.exec("INSERT INTO service_masters(service_id,master_id,share_percent) VALUES(32,'pdf-master',100)"));
        const auto read=[&]{return ReportsPdfSupport::collectPayrollDetails("2026-01-01","2026-01-31",&error);};
        auto details=read();QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(details.size(),1);QCOMPARE(qRound64(details.first().accrued*100),qint64(6));
        QCOMPARE(qRound64(details.first().orders.first().masterAmount*100),qint64(6));
        bool observed=false;
        QTimer::singleShot(0,[&]{auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)return;
            const auto close=qScopeGuard([&]{dialog->reject();});auto* table=dialog->findChild<QTableView*>();
            if(!table||!table->model())return;
            observed=table->model()->index(0,5).data().toString()==FormatUtils::money(0.06,2);
        });
        ReportsDialogSupport::openPayrollOrdersDialog(nullptr,"pdf-master","Synthetic master",details.first().orders);
        QVERIFY(observed);
        const auto text=ReportsSyncSupport::payrollWorksTextForMaster("11","pdf-master",50,&error);QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(text.count("0.03"),2);
        ReportsPdfRenderer::ExportResult result;auto render=[&]{return ReportsPdfRenderer::renderPayrollPdf(QDate(2026,1,1),QDate(2026,1,31),&result);};
        QVERIFY2(render(),qPrintable(result.errorText));const auto path=result.path;const auto good=PayrollReadFixture::bytes(path);QVERIFY(good.startsWith("%PDF"));
        QVERIFY2(q.exec("INSERT INTO router_cache_work_orders(number,order_uid,order_number,order_year,opened_at,closed_at,status,plate,plate_norm,car,client) "
                        "VALUES(12,'pdf-collision-order',8,2025,'2025-01-01','2026-01-02','closed','','','Archived car','Archived client')"),qPrintable(q.lastError().text()));
        QVERIFY(q.exec("INSERT INTO router_cache_work_order_services(id,order_number,order_uid,service_uid,title,qty,price,amount) VALUES(31,12,'pdf-collision-order','pdf-collision-service','Foreign work',1,100,100)"));
        QVERIFY(read().isEmpty());QVERIFY(!error.isEmpty());
        QVERIFY(!render());QVERIFY(!result.errorText.isEmpty());QCOMPARE(PayrollReadFixture::bytes(path),good);
        QVERIFY(q.exec("DELETE FROM router_cache_work_order_services WHERE order_uid='pdf-collision-order'"));
        QVERIFY(q.exec("DELETE FROM router_cache_work_orders WHERE order_uid='pdf-collision-order'"));
        QVERIFY2(render(),qPrintable(result.errorText));details=read();QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(qRound64(details.first().accrued*100),qint64(6));
        const auto repaired=PayrollReadFixture::bytes(path);QVERIFY(q.exec("DROP VIEW router_service_masters"));
        QVERIFY(!render());QVERIFY(!result.errorText.isEmpty());QCOMPARE(PayrollReadFixture::bytes(path),repaired);
        QVERIFY(rebuildRouterViews(db));QVERIFY2(render(),qPrintable(result.errorText));
        QVERIFY(q.exec("UPDATE staff_accounts SET active=0 WHERE id='pdf-owner'"));QVERIFY(!render());QVERIFY(!result.errorText.isEmpty());
        QVERIFY(q.exec("UPDATE staff_accounts SET active=1 WHERE id='pdf-owner'"));QVERIFY2(render(),qPrintable(result.errorText));
    }
    void payrollPdfSeparatesNamesakesAndIssuedAmounts() {
        preparePartsPayrollPdfFixture();
        QSqlQuery q(db);
        QVERIFY(q.exec("UPDATE masters SET active=0,active_to='2025-12-31' WHERE id<>'pdf-master'"));
        QVERIFY(q.exec("INSERT INTO masters(id,name,role,active) VALUES('pdf-second','Synthetic master','master',1)"));
        QVERIFY(q.exec("CREATE TABLE payroll_history(period_from TEXT,period_to TEXT,master_id TEXT,master_name TEXT,amount REAL)"));
        QVERIFY(q.exec("INSERT INTO payroll_history VALUES('2026-01-01','2026-01-31','pdf-master','Synthetic master',10)"));
        const QDate from(2026, 1, 1), to(2026, 1, 31);
        QString error;
        const auto details = [&](const QString& id) {
            return ReportsPdfSupport::collectPayrollDetails("2026-01-01", "2026-01-31", &error, true, id);
        };
        auto staff = details("pdf-master");
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(staff.size(), 1);
        QCOMPARE(staff.first().issued, 10.0);
        QCOMPARE(qRound64(staff.first().toIssue * 100), 3420);
        staff = details("pdf-second");
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(staff.size(), 1);
        QCOMPARE(staff.first().issued, 0.0);
        ReportsPdfRenderer::ExportResult result;
        QVERIFY2(ReportsPdfRenderer::renderPayrollPdf(from, to, &result, "pdf-master"), qPrintable(result.errorText));
        const QString firstPath = result.path;
        const QByteArray first = PayrollReadFixture::bytes(firstPath);
        QVERIFY2(ReportsPdfRenderer::renderPayrollPdf(from, to, &result, "pdf-second"), qPrintable(result.errorText));
        QVERIFY(result.path != firstPath);
        QCOMPARE(result.recordCount, 1);
        const QString capture = qEnvironmentVariable("CS_PAYROLL_SCOPE_CAPTURE");
        if (!capture.isEmpty()) {
            QVERIFY(QFile::copy(firstPath, capture + "/first.pdf"));
            QVERIFY(QFile::copy(result.path, capture + "/second.pdf"));
        }
        QVERIFY(!ReportsPdfRenderer::renderPayrollPdf(from, to, &result, "unknown"));
        QVERIFY(q.exec("INSERT INTO payroll_history VALUES('2026-01-01','2026-01-31',NULL,'Synthetic master',4)"));
        QVERIFY(!ReportsPdfRenderer::renderPayrollPdf(from, to, &result, "pdf-master"));
        QVERIFY(result.errorText.contains(QStringLiteral("без ID")));
        QCOMPARE(PayrollReadFixture::bytes(firstPath), first);
        QVERIFY(q.exec("UPDATE masters SET name='Other' WHERE id='pdf-second'"));
        staff = details("pdf-master");
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(staff.size(), 1);
        QCOMPARE(staff.first().issued, 14.0);
    }
    void payrollPdfSupplementFailuresScopeAndRepeat() {
        preparePartsPayrollPdfFixture();QString error;
        QSqlQuery staffing(db);
        QVERIFY(staffing.exec("UPDATE masters SET active=0,active_to='2025-12-31' WHERE id<>'pdf-master'"));
        const auto masters=ReportsPdfSupport::collectPayrollDetails("2026-01-01","2026-01-31",&error);QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(masters.size(),1);QCOMPARE(qRound64(masters.first().accrued*100),3815);
        QCOMPARE(qRound64(masters.first().sharedExpenses*100),1210);
        QVERIFY(masters.first().orders.first().works.contains(QStringLiteral("50.25")));
        QVERIFY(staffing.exec("INSERT INTO masters(id,name,role,active,active_from) VALUES('pdf-second','Second master','master',1,'2026-01-01')"));
        const auto shared=ReportsPdfSupport::collectPayrollDetails("2026-01-01","2026-01-31",&error);
        QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(shared.size(),1);
        QCOMPARE(qRound64(shared.first().sharedExpenses*100),605);
        QCOMPARE(qRound64(shared.first().accrued*100),4420);
        ReportsPdfRenderer::ExportResult result;const auto render=[&]{return ReportsPdfRenderer::renderPayrollPdf(QDate(2026,1,1),QDate(2026,1,31),&result);};QVERIFY2(render(),qPrintable(result.errorText));const auto path=result.path;QSqlQuery q(db);
        const auto good=PayrollReadFixture::bytes(path);QVERIFY(good.startsWith("%PDF"));QVERIFY(q.exec("ALTER TABLE expenses RENAME COLUMN cleaning_note TO broken_note"));
        QVERIFY(!render());QVERIFY(!result.errorText.isEmpty());QCOMPARE(result.recordCount,0);QCOMPARE(PayrollReadFixture::bytes(path),good);
        QVERIFY(q.exec("ALTER TABLE expenses RENAME COLUMN broken_note TO cleaning_note"));QVERIFY2(render(),qPrintable(result.errorText));
        const auto repaired=PayrollReadFixture::bytes(path);QVERIFY(q.exec("ALTER TABLE workshop_inventory_purchases RENAME TO broken_inventory"));QVERIFY(!render());QVERIFY(!result.errorText.isEmpty());QCOMPARE(PayrollReadFixture::bytes(path),repaired);
        QVERIFY(q.exec("ALTER TABLE broken_inventory RENAME TO workshop_inventory_purchases"));QVERIFY2(render(),qPrintable(result.errorText));
        const auto allowed=PayrollReadFixture::bytes(path);QVERIFY(q.exec("UPDATE staff_accounts SET role='master' WHERE id='pdf-owner'"));ActivityAuditService::setCurrentActor({"pdf-owner","Synthetic master","master","test"});QVERIFY(AccessControlService::currentOrderScope(db).restricted);
        QVERIFY(!render());QVERIFY(!result.errorText.isEmpty());QCOMPARE(PayrollReadFixture::bytes(path),allowed);
        QVERIFY(q.exec("UPDATE staff_accounts SET role='owner',active=0 WHERE id='pdf-owner'"));ActivityAuditService::setCurrentActor({"pdf-owner","Synthetic owner","owner","test"});QVERIFY(!render());
        QVERIFY(q.exec("UPDATE staff_accounts SET active=1 WHERE id='pdf-owner'"));QVERIFY2(render(),qPrintable(result.errorText));
        QVERIFY(ReportsPdfRenderer::renderPayrollPdf(QDate(2027,1,1),QDate(2027,1,31),&result));QCOMPARE(result.recordCount,0);
    }
    void partsPdfIdentityOwnersAttachmentsAndStock() {
        QString error;QVERIFY2(PartsPdfIdentityFixture::run(db,&error),qPrintable(error));
        configureFinancialReportDirectory();ReportsPdfRenderer::ExportResult result;
        const auto render=[&]{return ReportsPdfRenderer::renderPartsPdf(QDate(2026,1,1),QDate(2026,1,31),&result);};
        QVERIFY2(render(),qPrintable(result.errorText));QCOMPARE(result.recordCount,1);
        const auto path=result.path;const auto good=PayrollReadFixture::bytes(path);QVERIFY(good.startsWith("%PDF"));
        QSqlQuery q(db);QVERIFY(q.exec("UPDATE router_work_order_parts SET order_uid='unknown'"));
        QVERIFY(!render());QVERIFY(!result.errorText.isEmpty());QCOMPARE(result.recordCount,0);QCOMPARE(PayrollReadFixture::bytes(path),good);
        QVERIFY(q.exec("UPDATE router_work_order_parts SET order_uid='order-old'"));QVERIFY2(render(),qPrintable(result.errorText));QCOMPARE(result.recordCount,1);
    }
    void financialPdfReadFailuresAndRecovery() {
        QString error;QVERIFY2(FinancialPdfReadFixture::run(db,&error),qPrintable(error));
    }
    void expensePdfFailuresRightsAndRepeat() {
        QString error;QVERIFY2(FinancialPdfReadFixture::run(db,&error),qPrintable(error));
        configureFinancialReportDirectory();
        ReportsPdfRenderer::ExpensesFormData data;data.cleaning=12.25;data.other=7.10;data.total=30.35;
        data.elecPrev=1250.125; data.elecCurr=1252.625; data.elecKwh=2.5;
        data.elecTariff=4.3275; data.elecCost=11;
        ReportsPdfRenderer::ExportResult result;
        const auto render=[&]{return ReportsPdfRenderer::renderExpensesPdf(QDate(2026,1,1),QDate(2026,1,31),data,&result);};
        QVERIFY2(render(),qPrintable(result.errorText));QVERIFY(result.success);QVERIFY(result.fileSizeBytes>0);
        const auto capture = qEnvironmentVariable("CS_EXPENSES_DECIMALS_PDF");
        if (!capture.isEmpty()) QVERIFY(QFile::copy(result.path, capture));
        const auto path=result.path;const auto original=PayrollReadFixture::bytes(path);QVERIFY(original.startsWith("%PDF"));
        QSqlQuery q(db);QVERIFY(q.exec("ALTER TABLE expenses RENAME COLUMN cleaning TO invalid_cleaning"));
        QVERIFY(!render());QVERIFY(!result.success);QVERIFY(!result.errorText.isEmpty());QCOMPARE(result.recordCount,0);QCOMPARE(PayrollReadFixture::bytes(path),original);
        QVERIFY(q.exec("ALTER TABLE expenses RENAME COLUMN invalid_cleaning TO cleaning"));QVERIFY2(render(),qPrintable(result.errorText));
        QVERIFY(q.exec("UPDATE staff_accounts SET active=0"));QVERIFY(!render());QVERIFY(!result.errorText.isEmpty());
        QVERIFY(q.exec("UPDATE staff_accounts SET active=1"));QVERIFY2(render(),qPrintable(result.errorText));
        QVERIFY(q.exec("DROP TABLE settings"));QVERIFY(!render());QVERIFY(!result.errorText.isEmpty());
        QVERIFY(q.exec("CREATE TABLE settings(key TEXT PRIMARY KEY,value TEXT)"));
        configureFinancialReportDirectory();QVERIFY2(render(),qPrintable(result.errorText));
    }
    void outsourcePdfFailuresRightsAndRepeat() {
        QString error;QVERIFY2(FinancialPdfReadFixture::run(db,&error),qPrintable(error));
        configureFinancialReportDirectory();
        ReportsPdfRenderer::ExportResult result;
        const auto render=[&]{return ReportsPdfRenderer::renderOutsourcePdf(QDate(2026,1,1),QDate(2026,1,31),&result);};
        QVERIFY2(render(),qPrintable(result.errorText));QCOMPARE(result.recordCount,2);QVERIFY(result.fileSizeBytes>0);
        const auto path=result.path;const auto original=PayrollReadFixture::bytes(path);
        const auto sample=QFileInfo(path).absoluteDir().filePath("populated-outsource.pdf");
        QFile evidence(sample);QVERIFY(evidence.open(QIODevice::WriteOnly));QCOMPARE(evidence.write(original),original.size());evidence.close();
        QSqlQuery q(db);QVERIFY(q.exec("ALTER TABLE outsource_jobs RENAME COLUMN client_price TO invalid_price"));
        QVERIFY(!render());QVERIFY(!result.success);QVERIFY(!result.errorText.isEmpty());QCOMPARE(result.recordCount,0);QCOMPARE(PayrollReadFixture::bytes(path),original);
        QVERIFY(q.exec("ALTER TABLE outsource_jobs RENAME COLUMN invalid_price TO client_price"));QVERIFY2(render(),qPrintable(result.errorText));
        QVERIFY(q.exec("ALTER TABLE router_work_orders RENAME TO missing_orders"));QVERIFY(!render());QVERIFY(!result.errorText.isEmpty());
        QVERIFY(q.exec("ALTER TABLE missing_orders RENAME TO router_work_orders"));QVERIFY2(render(),qPrintable(result.errorText));
        QVERIFY(q.exec("UPDATE staff_accounts SET active=0"));QVERIFY(!render());QVERIFY(!result.errorText.isEmpty());
        QVERIFY(q.exec("UPDATE staff_accounts SET active=1"));QVERIFY2(render(),qPrintable(result.errorText));QCOMPARE(result.recordCount,2);
        QVERIFY(q.exec("DELETE FROM outsource_jobs"));QVERIFY2(render(),qPrintable(result.errorText));QCOMPARE(result.recordCount,0);
    }
    void statisticsReadFailuresAndRecovery() {
        QString error;QVERIFY2(StatisticsReadFixture::run(&error),qPrintable(error));
    }
    void statisticsWorkerFailuresAndCurrentRights() {
        QString error;QVERIFY(ensureWorkspaceSchema(db));QVERIFY(ensureRouterCacheSchema(db));QVERIFY(rebuildRouterViews(db));
        QVERIFY(StaffAccountService::ensureSchema(db,&error));QVERIFY(AccessControlService::ensureSchema(db,&error));QVERIFY(ActivityAuditService::ensureSchema(db,&error));
        QSqlQuery q(db);QVERIFY(q.exec("INSERT INTO staff_accounts(id,display_name,role,active) VALUES('stats-owner','Synthetic owner','owner',1)"));
        ActivityAuditService::setCurrentActor({"stats-owner","Synthetic owner","owner","test"});
        const auto cancel=std::make_shared<std::atomic_bool>(false);
        auto request=StatisticsReadService::capture(QDate(2026,1,1),QDate(2026,1,31),false);QVERIFY(request.allowed);
        const auto read=[&]{return StatisticsReadService::run(request,cancel);};
        auto empty=read();QVERIFY2(empty.error.isEmpty(),qPrintable(empty.error));QVERIFY(empty.parts.isEmpty());QVERIFY(empty.events.isEmpty());
        QVERIFY(q.exec("ALTER TABLE work_order_parts RENAME COLUMN name TO broken_name"));
        auto failed=read();QVERIFY(!failed.error.isEmpty());QVERIFY(failed.parts.isEmpty());QVERIFY(failed.events.isEmpty());
        QVERIFY(q.exec("ALTER TABLE work_order_parts RENAME COLUMN broken_name TO name"));QVERIFY2(read().error.isEmpty(),qPrintable(read().error));
        QVERIFY(q.exec("DROP VIEW router_work_orders"));
        QVERIFY(q.exec("CREATE VIEW router_work_orders AS SELECT number,order_uid,car,client,master_name_closed,master_id FROM work_orders "
                       "UNION ALL SELECT 475,'stats-current','Current car','Current client','','' "
                       "UNION ALL SELECT 475,'stats-history','Historical car','Historical client','',''"));
        QVERIFY(q.exec("DROP VIEW router_work_order_parts"));
        const QString partsView = QStringLiteral("CREATE VIEW router_work_order_parts AS SELECT id,order_number,order_uid,name,part_number,ordered_at,delivery_date,installed_at,store FROM work_order_parts "
                                                 "UNION ALL SELECT 21,475,'%1','Filter','','2026-01-02','','',''");
        QVERIFY(q.exec(partsView.arg("stats-current")));
        auto current = read();
        QVERIFY2(current.error.isEmpty(), qPrintable(current.error));
        QCOMPARE(current.parts.size(), 1);
        QCOMPARE(current.parts.first().client, QStringLiteral("Current client"));
        QVERIFY(q.exec("DROP VIEW router_work_order_parts"));
        QVERIFY(q.exec(partsView.arg("")));
        auto ambiguous = read();
        QVERIFY(!ambiguous.error.isEmpty());
        QVERIFY(ambiguous.parts.isEmpty());
        QVERIFY(q.exec("DROP VIEW router_work_order_parts"));
        QVERIFY(q.exec(partsView.arg("stats-history")));
        auto historical = read();
        QVERIFY2(historical.error.isEmpty(), qPrintable(historical.error));
        QCOMPARE(historical.parts.size(), 1);
        QCOMPARE(historical.parts.first().client, QStringLiteral("Historical client"));
        QVERIFY2(q.exec("INSERT INTO work_orders(number,order_uid,order_number,order_year,opened_at,closed_at,status,plate,plate_norm,car,client,master_id) "
                        "VALUES(11,'invoice-owner',7,2026,'2026-01-01','2026-01-02','closed','','','Car','Invoice client','m1')"), qPrintable(q.lastError().text()));
        QVERIFY2(q.exec("INSERT INTO work_order_parts(id,order_number,order_uid,part_uid,name,ordered_at) VALUES(21,11,'invoice-owner','invoice-part','Local','2026-01-02')"), qPrintable(q.lastError().text()));
        QVERIFY2(q.exec("INSERT INTO work_order_part_attachments(part_id,invoice_number) VALUES(21,'INV-21')"), qPrintable(q.lastError().text()));
        QVERIFY(q.exec("DROP VIEW router_work_order_parts"));
        QVERIFY(q.exec("CREATE VIEW router_work_order_parts AS SELECT id,order_number,order_uid,name,part_number,ordered_at,delivery_date,installed_at,store,part_uid FROM work_order_parts "
                       "UNION ALL SELECT 21,475,'stats-history','Historical','','2026-01-02','','','','invoice-part' "
                       "UNION ALL SELECT 21,11,'invoice-owner','Different part','','2026-01-02','','','','other-part'"));
        const auto checkInvoices = [&] {
            auto snapshot = read();
            QVERIFY2(snapshot.error.isEmpty(), qPrintable(snapshot.error));
            QCOMPARE(snapshot.parts.size(), 3);
            for (const auto& row : snapshot.parts)
                QCOMPARE(row.invoiceNumber, row.name == "Local" ? QStringLiteral("INV-21") : QString());
        };
        checkInvoices();
        // Keep dependent views pointing at the deliberately missing original table during this fault.
        QVERIFY(q.exec("PRAGMA legacy_alter_table=ON"));
        QVERIFY2(q.exec("ALTER TABLE work_order_part_attachments RENAME TO broken_invoices"), qPrintable(q.lastError().text()));
        auto broken = read();
        QVERIFY(!broken.error.isEmpty());
        QVERIFY(broken.parts.isEmpty());
        QVERIFY2(q.exec("ALTER TABLE broken_invoices RENAME TO work_order_part_attachments"), qPrintable(q.lastError().text()));
        QVERIFY(q.exec("PRAGMA legacy_alter_table=OFF"));
        checkInvoices();
        QVERIFY(q.exec("CREATE TABLE IF NOT EXISTS order_creation_reservations(request_uid TEXT,order_uid TEXT,internal_number INTEGER,order_year INTEGER,order_number INTEGER,created_at TEXT)"));
        QVERIFY(q.exec("INSERT INTO order_creation_reservations(request_uid,order_uid,internal_number,order_year,order_number,created_at) "
                       "VALUES('retired-request','retired-order',81,2025,7,'2025-01-01')"));
        for (int number : {11,81,97}) {
            QVERIFY2(ActivityAuditService::recordExternal(db, QStringLiteral("audit-%1").arg(number),
                 QDateTime(QDate(2026,1,12),QTime(12,0)), {"stats-owner","Synthetic owner","owner","test"},
                 "parts","review","Synthetic review",number,{}, {}, {}, "{}", &error), qPrintable(error));
        }
        auto audit=read();
        QVERIFY2(audit.error.isEmpty(),qPrintable(audit.error));
        QCOMPARE(audit.events.size(),3);
        for (const auto& event : audit.events) {
            const auto row=event.toMap();
            QCOMPARE(row.value("visibleOrder").toInt(),row.value("order").toInt()==97 ? 0 : 7);
        }
        QVERIFY(q.exec("UPDATE order_creation_reservations SET internal_number=11 WHERE internal_number=81"));
        audit=read();
        QVERIFY2(audit.error.isEmpty(),qPrintable(audit.error));
        for (const auto& event : audit.events) QCOMPARE(event.toMap().value("visibleOrder").toInt(),0);
        // A request captured before revocation must not authorize the later worker.
        QVERIFY(q.exec("UPDATE staff_accounts SET active=0 WHERE id='stats-owner'"));QVERIFY(!read().error.isEmpty());
        QVERIFY(q.exec("UPDATE staff_accounts SET active=1 WHERE id='stats-owner'"));QVERIFY(read().error.isEmpty());
        request.path=dir.filePath("missing-statistics-worker.db");QVERIFY(!read().error.isEmpty());QVERIFY(!QFile::exists(request.path));
        request.allowed=false;QVERIFY(!read().error.isEmpty());QVERIFY(!QFile::exists(request.path));
    }
    void periodPdfFailuresAndUidRows() {
        QString error;QVERIFY(ensureWorkspaceSchema(db));QVERIFY(ensureRouterCacheSchema(db));QVERIFY(rebuildRouterViews(db));QVERIFY(WorkshopSpareStockService::ensureSchema(db,&error));
        QVERIFY(StaffAccountService::ensureSchema(db,&error));QVERIFY(AccessControlService::ensureSchema(db,&error));QSqlQuery q(db);
        QVERIFY(q.exec("INSERT INTO staff_accounts(id,display_name,role,active) VALUES('pdf-owner','Synthetic owner','owner',1)"));
        ActivityAuditService::setCurrentActor({"pdf-owner","Synthetic owner","owner","test"});
        double parts=0,works=0,margin=0,outsource=0,salary=0,expenses=0;
        const auto read=[&]{return ReportsPdfSupport::collectPeriodPdfRows("2026-01-01","2026-01-31",parts,works,margin,outsource,salary,expenses,&error);};
        QVERIFY(read().isEmpty());QVERIFY2(error.isEmpty(),qPrintable(error));
        QVERIFY(q.exec("INSERT INTO work_orders(number,order_uid,order_number,order_year,opened_at,closed_at,status,plate,plate_norm,car,client,master_id) VALUES(11,'pdf-order',7,2026,'2026-01-01','2026-01-02','closed','','','Synthetic','','m1')"));
        QVERIFY(q.exec("INSERT INTO work_order_services(id,order_number,order_uid,service_uid,title,qty,price,amount) VALUES(31,11,'pdf-order','pdf-service','Synthetic service',1,100,100)"));
        QVERIFY(q.exec("INSERT INTO work_order_parts(id,order_number,order_uid,part_uid,name,store,ordered_at,qty,purchase_price,purchase_amount,price,amount,service_id,pricing_calc_version) VALUES(21,11,'pdf-order','pdf-part','Synthetic part','Shop','2026-01-01',1,30.10,30.10,45.25,45.25,31,3)"));
        auto rows=read();QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(rows.size(),1);QCOMPARE(rows[0].orderNumber,QString("7"));QCOMPARE(rows[0].workTitle,QString("Synthetic service"));QCOMPARE(qRound64(parts*100),4525);QCOMPARE(qRound64(works*100),10000);QCOMPARE(qRound64(margin*100),1515);
        ActivityAuditService::clearCurrentActor();QVERIFY(read().isEmpty());QVERIFY(!error.isEmpty());QCOMPARE(parts,0.0);QCOMPARE(works,0.0);
        ActivityAuditService::setCurrentActor({"pdf-owner","Synthetic owner","owner","test"});
        QVERIFY(q.exec("ALTER TABLE work_order_parts RENAME COLUMN amount TO broken_amount"));QVERIFY(read().isEmpty());QVERIFY(!error.isEmpty());QCOMPARE(parts,0.0);
        ReportsPdfRenderer::ExportResult failed;QVERIFY(!ReportsPdfRenderer::renderPeriodPdf(QDate(2026,1,1),QDate(2026,1,31),&failed));QVERIFY(!failed.errorText.isEmpty());QVERIFY(!QFile::exists(failed.path));
        QVERIFY(q.exec("ALTER TABLE work_order_parts RENAME COLUMN broken_amount TO amount"));rows=read();QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(rows.size(),1);QCOMPARE(qRound64(parts*100),4525);
    }
    void reportDetailAndElectricityReadFailures() {
        QString error;QVERIFY2(PayrollReadFixture::schema(db,&error),qPrintable(error));QVERIFY2(PayrollReadFixture::populate(db,&error),qPrintable(error));QSqlQuery q(db);
        for(const auto& column:QStringList{"order_number INTEGER","opened_at TEXT","plate TEXT","car TEXT"})QVERIFY(q.exec("ALTER TABLE router_work_orders ADD COLUMN "+column));QVERIFY(q.exec("ALTER TABLE router_work_order_services ADD COLUMN title TEXT"));QVERIFY(q.exec("UPDATE router_work_order_services SET title='Synthetic work'"));
        auto empty=ReportsSyncSupport::collectPayrollOrdersForMaster(db,"m","2027-01-01","2027-01-31",&error);QVERIFY(error.isEmpty());QVERIFY(empty.isEmpty());
        const auto read=[&]{return ReportsSyncSupport::collectPayrollOrdersForMaster(db,"m","2026-01-01","2026-01-31",&error);};auto rows=read();QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(rows.size(),1);QCOMPARE(rows[0].masterAmount,50.0);
        QVERIFY(q.exec("ALTER TABLE router_work_order_services RENAME TO broken_services"));QVERIFY(read().isEmpty());QVERIFY(!error.isEmpty());QVERIFY(q.exec("ALTER TABLE broken_services RENAME TO router_work_order_services"));QCOMPARE(read().size(),1);QVERIFY(error.isEmpty());
        QVERIFY(ReportsSyncSupport::loadElectricityPeriods(db,&error).isEmpty());QVERIFY(!error.isEmpty());
        QVERIFY(q.exec("CREATE TABLE electricity_periods(period_from TEXT,period_to TEXT,meter_before REAL,meter_after REAL,consumed_kwh REAL,tariff REAL,amount REAL)"));QVERIFY(ReportsSyncSupport::loadElectricityPeriods(db,&error).isEmpty());QVERIFY(error.isEmpty());
        QVERIFY(q.exec("INSERT INTO electricity_periods VALUES('2026-01-01','2026-01-31',100,120,20,4.25,85)"));QCOMPARE(ReportsSyncSupport::loadElectricityPeriods(db,&error).first().amount,85.0);QVERIFY(error.isEmpty());
        QVERIFY(q.exec("ALTER TABLE electricity_periods RENAME COLUMN amount TO broken_amount"));QVERIFY(ReportsSyncSupport::loadElectricityPeriods(db,&error).isEmpty());QVERIFY(!error.isEmpty());
        ReportsPdfRenderer::ExportResult failed;QVERIFY(!ReportsPdfRenderer::renderElectricityPdf(&failed));QVERIFY(!failed.errorText.isEmpty());QVERIFY(!QFile::exists(failed.path));
        QVERIFY(q.exec("ALTER TABLE electricity_periods RENAME COLUMN broken_amount TO amount"));QCOMPARE(ReportsSyncSupport::loadElectricityPeriods(db,&error).size(),1);QVERIFY(error.isEmpty());
        configureFinancialReportDirectory();
        QVERIFY(q.exec("UPDATE electricity_periods SET meter_before=1250.125, meter_after=1252.625, consumed_kwh=2.5, tariff=4.3275, amount=10.82"));
        ReportsPdfRenderer::ExportResult exported;
        QVERIFY2(ReportsPdfRenderer::renderElectricityPdf(&exported), qPrintable(exported.errorText));
        QCOMPARE(exported.recordCount, 1);
        const auto capture = qEnvironmentVariable("CS_ELECTRICITY_PDF");
        if (!capture.isEmpty()) QVERIFY(QFile::copy(exported.path, capture));
    }
    void partsReadFailuresAndRecovery() {
        QString error;QVERIFY(ensureWorkspaceSchema(db));QVERIFY(ensureRouterCacheSchema(db));QVERIFY(rebuildRouterViews(db));
        QVERIFY2(WorkshopSpareStockService::ensureSchema(db,&error),qPrintable(error));
        const auto read=[&](const QString& path){return ReportsDataLoader::buildPartsSnapshot(path,"2026-01-01","2026-01-31",0,0,{},{});};
        const auto missing=dir.filePath("missing-parts.db");QVERIFY(!read(missing).error.isEmpty());QVERIFY(!QFile::exists(missing));
        QVERIFY(!read(dir.path()).error.isEmpty());
        auto empty=read(db.databaseName());QVERIFY2(empty.error.isEmpty(),qPrintable(empty.error));QVERIFY(empty.rows.isEmpty());
        QSqlQuery q(db);
        QVERIFY2(q.exec("INSERT INTO work_orders(number,order_uid,order_number,order_year,opened_at,status,plate,plate_norm,car,client,master_id) "
                        "VALUES(11,'parts-report-order',7,2026,'2026-01-01','open','','','Synthetic','','m1')"),qPrintable(q.lastError().text()));
        QVERIFY(q.exec("INSERT INTO work_order_parts(id,order_number,order_uid,part_uid,name,store,ordered_at,qty,purchase_price,purchase_amount,price,amount,markup_percent,manual_markup_override) VALUES(21,11,'parts-report-order','parts-report-part','Synthetic part','Shop','2026-01-01',1,30,30,45,45,50,1)"));
        QVERIFY(q.exec("UPDATE work_order_parts SET manufacturer='QA manufacturer', installed_at='2026-01-24' WHERE id=21"));
        PartsQueryService::invalidateCache();
        auto good=read(db.databaseName());QVERIFY2(good.error.isEmpty(),qPrintable(good.error));QCOMPARE(good.rows.size(),2);
        QCOMPARE(good.rows.last().purchase,QString("30"));
        QVERIFY(good.rows.last().tooltip.contains(QStringLiteral("Виробник: QA manufacturer")));
        QVERIFY(good.rows.last().tooltip.contains(QStringLiteral("Встановлено: 2026-01-24")));
        QVERIFY(q.exec("INSERT INTO work_order_parts(id,order_number,order_uid,part_uid,name,store,ordered_at,qty,purchase_amount) VALUES "
                       "(22,11,'parts-report-order','group-other','Other part','Other shop','2026-01-02',3,41),"
                       "(23,11,'parts-report-order','group-same','Second part','Shop','2026-01-03',0.5,10.25)"));
        const auto grouped = read(db.databaseName());
        QVERIFY2(grouped.error.isEmpty(), qPrintable(grouped.error));
        QCOMPARE(grouped.rows.size(), 5);
        QVERIFY(grouped.rows[2].isStoreHeader);
        QCOMPARE(grouped.rows[2].store, QStringLiteral("Shop"));
        QCOMPARE(grouped.rows[2].qty, QStringLiteral("2"));
        QCOMPARE(grouped.rows[2].purchase, FormatUtils::formatNumberSpaces(40.25, 2));
        QCOMPARE(grouped.rows[3].partName, QStringLiteral("Second part"));
        QCOMPARE(grouped.rows[4].partName, QStringLiteral("Synthetic part"));
        QVERIFY(q.exec("DELETE FROM work_order_parts WHERE id IN (22,23)"));
        QVERIFY(q.exec("UPDATE work_order_parts SET store='База' WHERE id=21"));
        QVERIFY(q.exec("INSERT INTO workshop_inventory_purchases(purchased_on,material_name,material_key,quantity,unit_cost) "
                       "VALUES('2026-01-01','Synthetic part','synthetic part',1,30)"));
        QVERIFY(q.exec("INSERT INTO workshop_spare_orders(id,name,brand,model,plate,master_id,master_name,supplier,ordered_on,quantity,unit_cost) "
                       "VALUES(19,'Synthetic part','','','','m1','','Shop','2026-01-01',1,30)"));
        QVERIFY(q.exec("INSERT INTO workshop_inventory_purchases(id,purchased_on,material_name,material_key,quantity,unit_cost,stock_kind) "
                       "VALUES(91,'2026-01-01','Reserved part','reserved part',1,30,'spare')"));
        QVERIFY(q.exec("INSERT INTO workshop_spare_movements(spare_id,operation,quantity,moved_on,purchase_id) "
                       "VALUES(19,'receipt',1,'2026-01-01',91)"));
        QVERIFY(q.exec("INSERT INTO workshop_spare_movements(spare_id,operation,quantity,moved_on,part_id,order_number) "
                       "VALUES(19,'issue',1,'2026-01-02',21,99)"));
        const auto foreignIssue=read(db.databaseName());
        QVERIFY2(foreignIssue.error.isEmpty(),qPrintable(foreignIssue.error));
        QCOMPARE(foreignIssue.rows.size(),2);
        QCOMPARE(foreignIssue.rows.last().store,QStringLiteral("Склад СТО"));
        QVERIFY(q.exec("UPDATE workshop_spare_movements SET order_number=11 WHERE spare_id=19"));
        const auto ownedIssue=read(db.databaseName());
        QVERIFY2(ownedIssue.error.isEmpty(),qPrintable(ownedIssue.error));
        QCOMPARE(ownedIssue.rows.size(),2);
        QVERIFY(ownedIssue.rows.last().store!=QStringLiteral("Склад СТО"));
        QVERIFY(q.exec("UPDATE work_order_parts SET store='Shop' WHERE id=21"));
        QVERIFY(q.exec("CREATE TABLE other_report_header AS SELECT * FROM work_orders WHERE number=11"));
        QVERIFY(q.exec("UPDATE other_report_header SET order_uid='different-order',order_year=2025,car='Other owner'"));
        QVERIFY(q.exec("DROP VIEW router_work_orders"));
        QVERIFY(q.exec("CREATE VIEW router_work_orders AS SELECT * FROM work_orders UNION ALL SELECT * FROM other_report_header"));
        const auto routed=read(db.databaseName());
        QVERIFY2(routed.error.isEmpty(),qPrintable(routed.error));
        QCOMPARE(routed.rows.size(),2);
        QCOMPARE(routed.rows.last().purchase,QString("30"));
        QCOMPARE(routed.rows.last().orderUid,QString("parts-report-order"));
        ReportsTableSupport::ReportsSnapshotTableModel model;
        QTableView table;
        table.setModel(&model);
        ReportsTableSupport::applyPartsSnapshot(&table,&model,routed);
        QCOMPARE(model.index(1,1).data(Qt::UserRole).toInt(),11);
        QCOMPARE(model.index(1,1).data(ReportsTableSupport::OrderUidRole).toString(),QString("parts-report-order"));
        QVERIFY(!routed.rows.last().tooltip.contains("Other owner"));
        QVERIFY(q.exec("DROP VIEW router_work_orders"));
        QVERIFY(q.exec("CREATE VIEW router_work_orders AS SELECT * FROM other_report_header"));
        const auto missingOwner=read(db.databaseName());
        QVERIFY(missingOwner.error.contains("unique order"));
        QVERIFY(missingOwner.rows.isEmpty());
        QVERIFY(q.exec("DROP VIEW router_work_orders"));
        QVERIFY(q.exec("DROP TABLE other_report_header"));
        QVERIFY(rebuildRouterViews(db));
        const auto repairedOwner=read(db.databaseName());
        QVERIFY2(repairedOwner.error.isEmpty(),qPrintable(repairedOwner.error));
        QCOMPARE(repairedOwner.rows.size(),2);
        const auto original=PayrollReadFixture::bytes(db.databaseName());
        for(const auto& table:QStringList{"work_order_part_attachments","workshop_inventory_purchases","workshop_spare_movements","workshop_spare_returns","workshop_spare_attachments","settings"}) {
            QVERIFY2(q.exec("ALTER TABLE "+table+" RENAME TO parts_broken"),qPrintable(q.lastError().text()));
            const auto failed=read(db.databaseName());QVERIFY2(!failed.error.isEmpty(),qPrintable(table));QVERIFY(failed.rows.isEmpty());QVERIFY(failed.tileBodies.isEmpty());QCOMPARE(failed.stats.size(),1);
            QVERIFY(q.exec("ALTER TABLE parts_broken RENAME TO "+table));
            auto repaired=read(db.databaseName());QVERIFY2(repaired.error.isEmpty(),qPrintable(repaired.error));QCOMPARE(repaired.rows.size(),2);
        }
        const auto stable=PayrollReadFixture::bytes(db.databaseName());QVERIFY(!original.isEmpty());
        QVERIFY(read(db.databaseName()).error.isEmpty());QCOMPARE(PayrollReadFixture::bytes(db.databaseName()),stable);
    }
    void periodAndOrdersRetryOnTabReturn()
    {
        QVERIFY(ensureWorkspaceSchema(db));
        QVERIFY(ensureRouterCacheSchema(db));
        QVERIFY(rebuildRouterViews(db));
        QSqlQuery query(db);
        QVERIFY2(query.exec("DROP VIEW router_work_orders"), qPrintable(query.lastError().text()));

        ReportsPage page;
        page.show();
        auto* tabs = page.findChild<QTabWidget*>();
        auto* stats = page.findChild<QLabel*>("reportsStatsLabel");
        QVERIFY(tabs && stats);
        tabs->setCurrentIndex(0);
        page.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(stats->text().contains(QStringLiteral("Помилка читання звіту")), 10000);
        tabs->setCurrentIndex(1);
        QTRY_VERIFY_WITH_TIMEOUT(stats->text().contains(QStringLiteral("Не вдалося завантажити наряди")), 10000);

        QVERIFY(rebuildRouterViews(db));
        tabs->setCurrentIndex(0);
        QTRY_VERIFY_WITH_TIMEOUT(stats->text().contains(QStringLiteral("Роботи:")), 10000);
        QVERIFY(!stats->text().contains(QStringLiteral("Помилка")));
        tabs->setCurrentIndex(1);
        QTRY_VERIFY_WITH_TIMEOUT(stats->text().contains(QStringLiteral("Закупка:")), 10000);
        QVERIFY(!stats->text().contains(QStringLiteral("Не вдалося")));
    }

    void reportsCornerActions() {
        ReportsPage page;
        auto* tabs = page.findChild<QTabWidget*>();
        auto* pdf = page.findChild<QPushButton*>("reportsPdfButton");
        QVERIFY(tabs && pdf);
        QVERIFY(tabs->count() > 1);
        for (int i = 0; i < tabs->count(); ++i) {
            tabs->setCurrentIndex(i);
            QCOMPARE(tabs->cornerWidget(Qt::TopRightCorner), static_cast<QWidget*>(pdf));
            for (auto* action : page.findChildren<QAbstractButton*>())
                QVERIFY(action->text() != QStringLiteral("Завантаження постів"));
        }
    }
    void partsStoreCollapseAndExpand() {
        ReportsPage page;
        auto* collapse = page.findChild<QPushButton*>("partsCollapseAll");
        auto* expand = page.findChild<QPushButton*>("partsExpandAll");
        QVERIFY(collapse && expand);
        auto* table = collapse->parentWidget()->findChild<QTableView*>();
        QVERIFY(table);
        auto* model = dynamic_cast<ReportsTableSupport::ReportsSnapshotTableModel*>(table->model());
        QVERIFY(model);
        PartsSnapshot snapshot;
        PartsSnapshotRow header;
        header.isStoreHeader = true;
        header.store = "Store A";
        header.qty = "1";
        header.purchase = "45";
        PartsSnapshotRow part;
        part.invoiceNumber = "INV-71";
        snapshot.rows = {header, part};
        ReportsTableSupport::applyPartsSnapshot(table, model, snapshot);
        collapse->click();
        QVERIFY(!table->isRowHidden(0));
        QVERIFY(table->isRowHidden(1));
        header.store = "Store B";
        part.partName = "Air filter";
        part.orderNumber = "832";
        snapshot.rows.append(header);
        snapshot.rows.append(part);
        ReportsTableSupport::applyPartsSnapshot(table, model, snapshot);
        QVERIFY(!table->isRowHidden(2));
        QVERIFY(table->isRowHidden(3));
        expand->click();
        for (int row = 0; row < model->rowCount(); ++row) QVERIFY(!table->isRowHidden(row));
        auto* search = page.findChild<QLineEdit*>("partsReportSearch");
        auto* empty = page.findChild<QLabel*>("partsSearchEmpty");
        QVERIFY(search && empty);
        for (const QString& term : {QStringLiteral("832"), QStringLiteral(" FILTER ")}) {
            search->setText(term);
            QVERIFY(table->isRowHidden(0) && table->isRowHidden(1));
            QVERIFY(!table->isRowHidden(2) && !table->isRowHidden(3));
        }
        collapse->click();
        ReportsTableSupport::applyPartsSnapshot(table, model, snapshot);
        QVERIFY(table->isRowHidden(0) && !table->isRowHidden(2) && table->isRowHidden(3));
        expand->click();
        search->setText("missing");
        QVERIFY(!empty->isHidden());
        search->setText("inv-71");
        QVERIFY(empty->isHidden());
        for (int row = 0; row < model->rowCount(); ++row) QVERIFY(!table->isRowHidden(row));
        search->clear();
        QCOMPARE(model->cellText(3, 3), QStringLiteral("INV-71"));
        QVERIFY(qvariant_cast<QFont>(model->index(3, 3).data(Qt::FontRole)).underline());
    }

    void partsUiRecoversAfterReadFailure() {
        QString error;QVERIFY(ensureWorkspaceSchema(db));QVERIFY(ensureRouterCacheSchema(db));QVERIFY(rebuildRouterViews(db));QVERIFY(WorkshopSpareStockService::ensureSchema(db,&error));
        QSqlQuery q(db);QVERIFY(q.exec("ALTER TABLE work_order_part_attachments RENAME TO parts_broken"));
        ReportsPage page;page.show();auto* tabs=page.findChild<QTabWidget*>();auto* stats=page.findChild<QLabel*>("reportsStatsLabel");auto* pdf=page.findChild<QPushButton*>("reportsPdfButton");QVERIFY(tabs && stats && pdf);
        int parts=-1;for(int i=0;i<tabs->count();++i)if(tabs->tabText(i)==QString::fromUtf8("Придбання запчастин"))parts=i;QVERIFY(parts>=0);tabs->setCurrentIndex(parts);
        QTRY_VERIFY_WITH_TIMEOUT(stats->text().contains(QString::fromUtf8("Помилка читання запчастин")),10000);QVERIFY(!pdf->isEnabled());
        auto* table=tabs->widget(parts)->findChild<QTableView*>();
        QVERIFY(table && table->model());
        QCOMPARE(table->model()->rowCount(),0);
        QVERIFY(q.exec("ALTER TABLE parts_broken RENAME TO work_order_part_attachments"));page.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(pdf->isEnabled(),10000);QVERIFY(!stats->text().contains(QString::fromUtf8("Помилка читання запчастин")));
    }
    void payrollReadFailuresAndRecovery() {
        QString error;QVERIFY2(PayrollReadFixture::run(&error),qPrintable(error));
    }
    void payrollMonthlyBoundariesAndPeriodTotals() {
        QString error;
        QVERIFY2(PayrollReadFixture::schema(db, &error), qPrintable(error));
        QVERIFY2(PayrollReadFixture::populate(db, &error), qPrintable(error));
        QSqlQuery query(db);
        QVERIFY(query.exec("INSERT INTO expenses(period_from,period_to,cleaning) VALUES('2026-02-01','2026-02-10',20)"));
        const auto result = ReportsDataLoader::buildPayrollSnapshot(db.databaseName(), "2026-01-01", "2026-02-12", true);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.months.size(), 2);
        QCOMPARE(result.months[0].to, QDate(2026, 1, 31));
        QCOMPARE(result.months[1].from, QDate(2026, 2, 1));
        QCOMPARE(result.months[1].to, QDate(2026, 2, 12));
        const auto pay = [](const QList<PayrollSnapshotRow>& rows, const QString& id) {
            for (const auto& row : rows) if (row.masterId == id) return row.accruedValue;
            return -1.0;
        };
        QCOMPARE(pay(result.rows, "m"), 20.0);
        QCOMPARE(pay(result.months[0].rows, "m"), 40.0);
        QCOMPARE(pay(result.months[1].rows, "m"), 0.0);
        QCOMPARE(pay(result.months[0].rows, "a"), 3.0);
        const auto clipped = ReportsDataLoader::buildPayrollSnapshot(db.databaseName(), "2025-12-28", "2026-01-10", true);
        QVERIFY2(clipped.error.isEmpty(), qPrintable(clipped.error));
        QCOMPARE(clipped.months.size(), 2);
        QCOMPARE(clipped.months.first().from, QDate(2025, 12, 28));
        QCOMPARE(clipped.months.last().to, QDate(2026, 1, 10));
        QCOMPARE(pay(clipped.months.last().rows, "m"), 0.0);
    }
    void payrollGroupingAndMonthlyDrilldown() {
        QString error;
        QVERIFY2(PayrollIdentityFixture::run(db, &error), qPrintable(error));
        ReportsPage page;
        page.show();
        auto* grouping = page.findChild<QComboBox*>("payrollGrouping");
        auto* employee = page.findChild<QComboBox*>("payrollEmployee");
        auto* tabs = page.findChild<QTabWidget*>();
        auto* from = page.findChild<QDateEdit*>("dashboardFrom");
        auto* to = page.findChild<QDateEdit*>("dashboardTo");
        QVERIFY(grouping && employee && tabs && from && to);
        from->setDate(QDate(2026, 1, 1));
        to->setDate(QDate(2026, 2, 17));
        for (int i = 0; i < tabs->count(); ++i)
            if (tabs->tabText(i) == QStringLiteral("Зарплата")) tabs->setCurrentIndex(i);
        auto* table = tabs->currentWidget()->findChild<QTableView*>();
        QVERIFY(table);
        QTRY_VERIFY_WITH_TIMEOUT(employee->findData("m") >= 0, 10000);
        employee->setCurrentIndex(employee->findData("m"));
        grouping->setCurrentIndex(1);
        QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(), 3, 10000);
        QVERIFY(table->model()->index(0, 0).data().toString().contains("ID m"));
        grouping->setCurrentIndex(2);
        QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(), 4, 10000);
        QCOMPARE(table->model()->index(3, 0).data(ReportsTableSupport::PayrollToRole).toDate(), QDate(2026, 2, 17));
        for (const int row : {1, 3}) {
            QString detailText;
            QTimer::singleShot(0, [&] {
                auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                if (!dialog) return;
                auto* detail = dialog->findChild<QTableView*>();
                if (detail && detail->model() && detail->model()->rowCount() == 1) {
                    detailText = detail->model()->index(0, row == 1 ? 2 : 0).data().toString();
                }
                dialog->reject();
            });
            table->doubleClicked(table->model()->index(row, 0));
            QCOMPARE(detailText, row == 1 ? QStringLiteral("7") : QStringLiteral("Немає виконаних нарядів за вибраний період"));
        }
        grouping->setCurrentIndex(0);
        QTRY_VERIFY_WITH_TIMEOUT(employee->isEnabled() && table->model()->rowCount() == 1, 10000);
        QCOMPARE(table->model()->index(0, 4).data().toString(), FormatUtils::money(0.05, 2));
    }
    void payrollFiltersDistinguishNamesakes() {
        QString error;
        QVERIFY2(PayrollIdentityFixture::run(db, &error), qPrintable(error));
        QSqlQuery query(db);
        QVERIFY(query.exec("UPDATE masters SET name='Master' WHERE id='n'"));
        ReportsPage page;
        page.show();
        auto* tabs = page.findChild<QTabWidget*>();
        QVERIFY(tabs);
        auto* employee = page.findChild<QComboBox*>("payrollEmployee");
        auto* search = page.findChild<QLineEdit*>("payrollSearch");
        QVERIFY(employee && search);
        const auto dates = page.findChildren<QDateEdit*>();
        QVERIFY(dates.size() >= 2);
        dates[0]->setDate(QDate(2026, 1, 1));
        dates[1]->setDate(QDate(2026, 1, 31));
        for (int i = 0; i < tabs->count(); ++i)
            if (tabs->tabText(i) == QStringLiteral("Зарплата")) tabs->setCurrentIndex(i);
        auto* table = tabs->currentWidget()->findChild<QTableView*>();
        auto* stats = page.findChild<QLabel*>("reportsStatsLabel");
        QVERIFY(table && stats);
        QTRY_VERIFY_WITH_TIMEOUT(employee->findData("n") >= 0, 10000);
        const int first = employee->findData("m");
        const int second = employee->findData("n");
        QVERIFY(first >= 0 && employee->itemText(first) != employee->itemText(second));
        for (const int index : {first, second}) {
            employee->setCurrentIndex(index);
            QCOMPARE(table->model()->rowCount(), 1);
            const QString expected = FormatUtils::money(index == first ? 0.05 : 0.02, 2);
            QCOMPARE(table->model()->index(0, 4).data().toString(), expected);
            QVERIFY(stats->text().contains(expected));
        }
        search->setText("absent");
        QVERIFY(stats->text().contains(FormatUtils::money(0, 2)));
        search->setText("mAsT");
        employee->setCurrentIndex(0);
        QCOMPARE(table->model()->rowCount(), 2);
        QVERIFY(stats->text().contains(FormatUtils::money(0.07, 2)));
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

    void repeatPreviousExpensesOnlyChangesDraft() {
        ReportsPage page;
        page.show();
        auto* tabs = page.findChild<QTabWidget*>();
        auto* form = page.findChild<QWidget*>("reportsExpensesPage");
        QVERIFY(tabs && form);
        tabs->setCurrentWidget(form);
        auto* repeat = form->findChild<QPushButton*>("expenseRepeat");
        auto* status = form->findChild<QLabel*>("expenseValidation");
        auto* from = page.findChild<QDateEdit*>("dashboardFrom");
        auto* to = page.findChild<QDateEdit*>("dashboardTo");
        QVERIFY(repeat && status && from && to);
        from->setDate(QDate(2026,2,1));
        to->setDate(QDate(2026,2,28));
        QTRY_VERIFY_WITH_TIMEOUT(repeat->isEnabled(), 10000);
        repeat->click();
        QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QStringLiteral("немає збережених витрат")),10000);
        QSqlQuery q(db);
        QVERIFY(q.exec("INSERT INTO expenses(period_from,period_to,elec_curr,elec_tariff,rent,rent_note) VALUES"
                       "('2025-12-01','2025-12-31',50,3,80,'older'),"
                       "('01.01.26','31.01.26',100.125,4.3275,120.50,'chosen'),"
                       "('2026-01-15','2026-02-15',200,5,160,'overlap'),"
                       "('2026-03-01','2026-03-31',300,6,200,'future')"));
        auto field = [form](const QString& name) -> QLineEdit* {
            for (auto* item : form->findChildren<QLineEdit*>())
                if (item->accessibleName() == name) return item;
            return nullptr;
        };
        auto* rent = field(QStringLiteral("Оренда"));
        auto* previous = field(QStringLiteral("Попередній показник"));
        auto* current = field(QStringLiteral("Поточний показник"));
        auto* tariff = field(QStringLiteral("Тариф"));
        QVERIFY(rent && previous && current && tariff);
        QVERIFY(rent->text().isEmpty());
        repeat->click();
        QTRY_VERIFY_WITH_TIMEOUT(status->text().contains("31.01.26"),10000);
        QCOMPARE(previous->text(), QStringLiteral("100.125"));
        QVERIFY(current->text().isEmpty());
        QCOMPARE(tariff->text(), QStringLiteral("4.3275"));
        QCOMPARE(rent->text(), QStringLiteral("120.5"));
        rent->setText("130,75");
        bool noteCopied = false;
        for (auto* item : form->findChildren<QLineEdit*>())
            if (item->text() == "chosen") noteCopied = true;
        QVERIFY(noteCopied);
        QVERIFY(q.exec("SELECT COUNT(*),SUM(rent) FROM expenses"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(),4);
        QCOMPARE(q.value(1).toDouble(),560.5);
        QVERIFY(q.exec("SELECT COUNT(*) FROM settings WHERE key='reports.expenses.electricity_tariff'"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(),0);
    }

    void expenseFormPreservesFractionalInputs() {
        QWidget parent;
        using namespace ReportsExpensesSupport;
        const auto form = buildExpensesPage(&parent);
        ExpensesSnapshot snapshot;
        snapshot.foundRow = true;
        snapshot.elecPrev = 1250.125;
        snapshot.elecCurr = 1252.625;
        snapshot.elecTariff = 4.3275;
        snapshot.cleaning = 28.75;
        snapshot.rent = 1050.33;
        applySnapshot(snapshot, form);
        const auto result = recalcForm(form, false);
        const auto saved = buildSavePayload(form);
        const auto pdf = buildPdfData(form);
        QCOMPARE(saved.elecPrev, snapshot.elecPrev);
        QCOMPARE(saved.elecCurr, snapshot.elecCurr);
        QCOMPARE(saved.elecTariff, snapshot.elecTariff);
        QCOMPARE(saved.cleaning, snapshot.cleaning);
        QCOMPARE(saved.rent, snapshot.rent);
        QCOMPARE(result.kwh, 2.5);
        QCOMPARE(pdf.elecKwh, saved.elecKwh);
        QCOMPARE(pdf.elecCost, saved.elecCost);
        QCOMPARE(pdf.total, 1090.08);
    }

    void expenseFormRejectsInvalidInput() {
        QWidget parent;
        using namespace ReportsExpensesSupport;
        const auto form = buildExpensesPage(&parent);
        form.elecPrevEdit->setText("25");
        form.elecCurrEdit->setText("24");
        QVERIFY(!recalcForm(form, false).inputError.isEmpty());
        form.elecCurrEdit->setText("26");
        QVERIFY(!recalcForm(form, false).inputError.isEmpty());
        form.elecTariffEdit->setText("4,25");
        for (const auto& value : QStringList{"wrong", "NaN", "inf"}) {
            form.rentEdit->setText(value);
            QVERIFY(!recalcForm(form, false).inputError.isEmpty());
            QVERIFY(form.expensesTotalEdit->text().isEmpty());
        }
        form.rentEdit->setText("28,75");
        const auto corrected = recalcForm(form, false);
        QVERIFY(corrected.inputError.isEmpty());
        QCOMPARE(corrected.total, 32.75);
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
        auto* dashboard = page.findChild<QWidget*>("reportDashboard");
        QVERIFY(dashboard);
        QVERIFY(dashboard->styleSheet().contains("#145957"));
        QVERIFY(form->findChild<QFrame*>("electricityPanel"));
        QVERIFY(form->findChild<QFrame*>("otherExpensesPanel"));
        auto* save = form->findChild<QPushButton*>("expenseSave");
        auto* warning = form->findChild<QLabel*>("expenseValidation");
        QLineEdit* rent = nullptr;
        for (auto* field : form->findChildren<QLineEdit*>()) {
            if (field->accessibleName() == QStringLiteral("Оренда")) rent = field;
        }
        QVERIFY(save && warning && rent);
        rent->setText(QStringLiteral("помилка"));
        QVERIFY(!save->isEnabled());
        QVERIFY(warning->isVisible());
        QVERIFY(warning->text().contains(QStringLiteral("Оренда")));
        QVERIFY(QMetaObject::invokeMethod(&page, "saveExpenses", Qt::DirectConnection));
        QVERIFY(q.exec("SELECT count(*) FROM expenses"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 0);
        rent->setText("120,50");
        QVERIFY(save->isEnabled());
        QVERIFY(!warning->isVisible());
        const QString capture = qEnvironmentVariable("CS_EXPENSE_PALETTE_CAPTURE");
        if (!capture.isEmpty()) QVERIFY(page.grab().save(capture));
        auto* fromDate = dashboard->findChild<QDateEdit*>("dashboardFrom");
        QVERIFY(fromDate);
        fromDate->setDate(fromDate->date().addDays(-1));
        QVERIFY(QMetaObject::invokeMethod(&page, "saveExpenses", Qt::DirectConnection));
        QVERIFY(q.exec("SELECT count(*) FROM expenses"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 0);
        tabs->setCurrentIndex(0);
        QTRY_VERIFY(!dashboard->styleSheet().contains("#145957"));
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
    void totalExpensePeriodNavigation() {
        QVERIFY(AdditionalExpenseService::add(db, QDate(2024,2,29), "Rent", "Leap day", 12.34));
        QVERIFY(AdditionalExpenseService::add(db, QDate(2024,3,1), "Rent", "Next month", 5.01));
        ReportsPage page;
        page.show();
        auto* tabs = page.findChild<QTabWidget*>();
        QVERIFY(tabs);
        tabs->setCurrentIndex(6);
        auto* from = page.findChild<QDateEdit*>("dashboardFrom");
        auto* to = page.findChild<QDateEdit*>("dashboardTo");
        auto* month = page.findChild<QPushButton*>("totalExpensePeriod1");
        auto* quarter = page.findChild<QPushButton*>("totalExpensePeriod3");
        auto* year = page.findChild<QPushButton*>("totalExpensePeriod12");
        auto* custom = page.findChild<QPushButton*>("totalExpensePeriod0");
        auto* previous = page.findChild<QPushButton*>("totalExpensePreviousPeriod");
        auto* next = page.findChild<QPushButton*>("totalExpenseNextPeriod");
        QVERIFY(from && to && month && quarter && year && custom && previous && next);
        auto* total = page.findChild<QLabel*>("reportMetricValue0");
        auto* search = page.findChild<QLineEdit*>("totalExpenseSearch");
        QVERIFY(total && search);
        from->setDate(QDate(2024,2,15));
        month->click();
        QCOMPARE(from->date(), QDate(2024,2,1));
        QCOMPARE(to->date(), QDate(2024,2,29));
        QTRY_COMPARE_WITH_TIMEOUT(total->text(), QStringLiteral("12.34 грн"), 10000);
        search->setText("day");
        next->click();
        QCOMPARE(to->date(), QDate(2024,3,31));
        QTRY_COMPARE_WITH_TIMEOUT(total->text(), QStringLiteral("0 грн"), 10000);
        QCOMPARE(search->text(), QStringLiteral("day"));
        search->clear();
        QTRY_COMPARE_WITH_TIMEOUT(total->text(), QStringLiteral("5.01 грн"), 10000);
        quarter->click();
        QCOMPARE(from->date(), QDate(2024,1,1));
        previous->click();
        QCOMPARE(from->date(), QDate(2023,10,1));
        QCOMPARE(to->date(), QDate(2023,12,31));
        year->click();
        next->click();
        QCOMPARE(from->date(), QDate(2024,1,1));
        QCOMPARE(to->date(), QDate(2024,12,31));
        from->setDate(QDate(2024,12,29));
        QVERIFY(custom->isChecked());
        next->click();
        QCOMPARE(from->date(), QDate(2025,1,1));
        QCOMPARE(to->date(), QDate(2025,1,3));
        previous->click();
        QCOMPARE(from->date(), QDate(2024,12,29));
        QCOMPARE(to->date(), QDate(2024,12,31));
        from->setDate(from->minimumDate());
        year->click();
        QVERIFY(!previous->isEnabled());
        from->setDate(from->maximumDate());
        month->click();
        QCOMPARE(to->date(), to->maximumDate());
        QVERIFY(!next->isEnabled());
    }

    void totalExpenseMonthGroupsPreserveTotals() {
        QSqlQuery q(db);
        QVERIFY(q.exec("INSERT INTO expenses(period_from,period_to,rent,rent_note) VALUES('20.01.26','05.02.26',60.25,'Rent')"));
        qint64 januaryId = 0;
        QVERIFY(AdditionalExpenseService::add(db, QDate(2026,1,20), "Other", "January", 5.75, &januaryId));
        QVERIFY(AdditionalExpenseService::add(db, QDate(2026,2,10), QStringLiteral("Нова пошта"), "February", 10.05));
        ReportsPage page;
        page.resize(1400,850);
        page.show();
        auto* tabs = page.findChild<QTabWidget*>();
        QVERIFY(tabs);
        tabs->setCurrentIndex(6);
        auto* from = page.findChild<QDateEdit*>("dashboardFrom");
        auto* to = page.findChild<QDateEdit*>("dashboardTo");
        auto* table = page.findChild<QTableView*>("additionalExpensesTable");
        auto* total = page.findChild<QLabel*>("reportMetricValue0");
        auto* count = page.findChild<QLabel*>("reportMetricValue1");
        auto* postage = page.findChild<QLabel*>("reportMetricValue2");
        auto* remove = page.findChild<QPushButton*>("deleteAdditionalExpense");
        QVERIFY(from && to && table && total && count && postage && remove);
        from->setDate(QDate(2026,1,1));
        to->setDate(QDate(2026,2,28));
        auto* model = table->model();
        QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 5, 10000);
        QCOMPARE(model->index(1,2).data().toString(), QStringLiteral("February"));
        QCOMPARE(model->index(2,2).data().toString(), QStringLiteral("Rent"));
        auto* footer = page.findChild<QLabel*>("filteredExpenseTotal");
        QVERIFY(footer);
        const auto heading = model->index(0,0);
        QVERIFY(heading.data().toString().contains(QStringLiteral("Лютий 2026")));
        QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier, table->visualRect(heading).center());
        QCOMPARE(model->rowCount(), 3);
        QCOMPARE(count->text(), "3");
        QCOMPARE(total->text(), QStringLiteral("76.05 грн"));
        QCOMPARE(postage->text(), QStringLiteral("10.05 грн"));
        QCOMPARE(footer->text(), QStringLiteral("Разом за період: 76.05 грн"));
        QVERIFY(!remove->isEnabled());
        QCOMPARE(model->index(2,0).data(Qt::UserRole).toLongLong(), januaryId);
        table->setFocus();
        QTest::keyClick(table, Qt::Key_Space);
        QCOMPARE(model->rowCount(), 5);
        const auto capture = qEnvironmentVariable("CS_EXPENSE_GROUPS_CAPTURE");
        if (!capture.isEmpty()) QVERIFY(page.grab().save(capture));
    }

    void totalExpenseFiltersKeepAmountsAndIdentityTogether() {
        const QDate day = QDate::currentDate();
        QSqlQuery q(db);
        q.prepare("INSERT INTO expenses(period_from,period_to,rent,rent_note) VALUES(?,?,60.21,'Parcel rent')");
        q.addBindValue(day.toString(Qt::ISODate));
        q.addBindValue(day.toString(Qt::ISODate));
        QVERIFY(q.exec());
        const QString post = QStringLiteral("Нова пошта");
        qint64 chosen = 0;
        QVERIFY(AdditionalExpenseService::add(db, day, post, "Parcel A", 12.34, &chosen));
        QVERIFY(AdditionalExpenseService::add(db, day, post, "Letter B", 0.01));
        QVERIFY(AdditionalExpenseService::add(db, day, "Other", "Parcel C", 7.15));
        ReportsPage page;
        page.show();
        auto* tabs = page.findChild<QTabWidget*>();
        QVERIFY(tabs);
        tabs->setCurrentIndex(6);
        auto* grouping = page.findChild<QComboBox*>("totalExpenseGrouping");
        QVERIFY(grouping);
        grouping->setCurrentIndex(1);
        auto* table = page.findChild<QTableView*>("additionalExpensesTable");
        auto* category = page.findChild<QComboBox*>("totalExpenseCategory");
        auto* source = page.findChild<QComboBox*>("totalExpenseSource");
        auto* search = page.findChild<QLineEdit*>("totalExpenseSearch");
        auto* total = page.findChild<QLabel*>("reportMetricValue0");
        auto* count = page.findChild<QLabel*>("reportMetricValue1");
        auto* postage = page.findChild<QLabel*>("reportMetricValue2");
        QVERIFY(table && category && source && search && total && count && postage);
        QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(), 4, 10000);
        QCOMPARE(total->text(), QStringLiteral("79.71 грн"));
        QCOMPARE(postage->text(), QStringLiteral("12.35 грн"));
        search->setText("PARCEL");
        QCOMPARE(count->text(), "3");
        QCOMPARE(total->text(), QStringLiteral("79.7 грн"));
        source->setCurrentText(QStringLiteral("Додаткова витрата"));
        QCOMPARE(total->text(), QStringLiteral("19.49 грн"));
        category->setCurrentText(post);
        QCOMPARE(count->text(), "1");
        QCOMPARE(postage->text(), QStringLiteral("12.34 грн"));
        QCOMPARE(table->model()->index(0,0).data(Qt::UserRole).toLongLong(), chosen);
        source->setCurrentText(QStringLiteral("Витрати за період"));
        QCOMPARE(count->text(), "0");
        QCOMPARE(total->text(), QStringLiteral("0 грн"));
        QCOMPARE(postage->text(), QStringLiteral("0 грн"));
        category->setCurrentIndex(0);
        QCOMPARE(total->text(), QStringLiteral("60.21 грн"));
        QVERIFY(table->model()->index(0,0).data(Qt::UserRole).toLongLong() == 0);
        source->setCurrentIndex(0);
        search->clear();
        QCOMPARE(count->text(), "4");
        QCOMPARE(total->text(), QStringLiteral("79.71 грн"));
    }

    void repeatedExpenseDeleteKeepsNeighbour() {
        const QDate day = QDate::currentDate();
        qint64 target = 0, neighbour = 0;
        QVERIFY(AdditionalExpenseService::add(db, day, "Other", "Same description", 5.25, &target));
        QVERIFY(AdditionalExpenseService::add(db, day, "Other", "Same description", 5.25, &neighbour));
        ReportsPage page;
        page.show();
        auto* tabs = page.findChild<QTabWidget*>();
        QVERIFY(tabs);
        tabs->setCurrentIndex(6);
        auto* table = page.findChild<QTableView*>("additionalExpensesTable");
        auto* remove = page.findChild<QPushButton*>("deleteAdditionalExpense");
        QVERIFY(table && remove);
        auto* model = table->model();
        QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 3, 10000);
        const int selected = model->index(1,0).data(Qt::UserRole).toLongLong() == target ? 1 : 2;
        table->setCurrentIndex(model->index(selected,0));
        QVERIFY(remove->isEnabled());
        bool duplicatePrompt = false;
        QTimer::singleShot(0, [&] {
            auto* first = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            QVERIFY(first);
            QTimer::singleShot(0, &page, [&, first] {
                auto* second = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                if (second && second != first) {
                    duplicatePrompt = true;
                    second->button(QMessageBox::No)->click();
                }
            });
            remove->click();
            first->button(QMessageBox::Yes)->click();
        });
        remove->click();
        QVERIFY(!duplicatePrompt);
        QVERIFY(!remove->isEnabled());
        remove->click();
        QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 2, 10000);
        QCOMPARE(model->index(1,0).data(Qt::UserRole).toLongLong(), neighbour);
        QSqlQuery q(db);
        QVERIFY(q.exec("SELECT id, amount_cents FROM additional_expenses"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toLongLong(), neighbour);
        QCOMPARE(q.value(1).toLongLong(), 525);
        QVERIFY(!q.next());
    }

    void formAddFilterDeleteAndLayout() {
        ReportsPage page;
        page.resize(1400, 850);
        auto* tabs = page.findChild<QTabWidget*>(); QVERIFY(tabs);
        tabs->setCurrentIndex(6);
        page.show();
        auto* grouping = page.findChild<QComboBox*>("totalExpenseGrouping");
        QVERIFY(grouping);
        grouping->setCurrentIndex(1);
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
        QVERIFY(!dialog->isVisible());
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
