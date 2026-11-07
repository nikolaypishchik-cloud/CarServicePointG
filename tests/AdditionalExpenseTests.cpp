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
#include "core/services/StaffAccountService.h"
#include "core/services/AccessControlService.h"
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

    void financialPdfReadFailuresAndRecovery() {
        QString error;QVERIFY2(FinancialPdfReadFixture::run(db,&error),qPrintable(error));
    }
    void expensePdfFailuresRightsAndRepeat() {
        QString error;QVERIFY2(FinancialPdfReadFixture::run(db,&error),qPrintable(error));
        configureFinancialReportDirectory();
        ReportsPdfRenderer::ExpensesFormData data;data.cleaning=12.25;data.other=7.10;data.total=19.35;
        ReportsPdfRenderer::ExportResult result;
        const auto render=[&]{return ReportsPdfRenderer::renderExpensesPdf(QDate(2026,1,1),QDate(2026,1,31),data,&result);};
        QVERIFY2(render(),qPrintable(result.errorText));QVERIFY(result.success);QVERIFY(result.fileSizeBytes>0);
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
        PartsQueryService::invalidateCache();
        auto good=read(db.databaseName());QVERIFY2(good.error.isEmpty(),qPrintable(good.error));QCOMPARE(good.rows.size(),2);
        QCOMPARE(good.rows.last().purchase,QString("30"));
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
