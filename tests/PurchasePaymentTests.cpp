#include "TestSourceAssertions.h"
#include "core/services/orders/OrderDialogReadQueryService.h"
#include "ui/common/dialogs/InvoiceReviewDialog.h"
#include "ui/tabs/orders/CurrentOrdersPageLocalSupport.h"
#include "core/services/SupplierDebtReadService.h"
#include "core/services/ElitDocumentAssociation.h"
#include "core/io/SupplierInvoiceTemplateStore.h"
#include <QSemaphore>
#include <thread>
#include "infrastructure/db/sqlite/ScopedConnectionManager.h"
#include "ui/tabs/settings/StaffSessionDialog.h"
#include "infrastructure/db/sqlite/DbArchiveInternals.h"
#include "infrastructure/db/sqlite/DbThreadConnectionSupport.h"
#include "infrastructure/db/sqlite/ArchiveTransferJournal.h"
#include "core/services/orders/OrderClosureJournal.h"
#include "core/services/orders/OrderClosureApplicationService.h"
#include "core/services/ReportsPdfSupport.h"
#include "core/cloudsync/CloudSyncSettings.h"
#include "ui/tabs/reports/ReportsDialogSupport.h"
#include <QPdfWriter>
#include <QPainter>
#include <QProcess>
#include <QPointer>
#include "infrastructure/db/sqlite/OrderArchiveShardSqlSupport.h"
#include "ui/common/widgets/InvoiceImportProgressDialog.h"
#include <QtTest/QtTest>
#include "core/services/orders/PricingRecalculationService.h"
#include "core/services/orders/OrderFinancialService.h"
#include "core/services/MutationExecutor.h"
#include "core/cloudsync/CloudSyncSchemaService.h"
#include <QSqlQuery>
#include <QSqlRecord>
#include <QLoggingCategory>
#include <QSqlError>
#include <QTemporaryDir>
#include <QStandardPaths>
#include "ui/common/AdaptiveUi.h"
#include <QSettings>
#include <QFormLayout>
#include "ui/tabs/settings/SupplierConnectionPage.h"
#include <QDir>
#include "app/App.h"
#include <QTimer>
#include <QDesktopServices>
#include "ui/tabs/reports/ReportsPdfPresentation.h"
#include <QUrl>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QAction>
#include <QMenu>
#include <QContextMenuEvent>
#include "ui/common/filters/WindowsContextMenuFilter.h"
#include <QMessageBox>
#include <QScrollArea>
#include <QLabel>
#include <QScopeGuard>
#include <QTableWidget>
#include <QTreeWidget>
#include <QTableView>
#include <QToolButton>
#include <QTabWidget>
#include <QTabBar>
#include <QComboBox>
#include <QDateEdit>
#include <QLineEdit>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextTable>
#include <QTextBlock>
#include <QAbstractTextDocumentLayout>
#include <QGridLayout>
#include <QScrollBar>
#include <QStackedWidget>
#include <QHeaderView>
#include "ui/runtime/PageFreezeController.h"
#include "core/services/PurchasePaymentService.h"
#include "core/services/orders/BaySnapshot.h"
#include "core/services/settings/GeneralSettingsService.h"
#include "core/services/settings/PersonnelSettingsService.h"
#include "core/services/settings/PersonnelEmploymentHistory.h"
#include "core/services/ElitSoapRequest.h"
#include "core/services/ElitConnectionCheck.h"
#include "core/services/ElitInvoiceReader.h"
#include "core/services/ElitInvoiceStore.h"
#ifndef Q_MOC_RUN
#include "core/services/ElitInvoicePdf.h"
#include "core/services/ElitPdfOpenService.h"
#include "core/services/InvoiceReportService.h"
#endif
#include "core/services/SupplierInvoiceLedger.h"
#include "core/services/SupplierInvoiceStaging.h"
#include "core/services/ElitStartupSync.h"
#include "core/services/ElitSyncVoice.h"
#include "runtime/RuntimeSettings.h"
#include "infrastructure/db/sqlite/DbSettingsCacheSupport.h"
#include "core/util/SoundSupport.h"
#include "ui/tabs/settings/SettingsGeneralSection.h"
#include "ui/tabs/settings/SettingsPage.h"
#include "ui/tabs/settings/AccessControlPage.h"
#include "ui/tabs/settings/LabeledToggle.h"
#include "ui/tabs/reports/SupplierDebtOverview.h"
#include "core/services/SupplierDebtApplicationService.h"
#include "core/crm/CrmVehicleService.h"
#include "core/crm/CrmFinancialService.h"
#include "core/services/orders/ClientReceiptReader.h"
#include "core/crm/CrmTaskService.h"
#include "core/crm/CrmClientService.h"
#include "core/cloudsync/CloudSyncCredentials.h"
#include <QXmlStreamReader>
#include "ui/tabs/settings/SettingsIntegrationsSection.h"
#include "core/services/InvoiceRegistryService.h"
#include "ui/tabs/reports/InvoicesPage.h"
#include "ui/tabs/reports/ReportsInteractionSupport.h"
#include <QStandardItemModel>
#include "core/services/SupplierDebtReconciliationService.h"
#include "core/util/FormatUtils.h"
#include "core/services/PartsQueryService.h"
#include "core/services/WorkshopInventoryService.h"
#include "core/services/orders/PartRowPayloadBuilderService.h"
#include "core/services/ReportsDataLoader.h"
#include "core/services/AdditionalExpenseService.h"
#include "core/services/ReportsDataLoaderLocalSupport.h"
#include "core/services/orders/WorkPartsPersistenceRepositoryFacade.h"
#include "core/services/WorkshopSpareStockService.h"
#include "core/services/orders/OrderAttachmentService.h"
#include "core/import/ImportJob.h"
#include <QInputDialog>
#include "core/services/ActivityAuditService.h"
#include "core/services/AccessControlService.h"
#include "core/services/planning/PlanningRepository.h"
#include "core/services/planning/PlanningDispatch.h"
#include "ui/tabs/schedule/PlanningPage.h"
#include "ui/tabs/schedule/widgets/PlanningDayCell.h"
#include "core/services/MainWindowMetricsService.h"
#include "core/services/AppRuntimeService.h"
#include "core/perf/PerfWatchdog.h"
#include <QThread>
#include <QProgressDialog>
#include "core/services/StaffAccountService.h"
#include "ui/common/dialogs/PurchasePaymentDialog.h"
#include "ui/tabs/reports/PurchaseDebtsPage.h"
#include "ui/tabs/reports/ReportsPage.h"
#include "ui/tabs/reports/ReportDashboard.h"
#include "ui/tabs/reports/ReportsTableSupport.h"
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif
#include "ui/tabs/parts/PartsPage.h"
#include "ui/tabs/parts/PartsSupplierDashboard.h"
#include "ui/tabs/orders/CurrentOrdersPage.h"
#include "ui/tabs/orders/WorkPartsDialog.h"
#include "core/services/orders/OrderMutationFacade.h"
#include "core/services/orders/CurrentOrdersQueryService.h"
#include "ui/tabs/orders/CurrentOrdersPageModulePrelude.h"
#include "ui/tabs/history/HistoryPage.h"
#include "core/services/orders/OrderBatchMutationService.h"
#include "core/services/orders/PartPricingEngine.h"
#include "core/services/orders/OrderFinancialService.h"
#include "core/services/StatisticsSnapshotService.h"
#include <QNetworkProxy>
#include "ui/common/TabularSnapshotTableModel.h"
#include "ui/tabs/statistics/StatsPage.h"
#include <QFontDatabase>
#include "ui/tabs/inventory/WorkshopInventoryPage.h"
#include "ui/tabs/parts/PartsPageTableModelAdapter.h"
#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/db/sqlite/DbFacadeSupport.h"
#include "infrastructure/db/sqlite/RuntimeDbPreflightValidator.h"
#include "runtime/RuntimeJobDispatcher.h"
#include "ui/theme/ThemeManager.h"
#include "ui/main/ExternalDisplayController.h"
#include "ui/main/MainWindow.h"
#include "ui/splash/SplashScreen.h"
#include <QScreen>
#include <QSplitter>
#include <QListWidget>
#include <QGraphicsView>
#include <QGraphicsProxyWidget>
#include "ui/common/widgets/UnpaidBannerWidget.h"
#include "PerformanceAuditScenarios.h"

void refreshPartRowClientPricing(QTableWidget* table, int row, int changedColumn);

// Assert both diagnostics for an intentionally rejected transaction. QtTest fails
// if either expected message is absent; unrelated critical messages remain visible.
static void expectRejectedTransaction(const QString& reason)
{
    QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(
        "^DB_TRANSACTION_CONTEXT phase= rollback_ok  transactionId= [0-9a-f-]{36}  owner= TransactionRunner  operationId= tx_runner_[0-9]+  connection= carservice_main  threadId= [0-9]+  error= step_failed$"));
    QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(
        "^DB_TRANSACTION step_failed_rollback connection= carservice_main  transactionId= [0-9a-f-]{36}  rollbackOk= true  error= "
        + QRegularExpression::escape(reason) + "$"));
}

class PurchasePaymentTests final : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    QSqlDatabase db;
    QString previousWorkingDirectory;
    QStringList isolatedRuntimeProfiles;
    QUrl openedPdf;
    void preparePartsReportFixture() {
        QString error;
        QVERIFY2(WorkshopSpareStockService::ensureSchema(db, &error), qPrintable(error));
        QSqlQuery q(db);
        for (const auto& column : QStringList{"markup_percent REAL DEFAULT 0",
                 "manual_markup_override INTEGER DEFAULT 0", "pricing_calc_version INTEGER DEFAULT 1",
                 "note TEXT DEFAULT ''"}) {
            if (!db.record("work_order_parts").contains(column.section(' ',0,0)))
                QVERIFY2(q.exec("ALTER TABLE work_order_parts ADD COLUMN "+column), qPrintable(q.lastError().text()));
        }
        if (!db.record("work_orders").contains("order_year"))
            QVERIFY2(q.exec("ALTER TABLE work_orders ADD COLUMN order_year INTEGER DEFAULT 2026"), qPrintable(q.lastError().text()));
        if (!db.record("work_orders").contains("client"))
            QVERIFY2(q.exec("ALTER TABLE work_orders ADD COLUMN client TEXT DEFAULT ''"), qPrintable(q.lastError().text()));
        QVERIFY2(q.exec("CREATE VIEW IF NOT EXISTS router_work_order_parts AS SELECT * FROM work_order_parts"), qPrintable(q.lastError().text()));
        QVERIFY2(q.exec("CREATE TABLE IF NOT EXISTS work_order_part_attachments(part_id INTEGER,invoice_number TEXT,file_path TEXT,invoice_date TEXT)"), qPrintable(q.lastError().text()));
    }
    bool prepareFixtureOrderState(int number) {
        if(!DbThreadConnectionSupport::isRuntimeConnection(db.connectionName()))return true;
        if(!ensureWorkspaceOrderStateSchema(db))return false;
        QSqlQuery state(db);
        if(!state.prepare("INSERT OR IGNORE INTO workspace_order_state(order_number,order_uid,origin) SELECT number,order_uid,'native' FROM work_orders WHERE number=? AND TRIM(COALESCE(order_uid,''))<>''"))return false;
        state.addBindValue(number);if(!state.exec())return false;state.finish();
        if(!state.prepare("SELECT 1 FROM workspace_order_state s JOIN work_orders w ON w.number=s.order_number AND w.order_uid=s.order_uid WHERE s.order_number=?"))return false;
        state.addBindValue(number);return state.exec() && state.next();
    }
public slots:
    void capturePdfUrl(const QUrl& url) { openedPdf=url; }
private slots:
    void failedScopedConnectionReleasesHandleBeforeRemoval() {
        QTest::failOnWarning(QRegularExpression(".*connection.*still in use.*"));
        QString error,name;
        {
            ScopedSqliteConnection failed(dir.filePath("nonexistent-parent/failed-open.db"),"failed-open-test",&error);
            name=failed.connectionName();
            QVERIFY(!failed.isOpen());QVERIFY(!error.isEmpty());
            QVERIFY(!QSqlDatabase::contains(name));
        }
        QVERIFY(!QSqlDatabase::contains(name));
    }
    void pdfAmountsHideKopecksWithoutChangingAccounting() {
        QCOMPARE(FormatUtils::pdfMoney(193.75),QString("194"));
        QCOMPARE(FormatUtils::pdfMoney(387.5),QString("388"));
        QCOMPARE(FormatUtils::pdfMoney(933.6),QString("934"));
        QCOMPARE(FormatUtils::pdfMoney(22788.6),QString("22 789"));
        QCOMPARE(FormatUtils::pdfMoney(513.49),QString("513"));
        QCOMPARE(FormatUtils::pdfMoney(-513.5),QString("-514"));
        QCOMPARE(FormatUtils::pdfMoney(0),QString("0"));
        QCOMPARE(FormatUtils::roundMoneyValue(193.75),193.75);
        QCOMPARE(FormatUtils::moneyCents(2278860),QString("22 788,60"));
    }
    void closureStateReadsFailWithoutPartialResults() {
        const QString connection="closure-state-boundary";
        {
            auto local=QSqlDatabase::addDatabase("QSQLITE",connection);local.setDatabaseName(":memory:");QVERIFY(local.open());
            QSqlQuery q(local);QString error="old error";
            QCOMPARE(OrderClosureApplicationService::pendingOrders(local,&error),QStringList{});QVERIFY(error.isEmpty());
            QVERIFY(q.exec("CREATE TABLE work_orders(order_uid TEXT,status TEXT)"));
            QVERIFY(q.exec("CREATE TABLE work_order_parts(part_uid TEXT,order_uid TEXT,qty REAL,price REAL)"));
            QVERIFY(q.exec("CREATE TABLE work_order_services(service_uid TEXT,order_uid TEXT,qty REAL,price REAL)"));
            QVERIFY(q.exec("INSERT INTO work_orders VALUES('test-order','open')"));
            QVERIFY(q.exec("INSERT INTO work_order_parts VALUES('part','test-order',2,10)"));
            const auto state=OrderClosureApplicationService::readState(local,"test-order",&error);
            QVERIFY2(!state.isEmpty(),qPrintable(error));QVERIFY(error.isEmpty());
            QCOMPARE(state["parts"].toArray().size(),1);QCOMPARE(state["services"].toArray().size(),0);
            QVERIFY(OrderClosureApplicationService::readState(local,"missing",&error).isEmpty());QVERIFY(!error.isEmpty());
            QVERIFY(q.exec("DROP TABLE work_order_services"));
            QVERIFY(OrderClosureApplicationService::readState(local,"test-order",&error).isEmpty());QVERIFY(!error.isEmpty());
            QVERIFY(q.exec("CREATE TABLE order_closure_commands(order_uid TEXT,state TEXT)"));
            QVERIFY(q.exec("INSERT INTO order_closure_commands VALUES('second','pending'),('done','applied'),('first','pending')"));
            QCOMPARE(OrderClosureApplicationService::pendingOrders(local,&error),QStringList({"first","second"}));QVERIFY(error.isEmpty());
            QVERIFY(q.exec("ALTER TABLE order_closure_commands RENAME COLUMN state TO broken_state"));
            QVERIFY(OrderClosureApplicationService::pendingOrders(local,&error).isEmpty());QVERIFY(!error.isEmpty());
            local.close();
        }
        QSqlDatabase::removeDatabase(connection);
    }
    void closureJournalRejectsDuplicateReceipts() {
        const QString connection="closure-corrupt-response";
        {
            auto local=QSqlDatabase::addDatabase("QSQLITE",connection);local.setDatabaseName(":memory:");QVERIFY(local.open());
            QSqlQuery q(local);
            QVERIFY(q.exec("CREATE TABLE work_orders(number INTEGER,order_uid TEXT,status TEXT,closed_at TEXT)"));
            QVERIFY(q.exec("CREATE TABLE work_order_parts(part_uid TEXT,order_uid TEXT,installed_at TEXT)"));
            QVERIFY(q.exec("INSERT INTO work_orders VALUES(1,'closure-order','open','')"));
            QString error;
            const QJsonObject request{{"request_uid","closure-command"},{"order_uid","closure-order"}};
            QVERIFY(OrderClosureJournal::remember(local,request,&error));
            const QJsonObject payment{{"payment_uid","receipt-one"},{"request_uid","closure-command"},{"order_uid","closure-order"},
                {"amount_cents",1000},{"method","cash"},{"paid_on","2026-10-28"},{"actor","test"},{"recorded_ms",1}};
            QJsonObject response{{"schema_version",1},{"ok",true},{"request_uid","closure-command"},{"order_uid","closure-order"},{"status","closed"},
                {"order",QJsonObject{{"order_uid","closure-order"},{"closed_at","2026-10-28T10:00:00Z"}}},
                {"payments",QJsonArray{payment,payment}},{"received_cents",2000},{"installed_parts",QJsonArray{}}};
            QVERIFY2(!OrderClosureJournal::apply(local,response,&error),"Duplicate receipt accepted: received total differs from actual ledger");
            QVERIFY(!error.isEmpty());
            QVERIFY(q.exec("SELECT COUNT(*) FROM client_payments"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),0);q.finish();
            QVERIFY(q.exec("SELECT status FROM work_orders"));QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("open"));q.finish();
            QCOMPARE(OrderClosureJournal::pending(local,"closure-order",&error),request);
            response["payments"]=QJsonArray{};response["received_cents"]=0.5;
            QVERIFY2(!OrderClosureJournal::apply(local,response,&error),"Fractional receipt total accepted as zero cents");
            auto fractional=payment;fractional["amount_cents"]=1000.5;
            response["payments"]=QJsonArray{fractional};response["received_cents"]=1000;
            QVERIFY2(!OrderClosureJournal::apply(local,response,&error),"Fractional receipt accepted as integer cents");
            response["payments"]=QJsonArray{payment};response["received_cents"]=1000;
            QVERIFY2(OrderClosureJournal::apply(local,response,&error),qPrintable(error));
            QVERIFY2(OrderClosureJournal::apply(local,response,&error),qPrintable(error));
            QVERIFY(q.exec("SELECT COUNT(*),SUM(amount_cents) FROM client_payments"));QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(),1);QCOMPARE(q.value(1).toLongLong(),qint64(1000));q.finish();
            local.close();
        }
        QSqlDatabase::removeDatabase(connection);
    }
    void reportDoesNotJoinAnotherOrderWithSameInternalNumber() {
        QSqlQuery q(db);
        for (const auto& sql : {
            "ALTER TABLE work_order_parts ADD COLUMN markup_percent REAL DEFAULT 0",
            "ALTER TABLE work_order_parts ADD COLUMN manual_markup_override INTEGER DEFAULT 0",
            "ALTER TABLE work_order_parts ADD COLUMN pricing_calc_version INTEGER DEFAULT 3",
            "UPDATE work_orders SET status='closed',closed_at='2026-10-28' WHERE number=1",
            "CREATE VIEW router_work_order_parts AS SELECT * FROM work_order_parts",
            "DROP VIEW router_work_orders",
            "CREATE VIEW router_work_orders AS SELECT * FROM work_orders UNION ALL SELECT number,999,'other-year-order',plate,car,vin,status,opened_at,closed_at FROM work_orders"})
            QVERIFY2(q.exec(sql),qPrintable(q.lastError().text()));
        const auto totals=ReportsPdfSupport::collectClosedPartFinancialTotalsForReports("2026-10-01","2026-10-31");
        // Independent fixture: two rows belonging to order-a only: 200+100 cost, 250+200 client.
        QCOMPARE(totals.qtyTotal,3.0);
        QCOMPARE(totals.purchaseTotal,300.0);
        QCOMPARE(totals.clientTotal,450.0);
        QCOMPARE(totals.marginTotal,150.0);
        QVERIFY(q.exec("DROP VIEW router_work_orders"));
        QVERIFY(q.exec("CREATE VIEW router_work_orders AS SELECT * FROM work_orders"));
        QVERIFY(q.exec("UPDATE work_order_parts SET order_uid=NULL"));
        const auto legacy=ReportsPdfSupport::collectClosedPartFinancialTotalsForReports("2026-10-01","2026-10-31");
        QCOMPARE(legacy.purchaseTotal,300.0);QCOMPARE(legacy.clientTotal,450.0);
        QVERIFY(q.exec("UPDATE work_order_parts SET order_uid='unrelated-order'"));
        const auto unrelated=ReportsPdfSupport::collectClosedPartFinancialTotalsForReports("2026-10-01","2026-10-31");
        QCOMPARE(unrelated.qtyTotal,0.0);QCOMPARE(unrelated.clientTotal,0.0);
    }
    void reportPreservesRecordedVersionThreeMoney() {
        ReportsPdfSupport::ReportMarkupSnapshot snapshot;
        auto quote=ReportsPdfSupport::resolvePartFinancialsForReports(snapshot,"Fine quantity","Test supplier",
            1000,0.25,0.25,0.00025,100,true,3);
        QCOMPARE(quote.purchasePrice,0.00025);QCOMPARE(quote.purchaseAmount,0.25);
        QCOMPARE(quote.clientAmount,0.25);QCOMPARE(quote.added,0.0);
        quote=ReportsPdfSupport::resolvePartFinancialsForReports(snapshot,"Free replacement","Test supplier",
            1,0.0,100.0,100.0,100,true,3);
        QCOMPARE(quote.clientAmount,0.0);QCOMPARE(quote.added,-100.0);
        const auto legacy=ReportsPdfSupport::resolvePartFinancialsForReports(snapshot,"Fine quantity","Test supplier",
            1000,0.25,0.25,0.00025,0,false,1);
        QCOMPARE(legacy.purchaseAmount,0.0); // Compatibility is explicit, not retroactive.
    }
    void serviceWriteFailureKeepsRowsAndMasterLinks() {
        QSqlQuery q(db);
        for (const auto& sql : {
            "ALTER TABLE work_orders ADD COLUMN order_year INTEGER DEFAULT 2026",
            "ALTER TABLE work_orders ADD COLUMN works_sum REAL DEFAULT 0",
            "ALTER TABLE work_orders ADD COLUMN parts_sum REAL DEFAULT 0",
            "CREATE TABLE work_order_services(id INTEGER PRIMARY KEY,order_uid TEXT,order_number INTEGER,title TEXT,qty REAL,price REAL,amount REAL)",
            "CREATE TABLE service_masters(service_id INTEGER,master_id TEXT,share_percent REAL,role TEXT,master_name_closed TEXT)",
            "INSERT INTO work_order_services VALUES(10,'order-a',1,'Original',0.5,100,50)",
            "INSERT INTO service_masters VALUES(10,'m1',100,'master','Original master')",
            "CREATE TRIGGER reject_service_totals BEFORE UPDATE OF works_sum ON work_orders BEGIN SELECT RAISE(ABORT,'synthetic totals failure'); END"})
            QVERIFY2(q.exec(sql),qPrintable(q.lastError().text()));
        WorkOrderServiceRow row;row.title="New work";row.qty=0.5;row.price=100;row.amount=50;row.masterId="m2";
        QString error;int id=99;
        QVERIFY(!ServiceRepository::insert(db,1,row,&id,&error));
        QVERIFY(q.exec("SELECT COUNT(*) FROM work_order_services"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),1);q.finish();
        QCOMPARE(id,0);
        QVERIFY(!ServiceRepository::update(db,1,10,row,&error));
        QVERIFY(!ServiceRepository::remove(db,1,10,&error));
        QVERIFY(q.exec("SELECT title FROM work_order_services WHERE id=10"));QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("Original"));q.finish();
        QVERIFY(q.exec("SELECT master_id FROM service_masters WHERE service_id=10"));QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("m1"));q.finish();
        QVERIFY(q.exec("DROP TRIGGER reject_service_totals"));
        QVERIFY(!ServiceRepository::update(db,2,10,row,&error));
        QVERIFY(!ServiceRepository::remove(db,2,10,&error));
        QVERIFY(q.exec("CREATE TRIGGER reject_master_link BEFORE INSERT ON service_masters BEGIN SELECT RAISE(ABORT,'synthetic master failure'); END"));
        QVERIFY(!ServiceRepository::replaceMasterLink(db,10,"m2","New",&error));
        QVERIFY(q.exec("SELECT master_id FROM service_masters WHERE service_id=10"));QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("m1"));q.finish();
        QVERIFY(q.exec("DROP TRIGGER reject_master_link"));
        QVERIFY(db.transaction());QVERIFY(ServiceRepository::insert(db,1,row,&id,&error));QVERIFY(id>0);QVERIFY(db.rollback());
        QVERIFY(q.exec("SELECT COUNT(*) FROM work_order_services"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),1);q.finish();
    }
    void newPartPreservesSubKopeckUnitPriceOnSave() {
        QSqlQuery q(db);
        QVERIFY(q.exec("ALTER TABLE work_orders ADD COLUMN order_year INTEGER DEFAULT 2026"));
        for (const auto& sql : {
            "ALTER TABLE work_orders ADD COLUMN works_sum REAL DEFAULT 0",
            "ALTER TABLE work_orders ADD COLUMN parts_sum REAL DEFAULT 0",
            "CREATE TABLE work_order_services(id INTEGER,order_uid TEXT,order_number INTEGER,qty REAL,price REAL,amount REAL)",
            "ALTER TABLE work_order_parts ADD COLUMN note TEXT DEFAULT ''",
            "ALTER TABLE work_order_parts ADD COLUMN service_id INTEGER DEFAULT 0",
            "ALTER TABLE work_order_parts ADD COLUMN ordered_by_admin INTEGER DEFAULT 1",
            "ALTER TABLE work_order_parts ADD COLUMN markup_percent REAL DEFAULT 0",
            "ALTER TABLE work_order_parts ADD COLUMN manual_markup_override INTEGER DEFAULT 0",
            "ALTER TABLE work_order_parts ADD COLUMN markup_source TEXT DEFAULT 'policy'",
            "ALTER TABLE work_order_parts ADD COLUMN pricing_updated_at TEXT DEFAULT ''",
            "ALTER TABLE work_order_parts ADD COLUMN pricing_calc_version INTEGER DEFAULT 1"})
            QVERIFY2(q.exec(sql),qPrintable(q.lastError().text()));
        PartRowPayloadInput input; input.name="Precise unit test";input.store="Test supplier";
        input.qty=1000;input.purchasePrice=0.00025;
        auto row=PartRowPayloadBuilderService::build(input);
        QCOMPARE(row.purchasePrice,0.00025);QCOMPARE(row.purchaseAmount,0.25);
        QCOMPARE(row.clientAmount,0.25);QCOMPARE(row.pricingCalcVersion,3);
        QString error;int id=0;
        QVERIFY2(WorkPartsPersistenceRepositoryFacade::insertPart(db,1,row,&id,&error),qPrintable(error));
        q.prepare("SELECT purchase_price,purchase_amount,amount,pricing_calc_version FROM work_order_parts WHERE id=?");q.addBindValue(id);
        QVERIFY(q.exec());QVERIFY(q.next());QCOMPARE(q.value(0).toDouble(),0.00025);
        QCOMPARE(q.value(1).toDouble(),0.25);QCOMPARE(q.value(2).toDouble(),0.25);QCOMPARE(q.value(3).toInt(),3);q.finish();
        QVERIFY(q.exec("SELECT parts_sum FROM work_orders WHERE number=1"));QVERIFY(q.next());QCOMPARE(q.value(0).toDouble(),450.25);q.finish();
        for (const auto field : {QString("quantity"),QString("purchase price")}) {
            auto excessive=row;
            if(field=="quantity") excessive.qty=1000.0000001;
            else excessive.purchasePrice=0.0002501;
            error.clear();
            QVERIFY2(!WorkPartsPersistenceRepositoryFacade::insertPart(db,1,excessive,nullptr,&error),qPrintable(field+": excess precision silently accepted"));
            QVERIFY(!error.isEmpty());
        }
        row.qty=0;QVERIFY(!WorkPartsPersistenceRepositoryFacade::insertPart(db,1,row,nullptr,&error));
        QVERIFY(q.exec("SELECT COUNT(*) FROM work_order_parts"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),3);q.finish();
        QVERIFY(q.exec("CREATE TRIGGER reject_part_totals BEFORE UPDATE OF parts_sum ON work_orders BEGIN SELECT RAISE(ABORT,'synthetic totals failure'); END"));
        row.qty=1000;row.name="Must rollback";
        QVERIFY(!WorkPartsPersistenceRepositoryFacade::insertPart(db,1,row,nullptr,&error));
        QVERIFY(q.exec("SELECT COUNT(*) FROM work_order_parts"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),3);q.finish();
        QVERIFY(!WorkPartsPersistenceRepositoryFacade::updatePart(db,1,id,row,&error));
        q.prepare("SELECT name FROM work_order_parts WHERE id=?");q.addBindValue(id);QVERIFY(q.exec());QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(),QString("Precise unit test"));q.finish();
        QVERIFY(!WorkPartsPersistenceRepositoryFacade::removePart(db,1,id,&error));
        q.prepare("SELECT COUNT(*) FROM work_order_parts WHERE id=?");q.addBindValue(id);QVERIFY(q.exec());QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(),1);q.finish();
        QVERIFY(q.exec("DROP TRIGGER reject_part_totals"));
        // Successful nested writes must still roll back with the caller transaction.
        QVERIFY(db.transaction());
        int nestedId=0;
        QVERIFY2(WorkPartsPersistenceRepositoryFacade::insertPart(db,1,row,&nestedId,&error),qPrintable(error));
        QVERIFY(nestedId>0);QVERIFY(db.rollback());
        QVERIFY(q.exec("SELECT COUNT(*) FROM work_order_parts"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),3);q.finish();
        // An earlier successful row in a batch must roll back when a later row fails.
        auto invalid=row;invalid.qty=0;
        QVERIFY(!PartRepository::syncRows(db,1,{row,invalid},&error));
        QVERIFY(q.exec("SELECT name,amount FROM work_order_parts WHERE id=1"));QVERIFY(q.next());
        QVERIFY(q.value(0).toString()!=row.name);QCOMPARE(q.value(1).toDouble(),250.0);q.finish();
        QVERIFY(q.exec("SELECT parts_sum FROM work_orders WHERE number=1"));QVERIFY(q.next());
        QCOMPARE(q.value(0).toDouble(),450.25);q.finish();
        PartPricingInput legacy;legacy.qty=1000;legacy.purchasePrice=0.00025;legacy.sourceType="report";
        QCOMPARE(PartPricingEngine::calculate(legacy,{}).purchaseAmount,0.0); // Explicit compatibility, no historical rewrite.
        QVERIFY(q.exec("SELECT amount,pricing_calc_version FROM work_order_parts WHERE id=1"));QVERIFY(q.next());
        QCOMPARE(q.value(0).toDouble(),250.0);QCOMPARE(q.value(1).toInt(),1);
    }
    void legacyTokenRequiresProtectedPersistence() {
        const QString token="synthetic-token-never-sent";
        QVERIFY(Database::setSetting("settings.cloudsync.api_token",token));
        auto settings=CloudSyncSettings::load();
        QVERIFY(settings.apiToken==token); // Avoid printing credentials on assertion failures.
        const auto protectedValue=Database::getSetting("settings.cloudsync.api_token").toString();
        QVERIFY(protectedValue.startsWith("dpapi:v1:"));QVERIFY(protectedValue!=token);
        QString error;QVERIFY(CloudSyncCredentials::unprotectToken(protectedValue,&error)==token);
        QVERIFY(Database::setSetting("settings.cloudsync.api_token",token));
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TRIGGER reject_token_update BEFORE UPDATE ON settings WHEN NEW.key='settings.cloudsync.api_token' BEGIN SELECT RAISE(ABORT,'synthetic protected persistence failure'); END"));
        QVERIFY(q.exec("CREATE TRIGGER reject_token_insert BEFORE INSERT ON settings WHEN NEW.key='settings.cloudsync.api_token' BEGIN SELECT RAISE(ABORT,'synthetic protected persistence failure'); END"));
        settings=CloudSyncSettings::load();
        QVERIFY(settings.apiToken.isEmpty());QVERIFY(!settings.lastError.isEmpty());
        QVERIFY(!settings.lastError.contains(token));
        QVERIFY(Database::getSetting("settings.cloudsync.api_token").toString()==token);
        QVERIFY(q.exec("DROP TRIGGER reject_token_update"));QVERIFY(q.exec("DROP TRIGGER reject_token_insert"));
        QVERIFY(Database::setSetting("settings.cloudsync.api_token","dpapi:v1:not-valid"));
        settings=CloudSyncSettings::load();QVERIFY(settings.apiToken.isEmpty());QVERIFY(!settings.lastError.isEmpty());
    }
    void pdfPreviewCancelIsNonBlockingAndCleansRenderer() {
        const QString pdf=dir.filePath("synthetic-preview.pdf");
        {
            QPdfWriter writer(pdf);QPainter painter(&writer);
            painter.drawText(QPoint(100,200),"Isolated PDF preview test");
        }
        QVERIFY(QFileInfo(pdf).size()>0);
        for (int attempt=0;attempt<3;++attempt) {
            QWidget parent;
            auto* preview=ReportsDialogSupport::createInvoicePdfPreview(&parent,pdf);
            QVERIFY2(preview->objectName()=="orderReportPdfPreview","Bundled pdftoppm required; no skipped acceptance");
            QPointer<QProcess> renderer=qApp->findChild<QProcess*>("orderReportPdfRenderer");
            QVERIFY(renderer);
            QElapsedTimer elapsed;elapsed.start();delete preview;
            QVERIFY2(elapsed.elapsed()<500,"Closing PDF preview blocked the UI thread");
            bool heartbeat=false;QTimer::singleShot(0,this,[&]{heartbeat=true;});
            QTRY_VERIFY(heartbeat);
            QTRY_VERIFY_WITH_TIMEOUT(renderer.isNull(),5000);
        }
        QWidget parent;
        auto* preview=ReportsDialogSupport::createInvoicePdfPreview(&parent,pdf);
        QTRY_VERIFY_WITH_TIMEOUT(!preview->findChildren<QLabel*>("orderReportPdfPage").isEmpty(),10000);
        QTRY_VERIFY_WITH_TIMEOUT(qApp->findChild<QProcess*>("orderReportPdfRenderer")==nullptr,5000);
        delete preview;
        // Invalid input must also clean up its process without a late success page.
        preview=ReportsDialogSupport::createInvoicePdfPreview(&parent,dir.filePath("missing.pdf"));
        QTRY_VERIFY_WITH_TIMEOUT(qApp->findChild<QProcess*>("orderReportPdfRenderer")==nullptr,5000);
        QVERIFY(preview->findChildren<QLabel*>("orderReportPdfPage").isEmpty());
        delete preview;
    }
    void mandatoryAuditSnapshotReadFailureRollsBack() {
        QSqlQuery q(db);
        MutationExecutor::Request request;
        request.sourceModule="AuditSnapshotTest";request.entityType="work_order_part";
        request.entityId="1";request.operation="update_part";request.description="Test audit read errors";
        request.changedTables={"work_order_parts"};
        bool called=false;
        QVERIFY(q.exec("ALTER TABLE work_order_parts RENAME COLUMN id TO broken_id"));
        expectRejectedTransaction("Audit snapshot before mutation: no such column: id Unable to execute statement");
        auto result=MutationExecutor::execute(db,request,[&](QSqlDatabase&,QString*){called=true;return true;});
        QVERIFY(!result.ok);QVERIFY(!called);QVERIFY(result.errorText.contains("before mutation"));
        QVERIFY(q.exec("ALTER TABLE work_order_parts RENAME COLUMN broken_id TO id"));
        expectRejectedTransaction("Audit snapshot after mutation: no such column: id Unable to execute statement");
        result=MutationExecutor::execute(db,request,[&](QSqlDatabase& tx,QString*){
            QSqlQuery change(tx);called=true;
            return change.exec("UPDATE work_order_parts SET name='SHOULD ROLLBACK' WHERE id=1")
                && change.exec("ALTER TABLE work_order_parts RENAME COLUMN id TO broken_id");
        });
        QVERIFY(called);QVERIFY(!result.ok);QVERIFY(result.errorText.contains("after mutation"));
        QVERIFY(q.exec("SELECT name FROM work_order_parts WHERE id=1"));QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(),QString("Part"));
    }
    void archiveCleanupReportsFailureAndPreservesSource() {
        QSqlQuery q(db);
        for (const auto& sql : {
            "CREATE TABLE work_order_services(id INTEGER PRIMARY KEY,order_number INTEGER)",
            "CREATE TABLE service_masters(service_id INTEGER)",
            "UPDATE workspace_order_state SET origin='legacy_copy',source_db='',hydrated_at='',modified_at='' WHERE order_number=1 AND order_uid='order-a'",
            "CREATE TRIGGER reject_archive_cleanup BEFORE DELETE ON work_orders BEGIN SELECT RAISE(ABORT,'synthetic archive cleanup failure'); END"})
            QVERIFY2(q.exec(sql),qPrintable(q.lastError().text()));
        for (const auto& sql : {
            "CREATE TABLE work_order_part_attachments(part_id INTEGER)",
            "CREATE TABLE work_order_service_attachments(service_id INTEGER)",
            "CREATE TABLE outsource_jobs(order_number INTEGER)",
            "CREATE TABLE outsource_repairs(order_number INTEGER)",
            "CREATE TABLE router_cache_service_masters(service_id INTEGER)",
            "CREATE TABLE router_cache_outsource_repairs(order_number INTEGER)",
            "CREATE TABLE router_cache_work_order_parts(order_number INTEGER)",
            "CREATE TABLE router_cache_work_order_services(id INTEGER,order_number INTEGER)",
            "CREATE TABLE router_cache_work_orders(number INTEGER)"})
            QVERIFY2(q.exec(sql),qPrintable(q.lastError().text()));
        QString error;
        expectRejectedTransaction("synthetic archive cleanup failure Unable to fetch row");
        QVERIFY(!OrderArchiveShardSqlSupport::purgeHydratedArchiveCopiesFromActive(db,&error));
        QVERIFY2(error.contains("synthetic archive cleanup failure"),qPrintable(error));
        QVERIFY(q.exec("SELECT COUNT(*) FROM work_orders WHERE number=1"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),1);q.finish();
        QVERIFY(q.exec("SELECT COUNT(*),SUM(amount) FROM work_order_parts WHERE order_number=1"));QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(),2);QCOMPARE(q.value(1).toDouble(),450.0);q.finish();
        QVERIFY(q.exec("SELECT COUNT(*) FROM workspace_order_state WHERE order_number=1"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),1);q.finish();
    }
    void archiveTransferJournalSurvivesRestartAndRollback() {
        const QString name="archive-journal-restart", path=dir.filePath("archive-journal.db");
        QString operation,error;
        {
            auto local=QSqlDatabase::addDatabase("QSQLITE",name);local.setDatabaseName(path);QVERIFY(local.open());
            QVERIFY(ArchiveTransferJournal::begin(local,"stable-uid",7,"test-shard.db",&error));
            QSqlQuery q(local);QVERIFY(q.exec("SELECT operation_id FROM archive_transfer_journal_v1"));
            QVERIFY(q.next());operation=q.value(0).toString();q.finish();
            QVERIFY(!ArchiveTransferJournal::advance(local,"stable-uid","routed",&error));
            QVERIFY(ArchiveTransferJournal::advance(local,"stable-uid","copied",&error));
            QVERIFY(ArchiveTransferJournal::advance(local,"stable-uid","verified",&error));
            local.close();
        }
        QSqlDatabase::removeDatabase(name);
        {
            auto local=QSqlDatabase::addDatabase("QSQLITE",name);local.setDatabaseName(path);QVERIFY(local.open());
            QVERIFY(ArchiveTransferJournal::begin(local,"stable-uid",7,"test-shard.db",&error));
            QVERIFY(!ArchiveTransferJournal::begin(local,"stable-uid",8,"other.db",&error));
            QSqlQuery q(local);QVERIFY(q.exec("SELECT operation_id,phase FROM archive_transfer_journal_v1"));
            QVERIFY(q.next());QCOMPARE(q.value(0).toString(),operation);QCOMPARE(q.value(1).toString(),QString("verified"));q.finish();
            QVERIFY(ArchiveTransferJournal::advance(local,"stable-uid","routed",&error));
            QVERIFY(local.transaction());
            QVERIFY(ArchiveTransferJournal::advance(local,"stable-uid","complete",&error));
            QVERIFY(local.rollback());
            QVERIFY(q.exec("SELECT phase FROM archive_transfer_journal_v1"));QVERIFY(q.next());
            QCOMPARE(q.value(0).toString(),QString("routed"));q.finish();
            QVERIFY(local.transaction());QVERIFY(ArchiveTransferJournal::advance(local,"stable-uid","complete",&error));QVERIFY(local.commit());
            QVERIFY(ArchiveTransferJournal::begin(local,"stable-uid",7,"test-shard.db",&error));
            QVERIFY(q.exec("SELECT operation_id,phase FROM archive_transfer_journal_v1"));QVERIFY(q.next());
            QVERIFY(q.value(0).toString()!=operation);QCOMPARE(q.value(1).toString(),QString("prepared"));q.finish();
            local.close();
        }
        QSqlDatabase::removeDatabase(name);
    }
    void archiveVerificationRejectsIncompleteGraph() {
        QSqlQuery q(db); QString error;
        QVERIFY(q.exec("CREATE TABLE work_order_services(id INTEGER,order_number INTEGER,title TEXT)"));
        QVERIFY(q.exec("INSERT INTO work_order_services VALUES(10,1,'Test work')"));
        QVERIFY(q.exec("CREATE TABLE service_masters(service_id INTEGER,master_id TEXT)"));
        QVERIFY(q.exec("INSERT INTO service_masters VALUES(10,'m1')"));
        QVERIFY(q.exec("ATTACH DATABASE ':memory:' AS archive_rollover"));
        for (const auto& table : {"work_orders","work_order_services","work_order_parts","service_masters"})
            QVERIFY(q.exec(QString("CREATE TABLE archive_rollover.%1 AS SELECT * FROM main.%1").arg(table)));
        auto verify = [&] { error.clear(); return OrderArchiveShardSqlSupport::verifyCopiedOrderGraph(db,1,&error); };
        QVERIFY2(verify(),qPrintable(error));
        QVERIFY(q.exec("UPDATE archive_rollover.work_order_parts SET amount=251 WHERE id=1"));
        QVERIFY(!verify()); QVERIFY(error.contains("work_order_parts"));
        QVERIFY(q.exec("UPDATE archive_rollover.work_order_parts SET amount=250 WHERE id=1"));
        QVERIFY(q.exec("INSERT INTO archive_rollover.service_masters VALUES(10,'m1')"));
        QVERIFY(!verify()); QVERIFY(error.contains("service_masters"));
        QVERIFY(q.exec("DELETE FROM archive_rollover.service_masters WHERE rowid=(SELECT MAX(rowid) FROM archive_rollover.service_masters)"));
        QVERIFY2(verify(),qPrintable(error));
        QVERIFY(q.exec("CREATE TABLE work_order_part_attachments(part_id INTEGER,file_path TEXT)"));
        QVERIFY(q.exec("INSERT INTO work_order_part_attachments VALUES(1,'test-invoice.pdf')"));
        QVERIFY(!verify()); QVERIFY(error.contains("work_order_part_attachments"));
        QVERIFY(q.exec("CREATE TABLE archive_rollover.work_order_part_attachments AS SELECT * FROM main.work_order_part_attachments"));
        QVERIFY2(verify(),qPrintable(error));
        QVERIFY(q.exec("ALTER TABLE work_order_services ADD COLUMN discount_mode TEXT"));
        QVERIFY(!verify()); QVERIFY(error.contains("discount_mode"));
        QVERIFY(q.exec("ALTER TABLE archive_rollover.work_order_services ADD COLUMN discount_mode TEXT"));
        QVERIFY2(verify(),qPrintable(error));
        QVERIFY(q.exec("UPDATE archive_rollover.work_order_services SET discount_mode=''"));
        QVERIFY(!verify()); // NULL is not the empty string.
        QVERIFY(q.exec("SELECT COUNT(*),SUM(amount) FROM work_order_parts"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),2); QCOMPARE(q.value(1).toDouble(),450.0);q.finish();
        QVERIFY(!OrderArchiveShardSqlSupport::verifyCopiedOrderGraph(db,999,&error));
        QVERIFY(q.exec("CREATE TABLE outsource_jobs(id INTEGER PRIMARY KEY,order_number INTEGER,amount REAL)"));
        QVERIFY(q.exec("INSERT INTO outsource_jobs VALUES(40,1,123.45)"));
        QVERIFY(q.exec("CREATE TABLE outsource_repairs(id INTEGER PRIMARY KEY,order_number INTEGER,note TEXT)"));
        QVERIFY(q.exec("INSERT INTO outsource_repairs VALUES(41,1,'repair')"));
        QVERIFY(q.exec("CREATE TABLE work_order_service_attachments(service_id INTEGER,file_path TEXT)"));
        QVERIFY(q.exec("INSERT INTO work_order_service_attachments VALUES(10,'photo.jpg')"));
        QVERIFY(db.transaction());
        QVERIFY2(OrderArchiveShardSqlSupport::copyCompleteOrderGraph(db,1,&error),qPrintable(error));
        QVERIFY(db.commit());
        QVERIFY2(verify(),qPrintable(error));
        QVERIFY(q.exec("SELECT amount FROM archive_rollover.outsource_jobs WHERE id=40"));
        QVERIFY(q.next());QCOMPARE(q.value(0).toDouble(),123.45);q.finish();
        // Retry keeps one copy and restores source NULL rather than an empty string.
        QVERIFY(db.transaction());
        QVERIFY2(OrderArchiveShardSqlSupport::copyCompleteOrderGraph(db,1,&error),qPrintable(error));
        QVERIFY(db.commit());
        QVERIFY(q.exec("SELECT COUNT(*) FROM archive_rollover.work_order_service_attachments"));
        QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),1);q.finish();
        QVERIFY(q.exec("UPDATE archive_rollover.work_orders SET order_uid='another-order' WHERE number=1"));
        QVERIFY(db.transaction());
        QVERIFY(!OrderArchiveShardSqlSupport::copyCompleteOrderGraph(db,1,&error));
        QVERIFY(error.contains("identity conflict"));QVERIFY(db.rollback());
        QVERIFY(q.exec("SELECT order_uid FROM archive_rollover.work_orders WHERE number=1"));
        QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("another-order"));q.finish();
        expectRejectedTransaction("Archive verification failed; source retained: work_orders: source and archive differ");
        QVERIFY(!purgeVerifiedArchivedOrderGraph(db,1,&error));
        QVERIFY(error.contains("source and archive differ"));
        QVERIFY(q.exec("SELECT COUNT(*) FROM work_orders WHERE number=1"));
        QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),1);q.finish();
        QVERIFY(q.exec("SELECT COUNT(*) FROM work_order_part_attachments"));
        QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),1);q.finish();
        QVERIFY(q.exec("UPDATE work_orders SET status='closed' WHERE number=1"));
        QVERIFY(q.exec("UPDATE archive_rollover.work_orders SET order_uid=(SELECT order_uid FROM main.work_orders WHERE number=1),status='closed' WHERE number=1"));
        QVERIFY(q.exec("SELECT order_uid FROM work_orders WHERE number=1"));QVERIFY(q.next());
        const auto uid=q.value(0).toString();q.finish();
        QVERIFY(ArchiveTransferJournal::begin(db,uid,1,"test-shard",&error));
        for(const auto& phase:{"copied","verified","routed"})QVERIFY(ArchiveTransferJournal::advance(db,uid,phase,&error));
        for(const auto& sql:{
            "CREATE TABLE router_cache_service_masters(service_id INTEGER)",
            "CREATE TABLE router_cache_outsource_repairs(order_number INTEGER)",
            "CREATE TABLE router_cache_work_order_parts(order_number INTEGER)",
            "CREATE TABLE router_cache_work_order_services(id INTEGER,order_number INTEGER)",
            "CREATE TABLE router_cache_work_orders(number INTEGER)"})QVERIFY(q.exec(sql));
        QVERIFY(q.exec("CREATE TRIGGER stop_archive_purge BEFORE DELETE ON work_orders BEGIN SELECT RAISE(ABORT,'synthetic final delete failure'); END"));
        expectRejectedTransaction("synthetic final delete failure Unable to fetch row");
        QVERIFY(!purgeVerifiedArchivedOrderGraph(db,1,&error));
        QVERIFY2(error.contains("synthetic final delete failure"),qPrintable(error));
        QVERIFY(q.exec("SELECT phase FROM archive_transfer_journal_v1"));QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(),QString("routed"));q.finish();
        QVERIFY(q.exec("SELECT COUNT(*) FROM work_order_part_attachments"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),1);q.finish();
        QVERIFY(q.exec("DROP TRIGGER stop_archive_purge"));
        QVERIFY2(purgeVerifiedArchivedOrderGraph(db,1,&error),qPrintable(error));
        QVERIFY(q.exec("SELECT phase FROM archive_transfer_journal_v1"));QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(),QString("complete"));q.finish();
        QVERIFY(q.exec("SELECT COUNT(*) FROM work_orders WHERE number=1"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),0);q.finish();
        QVERIFY(q.exec("SELECT COUNT(*) FROM archive_rollover.work_order_part_attachments"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),1);q.finish();
        QVERIFY(q.exec("DETACH DATABASE archive_rollover"));
    }
    void modelTableContextDeleteUsesBusinessAction() {
        QTableView table; QStandardItemModel model(1,1);
        model.setData(model.index(0,0),"Test stock"); table.setModel(&model);
        table.setSelectionBehavior(QAbstractItemView::SelectRows);table.show();
        table.selectRow(0);
        auto* action=new QAction(&table);action->setObjectName("contextRowDeleteAction");
        int calls=0;connect(action,&QAction::triggered,&table,[&]{++calls;});
        WindowsContextMenuFilter filter;table.viewport()->installEventFilter(&filter);
        bool enabled=false;
        QTimer::singleShot(0,&table,[&]{
            auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());
            if(!menu)return;
            for(auto* candidate:menu->actions())if(candidate->shortcut()==QKeySequence::Delete){
                enabled=candidate->isEnabled();menu->setActiveAction(candidate);
                QTest::keyClick(menu,Qt::Key_Return);return;
            }
            menu->close();
        });
        const auto pos=table.visualRect(model.index(0,0)).center();
        QContextMenuEvent event(QContextMenuEvent::Mouse,pos,table.viewport()->mapToGlobal(pos));
        QApplication::sendEvent(table.viewport(),&event);
        QVERIFY(enabled);QCOMPARE(calls,1);QCOMPARE(model.rowCount(),1);
        table.viewport()->removeEventFilter(&filter);
    }
    void moneyPrecisionAndDiscountModes() {
        QCOMPARE(FormatUtils::roundMoneyValue(1.005), 1.01);
        QCOMPARE(FormatUtils::roundMoneyValue(-1.005), -1.01);
        QCOMPARE(FormatUtils::roundMoneyValue(2.675), 2.68);
        QCOMPARE(FormatUtils::roundMoneyValue(1.0049), 1.00);
        QCOMPARE(FormatUtils::roundMoneyValue(300 * (500.0 / 300)), 500.00);
        QCOMPARE(FormatUtils::discountAmount(50,"percent",1000),500.0);
        QCOMPARE(FormatUtils::discountAmount(50,"uah",1000),50.0);
        QCOMPARE(FormatUtils::discountAmount(50,"legacy",1000),50.0);
        QCOMPARE(FormatUtils::discountPercent(50,"legacy",1000),5.0);
        QCOMPARE(FormatUtils::discountAmount(12.5,"percent",1000.10),125.01);
    }
    void financialReturnsRefundAndCredit() {
        QSqlQuery q(db);QString error;
        QVERIFY(q.exec("CREATE TABLE masters(id TEXT PRIMARY KEY,name TEXT,active INTEGER,role TEXT)"));
        QVERIFY(q.exec("INSERT INTO masters VALUES('m1','Audit',1,'master')"));
        for(const auto& settlement:QStringList{"refund","credit"}) {
            WorkshopSpareOrder item;item.name="Audit return";item.brand="Audi";item.model="Q7";
            item.plate="AA 1234 AA";item.masterId="m1";item.supplier="Audit supplier";
            item.orderedOn=QDate(2026,10,28);item.quantity=2;item.unitCost=125.25;
            qint64 id=0;QVERIFY2(WorkshopSpareStockService::addOrder(db,item,&id,&error,{},false),qPrintable(error));
            auto reserve=WorkshopSpareStockService::orders(db,&error).first();
            QVERIFY2(WorkshopSpareStockService::receive(db,reserve,2,QDate(2026,10,28),100.50,&error,false),qPrintable(error));
            reserve=WorkshopSpareStockService::orders(db,&error).first();
            QCOMPARE(reserve.debtCents,qint64(15000));
            QVERIFY(!WorkshopSpareStockService::returnReserve(db,reserve,{},{},&error,settlement,1));
            QVERIFY2(WorkshopSpareStockService::returnReserve(db,reserve,{},{},&error,settlement,10050),qPrintable(error));
            QVERIFY(WorkshopSpareStockService::orders(db,&error).isEmpty());
            QVERIFY(!WorkshopSpareStockService::returnReserve(db,reserve,{},{},&error,settlement,10050));
            q.prepare("SELECT paid_cents,settlement FROM workshop_spare_returns WHERE spare_id=?");q.addBindValue(id);
            QVERIFY(q.exec());QVERIFY(q.next());QCOMPARE(q.value(0).toLongLong(),qint64(10050));QCOMPARE(q.value(1).toString(),settlement);q.finish();
            error.clear();
            const auto debts=PurchasePaymentService::debts(db,&error);QVERIFY2(error.isEmpty(),qPrintable(error));
            for(const auto& debt:debts)QVERIFY(debt.supplier!="Audit supplier");
        }
    }
    void financialIndependentDecimalOracle() {
        const auto path=qEnvironmentVariable("CS_FINANCIAL_ORACLE");
        if(path.isEmpty())QSKIP("Explicit independent oracle fixture required");
        QFile fixture(path);QVERIFY(fixture.open(QIODevice::ReadOnly));
        const auto oracle=QJsonDocument::fromJson(fixture.readAll()).object();
        QJsonArray evidence;QStringList failures;
        auto check=[&](QString id,QString metric,qint64 expected,double actual){
            const qint64 observed=qRound64(actual*100);
            evidence.append(QJsonObject{{"scenario",id},{"metric",metric},{"expected_cents",expected},{"actual_cents",observed},{"difference_cents",observed-expected}});
            if(expected!=observed)failures<<id+"/"+metric;
        };
        for(const auto& value:oracle["rows"].toArray()) {
            const auto row=value.toObject();PartPricingInput input;
            input.partName="Audit part";input.store="Audit supplier";
            input.qty=row["qty"].toString().toDouble();input.purchasePrice=row["cost"].toString().toDouble();
            input.manualMarkupOverride=true;input.manualMarkupPercent=row["markup"].toString().toDouble();
            const auto actual=PartPricingEngine::calculate(input,{});
            const auto id=row["id"].toString();
            // Unit prices retain precision; only extended monetary lines are cents.
            // Compare the independent Decimal unit values before cent rounding.
            QVERIFY(row.contains("purchase_price_decimal"));
            QVERIFY(row.contains("client_price_decimal"));
            const double expectedPurchase=row["purchase_price_decimal"].toString().toDouble();
            const double expectedClient=row["client_price_decimal"].toString().toDouble();
            QVERIFY2(qAbs(actual.purchasePrice-expectedPurchase)<0.0000001,qPrintable(id+"/purchase_price_decimal"));
            QVERIFY2(qAbs(actual.clientPrice-expectedClient)<0.0000001,qPrintable(id+"/client_price_decimal"));
            check(id,"purchase_amount",row["purchase_amount"].toInteger(),actual.purchaseAmount);
            check(id,"client_amount",row["client_amount"].toInteger(),actual.clientAmount);
        }
        QSqlQuery q(db);
        QVERIFY(q.exec("UPDATE work_order_parts SET qty=1,price=298.50,amount=298.50 WHERE id=1"));
        QVERIFY(q.exec("DELETE FROM work_order_parts WHERE id=2"));
        QVERIFY(q.exec("ALTER TABLE work_orders ADD COLUMN order_year INTEGER DEFAULT 2026"));
        QVERIFY(q.exec("CREATE TABLE work_order_services(id INTEGER,order_number INTEGER,order_uid TEXT,qty REAL,price REAL,amount REAL)"));
        check("one_invoice_line","order_parts_total",29850,OrderFinancialService::partsTotal(db,1));
        check("discount_50_of_1000","discount_percent",5000,FormatUtils::discountPercent(50,QStringLiteral("percent"),1000));
        const QStringList setup={
            "ALTER TABLE work_orders ADD COLUMN client TEXT",
            "ALTER TABLE work_orders ADD COLUMN works_sum REAL",
            "ALTER TABLE work_orders ADD COLUMN parts_sum REAL",
            "ALTER TABLE work_orders ADD COLUMN discount REAL",
            "ALTER TABLE work_orders ADD COLUMN discount_mode TEXT DEFAULT 'percent'",
            "UPDATE work_orders SET client='Audit',status='closed',closed_at='2026-10-28',works_sum=1000.10,parts_sum=298.50,discount=10",
            "UPDATE work_order_parts SET purchase_price=100,purchase_amount=100",
            "CREATE VIEW router_work_order_parts AS SELECT * FROM work_order_parts",
            "INSERT INTO work_order_services VALUES(1,1,'order-a',1,1000.10,1000.10)",
            "CREATE VIEW router_work_order_services AS SELECT * FROM work_order_services",
            "CREATE TABLE masters(id TEXT,name TEXT,admin_percent REAL)",
            "INSERT INTO masters VALUES('audit','Audit',33.33)",
            "CREATE TABLE router_service_masters(service_id INTEGER,master_id TEXT,share_percent REAL)",
            "INSERT INTO router_service_masters VALUES(1,'audit',100)",
            "CREATE TABLE router_outsource_repairs(sent_at TEXT,client_price REAL,repair_cost REAL)",
            "INSERT INTO router_outsource_repairs VALUES('2026-10-28',600,400)",
            "INSERT INTO expenses(id,period_from,period_to,rent) VALUES(1,'2026-10-28','2026-10-28',50)"
        };
        for(const auto& sql:setup)QVERIFY2(q.exec(sql),qPrintable(q.lastError().text()));
        QString error;QVERIFY2(AdditionalExpenseService::add(db,QDate(2026,10,28),"Audit","Delivery",25,nullptr,&error),qPrintable(error));
        const auto stats=StatisticsSnapshotService::collectFromPath(db.databaseName(),QDate(2026,10,28),QDate(2026,10,28));
        // Independent hand/Decimal ledger: works 1000.10 less 10%, payroll 33.33%, outsource margin 200, expenses 50+25.
        check("discounted_order","statistics_labor",90009,stats.revenueLabor);
        check("discounted_order","statistics_parts",29850,stats.revenueParts);
        check("discounted_order","statistics_cost",10000,stats.purchaseParts);
        check("discounted_order","statistics_payroll",30000,stats.payroll);
        check("discounted_order","statistics_outsource_margin",20000,stats.outsourceMargin);
        check("discounted_order","statistics_expenses",7500,stats.expenses);
        check("discounted_order","statistics_net_profit",92359,stats.netProfit);
        WorkshopInventoryPurchaseInput receipt;receipt.purchasedOn=QDate(2026,10,1);receipt.materialName="Audit oil";
        receipt.unit=QString::fromUtf8("л");receipt.quantity=10;receipt.unitCost=100;receipt.paidAmount=0;receipt.publishCommitted=false;
        QVERIFY2(WorkshopInventoryService::addPurchase(db,receipt,nullptr,&error),qPrintable(error));
        receipt.purchasedOn=QDate(2026,10,2);receipt.quantity=5;receipt.unitCost=160;
        QVERIFY2(WorkshopInventoryService::addPurchase(db,receipt,nullptr,&error),qPrintable(error));
        QVERIFY(q.exec("UPDATE work_order_parts SET name='Audit oil',store='Склад СТО',qty=3,installed_at='2026-10-03'"));
        const auto inventory=WorkshopInventoryService::buildSnapshot(db,QDate(2026,10,1),QDate(2026,10,28));
        QVERIFY2(inventory.errorText.isEmpty(),qPrintable(inventory.errorText));QCOMPARE(inventory.balanceRows.size(),1);QCOMPARE(inventory.consumptions.size(),1);
        check("weighted_average_10x100_5x160_issue3","inventory_average",12000,inventory.balanceRows[0].averageCost);
        check("weighted_average_10x100_5x160_issue3","inventory_remaining_value",144000,inventory.balanceRows[0].stockValue);
        check("weighted_average_10x100_5x160_issue3","inventory_consumption",36000,inventory.consumptions[0].totalCost);
        QFile output(qEnvironmentVariable("CS_FINANCIAL_ACTUAL"));QVERIFY(output.open(QIODevice::WriteOnly));
        output.write(QJsonDocument(evidence).toJson());output.close();
        QVERIFY2(failures.isEmpty(),qPrintable(failures.join(", ")));
    }
    void historyReopenPreservesPaymentsAndInstalledParts() {
        QSqlQuery q(db);
        QVERIFY(q.exec("UPDATE work_orders SET status='closed',closed_at='2026-10-28' WHERE number=1"));
        QVERIFY(q.exec("UPDATE work_order_parts SET installed_at='2026-10-27'"));
        QVERIFY(q.exec("CREATE TABLE client_payments(payment_uid TEXT PRIMARY KEY,order_uid TEXT,amount_cents INTEGER)"));
        QVERIFY(q.exec("INSERT INTO client_payments VALUES('payment-1','order-a',500000)"));
        auto snapshot=[&](const QString& table){QStringList rows;QSqlQuery s(db);if(!s.exec("SELECT * FROM "+table))return rows;while(s.next()){QStringList values;for(int i=0;i<s.record().count();++i)values<<s.value(i).toString();rows<<values.join('|');}return rows;};
        const auto parts=snapshot("work_order_parts"), payments=snapshot("client_payments");
        auto result=OrderBatchMutationService::reopen(1,"in_progress");
        QVERIFY2(result.ok,qPrintable(result.errorText));
        QVERIFY(q.exec("SELECT status,closed_at FROM work_orders WHERE number=1"));QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(),QString("in_progress"));QVERIFY(q.value(1).isNull());q.finish();
        expectRejectedTransaction(QString::fromUtf8("Наряд уже змінено або видалено. Оновіть список."));
        QVERIFY(!OrderBatchMutationService::reopen(1,"open").ok); // stale/repeated selection
        QCOMPARE(snapshot("work_order_parts"),parts);QCOMPARE(snapshot("client_payments"),payments);
        QVERIFY(q.exec("UPDATE work_orders SET status='completed',closed_at='2026-10-28' WHERE number=1"));
        QVERIFY(q.exec("CREATE TRIGGER fail_reopen BEFORE UPDATE ON work_orders BEGIN SELECT RAISE(ABORT,'write failed'); END"));
        expectRejectedTransaction("write failed Unable to fetch row");
        QVERIFY(!OrderBatchMutationService::reopen(1,"open").ok);
        QVERIFY(q.exec("SELECT status FROM work_orders WHERE number=1"));QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("completed"));q.finish();
        QVERIFY(q.exec("DROP TRIGGER fail_reopen"));
        result=OrderBatchMutationService::reopen(1,"open");QVERIFY2(result.ok,qPrintable(result.errorText));
        QCOMPARE(snapshot("work_order_parts"),parts);QCOMPARE(snapshot("client_payments"),payments);
        for(const auto& name:QSqlDatabase::connectionNames())
            if(name!=db.connectionName())QSqlDatabase::database(name,false).close();
    }
    void supplierConnectionLoginLayout() {
        ThemeManager::applySavedTheme();
        QString error;
        const auto before=PurchasePaymentService::balances(db,&error);QVERIFY(error.isEmpty());
        for(const auto& supplier:{QString("utr"),QString("bm"),QString("omega")}) {
            QScopedPointer<QWidget> page(buildSupplierConnectionPage(supplier,nullptr));
            page->resize(1100,650);page->show();QTest::qWait(100);
            QVERIFY(page->findChildren<QSpinBox*>().isEmpty());
            auto* form=page->findChild<QFormLayout*>();QVERIFY(form);
            auto* label=qobject_cast<QLabel*>(form->itemAt(0,QFormLayout::LabelRole)->widget());
            auto* login=qobject_cast<QLineEdit*>(form->itemAt(0,QFormLayout::FieldRole)->widget());
            QVERIFY(label);QVERIFY(login);QVERIFY(label->isVisible());
            if(supplier=="utr")QCOMPARE(label->text(),QString::fromUtf8("Логін (email)"));
            QVERIFY(!label->geometry().intersects(login->geometry()));
            QVERIFY(label->width()>=label->fontMetrics().horizontalAdvance(label->text()));
            const auto output=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
            if(supplier=="utr" && !output.isEmpty())QVERIFY(page->grab().save(QDir(output).filePath("supplier-login.png")));
        }
        error.clear();QCOMPARE(PurchasePaymentService::balances(db,&error),before);QVERIFY(error.isEmpty());
    }
    void reportPartsMeasuredSpacing() {
        preparePartsReportFixture();
        const auto initial = ReportsDataLoader::buildPartsSnapshot(db.databaseName(), "2026-10-29", "2026-11-07", 0, 0, {}, {});
        QVERIFY2(initial.error.isEmpty(), qPrintable(initial.error));
        ReportsPage page;page.resize(1500,800);page.show();
        auto* tabs=page.findChild<QTabWidget*>();QVERIFY(tabs);
        tabs->setCurrentIndex(3);
        auto* dashboard=page.findChild<QWidget*>("reportDashboard");QVERIFY(dashboard);
        auto* stock=page.findChild<QWidget*>("reportPartsTile2");QVERIFY(stock);
        QLabel* body=nullptr;
        for(auto* label:stock->findChildren<QLabel*>())
            if(label->textFormat()==Qt::RichText)body=label;
        QVERIFY(body);
        QTRY_VERIFY_WITH_TIMEOUT(body->text().contains(QString::fromUtf8("На складі:")),15000);
        QTest::qWait(200);
        QVERIFY(dashboard->isVisible());
        QVERIFY(!body->text().contains("REPORT_"));
        QTextDocument rendered;rendered.setDefaultFont(body->font());rendered.setHtml(body->text());
        const auto caption=rendered.find(QString::fromUtf8("На складі:"));QVERIFY(!caption.isNull());
        auto* table=caption.currentTable();QVERIFY(table);QCOMPARE(table->columns(),3);
        QVERIFY(body->text().contains("width='"+QString::number(qRound(5.0*page.logicalDpiX()/25.4))+"'"));
        for(int width:{1500,1800,1500}) {
            page.resize(width,800);QTest::qWait(100);
            QRect previous;
            for(int i=0;i<4;++i) {
                auto* tile=page.findChild<QWidget*>(QString("reportPartsTile%1").arg(i));QVERIFY(tile);
                QVERIFY(tile->isVisible());
                QVERIFY(!previous.intersects(tile->geometry()));previous=tile->geometry();
                QVERIFY(tile->parentWidget()->rect().contains(tile->geometry()));
            }
            QVERIFY(body->height()>=body->heightForWidth(body->width()));
            QVERIFY(tabs->y()>=dashboard->mapTo(&page,QPoint(0,dashboard->height())).y());
        }
        const int y=tabs->y();tabs->setCurrentIndex(0);QTest::qWait(100);tabs->setCurrentIndex(3);QTest::qWait(150);
        QCOMPARE(tabs->y(),y);
        const auto output=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
        if(!output.isEmpty())QVERIFY(page.grab().save(QDir(output).filePath("reports-measured.png")));
    }
    void partsHeaderCompactLayout() {
        PartsPage page;page.resize(1500,800);page.show();page.refresh();
        auto* filters=page.findChild<QWidget*>("partsFilterFrame");
        auto* host=page.findChild<QWidget*>("partsToolbarHost");
        QVERIFY(filters);QVERIFY(host);
        QTest::qWait(1500);
        for(int width:{1500,1800,1500}) {
            page.resize(width,800);QTest::qWait(150);
            QCOMPARE(host->height(),qRound(15.0*host->logicalDpiY()/25.4));
            QList<QRect> occupied;
            for(auto* tile:host->findChildren<QWidget*>(QString(),Qt::FindDirectChildrenOnly)) {
                if(!tile->property("partsSupplierTile").toBool())continue;
                QCOMPARE(tile->height(),host->height());
                QVERIFY(host->rect().contains(tile->geometry()));
                for(const auto& rect:occupied)QVERIFY(!rect.intersects(tile->geometry()));
                occupied.append(tile->geometry());
                for(auto* label:tile->findChildren<QLabel*>("partsSupplierAmount"))
                    QVERIFY(label->height()>=label->heightForWidth(label->width()));
            }
            QVERIFY(occupied.size()>=3); // period, search, total, plus suppliers
            for(auto* date:filters->findChildren<QDateEdit*>())
                QVERIFY(date->width()>=date->fontMetrics().horizontalAdvance("00.00.00")+52);
        }
        const auto output=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
        if(!output.isEmpty())QVERIFY(page.grab().save(QDir(output).filePath("parts-compact.png")));
    }
    void elitCabinetPaymentsWorkingCopyAudit() {
        const auto fixture=qEnvironmentVariable("CS_ECAT_PAYMENT_FIXTURE");
        if(fixture.isEmpty() || qEnvironmentVariable("CS_TAB_AUDIT_COPY").isEmpty())QSKIP("Requires isolated copy and read-only cabinet observations");
        QVERIFY(QFileInfo(db.databaseName()).absoluteFilePath().startsWith(App::sessionDir()));
        QFile file(fixture);QVERIFY(file.open(QIODevice::ReadOnly));
        const auto observations=QJsonDocument::fromJson(file.readAll()).array();QVERIFY(!observations.isEmpty());
        QString error;const auto balances=PurchasePaymentService::balances(db,&error);QVERIFY(error.isEmpty());
        QSqlQuery q(db);QVERIFY(q.exec("SELECT account FROM elit_invoice_snapshots LIMIT 1"));QVERIFY(q.next());
        const auto account=q.value(0).toString();q.finish();
        const auto original=ElitInvoiceStore::load(db,account,&error);QVERIFY(error.isEmpty());
        ElitInvoiceReader::Result result;result.complete=true;result.fetchedAt=QDateTime::currentDateTime();
        QSet<QString> checked;
        for(const auto& document:original.documents) {
            auto invoice=document.invoice;
            for(const auto& value:observations) {
                const auto object=value.toObject();const auto cells=object.value("cells").toArray();
                if(cells.size()!=4 || cells[1].toString()!=invoice.number)continue;
                QString html="<table><tr>";
                for(const auto& header:object.value("headers").toArray())html+="<th>"+header.toString().toHtmlEscaped()+"</th>";
                html+="</tr><tr>";
                for(const auto& cell:cells)html+="<td>"+cell.toString().toHtmlEscaped()+"</td>";
                html+="</tr></table>";
                qint64 remaining=-1;
                QVERIFY(ElitCabinet::paymentBalance(html,invoice,&remaining));
                invoice.remainingCents=remaining;checked.insert(document.identity);
            }
            result.invoices.append(invoice);
        }
        QCOMPARE(checked.size(),observations.size());
        QVERIFY(ElitInvoiceStore::publish(db,account,result,original.revision,&error));
        const auto ledger=SupplierInvoiceLedger::load(db,account);QVERIFY(ledger.error.isEmpty());
        for(const auto& entry:ledger.entries)if(checked.contains(entry.key)) {
            QCOMPARE(entry.remaining,qint64(0));QVERIFY(!entry.missingFromLatest);
        }
        QCOMPARE(ElitInvoiceStore::load(db,account).documents.size(),original.documents.size());
        QCOMPARE(PurchasePaymentService::balances(db,&error),balances);QVERIFY(error.isEmpty());
    }
    void elitMissingInvoicesWorkingCopyAudit() {
        if(qEnvironmentVariable("CS_TAB_AUDIT_COPY").isEmpty())QSKIP("Requires isolated working-data copy");
        QVERIFY(QFileInfo(db.databaseName()).absoluteFilePath().startsWith(App::sessionDir()));
        QString error;
        const auto balances=PurchasePaymentService::balances(db,&error);QVERIFY(error.isEmpty());
        QSqlQuery q(db);QVERIFY(q.exec("SELECT account FROM elit_invoice_snapshots LIMIT 1"));
        QVERIFY(q.next());const auto account=q.value(0).toString();q.finish();
        const auto original=ElitInvoiceStore::load(db,account,&error);QVERIFY(error.isEmpty());
        QVERIFY(original.documents.size()>1);
        const auto missing=original.documents.first();
        ElitInvoiceReader::Result result;result.complete=true;result.fetchedAt=QDateTime::currentDateTime();
        for(int i=1;i<original.documents.size();++i)result.invoices.append(original.documents[i].invoice);
        QVERIFY(ElitInvoiceStore::publish(db,account,result,original.revision,&error));
        auto retained=ElitInvoiceStore::load(db,account,&error);QVERIFY(error.isEmpty());
        QCOMPARE(retained.documents.size(),original.documents.size());
        bool found=false;
        for(const auto& doc:retained.documents)if(doc.identity==missing.identity) {
            found=true;QVERIFY(doc.missingFromLatest);
            QCOMPARE(doc.invoice.remainingCents,missing.invoice.remainingCents);
            QCOMPARE(doc.lastSeen,missing.lastSeen);
        }
        QVERIFY(found);
        const auto ledger=SupplierInvoiceLedger::load(db,account);QVERIFY(ledger.error.isEmpty());
        QVERIFY(ledger.hasUnresolved);
        for(const auto& entry:ledger.entries)if(entry.key==missing.identity)QVERIFY(entry.missingFromLatest);
        QVERIFY(ElitInvoiceStore::publish(db,account,result,retained.revision,&error));
        retained=ElitInvoiceStore::load(db,account);
        QCOMPARE(retained.documents.size(),original.documents.size());
        result.invoices.append(missing.invoice);result.invoices.last().remainingCents=0;
        QVERIFY(ElitInvoiceStore::publish(db,account,result,retained.revision,&error));
        const auto restored=ElitInvoiceStore::load(db,account);
        for(const auto& doc:restored.documents)if(doc.identity==missing.identity) {
            QVERIFY(!doc.missingFromLatest);QCOMPARE(doc.invoice.remainingCents,qint64(0));
        }
        result.invoices.last().posted=result.invoices.last().posted.addDays(1);
        QVERIFY(!ElitInvoiceStore::publish(db,account,result,restored.revision,&error));
        QCOMPARE(ElitInvoiceStore::load(db,account).revision,restored.revision);
        error.clear();QCOMPARE(PurchasePaymentService::balances(db,&error),balances);QVERIFY(error.isEmpty());
    }
    void statisticsPartsMovementWorkingCopyAudit() {
        if(qEnvironmentVariable("CS_TAB_AUDIT_COPY").isEmpty())QSKIP("Requires isolated working-data copy");
        QVERIFY(QFileInfo(db.databaseName()).absoluteFilePath().startsWith(App::sessionDir()));
        QString error;
        const auto before=PurchasePaymentService::balances(db,&error); QVERIFY(error.isEmpty());
        StatsPage page; page.resize(1400,850); page.show(); page.refresh();
        const auto dates=page.findChildren<QDateEdit*>(); QCOMPARE(dates.size(),2);
        dates[0]->setDate(QDate(2026,1,1)); dates[1]->setDate(QDate(2026,10,25));
        page.refresh();
        auto* tabs=page.findChild<QTabWidget*>(); QVERIFY(tabs);
        int partsTab=-1;
        for(int i=0;i<tabs->count();++i)
            if(tabs->tabText(i)==QString::fromUtf8("Рух запчастин")) partsTab=i;
        QVERIFY(partsTab>=0);
        QElapsedTimer timer; timer.start();
        tabs->setCurrentIndex(partsTab);
        const auto elapsed=timer.elapsed();
        qInfo()<<"PARTS_MOVEMENT_OPEN elapsed_ms="<<elapsed;
        QVERIFY2(elapsed<2000,qPrintable(QString("Parts movement took %1 ms").arg(elapsed)));
        const auto rows=page.property("partRows").toList(); QVERIFY(!rows.isEmpty());
        QTableView* table=tabs->currentWidget()->findChild<QTableView*>(); QVERIFY(table);
        QCOMPARE(table->model()->rowCount(),rows.size());
        bool hasInvoice=false;
        for(const auto& value:rows) if(!value.toMap().value("invoice").toString().isEmpty()) hasInvoice=true;
        QVERIFY(hasInvoice);
        tabs->setCurrentIndex(0); tabs->setCurrentIndex(partsTab);
        QCOMPARE(page.property("partRows").toList(),rows);
        const auto output=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
        if(!output.isEmpty()) QVERIFY(page.grab().save(QDir(output).filePath("parts-movement.png")));
        QCOMPARE(PurchasePaymentService::balances(db,&error),before); QVERIFY(error.isEmpty());
    }
    void statisticsCurrentRosterWorkingCopyAudit() {
        if(qEnvironmentVariable("CS_TAB_AUDIT_COPY").isEmpty())QSKIP("Requires isolated working-data copy");
        QVERIFY(QFileInfo(db.databaseName()).absoluteFilePath().startsWith(App::sessionDir()));
        StatsPage page;page.refresh();
        const auto orders=page.property("performanceOrders").toList();
        const auto period=ReportsDataLoader::buildPeriodSnapshot(db.databaseName(),"1900-01-01","2099-12-31");
        QSqlQuery q(db);
        QVERIFY(q.exec("SELECT id FROM masters WHERE role='master' LIMIT 1"));
        QVERIFY(q.next());const auto id=q.value(0).toString();q.finish();
        QVERIFY(q.exec("UPDATE masters SET active=0"));
        page.markDirty();page.refresh();
        QVERIFY(page.property("performanceStaff").toList().isEmpty());
        QCOMPARE(page.property("currentMasterCount").toInt(),0);
        QCOMPARE(page.property("performanceOrders").toList(),orders);
        q.prepare("UPDATE masters SET active=1,active_from='',active_to='',name='ROSTER TEST' WHERE id=?");
        q.addBindValue(id);QVERIFY(q.exec());
        page.markDirty();page.refresh();
        const auto staff=page.property("performanceStaff").toList();
        QCOMPARE(staff.size(),1);QCOMPARE(staff[0].toMap().value("id").toString(),id);
        QCOMPARE(page.property("currentMasterCount").toInt(),1);
        QVERIFY(q.exec("UPDATE masters SET active=1,active_from='',active_to='' WHERE role='admin'"));
        page.markDirty();page.refresh();
        QCOMPARE(page.property("currentMasterCount").toInt(),1);
        QCOMPARE(staff[0].toMap().value("name").toString(),QString("ROSTER TEST"));
        QCOMPARE(page.property("performanceOrders").toList(),orders);
        const auto after=ReportsDataLoader::buildPeriodSnapshot(db.databaseName(),"1900-01-01","2099-12-31");
        QCOMPARE(after.rows.size(),period.rows.size());QCOMPARE(after.stats,period.stats);
    }
    void periodReportIncludesDismissedWorkingCopyAudit() {
        if(qEnvironmentVariable("CS_TAB_AUDIT_COPY").isEmpty())QSKIP("Requires isolated working-data copy");
        QVERIFY(QFileInfo(db.databaseName()).absoluteFilePath().startsWith(App::sessionDir()));
        QSqlQuery q(db);
        QVERIFY(q.exec("SELECT m.id FROM masters m JOIN router_work_orders wo ON wo.master_id=m.id LIMIT 1"));
        QVERIFY(q.next());const auto id=q.value(0).toString();q.finish();
        const auto from=QString("1900-01-01"),to=QString("2099-12-31");
        const auto before=ReportsDataLoader::buildPeriodSnapshot(db.databaseName(),from,to);
        QVERIFY(!before.rows.isEmpty());
        QVERIFY(q.exec("UPDATE masters SET active=0"));
        const auto dismissed=ReportsDataLoader::buildPeriodSnapshot(db.databaseName(),from,to);
        QCOMPARE(dismissed.rows.size(),before.rows.size());
        QCOMPARE(dismissed.stats,before.stats);
        q.prepare("UPDATE masters SET active=1,active_from='',name='CURRENT ROSTER TEST' WHERE id=?");
        q.addBindValue(id);QVERIFY(q.exec());
        const auto active=ReportsDataLoader::buildPeriodSnapshot(db.databaseName(),from,to);
        QCOMPARE(active.rows.size(),before.rows.size());
        QCOMPARE(active.stats,before.stats);
        q.prepare("UPDATE masters SET active_from=? WHERE id=?");
        q.addBindValue(QDate::currentDate().addDays(1).toString(Qt::ISODate));q.addBindValue(id);QVERIFY(q.exec());
        const auto rehired=ReportsDataLoader::buildPeriodSnapshot(db.databaseName(),from,to);
        QCOMPARE(rehired.rows.size(),before.rows.size());
        QCOMPARE(rehired.stats,before.stats);
    }
    void debtRegisterOmitsInstallationColumns() {
        PurchaseDebt debt;debt.source="part";debt.id=1;debt.orderUid="order-a";
        debt.orderNumber=1;debt.name="Test part";debt.supplier="Store";debt.totalCents=20000;
        const auto html=PurchaseDebtAnalysisPage::analysisHtml({debt},{},QDate::currentDate(),{},0,{}, {},2);
        QVERIFY(!html.contains(QString::fromUtf8(">Встановлення</th>")));
        QVERIFY(!html.contains(QString::fromUtf8("До встановлення / очікування, дн")));
        QVERIFY(html.contains("colspan='10'"));
        QVERIFY(!html.contains("colspan='12'"));
        QTextDocument document;document.setHtml(html);
        bool registry=false;
        for(auto it=document.rootFrame()->begin();!it.atEnd();++it) {
            if(auto* table=qobject_cast<QTextTable*>(it.currentFrame())) {
                if(table->cellAt(0,0).firstCursorPosition().block().text()!=QString::fromUtf8("Магазин"))continue;
                QCOMPARE(table->columns(),10);registry=true;
                QCOMPARE(table->cellAt(0,9).firstCursorPosition().block().text(),QString::fromUtf8("Джерело"));
            }
        }
        QVERIFY(registry);
    }
    void debtAnalysisResponsiveWorkingCopyAudit() {
        if(qEnvironmentVariable("CS_TAB_AUDIT_COPY").isEmpty())QSKIP("Requires isolated working-data copy");
        QVERIFY(QFileInfo(db.databaseName()).absoluteFilePath().startsWith(App::sessionDir()));
        ThemeManager::applySavedTheme();
        auto dataSnapshot=[&] {
            QMap<QString,QList<QByteArray>> result;
            for(const auto& table:db.tables()) {
                QSqlQuery query(db);
                if(!query.exec("SELECT * FROM \""+table+"\""))return QMap<QString,QList<QByteArray>>{};
                auto& rows=result[table];
                while(query.next()) {
                    QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);
                    for(int i=0;i<query.record().count();++i)stream<<query.value(i);
                    rows.append(bytes);
                }
                std::sort(rows.begin(),rows.end());
            }
            return result;
        };
        const auto before=dataSnapshot();QVERIFY(!before.isEmpty());
        PurchaseDebtAnalysisPage page;page.resize(1400,850);page.show();
        auto* report=page.findChild<QTextBrowser*>("debtAnalysisReport");QVERIFY(report);
        auto* pdf=page.findChild<QPushButton*>("debtAnalysisPdf");QVERIFY(pdf);
        auto* views=page.findChild<QTabBar*>("debtAnalysisViews");QVERIFY(views);
        int completed=0;page.summaryChanged=[&](const QString& html){if(!html.isEmpty())++completed;};
        QElapsedTimer elapsed;elapsed.start();
        page.refresh();
        const auto dispatchMs=elapsed.elapsed();
        QCOMPARE(completed,0);
        QVERIFY2(dispatchMs<500,qPrintable(QString("UI dispatch took %1 ms").arg(dispatchMs)));
        bool heartbeat=false;QTimer::singleShot(0,&page,[&]{heartbeat=true;});
        views->setCurrentIndex(2);
        QTRY_VERIFY(heartbeat);
        QTRY_COMPARE_WITH_TIMEOUT(completed,1,30000);
        QTRY_VERIFY_WITH_TIMEOUT(pdf->isEnabled(),30000);
        QVERIFY(!report->toPlainText().contains(QString::fromUtf8("Не вдалося")));
        QVERIFY(!report->toPlainText().contains(QString::fromUtf8("Завантаження аналізу")));
        const auto firstReport=report->toPlainText();
        QVERIFY(!firstReport.isEmpty());
        qInfo()<<"DEBT_ANALYSIS_OPEN dispatch_ms="<<dispatchMs<<"loaded_ms="<<elapsed.elapsed();
        page.refresh();page.refresh();
        QTRY_COMPARE_WITH_TIMEOUT(completed,2,30000);
        QTRY_VERIFY_WITH_TIMEOUT(pdf->isEnabled(),30000);
        QCOMPARE(report->toPlainText(),firstReport);
        views->setCurrentIndex(3);
        auto* ledger=page.findChild<QTableView*>("supplierInvoiceLedger"); QVERIFY(ledger);
        QTRY_VERIFY_WITH_TIMEOUT(ledger->model()->rowCount()>0,30000);
        auto* overview=page.findChild<QWidget*>("supplierDebtOverview"); QVERIFY(overview);
        for(int i=0;i<10;++i) { page.resize(1400+i,850); QCoreApplication::processEvents(); }
        QVERIFY(ledger->model()->rowCount()>0);
        const auto output=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
        if(!output.isEmpty())QVERIFY(page.grab().save(QDir(output).filePath("debt-analysis-responsive.png")));
        QCOMPARE(dataSnapshot(),before);
    }
    void personnelPostsWorkingCopyAudit() {
        if(qEnvironmentVariable("CS_TAB_AUDIT_COPY").isEmpty())QSKIP("Requires isolated working-data copy");
        ThemeManager::applySavedTheme();
        if(qEnvironmentVariable("CS_UI_AUDIT_LIGHT")=="1")ThemeManager::setTheme("light");
        QVERIFY(QFileInfo(db.databaseName()).absoluteFilePath().startsWith(App::sessionDir()));
        auto financialRows=[&] {
            QMap<QString,QList<QByteArray>> result;
            for(const auto& table:db.tables()) {
                if(!table.contains("payment") && !table.contains("salary") && !table.contains("payroll") && !table.startsWith("workshop_") && !table.startsWith("work_order") && table!="service_masters" && table!="masters")continue;
                QSqlQuery query(db);if(!query.exec("SELECT * FROM \""+table+"\""))return QMap<QString,QList<QByteArray>>{};
                auto& rows=result[table];
                while(query.next()) {
                    QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);
                    for(int i=0;i<query.record().count();++i)stream<<query.value(i);
                    rows.append(bytes);
                }
                std::sort(rows.begin(),rows.end());
            }
            return result;
        };
        const auto financialBefore=financialRows();QVERIFY(!financialBefore.isEmpty());
        const auto payrollBefore=Database::payrollPercents();
        const auto orderPostsBefore=OrderMutationFacade::workshopPosts();
        QString error;
        const auto before=PersonnelSettingsService::loadWorkPostAssignments(&error);QVERIFY(error.isEmpty());
        const auto people=PersonnelSettingsService::loadPersonnel(true);QVERIFY(people.size()>=2);
        auto findButton=[](QWidget* page,const QString& text)->QPushButton* {
            for(auto* button:page->findChildren<QPushButton*>())if(button->isVisible() && button->text()==text)return button;
            return nullptr;
        };
        auto choiceFor=[](QTableWidget* table,const QString& id)->QComboBox* {
            for(int row=0;row<table->rowCount();++row)
                if(table->item(row,0) && table->item(row,0)->data(Qt::UserRole).toString()==id)
                    return qobject_cast<QComboBox*>(table->cellWidget(row,2));
            return nullptr;
        };
        SettingsPage page;page.resize(1500,850);page.show();QTest::qWait(150);
        auto* personnel=findButton(&page,QString::fromUtf8("Персонал"));QVERIFY(personnel);personnel->click();
        auto* posts=findButton(&page,QString::fromUtf8("Працівники за постами"));QVERIFY(posts);
        const auto output=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
        if(!output.isEmpty())QVERIFY(page.grab().save(QDir(output).filePath("personnel-menu.png")));
        posts->click();QTest::qWait(150);
        auto* table=page.findChild<QTableWidget*>("personnelPostsTable");QVERIFY(table && table->isVisible());
        const int originalRows=table->rowCount();
        WindowsContextMenuFilter contextFilter;
        table->viewport()->installEventFilter(&contextFilter);
        bool menuSeen=false,deleteDisabled=false;
        QTimer::singleShot(0,&page,[&]{
            if(auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
                menuSeen=true;
                for(auto* action:menu->actions())if(action->shortcut()==QKeySequence::Delete)deleteDisabled=!action->isEnabled();
                menu->close();
            }
        });
        const QPoint position=table->visualItemRect(table->item(1,0)).center();
        QContextMenuEvent contextEvent(QContextMenuEvent::Mouse,position,table->viewport()->mapToGlobal(position));
        QApplication::sendEvent(table->viewport(),&contextEvent);
        QVERIFY(menuSeen);QVERIFY(deleteDisabled);QCOMPARE(table->rowCount(),originalRows);
        table->viewport()->removeEventFilter(&contextFilter);
        // Even a programmatic/inadvertent removal must not save a partial roster.
        for(int row=0;row<table->rowCount();++row)if(table->cellWidget(row,2)){table->removeRow(row);break;}
        page.findChild<QPushButton*>("savePersonnelPosts")->click();
        QCOMPARE(PersonnelSettingsService::loadWorkPostAssignments(&error),before);
        QVERIFY(page.findChild<QLabel*>("personnelPostsStatus")->text().contains(QString::fromUtf8("неповний")));
        page.findChild<QPushButton*>("cancelPersonnelPosts")->click();
        QCOMPARE(table->rowCount(),originalRows);
        auto* first=choiceFor(table,people[0].id);QVERIFY(first);first->setCurrentIndex(first->findData(3));
        QCOMPARE(PersonnelSettingsService::loadWorkPostAssignments(&error),before);
        page.findChild<QPushButton*>("cancelPersonnelPosts")->click();
        QCOMPARE(choiceFor(table,people[0].id)->currentData().toInt(),before.value(people[0].id,0));
        first=choiceFor(table,people[0].id);first->setCurrentIndex(first->findData(3));
        auto* second=choiceFor(table,people[1].id);QVERIFY(second);second->setCurrentIndex(second->findData(1));
        page.findChild<QPushButton*>("savePersonnelPosts")->click();
        auto saved=PersonnelSettingsService::loadWorkPostAssignments(&error);QVERIFY(error.isEmpty());
        QCOMPARE(saved.value(people[0].id),3);QCOMPARE(saved.value(people[1].id),1);
        QVERIFY(!PersonnelSettingsService::saveWorkPostAssignments({{people[0].id,2},{"missing-person",1}},&error));
        QVERIFY(!error.isEmpty());QCOMPARE(PersonnelSettingsService::loadWorkPostAssignments(&error),saved);
        QVERIFY(!PersonnelSettingsService::saveWorkPostAssignments({{people[0].id,4}},&error));
        QCOMPARE(PersonnelSettingsService::loadWorkPostAssignments(&error),saved);
        SettingsPage reopened;reopened.resize(1500,850);reopened.show();QTest::qWait(150);
        findButton(&reopened,QString::fromUtf8("Персонал"))->click();
        findButton(&reopened,QString::fromUtf8("Працівники за постами"))->click();QTest::qWait(150);
        auto* restored=reopened.findChild<QTableWidget*>("personnelPostsTable");QVERIFY(restored);
        QCOMPARE(choiceFor(restored,people[0].id)->currentData().toInt(),3);
        QCOMPARE(choiceFor(restored,people[1].id)->currentData().toInt(),1);
        for(auto* combo:restored->findChildren<QComboBox*>()) {
            QVERIFY(combo->width()>=combo->fontMetrics().horizontalAdvance(combo->currentText())+40);
            QVERIFY(combo->height()>=combo->fontMetrics().height()+8);
        }
        if(!output.isEmpty())QVERIFY(reopened.grab().save(QDir(output).filePath("personnel-posts.png")));
        QCOMPARE(financialRows(),financialBefore);
        QCOMPARE(Database::payrollPercents(),payrollBefore);
        QCOMPARE(OrderMutationFacade::workshopPosts(),orderPostsBefore);
        const QString hiredName="Personnel lifecycle copy test";
        bool hireSeen=false;
        QTimer::singleShot(50,&reopened,[&]{
            if(auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
                hireSeen=true;
                dialog->findChild<QLineEdit*>("employeeName")->setText(hiredName);
                if(!output.isEmpty())dialog->grab().save(QDir(output).filePath("personnel-hire.png"));
                dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
            }
        });
        reopened.findChild<QPushButton*>("addPersonnelEmployee")->click();QVERIFY(hireSeen);
        MasterRow hired;for(const auto& p:Database::masters(true))if(p.name==hiredName)hired=p;
        QVERIFY(!hired.id.isEmpty());QVERIFY(hired.active);QCOMPARE(hired.activeFrom,QDate::currentDate().toString(Qt::ISODate));
        QVERIFY(!PersonnelSettingsService::hireEmployee(hiredName,"master",hired.activeFrom,&error));
        auto* hiredPost=choiceFor(restored,hired.id);QVERIFY(hiredPost);hiredPost->setCurrentIndex(hiredPost->findData(3));
        reopened.findChild<QPushButton*>("savePersonnelPosts")->click();
        QCOMPARE(PersonnelSettingsService::loadWorkPostAssignments(&error).value(hired.id),3);
        for(int r=0;r<restored->rowCount();++r)if(restored->item(r,0)->data(Qt::UserRole).toString()==hired.id)restored->setCurrentCell(r,0);
        QTimer::singleShot(50,&reopened,[]{if(auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))box->button(QMessageBox::Cancel)->click();});
        reopened.findChild<QPushButton*>("dismissPersonnelEmployee")->click();QVERIFY(choiceFor(restored,hired.id));
        QTimer::singleShot(50,&reopened,[]{if(auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))box->button(QMessageBox::Yes)->click();});
        reopened.findChild<QPushButton*>("dismissPersonnelEmployee")->click();
        QVERIFY(!choiceFor(restored,hired.id));
        for(const auto& p:Database::masters(true))QVERIFY(p.id!=hired.id);
        bool historyPreserved=false;for(const auto& p:Database::mastersForPeriod(hired.activeFrom,hired.activeFrom))if(p.id==hired.id){historyPreserved=true;QVERIFY(!p.active);QCOMPARE(p.activeTo,hired.activeFrom);}
        QVERIFY(historyPreserved);
        reopened.findChild<QCheckBox*>("showDismissedPersonnel")->setChecked(true);
        bool dismissedShown=false;for(int r=0;r<restored->rowCount();++r)if(restored->item(r,0)->data(Qt::UserRole).toString()==hired.id){dismissedShown=true;QCOMPARE(restored->item(r,3)->text(),QString::fromUtf8("Звільнено"));}
        QVERIFY(dismissedShown);
        auto selectEmployee=[&](const QString& id){for(int r=0;r<restored->rowCount();++r)if(restored->item(r,0)->data(Qt::UserRole).toString()==id){restored->setCurrentCell(r,0);return true;}return false;};
        QVERIFY(selectEmployee(hired.id));
        bool editSeen=false;
        QTimer::singleShot(50,&reopened,[&]{if(auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())){
            editSeen=true;dialog->findChild<QLineEdit*>("employeeName")->setText(hiredName+" edited");
            if(!output.isEmpty())dialog->grab().save(QDir(output).filePath("personnel-edit.png"));
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
        }});
        reopened.findChild<QPushButton*>("editPersonnelEmployee")->click();QVERIFY(editSeen);
        QVERIFY(!PersonnelEmploymentHistory::edit(hired,"Stale edit","master",&error));
        QVERIFY(selectEmployee(hired.id));
        QTimer::singleShot(50,&reopened,[]{if(auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()))dialog->reject();});
        reopened.findChild<QPushButton*>("reinstatePersonnelEmployee")->click();QVERIFY(!choiceFor(restored,hired.id));
        QTimer::singleShot(50,&reopened,[&]{if(auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())){
            if(!output.isEmpty())dialog->grab().save(QDir(output).filePath("personnel-reinstate.png"));
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
        }});
        reopened.findChild<QPushButton*>("reinstatePersonnelEmployee")->click();QVERIFY(choiceFor(restored,hired.id));
        QCOMPARE(PersonnelEmploymentHistory::load(hired.id,&error)["periods"].toArray().size(),2);
        QVERIFY(selectEmployee(hired.id));
        bool historySeen=false;
        QTimer::singleShot(50,&reopened,[&]{if(auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())){
            historySeen=dialog->findChild<QTableWidget*>("employmentPeriods")->rowCount()==2;
            if(!output.isEmpty())dialog->grab().save(QDir(output).filePath("personnel-history.png"));dialog->reject();
        }});
        reopened.findChild<QPushButton*>("personnelEmploymentHistory")->click();QVERIFY(historySeen);
        const auto today=QDate::currentDate();
        QVERIFY(PersonnelSettingsService::hireEmployee("Employment gap test","master",today.addDays(-30).toString(Qt::ISODate),&error));
        QString gapId;for(const auto& p:Database::masters(true))if(p.name=="Employment gap test")gapId=p.id;
        QVERIFY(!gapId.isEmpty());
        QVERIFY(PersonnelEmploymentHistory::setActive(gapId,false,today.addDays(-20).toString(Qt::ISODate),&error));
        QVERIFY(!PersonnelEmploymentHistory::setActive(gapId,true,today.addDays(-21).toString(Qt::ISODate),&error));
        const auto gapBefore=PersonnelEmploymentHistory::load(gapId);
        QSqlQuery trigger(db);QVERIFY(trigger.exec("CREATE TEMP TRIGGER block_employment_history BEFORE INSERT ON settings WHEN NEW.key='settings.staff.employment."+gapId+"' BEGIN SELECT RAISE(ABORT,'test rollback'); END"));
        QVERIFY(!PersonnelEmploymentHistory::setActive(gapId,true,today.addDays(-5).toString(Qt::ISODate),&error));
        QVERIFY(trigger.exec("DROP TRIGGER block_employment_history"));
        QCOMPARE(PersonnelEmploymentHistory::load(gapId),gapBefore);
        for(const auto& p:Database::masters(true))QVERIFY(p.id!=gapId);
        QVERIFY(PersonnelEmploymentHistory::setActive(gapId,true,today.addDays(-5).toString(Qt::ISODate),&error));
        auto containsGap=[&](QDate from,QDate to){for(const auto& p:Database::mastersForPeriod(from.toString(Qt::ISODate),to.toString(Qt::ISODate)))if(p.id==gapId)return true;return false;};
        QVERIFY(containsGap(today.addDays(-28),today.addDays(-22)));
        QVERIFY(!containsGap(today.addDays(-19),today.addDays(-6)));
        QVERIFY(containsGap(today.addDays(-4),today));
        QString readError;
        const auto roster=ReportsDataLoader::LocalSupport::loadMastersForPeriodForReports(db,
            today.addDays(-19).toString(Qt::ISODate),today.addDays(-6).toString(Qt::ISODate),&readError);
        QVERIFY2(readError.isEmpty(),qPrintable(readError));
        for(const auto& person:roster)QVERIFY(person.id!=gapId);
        QVERIFY(PersonnelEmploymentHistory::setActive(gapId,false,today.toString(Qt::ISODate),&error));
        QCOMPARE(PersonnelEmploymentHistory::load(gapId)["periods"].toArray().size(),2);
        if(!output.isEmpty())QVERIFY(reopened.grab().save(QDir(output).filePath("personnel-roster.png")));
        auto afterLifecycle=financialRows();auto beforeLifecycle=financialBefore;
        afterLifecycle.remove("masters");beforeLifecycle.remove("masters");QCOMPARE(afterLifecycle,beforeLifecycle);
        QCOMPARE(Database::payrollPercents(),payrollBefore);
    }
    void invoiceSortPreferencePersists() {
        QSettings settings;settings.remove("reports/invoices/sortMode");
        {
            InvoicesPage page;
            auto* sort=page.findChild<QComboBox*>("invoiceSort");QVERIFY(sort);
            QCOMPARE(sort->currentText(),QString::fromUtf8("За датою ↑"));
            sort->setCurrentIndex(1);
            QCOMPARE(sort->currentText(),QString::fromUtf8("За датою ↓"));
        }
        {
            InvoicesPage page;
            auto* sort=page.findChild<QComboBox*>("invoiceSort");
            QCOMPARE(sort->currentText(),QString::fromUtf8("За датою ↓"));
            sort->setCurrentIndex(3);
        }
        {
            InvoicesPage page;
            QCOMPARE(page.findChild<QComboBox*>("invoiceSort")->currentText(),QString::fromUtf8("Найбільший термін"));
        }
        settings.remove("reports/invoices/sortMode");
    }
    void invoiceReportSortsByOverdueDeadline() {
        const QDate today(2026,10,23);
        auto make=[](const QString& key,const QDate& date,const QDate& due,qint64 paid) {
            RegisteredInvoice invoice;invoice.key=key;invoice.number=key;invoice.date=date;
            invoice.dueDate=due;invoice.totalCents=10000;invoice.paidCents=paid;return invoice;
        };
        auto oldest=make("oldest",QDate(2026,10,20),QDate(2026,10,10),2000);
        auto overdue=make("overdue",QDate(2026,9,1),QDate(2026,10,21),0);
        auto fallback=make("fallback",QDate(2026,10,10),{},0);
        auto future=make("future",QDate(2026,10,1),QDate(2026,10,30),0);
        auto paid=make("paid",QDate(2026,9,1),QDate(2026,9,8),10000);
        auto unknown=make("unknown",{},{},0);
        QVector<RegisteredInvoice> data{paid,future,overdue,unknown,fallback,oldest};
        using Mode=InvoiceRegistryService::ReportSort;
        auto sorted=InvoiceRegistryService::sortedForReport(data,Mode::LongestOverdue,today);
        QStringList keys;for(const auto& invoice:sorted)keys<<invoice.key;
        QCOMPARE(keys,QStringList({"oldest","fallback","overdue","future","unknown","paid"}));
        sorted=InvoiceRegistryService::sortedForReport(data,Mode::NoDebt,today);
        QCOMPARE(sorted.size(),1);QCOMPARE(sorted.first().key,QString("paid"));
        QCOMPARE(InvoiceRegistryService::sortedForReport(data,Mode::Date,today).first().key,QString("oldest"));
        sorted=InvoiceRegistryService::sortedForReport(data,Mode::DateAscending,today);
        QCOMPARE(sorted.first().key,QString("overdue"));
        QCOMPARE(sorted.last().key,QString("unknown"));
        PurchaseDebt part;part.totalCents=10000;part.dueDate=QDate(2026,10,19);
        fallback.parts.append(part);
        QCOMPARE(InvoiceRegistryService::paymentDeadline(fallback),part.dueDate);
        fallback.dueDate=QDate(2026,10,15);
        QCOMPARE(InvoiceRegistryService::paymentDeadline(fallback),fallback.dueDate);
        QCOMPARE(data.first().paidCents,qint64(10000));
    }
    void remoteDebtReportingPolicy() {
        SupplierInvoiceLedger::Ledger ledger;
        ledger.snapshot.revision="complete";
        ElitInvoiceStore::Document document;
        document.invoice.currency="UAH";document.invoice.grossCents=50000;document.invoice.remainingCents=35000;
        ledger.snapshot.documents={document};
        PurchaseDebt elit,other;elit.supplier="ELIT";elit.totalCents=11900;
        other.supplier="Other";other.totalCents=5000;
        auto result=SupplierInvoiceLedger::summarize(ledger,{elit,other},{});
        QCOMPARE(result.totals.value(QString::fromUtf8("Еліт")),qint64(35000));
        QCOMPARE(result.totals.value("Other"),qint64(5000));QCOMPARE(result.count,2);
        ledger.snapshot.documents[0].invoice.remainingCents=0;
        result=SupplierInvoiceLedger::summarize(ledger,{elit,other},{});
        QCOMPARE(result.totals.value(QString::fromUtf8("Еліт")),qint64(0));QCOMPARE(result.count,1);
        ledger.snapshot.documents[0].invoice.currency="EUR";
        result=SupplierInvoiceLedger::summarize(ledger,{elit,other},{});
        QVERIFY(!result.remote);QCOMPARE(result.totals.value(QString::fromUtf8("Еліт")),qint64(11900));
    }
    void workshopPostPersistence() {
        QString error;
        const auto before=PurchasePaymentService::balances(db,&error);QVERIFY(error.isEmpty());
        QVERIFY2(OrderMutationFacade::setWorkshopPost(1,3,&error),qPrintable(error));
        QCOMPARE(OrderMutationFacade::workshopPosts().value("order-a"),3);
        QVERIFY(!OrderMutationFacade::setWorkshopPost(1,4,&error));
        expectRejectedTransaction(QString::fromUtf8("Наряд недоступний для зміни поста."));
        QVERIFY(!OrderMutationFacade::setWorkshopPost(99999,1,&error));
        QCOMPARE(OrderMutationFacade::workshopPosts().value("order-a"),3);
        db.close();QVERIFY(db.open());
        QCOMPARE(OrderMutationFacade::workshopPosts().value("order-a"),3);
        QVERIFY(OrderMutationFacade::setWorkshopPost(1,0,&error));
        QCOMPARE(OrderMutationFacade::workshopPosts().value("order-a"),0);
        error.clear();QCOMPARE(PurchasePaymentService::balances(db,&error),before);QVERIFY(error.isEmpty());
    }
    void reportInvoiceBackgroundWorkingCopyAudit() {
        if(qEnvironmentVariable("CS_TAB_AUDIT_COPY").isEmpty())QSKIP("Requires isolated working-data copy");
        ThemeManager::applySavedTheme();
        if(qEnvironmentVariable("CS_UI_AUDIT_LIGHT")=="1")ThemeManager::setTheme("light");
        QVERIFY(QFileInfo(db.databaseName()).absoluteFilePath().startsWith(App::sessionDir()));
        QString error;
        const auto before=PurchasePaymentService::balances(db,&error);QVERIFY(error.isEmpty());
        ReportsPage page;page.resize(1500,800);
        const QColor background(ThemeManager::currentTheme()=="light"?"#f6f8fb":"#526070");
        page.setStyleSheet(page.styleSheet()+"#reportsPage {background:"+background.name()+";}");
        page.show();
        auto* filters=page.findChild<QWidget*>("reportsFiltersFrame");QVERIFY(filters);
        const auto dates=filters->findChildren<QDateEdit*>();QCOMPARE(dates.size(),2);
        dates[0]->setDate(QDate(2026,10,1));dates[1]->setDate(QDate(2026,10,23));
        auto* tabs=page.findChild<QTabWidget*>();QVERIFY(tabs);
        auto* invoices=page.findChild<QWidget*>("invoicesPage");QVERIFY(invoices);
        QPushButton* refresh=nullptr;
        for(auto* button:invoices->findChildren<QPushButton*>())
            if(button->text()==QString::fromUtf8("Оновити"))refresh=button;
        QVERIFY(refresh);
        auto* status=invoices->findChild<QLabel*>("invoiceSummary");QVERIFY(status);
        for(int previous:{0,3,4,1,8}) {
            tabs->setCurrentIndex(previous);QTest::qWait(100);
            tabs->setCurrentIndex(2);QTest::qWait(500);
            const auto pixmap=page.grab();
            const auto image=pixmap.toImage();
            const qreal dpr=pixmap.devicePixelRatio();
            const auto output=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
            if(!output.isEmpty())QVERIFY(pixmap.save(QDir(output).filePath("invoice-background.png")));
            const QList<QPoint> samples{
                refresh->mapTo(&page,QPoint(refresh->width()-2,1)),
                status->mapTo(&page,QPoint(status->width()-2,status->height()/2))
            };
            for(const auto point:samples) {
                const QColor pixel=image.pixelColor(qRound(point.x()*dpr),qRound(point.y()*dpr));
                QVERIFY2(qAbs(pixel.red()-background.red())<=2 && qAbs(pixel.green()-background.green())<=2 && qAbs(pixel.blue()-background.blue())<=2,
                    qPrintable(QString("Background seam at %1,%2: %3, expected %4").arg(point.x()).arg(point.y()).arg(pixel.name(),background.name())));
            }
        }
        error.clear();QCOMPARE(PurchasePaymentService::balances(db,&error),before);QVERIFY(error.isEmpty());
    }
    void reportTabSwitchLayoutWorkingCopyAudit() {
        if(qEnvironmentVariable("CS_TAB_AUDIT_COPY").isEmpty())QSKIP("Requires isolated working-data copy");
        ThemeManager::applySavedTheme();
        if(qEnvironmentVariable("CS_UI_AUDIT_LIGHT")=="1")ThemeManager::setTheme("light");
        QVERIFY(QFileInfo(db.databaseName()).absoluteFilePath().startsWith(App::sessionDir()));
        QString error;
        const auto before=PurchasePaymentService::balances(db,&error);QVERIFY(error.isEmpty());
        ReportsPage page;page.resize(1500,800);page.show();
        const QString background=ThemeManager::currentTheme()=="light"?"#f6f8fb":"#526070";
        page.setStyleSheet(page.styleSheet()+"#reportsPage {background:"+background+";}");
        auto* tabs=page.findChild<QTabWidget*>();QVERIFY(tabs);
        auto* filters=page.findChild<QWidget*>("reportsFiltersFrame");QVERIFY(filters);
        const auto dates=filters->findChildren<QDateEdit*>();QCOMPARE(dates.size(),2);
        dates[0]->setDate(QDate(2026,10,1));dates[1]->setDate(QDate(2026,10,23));
        auto* banners=page.findChild<QWidget*>("reportsBannersHost");QVERIFY(banners);
        auto* header=page.findChild<QWidget*>("reportsTopHost");QVERIFY(header);
        int commonTabY=-1,commonTableY=-1;
        int commonDateWidth=-1,commonSearchWidth=-1;
        // Replay the video, including cold loads and cached returns from every other tab.
        const QList<int> sequence{3,2,4,2,0,2,5,2,6,2,7,2,8,2,9,2,1,3,1,2};
        for(int pass=0;pass<2;++pass)for(int index:sequence) {
            tabs->setCurrentIndex(index);
            if(index==3) {
                auto* summary=page.findChild<QLabel*>("reportsOilStatsLabel");QVERIFY(summary);
                QTRY_VERIFY_WITH_TIMEOUT(summary->text().contains(QString::fromUtf8("Запчастини:")),15000);
            }
            if(index==1 || index==2) {
                // Layout state must change before a refresh timer or worker can finish.
                QVERIFY2(!filters->styleSheet().contains("1px solid black"),"Previous tab border leaked into search tab");
                QVERIFY(banners->isHidden());
                const int gap=qMax(1,qRound(filters->logicalDpiX()*0.5/25.4));
                QCOMPARE(filters->layout()->spacing(),gap);
                QTest::qWait(pass==0?30:500);
                const QColor textColor(ThemeManager::currentTheme()=="light"?"#111111":"#ffffff");
                auto* dateLabel=qobject_cast<QLabel*>(filters->layout()->itemAt(0)->widget());
                QVERIFY(dateLabel);QCOMPARE(dateLabel->palette().color(dateLabel->foregroundRole()),textColor);
                if(index==2) {
                    auto* summary=page.findChild<QLabel*>("invoiceSummary");QVERIFY(summary);
                    QCOMPARE(summary->palette().color(summary->foregroundRole()),textColor);
                }
                auto* search=page.findChild<QLineEdit*>(index==1?"reportsOrdersSearch":"invoiceSearch");
                QVERIFY(search && search->isVisible());
                for(int j=1;j<4;++j) {
                    auto* left=filters->layout()->itemAt(j-1)->widget();
                    auto* right=filters->layout()->itemAt(j)->widget();
                    QCOMPARE(right->x()-left->geometry().right()-1,gap);
                }
                auto* last=filters->layout()->itemAt(3)->widget();
                QCOMPARE(search->mapTo(&page,QPoint()).x()-last->mapTo(&page,QPoint(last->width(),0)).x(),gap);
                const int fieldHeight=qRound(7.0*page.logicalDpiY()/25.4);
                QCOMPARE(search->height(),fieldHeight);
                for(auto* date:dates) {
                    QCOMPARE(date->height(),fieldHeight);
                    QCOMPARE(date->width(),date->property("reportDateBaseWidth").toInt()+qRound(5.0*page.logicalDpiX()/25.4));
                }
                if(index==2) {
                    auto* filter=page.findChild<QComboBox*>("invoiceStatusFilter");QVERIFY(filter);
                    auto* sort=page.findChild<QComboBox*>("invoiceSort");QVERIFY(sort);
                    auto* summary=page.findChild<QLabel*>("invoiceSummary");QVERIFY(summary);
                    auto* refresh=page.findChild<QPushButton*>("invoiceRefresh");QVERIFY(refresh);
                    for(auto* field:QList<QWidget*>{filter,sort,refresh}) {
                        QCOMPARE(field->mapTo(&page,QPoint()).y(),search->mapTo(&page,QPoint()).y());
                        QCOMPARE(field->height(),fieldHeight);
                    }
                    QVERIFY(filter->mapTo(&page,QPoint()).x()>=search->mapTo(&page,QPoint(search->width(),0)).x());
                    QCOMPARE(summary->mapTo(&page,QPoint()).x(),dates.first()->mapTo(&page,QPoint()).x());
                    QCOMPARE(summary->mapTo(&page,QPoint()).y()-search->mapTo(&page,QPoint(0,search->height())).y(),qRound(1.5*page.logicalDpiY()/25.4));
                }
                const auto output=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
                if(!output.isEmpty())QVERIFY(page.grab().save(QDir(output).filePath(QString("video-switch-%1.png").arg(index))));
            } else QTest::qWait(pass==0?30:500);
            if(index<8) {
                const int fieldHeight=qRound(7.0*page.logicalDpiY()/25.4);
                for(auto* date:dates) {
                    QCOMPARE(date->height(),fieldHeight);
                    if(commonDateWidth<0)commonDateWidth=date->width();
                    QCOMPARE(date->width(),commonDateWidth);
                    const int topGap=qRound(page.logicalDpiY()/25.4)-qRound(0.5*page.logicalDpiY()/25.4);
                    QVERIFY(qAbs(date->mapTo(&page,QPoint()).y()-topGap)<=1);
                }
                if(index==1 || index==2) {
                    auto* search=page.findChild<QLineEdit*>(index==1?"reportsOrdersSearch":"invoiceSearch");
                    if(commonSearchWidth<0)commonSearchWidth=search->width();
                    QCOMPARE(search->width(),commonSearchWidth);
                }
                {
                    if(commonTabY<0)commonTabY=tabs->y();
                    QCOMPARE(tabs->y(),commonTabY);
                }
                const int tabY=tabs->y();
                for(auto* table:tabs->currentWidget()->findChildren<QTableView*>())if(table->isVisible()) {
                    const int y=table->mapTo(&page,QPoint()).y();
                    {
                        if(commonTableY<0)commonTableY=y;
                        QCOMPARE(y,commonTableY);
                    }
                }
                QTest::qWait(100);
                QCOMPARE(tabs->y(),tabY);
                for(auto* control:header->findChildren<QWidget*>())if(control->isVisible() &&
                    (qobject_cast<QLineEdit*>(control) || qobject_cast<QComboBox*>(control) || qobject_cast<QLabel*>(control))) {
                    const QRect bounds(control->mapTo(header,QPoint()),control->size());
                    auto* content=page.findChild<QWidget*>("reportsHeaderContent");QVERIFY(content);
                    const QRect contentBounds(control->mapTo(content,QPoint()),control->size());
                    QVERIFY2(content->rect().contains(contentBounds),qPrintable("Header clipping: "+control->objectName()));
                }
                const auto output=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
                if(pass==1 && !output.isEmpty())QVERIFY(page.grab().save(QDir(output).filePath(QString("uniform-report-%1.png").arg(index))));
            }
            for(auto* button:page.findChildren<QPushButton*>())if(button->text()=="PDF" && button->isVisible()) {
                const QRect rect(button->mapTo(&page,QPoint()),button->size());
                QVERIFY(page.rect().contains(rect));
                QVERIFY(!header->isVisible() || !header->geometry().intersects(rect));
            }
        }
        error.clear();QCOMPARE(PurchasePaymentService::balances(db,&error),before);QVERIFY(error.isEmpty());
    }
    void workshopPostWorkingCopyAudit() {
        if(qEnvironmentVariable("CS_TAB_AUDIT_COPY").isEmpty())QSKIP("Requires isolated working-data copy");
        ThemeManager::applySavedTheme();
        if(qEnvironmentVariable("CS_UI_AUDIT_LIGHT")=="1")ThemeManager::setTheme("light");
        QVERIFY(QFileInfo(db.databaseName()).absoluteFilePath().startsWith(App::sessionDir()));
        // This UI audit never authenticates to the production authority.
        QVERIFY(Database::setSetting("settings.cloudsync.api_token",QString()));
        QString error;
        const auto before=PurchasePaymentService::balances(db,&error);QVERIFY(error.isEmpty());
        auto orders=CurrentOrdersQueryService::loadOpenOrders({});
        QVERIFY2(orders.errorText.isEmpty(),qPrintable(orders.errorText));QVERIFY(!orders.rows.isEmpty());
        const int number=orders.rows.first().value(orders.headers.indexOf("__internal_number")).toInt();
        const QString uid=orders.rows.first().value(orders.headers.indexOf("__order_uid")).toString();
        QVERIFY(number>0);QVERIFY(!uid.isEmpty());
        QVERIFY2(OrderMutationFacade::setWorkshopPost(number,3,&error),qPrintable(error));
        BaySnapshot::Data authority;
        authority.asOf=QDateTime::currentDateTimeUtc();
        authority.states.insert(uid,QJsonObject{{"state","on_bay"},{"bay_uid","3"},{"version",1}});
        BaySnapshot::remember(authority);
        orders=CurrentOrdersQueryService::loadOpenOrders({});
        bool found=false;
        for(const auto& row:orders.rows)if(row.value(orders.headers.indexOf("__order_uid")).toString()==uid) {
            QCOMPARE(row.value(orders.headers.indexOf(QString::fromUtf8("Пост"))).toInt(),3);found=true;
        }
        QVERIFY(found);
        CurrentOrdersPage page;page.resize(1500,850);page.show();page.refresh();
        auto* table=page.findChild<QTableView*>("premiumCurrentOrdersTable");QVERIFY(table);
        QTRY_VERIFY_WITH_TIMEOUT(table->model()->rowCount()>0,20000);
        int postColumn=-1;
        for(int col=0;col<table->model()->columnCount();++col)if(table->model()->headerData(col,Qt::Horizontal).toString()==QString::fromUtf8("Пост"))postColumn=col;
        QVERIFY(postColumn>=0);QCOMPARE(table->horizontalHeader()->visualIndex(postColumn),1);
        for(int col=0;col<table->model()->columnCount();++col)if(!table->isColumnHidden(col))
            qInfo()<<"POST_COLUMN"<<col<<table->model()->headerData(col,Qt::Horizontal)<<table->model()->index(0,col).data()<<table->visualRect(table->model()->index(0,col));
        table->setColumnWidth(postColumn,87);page.markDirty();page.refresh();QTest::qWait(1500);
        QCOMPARE(table->columnWidth(postColumn),87);
        table->selectRow(0);
        bool postDialogReadable=false;
        QTimer::singleShot(100,&page,[&]{
            if(auto* input=qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
                postDialogReadable=input->width()>=input->fontMetrics().horizontalAdvance(input->windowTitle())+100;
                qInfo()<<"POST_DIALOG_SIZE"<<input->width()<<input->fontMetrics().horizontalAdvance(input->windowTitle())<<input->windowTitle();
                const auto output=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
                if(!output.isEmpty()) postDialogReadable=input->grab().save(QDir(output).filePath("change-post.png")) && postDialogReadable;
                input->reject();
            }
        });
        page.findChild<QToolButton*>("changeWorkshopPost")->click();
        QVERIFY(postDialogReadable);
        QCOMPARE(OrderMutationFacade::workshopPosts().value(uid),3);
        QCOMPARE(BaySnapshot::currentPosts().value(uid),3);
        QTRY_COMPARE_WITH_TIMEOUT(table->model()->index(0,postColumn).data().toInt(),3,20000);
        QWidget parent;parent.resize(1500,850);parent.show();
        WorkPartsDialog dialog(&parent,number);dialog.resize(1500,850);dialog.show();QTest::qWait(1500);
        auto* combo=dialog.findChild<QComboBox*>("orderWorkshopPost");QVERIFY(combo);QCOMPARE(combo->currentIndex(),3);
        QVERIFY(combo->width()<=qRound(dialog.logicalDpiX()*15.0/25.4));
        QTimer::singleShot(100,&dialog,[]{if(auto* modal=qobject_cast<QDialog*>(QApplication::activeModalWidget())) modal->reject();});
        QVERIFY(QMetaObject::invokeMethod(combo,"activated",Qt::DirectConnection,Q_ARG(int,2)));
        QCOMPARE(OrderMutationFacade::workshopPosts().value(uid),3);
        QCOMPARE(BaySnapshot::currentPosts().value(uid),3);
        QCOMPARE(combo->currentIndex(),3);
        QTest::qWait(500);
        auto* toolbar=dialog.findChild<QWidget*>("workOrderToolbar");QVERIFY(toolbar);
        const auto controls=toolbar->findChildren<QWidget*>(QString(),Qt::FindDirectChildrenOnly);
        const int toolbarCenter=combo->geometry().center().y();
        QList<QWidget*> visibleControls;
        for(auto* control:controls)if(control->isVisible())visibleControls.append(control);
        std::sort(visibleControls.begin(),visibleControls.end(),[](QWidget* a,QWidget* b){return a->x()<b->x();});
        QVERIFY(!visibleControls.isEmpty());
        QCOMPARE(visibleControls.first()->x(),0);
        QCOMPARE(visibleControls.last()->geometry().right(),toolbar->width()-1);
        const int expectedGap=qMax(1,qRound(dialog.logicalDpiX()*0.5/25.4));
        for(int i=1;i<visibleControls.size();++i)
            QCOMPARE(visibleControls[i]->x()-visibleControls[i-1]->geometry().right()-1,expectedGap);
        for(auto* control:controls)if(control->isVisible())
            QVERIFY2(qAbs(control->geometry().center().y()-toolbarCenter)<=2,"Toolbar must stay on one row");
        for(int i=0;i<controls.size();++i)for(int j=i+1;j<controls.size();++j)
            if(controls[i]->isVisible() && controls[j]->isVisible())
                QVERIFY2(!controls[i]->geometry().intersects(controls[j]->geometry()),qPrintable(controls[i]->objectName()+" / "+controls[j]->objectName()));
        for(auto* button:dialog.findChildren<QPushButton*>())if(button->isVisible() &&
            QStringList{QString::fromUtf8("Зберегти майстра"),QString::fromUtf8("ШІ-підбір"),QString::fromUtf8("З/Ч клієнта"),"%","PDF",QString::fromUtf8("Імпорт накладної"),QString::fromUtf8("Аутсорс"),QString::fromUtf8("Закрити наряд")}.contains(button->text())) {
            QVERIFY2(button->width()>=button->fontMetrics().horizontalAdvance(button->text())+12,qPrintable(button->text()));
            QVERIFY(dialog.rect().contains(QRect(button->mapTo(&dialog,QPoint()),button->size())));
        }
        InvoicesPage invoicesPage;invoicesPage.resize(1500,800);invoicesPage.show();invoicesPage.refresh();
        auto* invoiceTable=invoicesPage.findChild<QTableView*>("invoicesTable");QVERIFY(invoiceTable);
        QTRY_VERIFY_WITH_TIMEOUT(invoiceTable->isEnabled() && invoiceTable->model()->rowCount()>0,20000);
        auto* invoiceSearch=invoicesPage.findChild<QLineEdit*>("invoiceSearch");QVERIFY(invoiceSearch);
        auto* invoiceSort=invoicesPage.findChild<QComboBox*>("invoiceSort");QVERIFY(invoiceSort);
        QCOMPARE(invoiceSort->currentText(),QString::fromUtf8("За датою ↑"));
        invoiceSort->setCurrentIndex(4);
        for(int r=0;r<invoiceTable->model()->rowCount();++r)
            QCOMPARE(invoiceTable->model()->index(r,5).data().toString(),QString::fromUtf8("Сплачена"));
        invoiceSort->setCurrentIndex(3);
        const QString invoiceNumber=invoiceTable->model()->index(0,1).data().toString();
        invoiceSearch->setText(invoiceNumber);
        QVERIFY(invoiceTable->model()->rowCount()>0);
        QCOMPARE(invoiceTable->model()->index(0,1).data().toString(),invoiceNumber);
        PartsPage partsPage;partsPage.resize(1500,800);partsPage.show();partsPage.refresh();
        ReportsPage reportsPage;reportsPage.resize(1500,800);reportsPage.show();
        const QString auditBackground=ThemeManager::currentTheme()=="light"?"#f6f8fb":"#526070";
        partsPage.setStyleSheet(partsPage.styleSheet()+"#partsArchivePage {background:"+auditBackground+";}");
        reportsPage.setStyleSheet(reportsPage.styleSheet()+"#reportsPage {background:"+auditBackground+";}");
        for(auto* date:reportsPage.findChildren<QDateEdit*>())date->setDate(date->date()==QDate::currentDate()?QDate(2026,10,23):QDate(2026,10,1));
        auto* reportTabs=reportsPage.findChild<QTabWidget*>();QVERIFY(reportTabs);
        for(int i=0;i<reportTabs->count();++i) {
            const auto title=reportTabs->tabText(i);
            if(title!=QString::fromUtf8("Наряди") && title!=QString::fromUtf8("Накладні"))continue;
            reportTabs->setCurrentIndex(i);
            QTest::qWait(1000);
            auto* frame=reportsPage.findChild<QWidget*>("reportsFiltersFrame");QVERIFY(frame);
            const int gap=qMax(1,qRound(frame->logicalDpiX()*0.5/25.4));
            auto* layout=frame->layout();QCOMPARE(layout->count(),4);
            for(int j=1;j<4;++j)
                QCOMPARE(layout->itemAt(j)->widget()->x()-layout->itemAt(j-1)->widget()->geometry().right()-1,gap);
            auto* search=title==QString::fromUtf8("Наряди")
                ? reportsPage.findChild<QLineEdit*>("reportsOrdersSearch")
                : reportsPage.findChild<QLineEdit*>("invoiceSearch");
            QVERIFY(search);QVERIFY(search->isVisible());
            auto* last=layout->itemAt(3)->widget();
            QCOMPARE(search->mapTo(&reportsPage,QPoint()).x()-last->mapTo(&reportsPage,QPoint(last->width(),0)).x(),gap);
            const auto output=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
            if(!output.isEmpty())QVERIFY(reportsPage.grab().save(QDir(output).filePath(QString("report-filters-%1.png").arg(i))));
        }
        for(int i=0;i<reportTabs->count();++i)if(reportTabs->tabText(i)==QString::fromUtf8("Придбання запчастин"))reportTabs->setCurrentIndex(i);
        SettingsPage settingsPage;settingsPage.resize(1300,850);settingsPage.show();
        bool personnel=false;
        for(auto* button:settingsPage.findChildren<QPushButton*>())if(button->text()==QString::fromUtf8("Персонал"))personnel=true;
        QVERIFY(personnel);
        QTest::qWait(1500);
        const auto output=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
        auto* filters=partsPage.findChild<QWidget*>("partsFilterFrame");
        auto* summary=reportsPage.findChild<QLabel*>("reportsOilStatsLabel");QVERIFY(summary);
        QVERIFY(summary->text().contains(QString::fromUtf8("Антифриз")));
        QVERIFY(summary->text().contains("width='23%'"));
        auto* header=reportsPage.findChild<QWidget*>("reportsTopHost");QVERIFY(header);
        for(auto* button:reportsPage.findChildren<QPushButton*>())if(button->text()=="PDF" && button->isVisible()) {
            const QRect rect(button->mapTo(&reportsPage,QPoint()),button->size());
            QVERIFY(reportsPage.rect().contains(rect));
            QVERIFY(!header->geometry().intersects(rect));
        }
        auto* debtsHost=partsPage.findChild<QWidget*>("partsDebtBannerHost");
        QVERIFY(filters);QVERIFY(debtsHost);
        const int shift=qRound(10.0*filters->logicalDpiX()/25.4);
        QVERIFY2(qAbs(debtsHost->width()-2*filters->width()+3*shift)<=3,"Parts header split must move right by 10 mm");
        const auto dates=filters->findChildren<QDateEdit*>();
        const auto searches=filters->findChildren<QLineEdit*>(QString(),Qt::FindDirectChildrenOnly);
        QCOMPARE(dates.size(),2);QCOMPARE(searches.size(),1);
        QCOMPARE(searches.first()->geometry().right(),qMax(dates[0]->geometry().right(),dates[1]->geometry().right()));
        QCOMPARE(searches.first()->x(),qMin(dates[0]->x(),dates[1]->x()));
        QVERIFY(searches.first()->y()>dates.first()->geometry().bottom());
        for(auto* button:partsPage.findChildren<QPushButton*>()) QVERIFY(button->text()!=QStringLiteral("PDF"));
        if(!output.isEmpty()) {
            QVERIFY(invoicesPage.grab().save(QDir(output).filePath("invoice-search.png")));
            QVERIFY(partsPage.grab().save(QDir(output).filePath("parts-summary.png")));
            QVERIFY(reportsPage.grab().save(QDir(output).filePath("reports-summary.png")));
            QVERIFY(settingsPage.grab().save(QDir(output).filePath("settings-menu.png")));
            QVERIFY(page.grab().save(QDir(output).filePath("orders-post.png")));
            QVERIFY(dialog.grab().save(QDir(output).filePath("workshop-post.png")));
            dialog.resize(1800,900);QTest::qWait(100);
            QVERIFY(dialog.grab().save(QDir(output).filePath("workshop-post-wide.png")));
            OrderDialog exporter(nullptr,number);
            exporter.setProperty("readOnlyOrderView",true);
            QVERIFY2(exporter.exportPartsReportToFile(QDir(output).filePath("order-report.pdf"),&error),qPrintable(error));
            bool aiChecked=false;
            QTimer::singleShot(100,&dialog,[&] {
                auto* ai=dialog.findChild<QDialog*>("repairAiDialog");
                if(!ai)return;
                auto* request=ai->findChild<QPushButton*>("repairAiRequest");
                aiChecked=request && request->height()>=request->fontMetrics().height()+8
                    && ai->frameGeometry().top()>=ai->screen()->availableGeometry().top()+qRound(ai->logicalDpiY()*7.0/25.4);
                if (request) {
                    for (auto* input : ai->findChildren<QPlainTextEdit*>()) {
                        const QRect inputRect(input->mapTo(ai, QPoint()), input->size());
                        const QRect buttonRect(request->mapTo(ai, QPoint()), request->size());
                        aiChecked = aiChecked && !inputRect.intersects(buttonRect);
                    }
                }
                ai->grab().save(QDir(output).filePath("repair-ai.png"));
                ai->reject();
            });
            for(auto* button:dialog.findChildren<QPushButton*>())if(button->text()==QString::fromUtf8("ШІ-підбір")){button->click();break;}
            QVERIFY(aiChecked);
        }
        dialog.close();
        error.clear();QCOMPARE(PurchasePaymentService::balances(db,&error),before);QVERIFY(error.isEmpty());
    }
    void debtAndStatisticsWorkingCopyAudit() {
        if(qEnvironmentVariable("CS_TAB_AUDIT_COPY").isEmpty()) QSKIP("Requires isolated working-data copy");
        QVERIFY(QFileInfo(db.databaseName()).absoluteFilePath().startsWith(App::sessionDir()));
        QString error;
        const auto before=PurchasePaymentService::balances(db,&error);QVERIFY(error.isEmpty());
        auto financialRows=[&] {
            QMap<QString,QList<QByteArray>> result;
            for(const auto& table:db.tables()) {
                if(!table.contains("payment") && !table.startsWith("workshop_") && table!="work_order_parts")continue;
                QSqlQuery query(db);if(!query.exec("SELECT * FROM \""+table+"\""))return QMap<QString,QList<QByteArray>>{};
                auto& rows=result[table];
                while(query.next()) {
                    QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);
                    for(int i=0;i<query.record().count();++i)stream<<query.value(i);
                    rows.append(bytes);
                }
                std::sort(rows.begin(),rows.end());
            }
            return result;
        };
        const auto financialBefore=financialRows();QVERIFY(!financialBefore.isEmpty());
        QSettings production(QSettings::NativeFormat,QSettings::UserScope,App::orgName(),App::appName());
        const auto plain=CloudSyncCredentials::unprotectToken(production.value("integrations/elit/credentials_dpapi").toString(),&error);
        QVERIFY2(!plain.isEmpty(),"Saved account identity unavailable");
        const auto identity=QJsonDocument::fromJson(plain.toUtf8()).object();
        GeneralSettingsService::ElitCredentials account;
        account.company=identity.value("company").toString();account.login=identity.value("login").toString();
        account.rememberPassword=false;
        QVERIFY(QCoreApplication::applicationName().startsWith("Safety-"));
        QVERIFY(GeneralSettingsService::saveElitCredentials(account,&error));
        const auto snapshot=ElitInvoiceStore::load(db,ElitInvoiceStore::accountKey(account),&error);
        QVERIFY(error.isEmpty());QVERIFY(!snapshot.revision.isEmpty());
        qint64 remote=0;int unpaid=0;
        for(const auto& document:snapshot.documents) {remote+=document.invoice.remainingCents;if(document.invoice.remainingCents>0)++unpaid;}
        QElapsedTimer timer;timer.start();
        const auto local=PurchasePaymentService::debts(db,&error);QVERIFY(error.isEmpty());
        auto facts=SupplierDebtReconciliationService::load(db,&error);QVERIFY(error.isEmpty());
        const auto localMs=timer.restart();
        const auto summary=SupplierInvoiceLedger::summary(db,local,facts,&error);QVERIFY(error.isEmpty());
        const auto summaryMs=timer.restart();
        QCOMPARE(summary.totals.value(QString::fromUtf8("Еліт")),remote);
        const auto report=SupplierInvoiceLedger::reportingDebts(db,local,&facts,&error);QVERIFY(error.isEmpty());
        qint64 reportRemote=0;for(const auto& row:report)if(row.source=="remote_invoice")reportRemote+=row.remainingCents();
        QCOMPARE(reportRemote,remote);
        PurchaseDebtAnalysisPage asyncAnalysis;
        asyncAnalysis.resize(1400,850);asyncAnalysis.show();
        QString analysisSummary;
        asyncAnalysis.summaryChanged=[&](const QString& html){analysisSummary=html;};
        timer.restart();asyncAnalysis.refresh();
        const auto analysisDispatchMs=timer.elapsed();
        QVERIFY(analysisDispatchMs<500);
        QTRY_VERIFY_WITH_TIMEOUT(!analysisSummary.isEmpty(),30000);
        QVERIFY(analysisSummary.contains(FormatUtils::money(remote/100.0,2)));
        QTRY_VERIFY_WITH_TIMEOUT(asyncAnalysis.findChild<QPushButton*>("debtAnalysisPdf")->isEnabled(),30000);
        asyncAnalysis.findChild<QTabBar*>("debtAnalysisViews")->setCurrentIndex(2);
        QVERIFY(asyncAnalysis.findChild<QTextBrowser*>("debtAnalysisReport")->toPlainText().contains(FormatUtils::money(remote/100.0,2)));
        qInfo()<<"ELIT_ANALYSIS_OPEN dispatch_ms="<<analysisDispatchMs<<"loaded_ms="<<timer.elapsed();
        InvoicesPage registry;registry.show();registry.refresh();
        auto* registryTable=registry.findChild<QTableView*>("invoicesTable");QVERIFY(registryTable);
        QTRY_VERIFY_WITH_TIMEOUT(registryTable->isEnabled() && registryTable->model()->rowCount()>0,20000);
        registry.findChild<QLineEdit*>("invoiceSearch")->setText(snapshot.documents.first().invoice.number);
        QCOMPARE(registryTable->model()->rowCount(),1);
        QCOMPARE(registryTable->model()->index(0,1).data().toString(),snapshot.documents.first().invoice.number);
        PurchaseDebtsPage page;page.resize(1200,750);page.show();page.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(!page.property("debtDataLoading").toBool(),30000);
        const auto debtMs=timer.restart();
        auto* debtTable=page.findChild<QTableView*>("purchaseDebtsTable"); QVERIFY(debtTable);
        int remoteRow=-1;
        for(int row=0;row<debtTable->model()->rowCount();++row)
            if(debtTable->model()->index(row,0).data(Qt::UserRole+1).toString()=="remote_invoice") {remoteRow=row;break;}
        QVERIFY(remoteRow>=0); QVERIFY(debtTable->model()->index(remoteRow,3).data(Qt::FontRole).value<QFont>().underline());
        bool checkedReconciliation=false;
        QVERIFY(Database::setSetting("settings.fin.store_markups", QString::fromUtf8("{\"Еліт\":0}")));
        QTimer::singleShot(0,&page,[&]{
            auto* dialog=page.findChild<QDialog*>("supplierDebtReconciliationDialog");
            if(!dialog)return;
            auto* table=dialog->findChild<QTableView*>("supplierReconciliationTable");
            if(table)for(int row=0;row<table->model()->rowCount();++row)if(SupplierInvoiceLedger::isElit(table->model()->index(row,0).data().toString())) {
                auto* value=qobject_cast<QDoubleSpinBox*>(table->indexWidget(table->model()->index(row,2)));
                checkedReconciliation=value && value->isReadOnly()
                    && PurchasePaymentService::cents(value->value())==remote
                    && table->model()->index(row,1).data().toString()==FormatUtils::money(remote/100.0,2);
            }
            dialog->reject();
        });
        page.findChild<QPushButton*>("reconcileSupplierDebts")->click();
        QVERIFY(checkedReconciliation);
        StatsPage stats;stats.resize(1200,750);stats.show();stats.refresh();
        const auto statsMs=timer.restart();
        QVERIFY(stats.property("partRows").toList().isEmpty());
        SupplierDebtOverview overview;overview.resize(1500,850);overview.show();overview.refresh();
        auto* tile=overview.findChild<QPushButton*>("debtTile0");
        QTRY_VERIFY_WITH_TIMEOUT(tile->isEnabled(),20000);
        QVERIFY(tile->text().contains(FormatUtils::money(remote/100.0,2)) || summary.totals.size()>1);
        if(qEnvironmentVariable("CS_ELIT_CLICK_PDF_AUDIT")=="1") {
            account.password=identity.value("password").toString();account.rememberPassword=true;
            QVERIFY2(!account.password.isEmpty(),"Saved eCat password unavailable");
            QVERIFY(GeneralSettingsService::saveElitCredentials(account,&error));
            auto* table=overview.findChild<QTableView*>("supplierInvoiceLedger");QVERIFY(table && table->model()->rowCount()>0);
            const auto number=table->model()->index(0,1).data().toString();
            QString documentId;for(const auto& document:snapshot.documents)if(document.invoice.number==number)documentId=document.identity;
            QVERIFY(!documentId.isEmpty());
            QSqlQuery remove(db);remove.prepare("DELETE FROM elit_invoice_pdf_files WHERE account=? AND invoice_id=?");
            remove.addBindValue(snapshot.account);remove.addBindValue(documentId);QVERIFY(remove.exec());
            openedPdf={};QDesktopServices::setUrlHandler("file",this,"capturePdfUrl");
            const auto resetHandler=qScopeGuard([]{QDesktopServices::unsetUrlHandler("file");});
            QString failure;QTimer closeErrors;
            connect(&closeErrors,&QTimer::timeout,this,[&]{
                if(auto* message=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {failure=message->text();message->accept();}
            });closeErrors.start(50);
            QVERIFY(QMetaObject::invokeMethod(table,"clicked",Qt::DirectConnection,Q_ARG(QModelIndex,table->model()->index(0,1))));
            QTRY_VERIFY_WITH_TIMEOUT(!openedPdf.isEmpty() || !failure.isEmpty(),180000);
            QVERIFY2(failure.isEmpty(),qPrintable(failure));
            QVERIFY(QFileInfo::exists(openedPdf.toLocalFile()));
            QVERIFY(openedPdf.toLocalFile().startsWith(QFileInfo(db.databaseName()).absolutePath()+"/attachments/"));
            QCOMPARE(ElitInvoicePdf::paths(db,snapshot.account).value(documentId),openedPdf.toLocalFile());
            const auto downloaded=openedPdf;
            account.password.clear();account.rememberPassword=false;QVERIFY(GeneralSettingsService::saveElitCredentials(account,&error));
            int pdfRow=-1;
            for(int row=0;row<debtTable->model()->rowCount();++row)
                if(debtTable->model()->index(row,0).data(Qt::UserRole+2).toString()==documentId) {pdfRow=row;break;}
            QVERIFY(pdfRow>=0); openedPdf={};
            QVERIFY(QMetaObject::invokeMethod(debtTable,"clicked",Qt::DirectConnection,Q_ARG(QModelIndex,debtTable->model()->index(pdfRow,3))));
            QTRY_VERIFY_WITH_TIMEOUT(!openedPdf.isEmpty() || !failure.isEmpty(),10000);
            QVERIFY2(failure.isEmpty(),qPrintable(failure)); QCOMPARE(openedPdf,downloaded);
            PurchaseDebtAnalysisPage analysis;analysis.resize(1200,750);analysis.show();analysis.refresh();
            auto* browser=analysis.findChild<QTextBrowser*>("debtAnalysisReport");QVERIFY(browser);
            openedPdf={};
            const auto key=PurchasePaymentService::key("remote_invoice",0,documentId);
            const auto url=QUrl("record:"+QString::fromLatin1(QUrl::toPercentEncoding(key)));
            QVERIFY(QMetaObject::invokeMethod(browser,"anchorClicked",Qt::DirectConnection,Q_ARG(QUrl,url)));
            QTRY_VERIFY_WITH_TIMEOUT(!openedPdf.isEmpty() || !failure.isEmpty(),10000);
            QVERIFY2(failure.isEmpty(),qPrintable(failure));QCOMPARE(openedPdf,downloaded);
            QTest::qWait(100);
        }
        const auto output=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
        if(!output.isEmpty()) {
            QVERIFY(overview.grab().save(QDir(output).filePath("debt-tiles-verified.png")));
            for(auto* button:overview.findChildren<QPushButton*>())if(button->objectName().startsWith("debtTile"))button->setFixedHeight(82);
            QTest::qWait(50);
            QVERIFY(overview.grab().save(QDir(output).filePath("debt-tiles-compact.png")));
            QFile log(QDir(output).filePath("debt-statistics-timings.txt"));QVERIFY(log.open(QIODevice::WriteOnly));
            log.write(QString("local_debts_ms=%1\nsummary_ms=%2\ndebt_page_ms=%3\nstatistics_ms=%4\nremote_unpaid=%5\nremote_remaining_cents=%6\n").arg(localMs).arg(summaryMs).arg(debtMs).arg(statsMs).arg(unpaid).arg(remote).toUtf8());
        }
        QCOMPARE(PurchasePaymentService::balances(db,&error),before);QVERIFY(error.isEmpty());
        QCOMPARE(financialRows(),financialBefore);
    }
    void reportInvoiceColumnsAndSupplierMenus() {
        QSqlQuery q(db);
        for(const auto& sql:QStringList{
            "CREATE TABLE masters(id TEXT PRIMARY KEY,name TEXT)",
            "ALTER TABLE work_orders ADD COLUMN client TEXT DEFAULT ''",
            "ALTER TABLE work_orders ADD COLUMN master_id TEXT DEFAULT ''",
            "ALTER TABLE work_orders ADD COLUMN master_name_closed TEXT DEFAULT ''",
            "CREATE VIEW router_work_order_parts AS SELECT * FROM work_order_parts",
            "CREATE TABLE work_order_part_attachments(part_id INTEGER PRIMARY KEY,invoice_number TEXT,invoice_date TEXT,file_path TEXT)",
            "INSERT INTO work_order_part_attachments VALUES(1,'INV-NEW','2026-09-01','test.pdf')"})QVERIFY2(q.exec(sql),qPrintable(q.lastError().text()));
        const auto snapshot=ReportsDataLoader::buildOrdersSnapshot(db.databaseName(),"2026-09-01","2026-09-30");
        QCOMPARE(snapshot.rows.size(),1);
        const auto& row=snapshot.rows.first();
        QCOMPARE(row.invoiceNumbers,QStringList{"INV-NEW"});
        QCOMPARE(row.paymentDue,QString("08.09.26"));
        QCOMPARE(row.invoiceTotal,FormatUtils::money(200,2));
        QCOMPARE(row.paymentStatus,QString::fromUtf8("Не сплачено повністю"));
        QVERIFY(q.exec("DELETE FROM work_order_part_attachments"));
        const auto unattached=ReportsDataLoader::buildOrdersSnapshot(db.databaseName(),"2026-09-01","2026-09-30");
        QCOMPARE(unattached.rows.first().paymentStatus,QString::fromUtf8("Не сплачено повністю"));
        QCOMPARE(unattached.rows.first().invoiceTotal,unattached.rows.first().purchaseTotal);
        QString error;
        QVERIFY(PurchasePaymentService::payOrderParts(db,1,PurchasePaymentService::orderPartDebts(db,1),&error));
        const auto after=ReportsDataLoader::buildOrdersSnapshot(db.databaseName(),"2026-09-01","2026-09-30");
        QCOMPARE(after.rows.first().paymentStatus,QString::fromUtf8("Сплачено"));
        {
            ReportsPage page;page.resize(1500,800);page.show();
            auto* tabs=page.findChild<QTabWidget*>();QVERIFY(tabs);tabs->setCurrentIndex(1);
            QTest::qWait(500);
            auto* table=tabs->widget(1)->findChild<QTableView*>();QVERIFY(table);
            auto* model=dynamic_cast<ReportsTableSupport::ReportsSnapshotTableModel*>(table->model());QVERIFY(model);
            auto paid=after;
            paid.rows.first().invoiceNumbers=QStringList{"INV-PAID"};
            ReportsTableSupport::applyOrdersSnapshot(table,model,paid);
            const auto balances=PurchasePaymentService::balances(db,&error);QVERIFY(error.isEmpty());
            bool paidMessage=false;
            QTimer::singleShot(0,&page,[&] {
                auto* active=QApplication::activeModalWidget();
                if(auto* message=qobject_cast<QMessageBox*>(active)) {
                    paidMessage=message->text().contains(QString::fromUtf8("Заборгованість відсутня"));
                    message->accept();
                } else if(auto* dialog=qobject_cast<QDialog*>(active))dialog->reject();
            });
            const auto index=model->index(0,6);table->setCurrentIndex(index);
            QVERIFY(QMetaObject::invokeMethod(table,"clicked",Qt::DirectConnection,Q_ARG(QModelIndex,index)));
            QVERIFY(paidMessage);
            QCOMPARE(PurchasePaymentService::balances(db,&error),balances);QVERIFY(error.isEmpty());
        }
        std::unique_ptr<QWidget> suppliers(SettingsIntegrationsSection::build("int.suppliers",nullptr,{},{},{}));
        suppliers->resize(1000,750);suppliers->show();QTest::qWait(50);
        for(const auto& name:QStringList{"int.api.elit","int.supplier.bm","int.supplier.omega","int.supplier.utr"}) {
            auto* button=suppliers->findChild<QPushButton*>(name);QVERIFY(button);QCOMPARE(button->width(),300);
            QVERIFY(!button->text().contains("API"));
        }
        QVERIFY(suppliers->grab().save(dir.filePath("supplier-menu.png")));
        const auto before=PurchasePaymentService::detail(db,"part",1,"order-a").paidCents;
        for(const auto& name:QStringList{QString::fromUtf8("Нова Пошта"),QString::fromUtf8("масло"),QString::fromUtf8("Без магазину")}) {
            q.prepare("UPDATE work_order_parts SET store=? WHERE id=2");q.addBindValue(name);QVERIFY(q.exec());
            QVERIFY(!SupplierDebtReconciliationService::suppliers(db).contains(name));
        }
        QCOMPARE(PurchasePaymentService::detail(db,"part",1,"order-a").paidCents,before);
    }
    void elitVoiceOutcomesAndSettings() {
        using O=ElitSyncVoice::Outcome;using F=ElitInvoiceReader::Failure;
        QCOMPARE(ElitSyncVoice::classify(true,true,F::Incomplete),O::Success);
        QCOMPARE(ElitSyncVoice::classify(false,true,F::Other),O::Cancelled);
        QCOMPARE(ElitSyncVoice::classify(false,false,F::Cancelled),O::Cancelled);
        QCOMPARE(ElitSyncVoice::classify(false,false,F::Incomplete),O::Incomplete);
        for(auto failure:{F::Access,F::Timeout,F::Connection,F::Other,F::None})
            QCOMPARE(ElitSyncVoice::classify(false,false,failure),O::Error);
        QSet<QString> files;
        for(auto outcome:{O::Success,O::Error,O::Cancelled,O::Incomplete}) {
            const auto name=ElitSyncVoice::assetName(outcome);files.insert(name);
            QFile asset(":/assets/"+name);QVERIFY(asset.open(QIODevice::ReadOnly));
            const auto bytes=asset.readAll();QVERIFY(bytes.startsWith("RIFF"));QCOMPARE(bytes.mid(8,4),QByteArray("WAVE"));
            QVERIFY(bytes.size()>50000);
        }QCOMPARE(files.size(),4);
        QSqlQuery q(db);QVERIFY(q.exec("DELETE FROM settings WHERE key='settings.sounds.elit_voice'"));
        RuntimeSettings::set("settings.sounds.elit_voice",QVariant());
        DbSettingsCacheSupport::remove("settings.sounds.elit_voice");
        QVERIFY(ElitSyncVoice::enabled());
        QWidget owner;
        auto* page=SettingsGeneralSection::build("general.sounds",&owner,{});QVERIFY(page);
        auto* toggle=page->findChild<LabeledToggle*>("elitVoiceEnabled");QVERIFY(toggle);QVERIFY(toggle->isChecked());
        auto* preview=page->findChild<QPushButton*>("elitVoicePreview");QVERIFY(preview);QVERIFY(preview->isEnabled());
        toggle->setChecked(false);QVERIFY(!ElitSyncVoice::enabled());QVERIFY(!preview->isEnabled());
        QVERIFY(!ElitSyncVoice::play(O::Success));
        auto* reopened=SettingsGeneralSection::build("general.sounds",&owner,{});
        QVERIFY(!reopened->findChild<LabeledToggle*>("elitVoiceEnabled")->isChecked());
        toggle->setChecked(true);QVERIFY(ElitSyncVoice::enabled());
        QVERIFY(Database::setSetting("settings.sounds.volume",0));SoundSupport::invalidateSettingsCache();
        QVERIFY(!ElitSyncVoice::play(O::Error));
        if(qEnvironmentVariable("CS_ELIT_AUDIO_AUDIT")=="1") {
            QVERIFY(Database::setSetting("settings.sounds.volume",85));SoundSupport::invalidateSettingsCache();
            for(auto outcome:{O::Success,O::Error,O::Cancelled,O::Incomplete}) {
                QVERIFY(ElitSyncVoice::play(outcome));QTest::qWait(5800);ElitSyncVoice::stop();
            }
        }
        QVERIFY(Database::setSetting("settings.sounds.elit_voice",false));
        QVERIFY(Database::setSetting("settings.booking.sync.enabled",false));
        QVERIFY(Database::setSetting("settings.cloudsync.enabled",false));
        QVERIFY(Database::setSetting("settings.booking.api.token",QString()));
        QSettings().setValue("EmailSync/enabled",false);SoundSupport::invalidateSettingsCache();
    }
    void supplierDebtOverviewTilesAndReadOnlyRefresh() {
        QString error;
        const auto before=PurchasePaymentService::detail(db,"part",1,"order-a");
        GeneralSettingsService::ElitCredentials credentials;
        QVERIFY(GeneralSettingsService::loadElitCredentials(&credentials));
        credentials.login="ui-fixture";credentials.password.clear();credentials.rememberPassword=false;
        QVERIFY(GeneralSettingsService::saveElitCredentials(credentials,&error));
        ElitInvoiceReader::Invoice invoice;invoice.number="UNMATCHED-UI";invoice.currency="UAH";
        invoice.posted=QDate::currentDate();invoice.due=invoice.posted.addDays(-3);
        invoice.amountCents=10000;invoice.grossCents=12000;invoice.amountVatCents=2000;invoice.remainingCents=12000;
        invoice.detailsComplete=true;invoice.lines={{"item","UI-OEM","Unmatched part",1,12000}};
        ElitInvoiceReader::Result result;result.complete=true;result.fetchedAt=QDateTime::currentDateTime();result.invoices={invoice};
        auto paidInvoice=invoice;paidInvoice.number="PAID-MUST-BE-HIDDEN";paidInvoice.remainingCents=0;
        result.invoices.append(paidInvoice);
        QVERIFY2(ElitInvoiceStore::publish(db,ElitInvoiceStore::accountKey(credentials),result,{},&error),qPrintable(error));
        SupplierDebtOverview view;view.resize(1200,720);view.show();
        int snapshots=0;
        view.suppliersChanged=[&](const QStringList&) { ++snapshots; };
        view.refresh();
        for(int i=0;i<20;++i) view.refresh();
        auto* table=view.findChild<QTableView*>("supplierInvoiceLedger");QVERIFY(table);
        auto* first=view.findChild<QPushButton*>("debtTile0");QVERIFY(first);
        QTRY_VERIFY_WITH_TIMEOUT(first->isEnabled(),10000);
        QTRY_COMPARE_WITH_TIMEOUT(snapshots,2,10000);
        QVERIFY(table->model()->rowCount()>=2);
        for(int row=0;row<table->model()->rowCount();++row) {
            QVERIFY(table->model()->index(row,1).data().toString()!=paidInvoice.number);
            QVERIFY(table->model()->index(row,6).data().toString()!="0");
        }
        QVERIFY(!view.reportHtml().contains(paidInvoice.number));
        for(int i=0;i<3;++i) {
            auto* tile=view.findChild<QPushButton*>("debtTile"+QString::number(i));QVERIFY(tile);
            QVERIFY(qAbs(tile->width()-first->width())<=1);
        }
        QVERIFY(!view.findChild<QPushButton*>("debtTile3"));
        QVERIFY(!view.findChild<QPushButton*>("debtTile4"));
        auto* last=view.findChild<QPushButton*>("debtTile2");
        QCOMPARE(first->x(),0);QCOMPARE(last->geometry().right(),view.width()-1);
        view.findChild<QPushButton*>("debtTile1")->click();
        QCOMPARE(table->model()->rowCount(),1);QCOMPARE(table->model()->index(0,1).data().toString(),QString("UNMATCHED-UI"));
        QCOMPARE(table->model()->index(0,4).data().toString(),QString("3"));
        view.setFilters("Other supplier",{},{});QCOMPARE(table->model()->rowCount(),0);
        view.setFilters({}, {}, {});QCOMPARE(table->model()->rowCount(),1);
        QVERIFY(!table->horizontalScrollBar()->isVisible());
        QVERIFY(view.reportHtml().contains("UNMATCHED-UI"));
        const auto screenshots=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
        if(!screenshots.isEmpty()) QVERIFY(view.grab().save(QDir(screenshots).filePath("supplier-debt-overview.png")));
        QCOMPARE(PurchasePaymentService::detail(db,"part",1,"order-a").paidCents,before.paidCents);
        QObject owner;bool callback=false;
        QVERIFY(!ElitStartupSync::request(&owner,[&](bool ok,const QString& message){callback=true;QVERIFY(!ok);QVERIFY(!message.isEmpty());}));
        QVERIFY(callback);QVERIFY(!ElitStartupSync::isRunning());
        const auto disabled=qgetenv("CS_ELIT_DISABLE_STARTUP");
        qunsetenv("CS_ELIT_DISABLE_STARTUP");
        QString startupStatus;int statusCount=0;
        const auto progress=[&](const QString& message){startupStatus=message;++statusCount;};
        ElitStartupSync::start(&owner,[](bool,const QString&){},progress);
        auto* retry=owner.findChild<QTimer*>("elitStartupRetry");QVERIFY(retry);
        QVERIFY(retry->isActive());QVERIFY(!startupStatus.isEmpty());QCOMPARE(statusCount,1);
        ElitStartupSync::start(&owner,[](bool,const QString&){},progress);
        QCOMPARE(owner.findChildren<QTimer*>("elitStartupRetry").size(),1);
        QVERIFY(QMetaObject::invokeMethod(retry,"timeout",Qt::DirectConnection));
        QCOMPARE(statusCount,1);QVERIFY(!ElitStartupSync::isRunning());
        qputenv("CS_ELIT_DISABLE_STARTUP",disabled);
    }
    void elitStartupWorkingCopyAudit() {
        if(qEnvironmentVariable("CS_TAB_AUDIT_COPY").isEmpty()
            || qEnvironmentVariable("CS_ELIT_STARTUP_AUDIT")!="1") QSKIP("Explicit isolated startup audit required");
        QVERIFY(QFileInfo(db.databaseName()).absoluteFilePath().startsWith(App::sessionDir()));
        QString error;
        const auto before=PurchasePaymentService::balances(db,&error);QVERIFY(error.isEmpty());
        QSettings production(QSettings::NativeFormat,QSettings::UserScope,App::orgName(),App::appName());
        const auto plain=CloudSyncCredentials::unprotectToken(production.value("integrations/elit/credentials_dpapi").toString(),&error);
        const auto identity=QJsonDocument::fromJson(plain.toUtf8()).object();
        GeneralSettingsService::ElitCredentials credentials;
        credentials.company=identity.value("company").toString();credentials.login=identity.value("login").toString();
        credentials.password=identity.value("password").toString();credentials.rememberPassword=true;
        QVERIFY(!credentials.password.isEmpty());
        QVERIFY(GeneralSettingsService::saveElitCredentials(credentials,&error));
        Database::setSetting("settings.sounds.elit_voice",false);
        qunsetenv("CS_ELIT_DISABLE_STARTUP");
        QObject owner;bool done=false,success=false;QString message;QStringList stages;
        ElitStartupSync::start(&owner,[&](bool ok,const QString& text){done=true;success=ok;message=text;},
            [&](const QString& text){stages.append(text);qInfo().noquote()<<"STARTUP_STAGE"<<text;});
        QVERIFY(ElitStartupSync::isRunning());
        QTimer deadline;deadline.setSingleShot(true);
        connect(&deadline,&QTimer::timeout,&owner,[]{ElitStartupSync::cancel();});deadline.start(180000);
        QTRY_VERIFY_WITH_TIMEOUT(done,210000);
        qInfo().noquote()<<"STARTUP_RESULT"<<success<<message;
        QVERIFY(!ElitStartupSync::isRunning());QVERIFY(stages.size()>=1);
        QCOMPARE(PurchasePaymentService::balances(db,&error),before);
        QVERIFY2(success,qPrintable(message));
    }
    void elitLiveReadOnly() {
        if (qEnvironmentVariable("CS_ELIT_LIVE_READ") != "1") QSKIP("Explicit live-read opt-in required");
        // Explicit native settings bypass the isolated test settings, read-only.
        QSettings saved(QSettings::NativeFormat, QSettings::UserScope, App::orgName(), App::appName());
        QString error;
        const auto protectedValue = saved.value("integrations/elit/credentials_dpapi").toString();
        QVERIFY2(!protectedValue.isEmpty(), "No saved ELIT credentials");
        const auto plain = CloudSyncCredentials::unprotectToken(protectedValue, &error);
        QVERIFY2(!plain.isEmpty(), "Cannot decrypt saved ELIT credentials for this Windows user");
        const auto values = QJsonDocument::fromJson(plain.toUtf8()).object();
        GeneralSettingsService::ElitCredentials credentials;
        credentials.company = values.value("company").toString();
        credentials.login = values.value("login").toString();
        credentials.password = values.value("password").toString();
        QVERIFY2(!credentials.password.isEmpty(), "ELIT password not saved");
        QObject owner;
        bool done = false;
        ElitInvoiceReader::Result fetched;
        ElitInvoiceReader::Options options;
        options.details = qEnvironmentVariable("CS_ELIT_LIVE_DETAILS") == "1";
        ElitInvoiceReader::start(&owner, credentials, [&](const auto& result) { fetched = result; done = true; }, options);
        QTRY_VERIFY_WITH_TIMEOUT(done, 125000);
        QVERIFY2(fetched.complete, qPrintable(fetched.error));
        int unpaid = 0;
        for (const auto& invoice : fetched.invoices) if (invoice.remainingCents > 0) ++unpaid;
        qInfo() << "ELIT live read: available=" << fetched.availableCount
                << "selected=" << fetched.invoices.size() << "unpaid=" << unpaid;
        if (options.details) for (const auto& invoice : fetched.invoices) QVERIFY(invoice.detailsComplete);
    }
    void elitCabinetPdfSafety() {
        QVERIFY(ElitCabinet::allowed(QUrl("https://www.ecat.ua/account/logonnow")));
        QVERIFY(ElitCabinet::allowed(QUrl("https://ecabinet.ecat.ua:8451/Customer/Balance")));
        QVERIFY(!ElitCabinet::allowed(QUrl("http://www.ecat.ua/")));
        QVERIFY(!ElitCabinet::allowed(QUrl("https://www.ecat.ua.attacker.example/")));
        QVERIFY(!ElitCabinet::allowed(QUrl("https://user:secret@www.ecat.ua/")));
        QVERIFY(!ElitCabinet::allowed(QUrl("https://ecabinet.ecat.ua:443/")));
        const auto tags=ElitCabinet::tags("<input name='__RequestVerificationToken' value='a&amp;b'><input name=\"DocumentNo\" value=\"KAM-1\">","input");
        QCOMPARE(tags.size(),2);QCOMPARE(tags[0].value("value"),QString("a&b"));
        ElitInvoiceReader::Invoice invoice;invoice.number="KAM-1";invoice.posted=QDate(2026,10,23);invoice.grossCents=29850;
        const QString text=QString::fromUtf8("Накладна KAM-1 від 23 жовтня 2026. Всього: 298,50 грн");
        QVERIFY(ElitInvoicePdf::matchesText(text,invoice));
        QVERIFY(!ElitInvoicePdf::matchesText(QString(text).replace("KAM-1","KAM-10"),invoice));
        QVERIFY(!ElitInvoicePdf::matchesText(QString(text).replace("298,50","298,51"),invoice));
        QVERIFY(!ElitInvoicePdf::matchesText(QString(text).replace("2026","2025"),invoice));
        ElitCabinet::Client cancelled([]{return false;});
        QVERIFY(cancelled.request(QUrl("https://www.ecat.ua/")).body.isEmpty());QVERIFY(!cancelled.error.isEmpty());
        const QString job="<tr><td>23.10.26 13:33</td><td>Period: 14.10.26 - 14.10.26</td><td><a data-action='DownloadInvoice' data-action-param='EntryNo=123'></a></td></tr>";
        QVERIFY(ElitCabinet::printJob(job,QDate(2026,10,14)).present);
        QCOMPARE(ElitCabinet::printJob(job,QDate(2026,10,14)).parameter,QString("EntryNo=123"));
        QVERIFY(!ElitCabinet::printJob(job,QDate(2026,10,15)).present);
        QVERIFY(ElitCabinet::printJob(QString(job).replace("DownloadInvoice","CreateReturn"),QDate(2026,10,14)).parameter.isEmpty());
        QVERIFY(ElitCabinet::printJob(QString(job).replace("EntryNo=123","EntryNo=123;other=1"),QDate(2026,10,14)).parameter.isEmpty());
        QVERIFY(ElitCabinet::printJob("<tr><td>now</td><td>14.10.26 - 14.10.26</td><td>waiting</td></tr>",QDate(2026,10,14)).present);
        QVERIFY(ElitCabinet::printJob(job+"<tr><td>now</td><td>14.10.26 - 14.10.26</td><td>waiting</td></tr>",QDate(2026,10,14)).parameter.isEmpty());
    }
    void elitPdfArchiveSafety() {
        const auto valid=QByteArray::fromBase64("UEsDBBQAAAAIALJsV1060mbSCwAAAAkAAAAFAAAAQS5wZGZTDXBx0y1JLS4BAFBLAQIUABQAAAAIALJsV1060mbSCwAAAAkAAAAFAAAAAAAAAAAAAACAAQAAAABBLnBkZlBLBQYAAAAAAQABADMAAAAuAAAAAAA=");
        auto badCrc=valid;badCrc[badCrc.indexOf("PK\001\002")+16]^=1;
        auto oversized=valid;const auto sizeOffset=oversized.indexOf("PK\001\002")+24;
        oversized[sizeOffset]=0;oversized[sizeOffset+1]=0;oversized[sizeOffset+2]=0;oversized[sizeOffset+3]=2;
        const QList<QByteArray> rejected={badCrc,oversized,
            QByteArray::fromBase64("UEsDBBQAAAAIALJsV1060mbSCwAAAAkAAAAIAAAALi4vQS5wZGZTDXBx0y1JLS4BAFBLAQIUABQAAAAIALJsV1060mbSCwAAAAkAAAAIAAAAAAAAAAAAAACAAQAAAAAuLi9BLnBkZlBLBQYAAAAAAQABADYAAAAxAAAAAAA="),
            QByteArray::fromBase64("UEsDBBQAAAAIALJsV1060mbSCwAAAAkAAAAFAAAAQS5wZGZTDXBx0y1JLS4BAFBLAwQUAAAACACybFddOtJm0gsAAAAJAAAABQAAAEEucGRmUw1wcdMtSS0uAQBQSwECFAAUAAAACACybFddOtJm0gsAAAAJAAAABQAAAAAAAAAAAAAAgAEAAAAAQS5wZGZQSwECFAAUAAAACACybFddOtJm0gsAAAAJAAAABQAAAAAAAAAAAAAAgAEuAAAAQS5wZGZQSwUGAAAAAAIAAgBmAAAAXAAAAAAA"),
            QByteArray::fromBase64("UEsDBBQAAAAIALJsV137OSuCBQAAAAMAAAAFAAAAQS5wZGZLSkwBAFBLAQIUABQAAAAIALJsV137OSuCBQAAAAMAAAAFAAAAAAAAAAAAAACAAQAAAABBLnBkZlBLBQYAAAAAAQABADMAAAAoAAAAAAA="),valid.left(40)};
        bool success=true;QString error;
        auto* thread=QThread::create([&]{
            const auto files=ElitPdfArchive::unpack(valid,{"A"},[]{return true;},&error);
            success=error.isEmpty() && files.value("A")==QByteArray("%PDF-test");
            error.clear();success &= ElitPdfArchive::unpack(valid,{"B"},[]{return true;},&error).isEmpty() && error.isEmpty();
            for(const auto& bytes:rejected) {
                error.clear();success &= ElitPdfArchive::unpack(bytes,{"A"},[]{return true;},&error).isEmpty() && !error.isEmpty();
            }
            error.clear();success &= ElitPdfArchive::unpack(valid,{"A"},[]{return false;},&error).isEmpty() && !error.isEmpty();
        });
        thread->start();QTRY_VERIFY_WITH_TIMEOUT(thread->isFinished(),30000);thread->wait();delete thread;
        QVERIFY2(success,qPrintable(error));
    }
    void elitCabinetLivePdfCopy() {
        if(qEnvironmentVariable("CS_ELIT_PDF_LIVE_READ")!="1")QSKIP("Explicit live PDF read opt-in required");
        QSettings saved(QSettings::NativeFormat,QSettings::UserScope,App::orgName(),App::appName());
        QString error;
        const auto plain=CloudSyncCredentials::unprotectToken(saved.value("integrations/elit/credentials_dpapi").toString(),&error);
        QVERIFY2(!plain.isEmpty(),"Saved credentials unavailable");
        const auto values=QJsonDocument::fromJson(plain.toUtf8()).object();
        GeneralSettingsService::ElitCredentials credentials;
        credentials.company=values.value("company").toString();credentials.login=values.value("login").toString();credentials.password=values.value("password").toString();
        ElitInvoiceStore::Document document;
        document.invoice.number="KAM26F058793";document.invoice.currency="UAH";
        document.invoice.posted=QDate(2026,10,23);document.invoice.grossCents=29850;
        document.identity=ElitInvoiceStore::identity(document.invoice);
        const auto account=ElitInvoiceStore::accountKey(credentials);
        const auto databasePath=db.databaseName();
        const auto before=PurchasePaymentService::balances(db,&error);QVERIFY(error.isEmpty());
        auto unchangedTables=[&] {
            QMap<QString,QList<QByteArray>> state;
            for(const auto& table:{"purchase_payment_balances","purchase_payment_events","purchase_payment_terms",
                                  "workshop_spare_payment_allocations","work_order_parts","workshop_inventory_purchases","workshop_spare_movements"}) {
                if(!db.tables().contains(QString::fromLatin1(table)))continue;
                QSqlQuery query(db);if(!query.exec("SELECT * FROM \""+QString::fromLatin1(table)+"\""))return QMap<QString,QList<QByteArray>>{};
                auto& rows=state[QString::fromLatin1(table)];
                while(query.next()) {
                    QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);
                    for(int i=0;i<query.record().count();++i)stream<<query.value(i);
                    rows.append(bytes);
                }
                std::sort(rows.begin(),rows.end());
            }
            return state;
        };
        const auto tableState=unchangedTables();QVERIFY(!tableState.isEmpty());
        bool success=false;QString path;
        auto* thread=QThread::create([&]{
            const QString connectionName="elit_pdf_copy";
            const auto cleanup=qScopeGuard([&]{QSqlDatabase::removeDatabase(connectionName);});
            {
                auto copy=QSqlDatabase::addDatabase("QSQLITE",connectionName);copy.setDatabaseName(databasePath);
                if(!copy.open()){error="Test copy unavailable";return;}
                ElitCabinet::Client client([]{return true;});
                if(!client.login(credentials)){error=client.error;return;}
                const auto pdf=client.invoice(document.invoice);
                if(!client.error.isEmpty()){error=client.error;return;}
                success=ElitInvoicePdf::save(copy,account,document,pdf.body,[]{return true;},&error);
                if(success) {
                    path=ElitInvoicePdf::paths(copy,account).value(document.identity);
                    ElitInvoiceStore::Snapshot snapshot;snapshot.account=account;snapshot.documents={document};
                    const auto repeat=ElitInvoicePdf::synchronize(copy,{},snapshot,[]{return true;});
                    success=repeat.existing==1 && repeat.downloaded==0 && repeat.missing==0;
                    if(success && qEnvironmentVariable("CS_ELIT_PDF_ALL")=="1") {
                        const auto all=ElitInvoiceStore::load(copy,account,&error);
                        if(!error.isEmpty() || all.documents.isEmpty()){success=false;return;}
                        const auto batch=ElitInvoicePdf::synchronize(copy,credentials,all,[]{return true;});
                        qInfo()<<"PDF batch: documents="<<all.documents.size()<<"downloaded="<<batch.downloaded
                               <<"cached="<<batch.existing<<"unavailable="<<batch.missing;
                        success=batch.downloaded+batch.existing==all.documents.size() && batch.missing==0 && !batch.cancelled;
                        if(!success)error=batch.errors.join("; ");
                        if(success) {
                            const auto repeated=ElitInvoicePdf::synchronize(copy,{},all,[]{return true;});
                            success=repeated.existing==all.documents.size() && repeated.missing==0 && repeated.downloaded==0;
                            const auto output=QDir(qEnvironmentVariable("CS_TAB_AUDIT_COPY")).filePath("verified-pdfs");
                            if(!qEnvironmentVariable("CS_TAB_AUDIT_COPY").isEmpty() && QDir().mkpath(output)) {
                                const auto cached=ElitInvoicePdf::paths(copy,account);
                                for(const auto& doc:all.documents) {
                                    const auto destination=QDir(output).filePath(doc.invoice.number+".pdf");
                                    if(!QFileInfo::exists(destination))success &= QFile::copy(cached.value(doc.identity),destination);
                                }
                            }
                        }
                    }
                }
                copy.close();
            }
        });
        thread->start();QTRY_VERIFY_WITH_TIMEOUT(thread->isFinished(),650000);thread->wait();delete thread;
        QVERIFY2(success,qPrintable(error));QVERIFY(QFileInfo::exists(path));
        QCOMPARE(PurchasePaymentService::balances(db,&error),before);
        QCOMPARE(unchangedTables(),tableState);
        QVERIFY(ElitInvoicePdf::paths(db,"another-account").isEmpty());
        QFile damaged(path);QVERIFY(damaged.open(QIODevice::WriteOnly));damaged.write("corrupted");damaged.close();
        QVERIFY(!ElitInvoicePdf::paths(db,account).contains(document.identity));
        qInfo()<<"Original eCat PDF downloaded and validated on isolated database; repeat skipped; payments unchanged";
    }
    void elitInvoiceDetailsAndSnapshots() {
        using namespace ElitInvoiceReader;
        const QByteArray prefix="<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body><b:";
        auto response=[&](const QByteArray& operation,const QByteArray& content) {
            return prefix+operation+"Response xmlns:b=\"http://buyer.elit.cz/\">"+content+"</b:"+operation+"Response></s:Body></s:Envelope>";
        };
        Invoice invoice; invoice.number="UA-1"; invoice.currency="UAH"; invoice.posted=QDate(2026,10,1); invoice.remainingCents=6000;
        const QByteArray line="<return><no>internal-code</no><crossReferenceNo>OEM-1</crossReferenceNo><description>Test</description><invoiceNo>UA-1</invoiceNo><quantity>2</quantity><amountIncludingVat>120</amountIncludingVat></return>";
        const QByteArray vat="<return><inclVat>120</inclVat><vatBase>100</vatBase><vatAmount>20</vatAmount></return>";
        QString error;
        QVERIFY(parseDetail(response("getInvoiceLines",line),&invoice,false,&error));
        QCOMPARE(invoice.grossCents,qint64(12000));
        QCOMPARE(invoice.lines[0].reference,QString("OEM-1"));
        QVERIFY(!invoice.detailsComplete);
        QVERIFY(parseDetail(response("getInvoiceVatSummary",vat),&invoice,true,&error));
        QVERIFY(invoice.detailsComplete);
        QVERIFY(!parseDetail(response("getInvoiceVatSummary",QByteArray(vat).replace("120","121")),&invoice,true,&error));
        QVERIFY(!parseDetail(response("getInvoiceLines",QByteArray(line).replace("UA-1","OTHER")),&invoice,false,&error));
        QVERIFY(!parseDetail(response("getInvoiceLines",QByteArray(line).replace(">2<",">nan<")),&invoice,false,&error));
        QVERIFY(!parseDetail(response("getInvoiceLines",""),&invoice,false,&error));
        QVERIFY(!parseDetail("<!DOCTYPE x>"+response("getInvoiceLines",line),&invoice,false,&error));
        QVERIFY(!parseDetail(response("getInvoiceLines",line).left(90),&invoice,false,&error));
        Result result; result.complete=true; result.fetchedAt=QDateTime::currentDateTime(); result.invoices={invoice};
        error.clear();
        const auto before=PurchasePaymentService::balances(db,&error); QVERIFY2(error.isEmpty(),qPrintable(error));
        QVERIFY2(ElitInvoiceStore::publish(db,"account-a",result,{},&error),qPrintable(error));
        const auto saved=ElitInvoiceStore::load(db,"account-a",&error); QVERIFY(error.isEmpty());
        QCOMPARE(saved.documents.size(),1); QVERIFY(saved.documents[0].isNew);
        QVERIFY(ElitInvoiceStore::load(db,"account-b",&error).documents.isEmpty());
        QVERIFY(!ElitInvoiceStore::publish(db,"account-a",result,{},&error));
        result.complete=false;
        QVERIFY(!ElitInvoiceStore::publish(db,"account-a",result,saved.revision,&error));
        QCOMPARE(ElitInvoiceStore::load(db,"account-a").revision,saved.revision);
        result.complete=true;
        result.invoices[0].remainingCents=0;
        QVERIFY(ElitInvoiceStore::publish(db,"account-a",result,saved.revision,&error));
        auto paid=ElitInvoiceStore::load(db,"account-a");
        QVERIFY(!paid.documents[0].isNew); QCOMPARE(paid.documents[0].invoice.remainingCents,qint64(0));
        result.invoices.clear();
        const auto lastSeen=paid.documents[0].lastSeen;
        QVERIFY(ElitInvoiceStore::publish(db,"account-a",result,paid.revision,&error));
        paid=ElitInvoiceStore::load(db,"account-a");
        QCOMPARE(paid.documents.size(),1);QVERIFY(paid.documents[0].missingFromLatest);
        QCOMPARE(paid.documents[0].lastSeen,lastSeen);
        QCOMPARE(paid.documents[0].invoice.remainingCents,qint64(0));
        result.invoices=paid.documents.isEmpty()?QVector<Invoice>{}:QVector<Invoice>{paid.documents[0].invoice};
        result.invoices[0].grossCents=std::numeric_limits<qint64>::min();
        QVERIFY(!ElitInvoiceStore::publish(db,"account-a",result,paid.revision,&error));
        QCOMPARE(ElitInvoiceStore::load(db,"account-a").revision,paid.revision);
        result.invoices[0]=paid.documents[0].invoice;
        result.invoices[0].lines[0].grossCents=std::numeric_limits<qint64>::min();
        QVERIFY(!ElitInvoiceStore::publish(db,"account-a",result,paid.revision,&error));
        QCOMPARE(ElitInvoiceStore::load(db,"account-a").revision,paid.revision);
        error.clear(); QCOMPARE(PurchasePaymentService::balances(db,&error),before); QVERIFY(error.isEmpty());
    }
    void supplierInvoiceLedgerIsolation() {
        QSqlQuery q(db);
        QVERIFY(q.exec("UPDATE work_order_parts SET store='ELIT' WHERE id=1"));
        PartsQueryService::invalidateCache();
        QString error;
        const auto payments=PurchasePaymentService::balances(db,&error);
        QVERIFY2(error.isEmpty(),qPrintable(error));
        auto ledger=SupplierInvoiceLedger::load(db,"ledger-test");
        QVERIFY2(ledger.error.isEmpty(),qPrintable(ledger.error));
        QCOMPARE(SupplierInvoiceLedger::debtTotal(ledger),qint64(20000));
        ElitInvoiceReader::Invoice invoice;
        invoice.number="TEST-1"; invoice.posted=QDate(2026,9,1); invoice.currency="UAH";
        invoice.grossCents=20000; invoice.remainingCents=10000; invoice.detailsComplete=true;
        invoice.lines={{"internal","OEM","Part",2,20000}};
        ElitInvoiceReader::Result result;
        result.complete=true; result.fetchedAt=QDateTime::currentDateTime(); result.invoices={invoice};
        QVERIFY2(ElitInvoiceStore::publish(db,"ledger-test",result,{},&error),qPrintable(error));
        ledger=SupplierInvoiceLedger::load(db,"ledger-test");
        QVERIFY2(ledger.error.isEmpty(),qPrintable(ledger.error));
        QVERIFY(!ledger.hasUnresolved);
        QCOMPARE(SupplierInvoiceLedger::debtTotal(ledger),qint64(10000));
        QVERIFY(ledger.entries[0].matched); QVERIFY(ledger.entries[0].discrepancy);
        QVERIFY2(SupplierInvoiceLedger::confirmLinks(db,"ledger-test",ledger.snapshot.revision,
            ledger.entries[0].key,ledger.entries[0].links,&error),qPrintable(error));
        QCOMPARE(PurchasePaymentService::balances(db,&error),payments);
        result.invoices[0].remainingCents=-1;
        QVERIFY(ElitInvoiceStore::publish(db,"ledger-test",result,ledger.snapshot.revision,&error));
        ledger=SupplierInvoiceLedger::load(db,"ledger-test");
        QVERIFY2(ledger.error.isEmpty(),qPrintable(ledger.error));
        QVERIFY(ledger.hasUnresolved);
        QCOMPARE(SupplierInvoiceLedger::debtTotal(ledger),qint64(20000));
        QCOMPARE(SupplierInvoiceLedger::debtTotal(SupplierInvoiceLedger::load(db,"another-account")),qint64(20000));
        QCOMPARE(PurchasePaymentService::balances(db,&error),payments);
    }
    void elitPurchaseAcrossInvoiceLines() {
        QString error;QSqlQuery q(db);
        QVERIFY(q.exec("UPDATE work_order_parts SET store='ELIT' WHERE id=1"));PartsQueryService::invalidateCache();
        WorkshopInventoryPurchaseInput stock;stock.purchasedOn=QDate(2026,9,1);stock.materialName="Rounding";stock.unit="pcs";
        stock.quantity=3;stock.unitCost=0.335;stock.supplier="ELIT";stock.article="ROUND";stock.paidAmount=0;
        qint64 receipt=0;QVERIFY(WorkshopInventoryService::addPurchase(db,stock,&receipt,&error));
        const auto payments=PurchasePaymentService::balances(db,&error);QVERIFY(error.isEmpty());
        for(const auto& table:db.tables())if(table.startsWith("purchase_payment_"))
            for(const QString& operation:{QString("INSERT"),QString("UPDATE"),QString("DELETE")})
                QVERIFY(q.exec(QString("CREATE TEMP TRIGGER split_forbid_%1_%2 BEFORE %2 ON %1 BEGIN SELECT RAISE(ABORT,'Payment mutation'); END").arg(table,operation)));
        ElitInvoiceReader::Invoice invoice;invoice.number="SPLIT-LINES";invoice.posted=QDate(2026,9,1);invoice.currency="UAH";
        invoice.grossCents=20000;invoice.remainingCents=10000;invoice.detailsComplete=true;
        invoice.lines={{"a","OEM","Part",0.5,5000},{"b","OEM","Part",1.5,15000}};
        auto rounding=invoice;rounding.number="ROUND-LINES";rounding.grossCents=101;rounding.remainingCents=0;
        rounding.lines={{"c","ROUND","Rounding",1,34},{"d","ROUND","Rounding",2,67}};
        ElitInvoiceReader::Result result;result.complete=true;result.fetchedAt=QDateTime::currentDateTime();result.invoices={invoice,rounding};
        int linked=0,pending=0;
        QVERIFY2(SupplierInvoiceLedger::publishAndLink(db,"split-lines",result,{},[]{return true;},&linked,&pending,&error),qPrintable(error));
        QCOMPARE(linked,2);QCOMPARE(pending,0);
        auto ledger=SupplierInvoiceLedger::load(db,"split-lines");QVERIFY(ledger.error.isEmpty());QVERIFY(!ledger.hasUnresolved);
        QCOMPARE(SupplierInvoiceLedger::debtTotal(ledger),qint64(10000));
        QCOMPARE(SupplierInvoiceLedger::savedLinks(db,"split-lines").size(),2);
        for(const auto& entry:ledger.entries)if(entry.remote) {
            QVERIFY(entry.counted);QCOMPARE(entry.items.size(),1);QCOMPARE(entry.allocations.size(),2);
            QCOMPARE(entry.links[0],entry.links[1]);
            QCOMPARE(entry.allocations[0].totalCents+entry.allocations[1].totalCents,entry.total);
            QCOMPARE(entry.allocations[0].quantity+entry.allocations[1].quantity,entry.items[0].purchase.quantity);
            QCOMPARE(entry.localRemaining,entry.items[0].purchase.remainingCents());
            auto partial=entry.links;partial[1].clear();
            QVERIFY(SupplierInvoiceLedger::confirmLinks(db,"split-lines",ledger.snapshot.revision,entry.key,partial,&error));
            auto duplicate=entry.links;duplicate[0].append(duplicate[0].first());
            QVERIFY(!SupplierInvoiceLedger::confirmLinks(db,"split-lines",ledger.snapshot.revision,entry.key,duplicate,&error));
            QVERIFY2(SupplierInvoiceLedger::confirmLinks(db,"split-lines",ledger.snapshot.revision,entry.key,entry.links,&error),qPrintable(error));
        }
        QCOMPARE(PurchasePaymentService::balances(db,&error),payments);
        // Repeated sync neither duplicates purchase projections nor changes payments.
        QVERIFY2(SupplierInvoiceLedger::publishAndLink(db,"split-lines",result,ledger.snapshot.revision,[]{return true;},&linked,&pending,&error),qPrintable(error));
        QCOMPARE(linked,0);QCOMPARE(pending,0);QCOMPARE(SupplierInvoiceLedger::savedLinks(db,"split-lines").size(),2);
        QCOMPARE(PurchasePaymentService::balances(db,&error),payments);
        // The same purchase cannot silently be claimed by another document.
        auto competing=invoice;competing.number="OTHER-SPLIT";result.invoices.append(competing);
        QVERIFY(ElitInvoiceStore::publish(db,"ambiguous-split",result,{},&error));
        const auto ambiguous=SupplierInvoiceLedger::load(db,"ambiguous-split");QVERIFY(ambiguous.hasUnresolved);
        for(const auto& entry:ambiguous.entries)if(entry.remote && entry.number!="ROUND-LINES")QVERIFY(!entry.matched);
        // A saved split becomes unresolved if the original purchase quantity changes.
        QVERIFY(q.exec("UPDATE work_order_parts SET qty=3 WHERE id=1"));PartsQueryService::invalidateCache();
        ledger=SupplierInvoiceLedger::load(db,"split-lines");QVERIFY(ledger.hasUnresolved);
        for(const auto& entry:ledger.entries)if(entry.remote && entry.number==invoice.number){QVERIFY(!entry.counted);QVERIFY(entry.allocations.isEmpty());}
        QCOMPARE(PurchasePaymentService::balances(db,&error),payments);
    }
    void elitPurchaseAcrossDocuments() {
        QString error;QSqlQuery q(db);QVERIFY(q.exec("UPDATE work_order_parts SET store='ELIT' WHERE id=1"));PartsQueryService::invalidateCache();
        GeneralSettingsService::ElitCredentials credentials;QVERIFY(GeneralSettingsService::loadElitCredentials(&credentials));
        credentials.login="cross-documents";credentials.password.clear();credentials.rememberPassword=false;QVERIFY(GeneralSettingsService::saveElitCredentials(credentials,&error));
        const auto account=ElitInvoiceStore::accountKey(credentials);
        const auto existingPurchase=PurchasePaymentService::detail(db,"part",1,"order-a",&error);
        QVERIFY2(PurchasePaymentService::pay(db,existingPurchase,10000,&error),qPrintable(error));
        WorkshopInventoryPurchaseInput stock;stock.purchasedOn=QDate(2026,9,1);stock.materialName="Rounding";stock.unit="pcs";
        stock.quantity=3;stock.unitCost=0.335;stock.supplier="ELIT";stock.article="ROUND";stock.paidAmount=0;
        qint64 receipt=0;QVERIFY(WorkshopInventoryService::addPurchase(db,stock,&receipt,&error));
        const auto payments=PurchasePaymentService::balances(db,&error);QVERIFY(error.isEmpty());
        for(const auto& table:db.tables())if(table.startsWith("purchase_payment_"))
            for(const QString& operation:{QString("INSERT"),QString("UPDATE"),QString("DELETE")})
                QVERIFY(q.exec(QString("CREATE TEMP TRIGGER cross_forbid_%1_%2 BEFORE %2 ON %1 BEGIN SELECT RAISE(ABORT,'Payment mutation'); END").arg(table,operation)));
        ElitInvoiceReader::Invoice first;first.number="CROSS-A";first.currency="UAH";first.posted=QDate(2026,9,1);
        first.grossCents=10034;first.remainingCents=5000;first.detailsComplete=true;
        first.lines={{"a","OEM","Part",1,10000},{"r1","ROUND","Rounding",1,34}};
        auto second=first;second.number="CROSS-B";second.posted=QDate(2026,9,2);second.grossCents=5067;second.remainingCents=0;
        second.lines={{"b","OEM","Part",0.5,5000},{"r2","ROUND","Rounding",2,67}};
        auto confirmDifferentDate = [&](const QString& number) {
            const auto candidateLedger=SupplierInvoiceLedger::load(db,account);
            QVERIFY2(candidateLedger.error.isEmpty(),qPrintable(candidateLedger.error));
            bool found=false;
            for(const auto& entry:candidateLedger.entries) if(entry.remote && entry.number==number) {
                found=true;QVERIFY(!entry.matched);
                QVector<QVector<QString>> links;
                for(int line=0;line<entry.lines.size();++line) {
                    const auto candidates=SupplierInvoiceLedger::linkCandidates(candidateLedger,entry,line);
                    QCOMPARE(candidates.size(),1);
                    links.append({candidates.first().key});
                }
                QVERIFY2(SupplierInvoiceLedger::confirmLinks(db,account,candidateLedger.snapshot.revision,entry.key,links,&error),qPrintable(error));
            }
            QVERIFY(found);
        };
        ElitInvoiceReader::Result result;result.complete=true;result.fetchedAt=QDateTime::currentDateTime();result.invoices={first,second};
        int linked=0,pending=0;
        QVERIFY2(SupplierInvoiceLedger::publishAndLink(db,account,result,{},[]{return true;},&linked,&pending,&error),qPrintable(error));
        QCOMPARE(linked,1);QCOMPARE(pending,1);
        // Different dates never silently create the second association.
        for(const auto& link:SupplierInvoiceLedger::activeLinks(db))
            for(const auto& document:link.documents) QVERIFY(document.number!="CROSS-B");
        confirmDifferentDate("CROSS-B");
        auto ledger=SupplierInvoiceLedger::load(db,account);QVERIFY2(ledger.error.isEmpty(),qPrintable(ledger.error));
        QCOMPARE(SupplierInvoiceLedger::debtTotal(ledger),qint64(7500));
        double remainder=0;for(const auto& entry:ledger.entries)if(!entry.remote)for(const auto& item:entry.items)if(item.purchase.id==1 && item.purchase.source=="part")remainder+=item.purchase.quantity;
        QCOMPARE(remainder,0.5);
        auto projection=SupplierInvoiceLedger::activeLinks(db);QCOMPARE(projection.size(),2);
        for(const auto& link:projection){QVERIFY(link.apportioned);QCOMPARE(link.documents.size(),2);QVERIFY(link.number.contains("CROSS-A"));QVERIFY(link.number.contains("CROSS-B"));}
        auto invoices=InvoiceRegistryService::load(db,&error);QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(invoices.size(),2);
        qint64 registryTotal=0,registryPaid=0;
        for(const auto& invoice:invoices){QVERIFY(invoice.apportioned);registryPaid+=invoice.paidCents;registryTotal+=invoice.totalCents;QVERIFY(!PurchasePaymentService::payInvoice(db,invoice,&error));}
        QCOMPARE(registryTotal,qint64(15101));
        QCOMPARE(registryPaid,qint64(7500)); // The remaining 2500 paid cents belong to the unallocated half unit.
        // Cache projection fixtures test many-to-many links; live tests separately validate actual PDFs.
        QVERIFY(q.exec("CREATE TABLE elit_invoice_pdf_files(account TEXT NOT NULL,invoice_id TEXT NOT NULL,file_path TEXT NOT NULL,sha256 TEXT NOT NULL,PRIMARY KEY(account,invoice_id))"));
        QHash<QString,QString> expectedPaths;
        for(const auto& document:{first,second}) {
            const auto path=dir.filePath(document.number+".cache-fixture");const auto bytes=document.number.toUtf8();
            QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(bytes),bytes.size());file.close();
            q.prepare("INSERT INTO elit_invoice_pdf_files VALUES(?,?,?,?)");q.addBindValue(account);
            q.addBindValue(ElitInvoiceStore::identity(document));q.addBindValue(path);q.addBindValue(ElitInvoicePdf::digest(bytes));QVERIFY(q.exec());
            expectedPaths.insert(document.number,path);
        }
        projection=SupplierInvoiceLedger::activeLinks(db);
        for(const auto& link:projection) {
            QVERIFY(link.filePath.isEmpty());
            for(const auto& document:link.documents)QCOMPARE(document.filePath,expectedPaths.value(document.number));
        }
        error.clear();invoices=InvoiceRegistryService::load(db,&error);QVERIFY2(error.isEmpty(),qPrintable(error));
        for(const auto& invoice:invoices)QCOMPARE(invoice.filePath,expectedPaths.value(invoice.number));
        QVERIFY(ElitInvoicePdf::paths(db,"wrong-account").isEmpty());
        error.clear();
        QCOMPARE(PurchasePaymentService::balances(db,&error),payments);
        {
            SupplierDebtOverview view;view.resize(1200,720);view.show();view.refresh();
            auto* table=view.findChild<QTableView*>("supplierInvoiceLedger");QVERIFY(table);
            QTRY_VERIFY_WITH_TIMEOUT(view.findChild<QPushButton*>("debtTile0")->isEnabled(),10000);
            // Debt view excludes CROSS-B: its observed supplier remainder is zero.
            view.setFilters({},"invoice",{});QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(),1,10000);table->selectRow(0);
            bool shown=false;
            QTimer::singleShot(50,[&]{
                auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)return;
                auto* browser=dialog->findChild<QTextBrowser*>();shown=browser && browser->toPlainText().contains(QString::fromUtf8("пропорційно лише для порівняння"));
                const auto screenshots=qEnvironmentVariable("CS_TEST_SCREENSHOTS");if(!screenshots.isEmpty())dialog->grab().save(QDir(screenshots).filePath("elit-cross-document-details.png"));dialog->reject();
            });
            view.findChild<QPushButton*>("debtInvoiceDetails")->click();QVERIFY(shown);
        }
        // A later invoice may claim exactly the still-unallocated half unit.
        auto third=second;third.number="CROSS-C";third.posted=QDate(2026,9,3);third.grossCents=5000;third.remainingCents=5000;third.lines={{"c","OEM","Part",0.5,5000}};result.invoices.append(third);
        QVERIFY2(SupplierInvoiceLedger::publishAndLink(db,account,result,ledger.snapshot.revision,[]{return true;},&linked,&pending,&error),qPrintable(error));
        QCOMPARE(linked,0);QCOMPARE(pending,1);
        confirmDifferentDate("CROSS-C");
        ledger=SupplierInvoiceLedger::load(db,account);QCOMPARE(SupplierInvoiceLedger::debtTotal(ledger),qint64(10000));
        QCOMPARE(SupplierInvoiceLedger::activeLinks(db).value(PurchasePaymentService::key("part",1,"order-a")).documents.size(),3);
        // Publication and the new multi-document projection roll back together.
        QVERIFY(q.exec("CREATE TEMP TRIGGER fail_cross_projection BEFORE INSERT ON elit_purchase_invoice_allocations BEGIN SELECT RAISE(ABORT,'projection failure'); END"));
        QVERIFY(!SupplierInvoiceLedger::publishAndLink(db,account,result,ledger.snapshot.revision,[]{return true;},&linked,&pending,&error));
        QCOMPARE(ElitInvoiceStore::load(db,account).revision,ledger.snapshot.revision);
        QCOMPARE(SupplierInvoiceLedger::activeLinks(db).value(PurchasePaymentService::key("part",1,"order-a")).documents.size(),3);
        QVERIFY(q.exec("DROP TRIGGER fail_cross_projection"));
        error.clear();
        QCOMPARE(PurchasePaymentService::balances(db,&error),payments);
        // A fourth claim exceeds stock and remains unresolved without stealing it.
        auto fourth=third;fourth.number="CROSS-D";result.invoices.append(fourth);
        QVERIFY(SupplierInvoiceLedger::publishAndLink(db,account,result,ledger.snapshot.revision,[]{return true;},&linked,&pending,&error));
        QCOMPARE(pending,1);ledger=SupplierInvoiceLedger::load(db,account);QCOMPARE(SupplierInvoiceLedger::debtTotal(ledger),qint64(10000));
        for(const auto& entry:ledger.entries)if(entry.remote && entry.number==fourth.number){QVERIFY(!entry.matched);QVERIFY(!entry.counted);}
        QCOMPARE(PurchasePaymentService::balances(db,&error),payments);
        QVERIFY(SupplierInvoiceLedger::savedLinks(db,"other-account").isEmpty());
        // Changed originals stay blocked after repeated sync; stale baselines
        // must not be discarded and accidentally revalidated on the next run.
        QVERIFY(q.exec("UPDATE work_order_parts SET qty=3 WHERE id=1"));PartsQueryService::invalidateCache();
        for(int attempt=0;attempt<2;++attempt) {
            QVERIFY(SupplierInvoiceLedger::publishAndLink(db,account,result,ledger.snapshot.revision,[]{return true;},&linked,&pending,&error));
            ledger=SupplierInvoiceLedger::load(db,account);
            for(const auto& entry:ledger.entries)if(entry.remote)QVERIFY(!entry.counted);
        }
        QCOMPARE(PurchasePaymentService::balances(db,&error),payments);
    }
    void elitSplitLineDetails() {
        QString error;QSqlQuery q(db);QVERIFY(q.exec("UPDATE work_order_parts SET store='ELIT' WHERE id=1"));PartsQueryService::invalidateCache();
        GeneralSettingsService::ElitCredentials credentials;QVERIFY(GeneralSettingsService::loadElitCredentials(&credentials));
        credentials.login="split-line-ui";credentials.password.clear();credentials.rememberPassword=false;
        QVERIFY(GeneralSettingsService::saveElitCredentials(credentials,&error));
        const auto account=ElitInvoiceStore::accountKey(credentials);
        ElitInvoiceReader::Invoice invoice;invoice.number="SPLIT-UI";invoice.currency="UAH";invoice.posted=QDate(2026,9,1);
        invoice.grossCents=20000;invoice.remainingCents=10000;invoice.detailsComplete=true;
        invoice.lines={{"a","OEM","Part",0.5,5000},{"b","OEM","Part",1.5,15000}};
        ElitInvoiceReader::Result result;result.complete=true;result.fetchedAt=QDateTime::currentDateTime();result.invoices={invoice};
        int linked=0,pending=0;QVERIFY(SupplierInvoiceLedger::publishAndLink(db,account,result,{},[]{return true;},&linked,&pending,&error));
        SupplierDebtOverview view;view.resize(1200,720);view.show();view.refresh();
        auto* table=view.findChild<QTableView*>("supplierInvoiceLedger");QVERIFY(table);
        QTRY_VERIFY_WITH_TIMEOUT(view.findChild<QPushButton*>("debtTile0")->isEnabled(),10000);
        view.setFilters({},"invoice",{});QCOMPARE(table->model()->rowCount(),1);table->selectRow(0);
        bool shown=false;
        QTimer::singleShot(50,[&]{
            auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)return;
            auto* browser=dialog->findChild<QTextBrowser*>();
            shown=browser && browser->toPlainText().contains(QString::fromUtf8("Розподіл закупівель між рядками"))
                && browser->toPlainText().contains("0.5") && browser->toPlainText().contains("1.5");
            const auto screenshots=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
            if(!screenshots.isEmpty())dialog->grab().save(QDir(screenshots).filePath("elit-split-line-details.png"));
            dialog->reject();
        });
        view.findChild<QPushButton*>("debtInvoiceDetails")->click();QVERIFY(shown);
    }
    void elitSharedDebtSummary() {
        QString error;QSqlQuery q(db);
        QVERIFY(q.exec("UPDATE work_order_parts SET store='ELIT' WHERE id=1"));
        PartsQueryService::invalidateCache();
        const auto payments=PurchasePaymentService::balances(db,&error);QVERIFY(error.isEmpty());
        const auto debts=PurchasePaymentService::debts(db,&error);QVERIFY2(error.isEmpty(),qPrintable(error));
        GeneralSettingsService::ElitCredentials credentials;
        QVERIFY(GeneralSettingsService::loadElitCredentials(&credentials));
        credentials.login="summary-test";credentials.password.clear();credentials.rememberPassword=false;
        QVERIFY(GeneralSettingsService::saveElitCredentials(credentials,&error));
        const auto account=ElitInvoiceStore::accountKey(credentials);
        ElitInvoiceReader::Invoice invoice;invoice.number="SUMMARY";invoice.currency="UAH";
        invoice.posted=QDate(2026,9,1);invoice.grossCents=20000;invoice.remainingCents=10000;invoice.detailsComplete=true;
        invoice.due=QDate::currentDate().addDays(-2);
        invoice.lines={{"item","OEM","Part",2,20000}};
        ElitInvoiceReader::Result result;result.complete=true;result.fetchedAt=QDateTime::currentDateTime();result.invoices={invoice};
        QVERIFY(ElitInvoiceStore::publish(db,account,result,{},&error));
        auto ledger=SupplierInvoiceLedger::load(db,account);QVERIFY(ledger.error.isEmpty());
        const auto name=ledger.entries[0].supplier;
        auto summary=SupplierInvoiceLedger::summary(db,debts,{},&error);QVERIFY(error.isEmpty());
        QVERIFY(summary.remote);QCOMPARE(summary.totals.value(name),qint64(10000));
        int count=0;double amount=0;
        QVERIFY(MainWindowMetricsService::loadUnpaidPurchasesSummary(count,amount));
        QCOMPARE(amount,100.0);QCOMPARE(count,1);
        const auto html=PurchaseDebtsPage::summaryHtml(debts,27,{},&summary.totals);
        QVERIFY(html.contains(FormatUtils::money(100,2)));
        SupplierDebtReconciliation fact;fact.supplier=name;fact.computedCents=20000;fact.actualCents=15000;
        QVERIFY2(SupplierDebtReconciliationService::save(db,{fact},&error),qPrintable(error));
        const auto facts=SupplierDebtReconciliationService::load(db,&error);
        summary=SupplierInvoiceLedger::summary(db,debts,facts,&error);QVERIFY(error.isEmpty());
        QCOMPARE(summary.totals.value(name),qint64(10000));QVERIFY(!summary.manualSuppliers.contains(name));
        QVERIFY(MainWindowMetricsService::loadUnpaidPurchasesSummary(count,amount));QCOMPARE(amount,100.0);
        {
            SupplierDebtOverview view;view.resize(1200,720);view.show();view.refresh();
            auto* tile=view.findChild<QPushButton*>("debtTile0");QVERIFY(tile);
            QTRY_VERIFY_WITH_TIMEOUT(tile->isEnabled(),10000);
            QVERIFY(tile->text().contains(FormatUtils::money(100,2)));
            QVERIFY(view.findChild<QPushButton*>("debtTile1")->text().contains(FormatUtils::money(100,2)));
            QVERIFY(view.reportHtml().contains(FormatUtils::money(100,2)));
        }
        // Matching is independent of the remote liability; local debt is not added again.
        result.invoices[0].lines[0].reference="NO-MATCH";
        QVERIFY(ElitInvoiceStore::publish(db,account,result,ledger.snapshot.revision,&error));
        summary=SupplierInvoiceLedger::summary(db,debts,{},&error);QVERIFY(error.isEmpty());
        QCOMPARE(summary.totals.value(name),qint64(10000));
        auto reportFacts=facts;
        const auto reporting=SupplierInvoiceLedger::reportingDebts(db,debts,&reportFacts,&error);
        QVERIFY(error.isEmpty());QCOMPARE(reporting.size(),1);
        QCOMPARE(reporting.first().source,QString("remote_invoice"));
        QCOMPARE(reporting.first().remainingCents(),qint64(10000));
        QVERIFY(!reportFacts.contains(name));
        credentials.login="other-account";QVERIFY(GeneralSettingsService::saveElitCredentials(credentials,&error));
        summary=SupplierInvoiceLedger::summary(db,debts,{},&error);QVERIFY(!summary.remote);
        QCOMPARE(summary.totals.value(name),qint64(20000));
        QCOMPARE(PurchasePaymentService::balances(db,&error),payments);QVERIFY(error.isEmpty());
    }
    void elitApprovedDocumentWorkingCopy() {
        QVERIFY(!qEnvironmentVariable("CS_TAB_AUDIT_COPY").isEmpty());
        QVERIFY(QFileInfo(db.databaseName()).absoluteFilePath().startsWith(App::sessionDir()));
        QSqlQuery q(db);QVERIFY(q.exec("SELECT account FROM elit_invoice_snapshots"));QVERIFY(q.next());
        const auto account=q.value(0).toString();q.finish();QString error;
        const auto payments=PurchasePaymentService::balances(db,&error);QVERIFY(error.isEmpty());
        const auto snapshot=ElitInvoiceStore::load(db,account,&error);QVERIFY(error.isEmpty());
        QVector<ElitDocumentAssociation::Candidate> selected;
        for(const auto& c:ElitDocumentAssociation::candidates(db,account,&error))
            if(c.orderNumber==538 && c.number=="KAM26F058997" && QStringList{"EL728.760","EL908.380","059121149B"}.contains(c.article))selected.append(c);
        QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(selected.size(),3);
        for(const auto& c:selected){QVERIFY(!c.exact);QCOMPARE(c.partDate,QDate(2026,10,26));QCOMPARE(c.invoiceDate,QDate(2026,10,24));}
        QVERIFY2(ElitDocumentAssociation::confirm(db,account,selected,false,&error),qPrintable(error));
        QVERIFY2(ElitDocumentAssociation::confirm(db,account,selected,false,&error),qPrintable(error));
        const auto links=SupplierInvoiceLedger::savedLinks(db,account);
        QJsonArray exported;
        for(const auto& c:selected){QCOMPARE(links.value(c.purchaseKey).number,c.number);
            exported.append(QJsonObject{{"account",account},{"purchase_key",c.purchaseKey},{"invoice_id",c.invoiceId},{"line",c.line},{"fingerprint",c.fingerprint}});}
        QCOMPARE(PurchasePaymentService::balances(db,&error),payments);QVERIFY(error.isEmpty());
        QCOMPARE(ElitInvoiceStore::load(db,account).revision,snapshot.revision);
        QVERIFY(q.exec("PRAGMA integrity_check"));QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("ok"));q.finish();
        QFile output(QDir(qEnvironmentVariable("CS_TEST_DATA_ROOT")).filePath("approved-538-associations.json"));QVERIFY(output.open(QIODevice::WriteOnly));output.write(QJsonDocument(exported).toJson());
    }
    void elitDocumentAssociations() {
        QSqlQuery q(db);QString error;
        QVERIFY(q.exec("UPDATE work_order_parts SET store='ELIT' WHERE id=1"));
        PartsQueryService::invalidateCache();
        const auto balances=PurchasePaymentService::balances(db,&error);QVERIFY(error.isEmpty());
        ElitInvoiceReader::Invoice inv;inv.number="DOC-1";inv.currency="UAH";inv.posted=QDate(2026,9,1);
        inv.grossCents=18000;inv.remainingCents=5000;inv.detailsComplete=true;inv.lines={{"OEM","","part",2,18000}};
        ElitInvoiceReader::Result input;input.complete=true;input.fetchedAt=QDateTime::currentDateTime();input.invoices={inv};
        int linked=0,pending=0;
        QVERIFY2(SupplierInvoiceLedger::publishAndLink(db,"documents",input,{},[]{return true;},&linked,&pending,&error),qPrintable(error));
        QCOMPARE(linked,1);
        const auto key=PurchasePaymentService::key("part",1,"order-a");
        QCOMPARE(SupplierInvoiceLedger::savedLinks(db,"documents").value(key).number,QString("DOC-1"));
        auto options=ElitDocumentAssociation::candidates(db,"documents",&error);QVERIFY(error.isEmpty());QCOMPARE(options.size(),1);QVERIFY(options[0].exact);
        QVERIFY(ElitDocumentAssociation::confirm(db,"documents",options,false,&error)); // replay
        QVERIFY(q.exec("SELECT COUNT(*) FROM elit_document_associations"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),1);q.finish();
        const auto snapshot=ElitInvoiceStore::load(db,"documents");
        QCOMPARE(snapshot.documents[0].invoice.remainingCents,qint64(5000));
        for(const auto& e:SupplierInvoiceLedger::load(db,"documents").entries)if(e.remote)QVERIFY(!e.matched); // no fictitious financial reconciliation
        QCOMPARE(PurchasePaymentService::balances(db,&error),balances);QVERIFY(error.isEmpty());
        QVERIFY(q.exec("UPDATE work_order_parts SET ordered_at='2026-09-03' WHERE id=1"));PartsQueryService::invalidateCache();
        QVERIFY(ElitDocumentAssociation::links(db,"documents").isEmpty()); // stale association hidden
        QVERIFY(!ElitDocumentAssociation::confirm(db,"documents",options,false,&error));
        options=ElitDocumentAssociation::candidates(db,"documents",&error);QCOMPARE(options.size(),1);QVERIFY(!options[0].exact);
        QVERIFY(!ElitDocumentAssociation::confirm(db,"documents",options,true,&error));
        QVERIFY2(ElitDocumentAssociation::confirm(db,"documents",options,false,&error),qPrintable(error));
        QCOMPARE(ElitDocumentAssociation::links(db,"documents").size(),1);
        QVERIFY(q.exec("CREATE TEMP TRIGGER deny_document BEFORE INSERT ON elit_document_associations BEGIN SELECT RAISE(ABORT,'test'); END"));
        QVERIFY(q.exec("UPDATE work_order_parts SET ordered_at='2026-09-04' WHERE id=1"));PartsQueryService::invalidateCache();
        const auto next=ElitDocumentAssociation::candidates(db,"documents",&error);
        QVERIFY(!ElitDocumentAssociation::confirm(db,"documents",next,false,&error));
        QVERIFY(q.exec("SELECT fingerprint FROM elit_document_associations"));QVERIFY(q.next());QCOMPARE(q.value(0).toString(),options[0].fingerprint);q.finish();
        QVERIFY(q.exec("DROP TRIGGER deny_document"));
        ActivityAuditService::setCurrentActor({"master","Master","master","test"});
        QVERIFY(!ElitDocumentAssociation::confirm(db,"documents",next,false,&error));
        ActivityAuditService::setCurrentActor({"owner","Owner","owner","test"});
        error.clear();QCOMPARE(PurchasePaymentService::balances(db,&error),balances);QVERIFY(error.isEmpty());
    }
    void elitDocumentAmbiguityAndAliases() {
        QSqlQuery q(db);QString error;
        QVERIFY(q.exec("UPDATE work_order_parts SET store='ELIT',ordered_at='2026-10-26',part_number='059121149B',name='Seal',qty=1 WHERE id=1"));
        PartsQueryService::invalidateCache();
        ElitInvoiceReader::Invoice inv;inv.number="DOC-A";inv.currency="UAH";inv.posted=QDate(2026,10,24);
        inv.grossCents=58908;inv.remainingCents=0;inv.detailsComplete=true;inv.lines={{"VW.059121149B","","Seel",1,58908}};
        ElitInvoiceReader::Result input;input.complete=true;input.fetchedAt=QDateTime::currentDateTime();input.invoices={inv};
        QVERIFY(ElitInvoiceStore::publish(db,"aliases",input,{},&error));
        auto candidates=ElitDocumentAssociation::candidates(db,"aliases",&error);QCOMPARE(candidates.size(),1);QVERIFY(!candidates[0].exact);
        int count=0;QVERIFY(ElitDocumentAssociation::autoLink(db,"aliases",&count,&error));QCOMPARE(count,0);
        QVERIFY(ElitDocumentAssociation::confirm(db,"aliases",candidates,false,&error));
        QVERIFY(q.exec("DELETE FROM elit_document_associations"));
        QVERIFY(q.exec("UPDATE work_order_parts SET ordered_at='2026-10-24',part_number='VW.059121149B',name='Seel' WHERE id=1"));PartsQueryService::invalidateCache();
        auto other=inv;other.number="DOC-B";input.invoices.append(other);
        QVERIFY(ElitInvoiceStore::publish(db,"aliases",input,ElitInvoiceStore::load(db,"aliases").revision,&error));
        QVERIFY(ElitDocumentAssociation::autoLink(db,"aliases",&count,&error));QCOMPARE(count,0); // two invoices
        candidates=ElitDocumentAssociation::candidates(db,"aliases",&error);QCOMPARE(candidates.size(),2);
        QVERIFY(!ElitDocumentAssociation::confirm(db,"aliases",candidates,false,&error)); // same part twice, atomic rollback
        QVERIFY(ElitDocumentAssociation::links(db,"aliases").isEmpty());
        input.invoices={inv};QVERIFY(ElitInvoiceStore::publish(db,"aliases",input,ElitInvoiceStore::load(db,"aliases").revision,&error));
        QVERIFY(q.exec("UPDATE work_order_parts SET store='ELIT',ordered_at='2026-10-24',part_number='VW.059121149B',name='Seel',qty=1 WHERE id=2"));PartsQueryService::invalidateCache();
        QVERIFY(ElitDocumentAssociation::autoLink(db,"aliases",&count,&error));QCOMPARE(count,0); // one unit claimed twice
        candidates=ElitDocumentAssociation::candidates(db,"aliases",&error);QCOMPARE(candidates.size(),2);
        QVERIFY(!ElitDocumentAssociation::confirm(db,"aliases",candidates,false,&error));
        QVERIFY(ElitDocumentAssociation::links(db,"aliases").isEmpty());
        QVERIFY(q.exec("UPDATE work_order_parts SET store='Other' WHERE id=2"));PartsQueryService::invalidateCache();
        QVERIFY(ElitDocumentAssociation::autoLink(db,"aliases",&count,&error));QCOMPARE(count,1);
        QVERIFY(ElitDocumentAssociation::links(db,"other-account").isEmpty());
    }
    void elitManualLinkProjection() {
        QSqlQuery q(db); QString error;
        QVERIFY(q.exec("UPDATE work_order_parts SET store='ELIT' WHERE id=1"));
        PartsQueryService::invalidateCache();
        const auto payments=PurchasePaymentService::balances(db,&error);
        QVERIFY(error.isEmpty());
        for(const auto& table:db.tables()) if(table.startsWith("purchase_payment_"))
            for(const QString& operation:{QString("INSERT"),QString("UPDATE"),QString("DELETE")})
                QVERIFY(q.exec(QString("CREATE TEMP TRIGGER manual_forbid_%1_%2 BEFORE %2 ON %1 BEGIN SELECT RAISE(ABORT,'Payment mutation'); END").arg(table,operation)));
        ElitInvoiceReader::Invoice invoice;
        invoice.number="MANUAL-1";invoice.posted=QDate(2026,9,1);invoice.currency="UAH";
        invoice.grossCents=20000;invoice.remainingCents=10000;invoice.detailsComplete=true;
        invoice.lines={{"internal","OEM","Part",2,20000}};
        auto duplicate=invoice;duplicate.number="MANUAL-2";
        ElitInvoiceReader::Result result;result.complete=true;result.fetchedAt=QDateTime::currentDateTime();result.invoices={invoice,duplicate};
        QVERIFY2(ElitInvoiceStore::publish(db,"manual",result,{},&error),qPrintable(error));
        auto ledger=SupplierInvoiceLedger::load(db,"manual");
        QVERIFY(ledger.error.isEmpty());QVERIFY(ledger.hasUnresolved);
        const auto entry=ledger.entries[0];
        QVERIFY(entry.remote);QVERIFY(!entry.matched);
        const auto candidates=SupplierInvoiceLedger::linkCandidates(ledger,entry,0);
        QCOMPARE(candidates.size(),1);
        const QVector<QVector<QString>> links={{candidates[0].key}};
        QVERIFY(!SupplierInvoiceLedger::confirmLinks(db,"manual","stale",entry.key,links,&error));
        QVERIFY(SupplierInvoiceLedger::savedLinks(db,"manual").isEmpty());
        QVERIFY2(SupplierInvoiceLedger::confirmLinks(db,"manual",ledger.snapshot.revision,entry.key,links,&error),qPrintable(error));
        QCOMPARE(SupplierInvoiceLedger::savedLinks(db,"manual").value(candidates[0].key).number,entry.number);
        QVERIFY2(SupplierInvoiceLedger::confirmLinks(db,"manual",ledger.snapshot.revision,entry.key,links,&error),qPrintable(error));
        QCOMPARE(SupplierInvoiceLedger::savedLinks(db,"manual").size(),1);
        ledger=SupplierInvoiceLedger::load(db,"manual");
        for(const auto& other:ledger.entries) if(other.remote && other.key!=entry.key) {
            QVERIFY(SupplierInvoiceLedger::linkCandidates(ledger,other,0).isEmpty());
            QVERIFY(!SupplierInvoiceLedger::confirmLinks(db,"manual",ledger.snapshot.revision,other.key,links,&error));
        }
        // A failed projection update rolls back the confirmed link and deletion.
        QVERIFY(q.exec("CREATE TEMP TRIGGER reject_projection BEFORE INSERT ON elit_purchase_invoice_links BEGIN SELECT RAISE(ABORT,'Test failure'); END"));
        QVERIFY(!SupplierInvoiceLedger::confirmLinks(db,"manual",ledger.snapshot.revision,entry.key,links,&error));
        QCOMPARE(SupplierInvoiceLedger::savedLinks(db,"manual").value(candidates[0].key).number,entry.number);
        QVERIFY(q.exec("DROP TRIGGER reject_projection"));
        QVERIFY(q.exec("UPDATE work_order_parts SET part_number='CHANGED' WHERE id=1"));
        QVERIFY(!SupplierInvoiceLedger::confirmLinks(db,"manual",ledger.snapshot.revision,entry.key,links,&error));
        QCOMPARE(SupplierInvoiceLedger::savedLinks(db,"manual").value(candidates[0].key).number,entry.number);
        error.clear();QCOMPARE(PurchasePaymentService::balances(db,&error),payments);QVERIFY(error.isEmpty());
        QCOMPARE(ElitInvoiceStore::load(db,"manual").revision,ledger.snapshot.revision);
    }
    void invoiceTemplateExtractionJobLifecycle() {
        QTemporaryDir files;
        QVERIFY(files.isValid());
        const QString fixture = qEnvironmentVariable("CS_TEST_INVOICE_FIXTURE",
            QDir(QCoreApplication::applicationDirPath()).filePath(
                QStringLiteral("../../tests/fixtures/invoices/avtonova_d_excel_vat.csv")));
        QVERIFY(!fixture.isEmpty());
        const QString path = files.filePath(QStringLiteral("invoice.csv"));
        QVERIFY(QFile::copy(fixture, path));
        InvoiceImport::ImportJob job;
        QSignalSpy done(&job, &InvoiceImport::ImportJob::finished);
        InvoiceImport::ImportJobRequest request;
        request.filePath = path;
        QVERIFY(job.start(request));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 60000);
        const auto success = qvariant_cast<InvoiceImport::ImportJobResult>(done.first().first());
        QVERIFY2(success.ok(), qPrintable(success.error.message));
        QVERIFY(!success.data.rows.isEmpty());
        QCOMPARE(success.percent, 100);
        done.clear();
        QVERIFY(job.start(request));
        job.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 60000);
        const auto cancelled = qvariant_cast<InvoiceImport::ImportJobResult>(done.first().first());
        QCOMPARE(cancelled.status, InvoiceImport::ImportJobStatus::Cancelled);
        QVERIFY(!cancelled.ok());
        QTest::qWait(200);
        QCOMPARE(done.size(), 1); // No late successful result after cancellation.
    }

    void supplierInvoiceTemplateStorageBoundaries() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("nested/templates.json"));
        QVERIFY(SupplierInvoiceTemplateStore::read(path).isEmpty());
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("nested"))));
        const QJsonObject first{{QStringLiteral("elit"), QJsonObject{{QStringLiteral("vatPriceColumn"), 5}}}};
        QVERIFY(SupplierInvoiceTemplateStore::update(path, [&](QJsonObject& root) { root = first; }));
        QCOMPARE(SupplierInvoiceTemplateStore::read(path), first);
        const QJsonObject next{{QStringLiteral("avtonova_d"), QJsonObject{{QStringLiteral("vatPriceColumn"), 7}}}};
        QVERIFY(SupplierInvoiceTemplateStore::update(path, [&](QJsonObject& root) { root = next; }));
        QCOMPARE(SupplierInvoiceTemplateStore::read(path), next);
        // A file cannot become a parent directory: a failed save must preserve it.
        QVERIFY(!SupplierInvoiceTemplateStore::update(path + QStringLiteral("/child.json"), [&](QJsonObject& root) { root = first; }));
        QCOMPARE(SupplierInvoiceTemplateStore::read(path), next);
        for (const QByteArray bytes : {QByteArray("{broken"), QByteArray("[]")}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            QCOMPARE(file.write(bytes), qint64(bytes.size()));
            file.close();
            QVERIFY(SupplierInvoiceTemplateStore::read(path).isEmpty());
            QString error;
            bool called = false;
            QVERIFY(!SupplierInvoiceTemplateStore::update(path, [&](QJsonObject&) { called = true; }, &error));
            QVERIFY(!called);
            QVERIFY(!error.isEmpty());
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), bytes); // Never replace damaged evidence with an empty store.
        }
        QVERIFY(QFile::remove(path)); // Explicit repair of this test fixture only.
        QVERIFY(SupplierInvoiceTemplateStore::update(path, [&](QJsonObject& root) { root = first; }));
        QCOMPARE(SupplierInvoiceTemplateStore::read(path), first);
    }

    void supplierInvoiceTemplateConcurrentUpdates() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("templates.json"));
        QVERIFY(SupplierInvoiceTemplateStore::update(path, [](QJsonObject& root) { root.insert("seed", 1); }));
        QSemaphore entered, release;
        bool firstSaved = false;
        std::thread writer([&] {
            firstSaved = SupplierInvoiceTemplateStore::update(path, [&](QJsonObject& root) {
                root.insert("elit", 2);
                entered.release();
                release.acquire();
            });
        });
        const bool firstEntered = entered.tryAcquire(1, 5000);
        QString error;
        bool secondCalled = false;
        const bool secondSaved = SupplierInvoiceTemplateStore::update(path, [&](QJsonObject& root) {
            secondCalled = true;
            root.insert("avtonova_d", 3);
        }, &error);
        const auto duringWrite = SupplierInvoiceTemplateStore::read(path);
        release.release();
        writer.join(); // Always release and join before assertions can leave the test.
        QVERIFY(firstEntered);
        QVERIFY(firstSaved);
        QVERIFY(!secondSaved);
        QVERIFY(!secondCalled);
        QVERIFY(!error.isEmpty());
        QCOMPARE(duringWrite, QJsonObject({{"seed", 1}}));
        QVERIFY(SupplierInvoiceTemplateStore::update(path, [](QJsonObject& root) { root.insert("avtonova_d", 3); }, &error));
        QVERIFY(error.isEmpty());
        QCOMPARE(SupplierInvoiceTemplateStore::read(path), QJsonObject({{"seed", 1}, {"elit", 2}, {"avtonova_d", 3}}));
        QVERIFY(!QFileInfo::exists(path + QStringLiteral(".lock")));
        QVERIFY(!SupplierInvoiceTemplateStore::update(QString(), [](QJsonObject&) {}, &error));
        QVERIFY(!SupplierInvoiceTemplateStore::update(path, {}, &error));
    }

    void crmTaskRequestReplay() {
        QString error;
        QVERIFY2(CloudSyncSchemaService::ensureSchema(db,&error),qPrintable(error));
        QVERIFY2(CrmClientService::ensureLocalSchema(db,&error),qPrintable(error));
        QVERIFY2(ActivityAuditService::ensureSchema(db,&error),qPrintable(error));
        QVERIFY(Database::setSetting("settings.cloudsync.capture_outbox_while_disabled",true));
        const auto counts=[&] {
            QList<qint64> values;
            for(const auto& table:{"crm_client_tasks","crm_client_events","activity_audit_events","sync_outbox"}) {
                QSqlQuery q(db);
                if(!q.exec(QString("SELECT COUNT(*) FROM %1").arg(table)) || !q.next())return QList<qint64>{};
                values.append(q.value(0).toLongLong());
            }
            return values;
        };
        const auto before=counts();QCOMPARE(before.size(),4);
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TRIGGER reject_request_event BEFORE INSERT ON crm_client_events BEGIN SELECT RAISE(ABORT,'request retry failure'); END"));
        expectRejectedTransaction("request retry failure Unable to fetch row");
        QVERIFY(!CrmTaskService::createTaskWithTimeline("replay-client",{},"Replay",{},&error,db,"request-one"));
        QCOMPARE(counts(),before);
        QVERIFY(q.exec("SELECT COUNT(*) FROM crm_task_requests WHERE request_id='request-one'"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),0);q.finish();
        QVERIFY(q.exec("DROP TRIGGER reject_request_event"));
        QVERIFY2(CrmTaskService::createTaskWithTimeline("replay-client",{},"Replay",{},&error,db,"request-one"),qPrintable(error));
        const auto committed=counts();QCOMPARE(committed.size(),4);
        QCOMPARE(committed[0],before[0]+1);QCOMPARE(committed[1],before[1]+1);
        QCOMPARE(committed[2],before[2]+2);QCOMPARE(committed[3],before[3]+2);
        QVERIFY(CrmTaskService::createTaskWithTimeline("replay-client",{},"Replay",{},&error,db,"request-one"));
        QCOMPARE(counts(),committed);
        QVERIFY(!CrmTaskService::createTaskWithTimeline("replay-client",{},"Different",{},&error,db,"request-one"));
        QVERIFY(!error.isEmpty());QCOMPARE(counts(),committed);
        {
            auto reopened=QSqlDatabase::addDatabase("QSQLITE","crm-request-reopen");
            reopened.setDatabaseName(db.databaseName());QVERIFY(reopened.open());
            QVERIFY2(CrmTaskService::createTaskWithTimeline("replay-client",{},"Replay",{},&error,reopened,"request-one"),qPrintable(error));
        }
        QSqlDatabase::removeDatabase("crm-request-reopen");
        QCOMPARE(counts(),committed);
    }
    void crmTaskCompletionRejectsMissingAndRepeatedWrites() {
        QString error;
        QVERIFY2(CloudSyncSchemaService::ensureSchema(db,&error),qPrintable(error));
        QVERIFY2(CrmClientService::ensureLocalSchema(db,&error),qPrintable(error));
        QVERIFY2(ActivityAuditService::ensureSchema(db,&error),qPrintable(error));
        QVERIFY(Database::setSetting("settings.cloudsync.capture_outbox_while_disabled",true));
        QString uid;
        QVERIFY2(CrmTaskService::createTask(db,"completion-client",{},{},"Complete me",{},"normal",{},&uid,&error),qPrintable(error));
        const auto count=[&](const QString& table) {
            QSqlQuery query(db);
            if(!query.exec("SELECT COUNT(*) FROM "+table) || !query.next())return qint64(-1);
            return query.value(0).toLongLong();
        };
        const auto audit=count("activity_audit_events"),outbox=count("sync_outbox");
        QVERIFY(audit>=0);QVERIFY(outbox>=0);
        QVERIFY(!CrmTaskService::markCompleted(db,{},&error));QVERIFY(!error.isEmpty());
        expectRejectedTransaction(QString::fromUtf8("Задачу не знайдено або вже завершено. Оновіть список задач."));
        QVERIFY(!CrmTaskService::markCompleted(db,"missing-task",&error));QVERIFY(!error.isEmpty());
        QCOMPARE(count("activity_audit_events"),audit);QCOMPARE(count("sync_outbox"),outbox);
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TRIGGER reject_completion_outbox BEFORE INSERT ON sync_outbox WHEN NEW.entity_type='crm_task' BEGIN SELECT RAISE(ABORT,'synthetic completion failure'); END"));
        expectRejectedTransaction("synthetic completion failure Unable to fetch row");
        QVERIFY(!CrmTaskService::markCompleted(db,uid,&error));QVERIFY(error.contains("synthetic completion failure"));
        q.prepare("SELECT status,completed_at FROM crm_client_tasks WHERE task_uid=?");q.addBindValue(uid);
        QVERIFY(q.exec());QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("open"));QVERIFY(q.value(1).toString().isEmpty());q.finish();
        QCOMPARE(count("activity_audit_events"),audit);QCOMPARE(count("sync_outbox"),outbox);
        QVERIFY(q.exec("DROP TRIGGER reject_completion_outbox"));
        QVERIFY2(CrmTaskService::markCompleted(db,uid,&error),qPrintable(error));
        q.prepare("SELECT status,completed_at FROM crm_client_tasks WHERE task_uid=?");q.addBindValue(uid);
        QVERIFY(q.exec());QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("completed"));
        const auto completed=q.value(1).toString();QVERIFY(!completed.isEmpty());q.finish();
        QCOMPARE(count("activity_audit_events"),audit+1);QCOMPARE(count("sync_outbox"),outbox+1);
        q.prepare("SELECT payload_json FROM activity_audit_events WHERE entity_type='crm_task' AND entity_id=? ORDER BY id DESC LIMIT 1");q.addBindValue(uid);
        QVERIFY(q.exec());QVERIFY(q.next());
        const auto payload=QJsonDocument::fromJson(q.value(0).toByteArray()).object();q.finish();
        QCOMPARE(payload.value("before").toObject().value("status").toString(),QString("open"));
        QCOMPARE(payload.value("after").toObject().value("status").toString(),QString("completed"));
        expectRejectedTransaction(QString::fromUtf8("Задачу не знайдено або вже завершено. Оновіть список задач."));
        QVERIFY(!CrmTaskService::markCompleted(db,uid,&error));QVERIFY(!error.isEmpty());
        QCOMPARE(count("activity_audit_events"),audit+1);QCOMPARE(count("sync_outbox"),outbox+1);
        q.prepare("SELECT completed_at FROM crm_client_tasks WHERE task_uid=?");q.addBindValue(uid);
        QVERIFY(q.exec());QVERIFY(q.next());QCOMPARE(q.value(0).toString(),completed);
    }
    void crmTaskAndTimelineAreAtomic() {
        QString error;
        QVERIFY2(CloudSyncSchemaService::ensureSchema(db,&error),qPrintable(error));
        QVERIFY2(CrmClientService::ensureLocalSchema(db,&error),qPrintable(error));
        QVERIFY2(ActivityAuditService::ensureSchema(db,&error),qPrintable(error));
        QVERIFY(Database::setSetting("settings.cloudsync.capture_outbox_while_disabled",true));
        const auto count=[&](const QString& table) {
            QSqlQuery q(db);
            if(!q.exec("SELECT COUNT(*) FROM "+table) || !q.next())return qint64(-1);
            return q.value(0).toLongLong();
        };
        const auto tasks=count("crm_client_tasks"), events=count("crm_client_events");
        const auto outbox=count("sync_outbox");
        const auto audit=count("activity_audit_events");
        QVERIFY(tasks>=0);QVERIFY(events>=0);QVERIFY(outbox>=0);
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TRIGGER reject_crm_history BEFORE INSERT ON crm_client_events BEGIN SELECT RAISE(ABORT,'synthetic timeline failure'); END"));
        expectRejectedTransaction("synthetic timeline failure Unable to fetch row");
        QVERIFY(!CrmTaskService::createTaskWithTimeline("test-client",{},"Test task",{},&error,db));
        QVERIFY(error.contains("synthetic timeline failure"));
        QCOMPARE(count("crm_client_tasks"),tasks);QCOMPARE(count("crm_client_events"),events);
        QCOMPARE(count("sync_outbox"),outbox);
        QCOMPARE(count("activity_audit_events"),audit);
        QVERIFY(q.exec("DROP TRIGGER reject_crm_history"));
        QVERIFY(q.exec("CREATE TRIGGER reject_crm_task_outbox BEFORE INSERT ON sync_outbox WHEN NEW.entity_type='crm_task' BEGIN SELECT RAISE(ABORT,'synthetic task outbox failure'); END"));
        expectRejectedTransaction("synthetic task outbox failure Unable to fetch row");
        QVERIFY(!CrmTaskService::createTaskWithTimeline("test-client",{},"Test task",{},&error,db));
        QVERIFY(error.contains("synthetic task outbox failure"));
        QCOMPARE(count("crm_client_tasks"),tasks);QCOMPARE(count("crm_client_events"),events);
        QCOMPARE(count("sync_outbox"),outbox);QCOMPARE(count("activity_audit_events"),audit);
        QVERIFY(q.exec("DROP TRIGGER reject_crm_task_outbox"));
        QVERIFY2(CrmTaskService::createTaskWithTimeline("test-client",{},"Test task",{},&error,db),qPrintable(error));
        QCOMPARE(count("crm_client_tasks"),tasks+1);QCOMPARE(count("crm_client_events"),events+1);
        QCOMPARE(count("sync_outbox"),outbox+2);QCOMPARE(count("activity_audit_events"),audit+2);
        QVERIFY(q.exec("SELECT title,status FROM crm_client_tasks WHERE client_uid='test-client'"));
        QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("Test task"));QCOMPARE(q.value(1).toString(),QString("open"));
        const auto committedTasks=count("crm_client_tasks"), committedEvents=count("crm_client_events");
        QVERIFY(!CrmTaskService::createTaskWithTimeline("test-client",{},"  ",{},&error,db));
        QVERIFY(!error.isEmpty());QCOMPARE(count("crm_client_tasks"),committedTasks);QCOMPARE(count("crm_client_events"),committedEvents);
    }
    void orderReportsDoNotPrepareOrMutateDatabase() {
        QSqlQuery q(db);
        for(const auto& sql:QStringList{
            "CREATE TABLE masters(id TEXT PRIMARY KEY,name TEXT)",
            "ALTER TABLE work_orders ADD COLUMN client TEXT DEFAULT ''",
            "ALTER TABLE work_orders ADD COLUMN master_id TEXT DEFAULT ''",
            "ALTER TABLE work_orders ADD COLUMN master_name_closed TEXT DEFAULT ''",
            "CREATE VIEW router_work_order_parts AS SELECT * FROM work_order_parts"})
            QVERIFY2(q.exec(sql),qPrintable(q.lastError().text()));
        auto schema=[&]{QStringList entries;QSqlQuery read(db);if(!read.exec("SELECT name,sql FROM sqlite_master ORDER BY name"))return entries;while(read.next())entries.append(read.value(0).toString()+read.value(1).toString());return entries;};
        const auto before=schema(); QVERIFY(!before.isEmpty());
        const auto connections=QSqlDatabase::connectionNames();
        QVERIFY(q.exec("PRAGMA query_only=ON"));
        QString error;
        InvoiceRegistryService::load(db,&error);
        QVERIFY2(error.isEmpty(),qPrintable(error));
        QCOMPARE(schema(),before);
        const auto report=ReportsDataLoader::buildOrdersSnapshot(db.databaseName(),"2026-09-01","2026-09-30");
        QCOMPARE(report.rows.size(),1);
        QVERIFY(report.rows.first().paymentStatus!=QStringLiteral("Помилка звірки"));
        QCOMPARE(schema(),before);
        QCOMPARE(QSqlDatabase::connectionNames(),connections);
        QVERIFY(q.exec("PRAGMA query_only=OFF"));
        const QString missing=dir.filePath("missing-report-database.db");
        QVERIFY(!QFileInfo::exists(missing));
        const auto failed=ReportsDataLoader::buildOrdersSnapshot(missing,"2026-09-01","2026-09-30");
        QVERIFY(failed.rows.isEmpty()); QVERIFY(!failed.clientPaymentError.isEmpty());
        QVERIFY(!QFileInfo::exists(missing));
        QCOMPARE(QSqlDatabase::connectionNames(),connections);
        // Read failure must not masquerade as a successfully empty report.
        QVERIFY(q.exec("DROP VIEW router_work_order_parts"));
        const auto invalid=ReportsDataLoader::buildOrdersSnapshot(db.databaseName(),"2026-09-01","2026-09-30");
        QVERIFY(invalid.rows.isEmpty()); QVERIFY(!invalid.clientPaymentError.isEmpty());
        QCOMPARE(QSqlDatabase::connectionNames(),connections);
    }
    void orderReportConcurrentCommittedSnapshots() {
        QSqlQuery q(db);
        for(const auto& sql:QStringList{
            "PRAGMA journal_mode=WAL",
            "CREATE TABLE masters(id TEXT PRIMARY KEY,name TEXT)",
            "ALTER TABLE work_orders ADD COLUMN client TEXT DEFAULT ''",
            "ALTER TABLE work_orders ADD COLUMN master_id TEXT DEFAULT ''",
            "ALTER TABLE work_orders ADD COLUMN master_name_closed TEXT DEFAULT ''",
            "CREATE VIEW router_work_order_parts AS SELECT * FROM work_order_parts",
            "CREATE TABLE client_payments(order_uid TEXT,amount_cents INTEGER)",
            "INSERT INTO client_payments VALUES('order-a',100)",
            "UPDATE work_orders SET car='generation-100'"})
            QVERIFY2(q.exec(sql),qPrintable(q.lastError().text()));
        q.finish();
        const auto path=db.databaseName();
        const auto connections=QSqlDatabase::connectionNames();
        std::atomic_bool stop{false};
        std::atomic_int commits{0};
        QString writerError;
        QSemaphore ready;
        std::thread writer([&] {
            const QString name="order-report-concurrent-writer";
            {
                auto write=QSqlDatabase::addDatabase("QSQLITE",name);
                write.setDatabaseName(path);
                write.setConnectOptions("QSQLITE_BUSY_TIMEOUT=3000");
                if(!write.open()) writerError=write.lastError().text();
                ready.release();
                for(int generation=101;writerError.isEmpty() && !stop.load();++generation) {
                    if(!write.transaction()){writerError=write.lastError().text();break;}
                    QSqlQuery change(write);
                    change.prepare("UPDATE work_orders SET car=? WHERE order_uid='order-a'");
                    change.addBindValue(QString("generation-%1").arg(generation));
                    if(!change.exec())writerError=change.lastError().text();
                    change.prepare("UPDATE client_payments SET amount_cents=? WHERE order_uid='order-a'");
                    change.addBindValue(generation);
                    if(writerError.isEmpty() && !change.exec())writerError=change.lastError().text();
                    change.finish();
                    if(!writerError.isEmpty()){write.rollback();break;}
                    if(!write.commit()){writerError=write.lastError().text();write.rollback();break;}
                    ++commits;
                    QThread::msleep(1);
                }
            }
            QSqlDatabase::removeDatabase(name);
        });
        ready.acquire();
        // Never assert/return while the writer is joinable.
        QList<OrdersSnapshot> snapshots;
        for(int i=0;i<30;++i)
            snapshots.append(ReportsDataLoader::buildOrdersSnapshot(path,"2026-09-01","2026-09-30"));
        stop=true;writer.join();
        QVERIFY2(writerError.isEmpty(),qPrintable(writerError));
        QVERIFY(commits.load()>0);
        QSet<qint64> observed;
        for(const auto& snapshot:snapshots) {
            QVERIFY2(snapshot.clientPaymentError.isEmpty(),qPrintable(snapshot.clientPaymentError));
            QCOMPARE(snapshot.rows.size(),1);
            const auto& row=snapshot.rows.first();
            QVERIFY(row.paymentStatus!=QStringLiteral("Помилка звірки"));
            bool valid=false;
            const auto generation=row.car.section("generation-",1).toLongLong(&valid);
            QVERIFY(valid);
            QCOMPARE(row.clientReceivedCents,generation);
            observed.insert(generation);
        }
        QVERIFY(observed.size()>1);
        QCOMPARE(QSqlDatabase::connectionNames(),connections);
        QVERIFY(q.exec("PRAGMA integrity_check"));QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("ok"));
        qInfo()<<"Concurrent order report snapshots:"<<snapshots.size()<<"commits:"<<commits.load()<<"observed generations:"<<observed.size();
    }
    void reportClientReceiptsUseValidatedLedger() {
        QSqlQuery q(db);
        for(const auto& sql:QStringList{
            "CREATE TABLE masters(id TEXT PRIMARY KEY,name TEXT)",
            "ALTER TABLE work_orders ADD COLUMN client TEXT DEFAULT ''",
            "ALTER TABLE work_orders ADD COLUMN master_id TEXT DEFAULT ''",
            "ALTER TABLE work_orders ADD COLUMN master_name_closed TEXT DEFAULT ''",
            "CREATE VIEW router_work_order_parts AS SELECT * FROM work_order_parts"})
            QVERIFY2(q.exec(sql),qPrintable(q.lastError().text()));
        auto read=[&]{return ReportsDataLoader::buildOrdersSnapshot(db.databaseName(),"2026-09-01","2026-09-30");};
        auto snapshot=read(); QCOMPARE(snapshot.rows.size(),1); QVERIFY(!snapshot.clientPaymentError.isEmpty());
        QVERIFY(q.exec("CREATE TABLE client_payments(order_uid TEXT,amount_cents INTEGER,paid_on TEXT)"));
        QVERIFY(q.exec("INSERT INTO client_payments VALUES('order-a',1005,'2026-10-01'),('order-a',2000,'2026-08-01'),('other',9999,'2026-09-01')"));
        snapshot=read(); QVERIFY2(snapshot.clientPaymentError.isEmpty(),qPrintable(snapshot.clientPaymentError));
        QCOMPARE(snapshot.rows.first().clientReceivedCents,3005);
        // These are receipts for selected orders, not a payment-date cash-flow report.
        QVERIFY(q.exec("UPDATE work_order_parts SET paid=1"));
        QCOMPARE(read().rows.first().clientReceivedCents,3005);
        QVERIFY(q.exec("INSERT INTO client_payments VALUES('order-a',1.5,'2026-09-01')"));
        snapshot=read(); QVERIFY(!snapshot.clientPaymentError.isEmpty());
        QCOMPARE(snapshot.rows.first().clientReceivedCents,0);
        QVERIFY(q.exec("DELETE FROM client_payments WHERE amount_cents=1.5"));
        snapshot=ReportsDataLoader::buildOrdersSnapshot(db.databaseName(),"2025-01-01","2025-01-31");
        QVERIFY(snapshot.rows.isEmpty()); QVERIFY(snapshot.clientPaymentError.isEmpty());
    }
    void clientReceiptReaderRejectsCorruptionAndOverflow() {
        const QString connection="client-receipt-boundaries";
        {
            auto local=QSqlDatabase::addDatabase("QSQLITE",connection);
            local.setDatabaseName(":memory:"); QVERIFY(local.open());
            auto result=ClientReceiptReader::read(local,{"a"});
            QVERIFY(result.ok()); QVERIFY(!result.available); QVERIFY(result.centsByOrder.isEmpty());
            QSqlQuery q(local);
            QVERIFY(q.exec("CREATE TABLE client_payments(order_uid TEXT,amount_cents INTEGER)"));
            QVERIFY(q.exec("INSERT INTO client_payments VALUES('a',1005),('a',2000),('b',500),('other','bad')"));
            result=ClientReceiptReader::read(local,{"a","b"});
            QVERIFY2(result.ok(),qPrintable(result.error)); QVERIFY(result.available);
            QCOMPARE(result.centsByOrder.value("a"),3005); QCOMPARE(result.centsByOrder.value("b"),500);
            const auto combined=ClientReceiptReader::total(result.centsByOrder.values());
            QVERIFY(combined.ok()); QCOMPARE(combined.cents,3505);
            QVERIFY(ClientReceiptReader::read(local,{}).ok());
            QVERIFY(!ClientReceiptReader::read(local,{""}).ok());
            for(const auto& value:QStringList{"0","-1","1.5","NULL","'broken'"}) {
                QVERIFY(q.exec("INSERT INTO client_payments VALUES('a',"+value+")"));
                result=ClientReceiptReader::read(local,{"a","b"});
                QVERIFY(!result.ok()); QVERIFY(result.centsByOrder.isEmpty());
                QVERIFY(q.exec("DELETE FROM client_payments WHERE rowid=(SELECT MAX(rowid) FROM client_payments)"));
            }
            QVERIFY(q.exec("DELETE FROM client_payments"));
            QVERIFY(q.exec("INSERT INTO client_payments VALUES('a',9223372036854775807)"));
            result=ClientReceiptReader::read(local,{"a"}); QVERIFY(result.ok());
            QCOMPARE(result.centsByOrder.value("a"),std::numeric_limits<qint64>::max());
            QCOMPARE(ClientReceiptReader::money(result.centsByOrder.value("a")),QStringLiteral("92233720368547758,07 грн"));
            QVERIFY(!ClientReceiptReader::total({result.centsByOrder.value("a"),1}).ok());
            QVERIFY(!ClientReceiptReader::total({-1}).ok());
            QVERIFY(q.exec("INSERT INTO client_payments VALUES('a',1)"));
            result=ClientReceiptReader::read(local,{"a"}); QVERIFY(!result.ok()); QVERIFY(result.centsByOrder.isEmpty());
            QVERIFY(q.exec("DROP TABLE client_payments"));
            QVERIFY(q.exec("CREATE TABLE client_payments(order_uid TEXT)"));
            QVERIFY(!ClientReceiptReader::read(local,{"a"}).ok());
        }
        QSqlDatabase::removeDatabase(connection);
    }
    void crmClientFinancialReadModel() {
        const QString connection="crm-financial-test";
        {
            auto local=QSqlDatabase::addDatabase("QSQLITE",connection);
            local.setDatabaseName(":memory:"); QVERIFY(local.open());
            QVERIFY(!CrmFinancialService::read(local,"a").ok());
            QSqlQuery q(local);
            QVERIFY(q.exec("CREATE TABLE work_orders(number INTEGER,order_uid TEXT,client_uid TEXT,car TEXT,discount TEXT,discount_mode TEXT)"));
            QVERIFY(q.exec("CREATE TABLE work_order_services(order_uid TEXT,order_number INTEGER,qty TEXT,price TEXT,amount TEXT)"));
            QVERIFY(q.exec("CREATE TABLE work_order_parts(order_uid TEXT,order_number INTEGER,qty TEXT,price TEXT,amount TEXT,paid INTEGER)"));
            QVERIFY(q.exec("INSERT INTO work_orders VALUES(1,'a1','a','Car','10','percent'),(2,'a2','a','Other','50','uah'),(3,'b1','b','Hidden','0','legacy')"));
            QVERIFY(q.exec("INSERT INTO work_order_services VALUES('a1',1,'1','1000',NULL),('a2',2,'1','1000',NULL)"));
            QVERIFY(q.exec("INSERT INTO work_order_parts VALUES('a1',1,'1','3000',NULL,0),('a2',2,'0.5','0.01',NULL,1),('a2',2,'0.5','0.01',NULL,1)"));
            auto result=CrmFinancialService::read(local,"a");
            QVERIFY2(result.ok(),qPrintable(result.error)); QVERIFY(!result.ledgerAvailable);
            QCOMPARE(result.totalCents,485002); QCOMPARE(result.receivedCents,0);
            QVERIFY(q.exec("CREATE TABLE client_payments(order_uid TEXT,amount_cents INTEGER)"));
            QVERIFY(q.exec("INSERT INTO client_payments VALUES('a1',200000),('a1',190000),('a2',100000),('b1',999999)"));
            result=CrmFinancialService::read(local,"a");
            QVERIFY2(result.ok(),qPrintable(result.error)); QVERIFY(result.ledgerAvailable);
            QCOMPARE(result.totalCents,485002); QCOMPARE(result.receivedCents,490000);
            QVERIFY(CrmFinancialService::readOrder(local,"b1").documentedOrderReceipts);
            QVERIFY(q.exec("INSERT INTO work_orders VALUES(4,'fresh','c','New','0','uah')"));
            QVERIFY(!CrmFinancialService::readOrder(local,"fresh").documentedOrderReceipts);
            QString trackingError;
            QVERIFY(ClientReceiptReader::beginTrackingNewOrder(local,"fresh",&trackingError));
            QVERIFY(CrmFinancialService::readOrder(local,"fresh").documentedOrderReceipts);
            QVERIFY(q.exec("UPDATE work_order_parts SET amount='0' WHERE order_uid='a2'"));
            const auto single = CrmFinancialService::readOrder(local,"a2");
            QVERIFY(q.exec("UPDATE work_order_parts SET amount=NULL WHERE order_uid='a2'"));
            QVERIFY(single.ok()); QCOMPARE(single.orders.size(),1);
            QCOMPARE(single.totalCents,95002); QCOMPARE(single.receivedCents,100000);
            QCOMPARE(single.overpaidCents,4998);
            QCOMPARE(result.outstandingCents,0); QCOMPARE(result.overpaidCents,4998);
            // Supplier paid flag never controls client receipts or balance.
            QVERIFY(q.exec("UPDATE work_order_parts SET paid=1-paid"));
            auto changed=CrmFinancialService::read(local,"a");
            QCOMPARE(changed.receivedCents,result.receivedCents); QCOMPARE(changed.overpaidCents,result.overpaidCents);
            QVERIFY(q.exec("DELETE FROM client_payments WHERE order_uid='a1' AND amount_cents=190000"));
            changed=CrmFinancialService::read(local,"a");
            QCOMPARE(changed.outstandingCents,190000); QCOMPARE(changed.overpaidCents,4998);
            // Routing includes cached/archive rows and never silently falls back on broken views.
            QVERIFY(q.exec("CREATE VIEW router_work_orders AS SELECT * FROM work_orders UNION ALL SELECT 4,'archive','a','Old','0','legacy'"));
            QVERIFY(q.exec("CREATE VIEW router_work_order_services AS SELECT * FROM work_order_services UNION ALL SELECT 'archive',4,'1','20',NULL"));
            QVERIFY(q.exec("CREATE VIEW router_work_order_parts AS SELECT * FROM work_order_parts"));
            changed=CrmFinancialService::read(local,"a");
            QVERIFY2(changed.ok(),qPrintable(changed.error)); QCOMPARE(changed.orders.size(),3); QCOMPARE(changed.totalCents,487002);
            QVERIFY(CrmFinancialService::read(local,"missing").orders.isEmpty());
            QVERIFY(CrmFinancialService::read(local,"a' OR 1=1 --").orders.isEmpty());
            QVERIFY(q.exec("INSERT INTO client_payments VALUES('a1',1.5)"));
            QVERIFY(!CrmFinancialService::read(local,"a").ok());
            QVERIFY(q.exec("DELETE FROM client_payments WHERE amount_cents=1.5"));
            QVERIFY(q.exec("UPDATE work_order_services SET amount='invalid' WHERE order_uid='a1'"));
            auto invalid=CrmFinancialService::read(local,"a");
            QVERIFY(!invalid.ok()); QVERIFY(invalid.orders.isEmpty()); QCOMPARE(invalid.totalCents,0);
            QCOMPARE(CrmFinancialService::money(1005),QStringLiteral("10,05 грн"));
            QCOMPARE(CrmFinancialService::money(-1),QStringLiteral("-0,01 грн"));
        }
        QSqlDatabase::removeDatabase(connection);
    }
    void crmPrimaryStatusReadBoundaries() {
        const auto connection=QStringLiteral("crm-status-test");
        {
            auto local=QSqlDatabase::addDatabase("QSQLITE",connection);
            local.setDatabaseName(":memory:");QVERIFY(local.open());
            auto result=CrmVehicleService::primaryStatuses("client-a",local);
            QVERIFY(!result.succeeded());QVERIFY(result.vehicles.isEmpty());
            QSqlQuery query(local);
            QVERIFY(query.exec("CREATE TABLE crm_vehicles(vehicle_uid TEXT,client_uid TEXT,is_primary INTEGER)"));
            QVERIFY(query.exec("INSERT INTO crm_vehicles VALUES ('a1','client-a',1),('a2','client-a',NULL),('b1','client-b',1)"));
            result=CrmVehicleService::primaryStatuses("client-a",local);
            QVERIFY2(result.succeeded(),qPrintable(result.error));
            QCOMPARE(result.vehicles.size(),2);
            QVERIFY(result.vehicles.value("a1"));QVERIFY(!result.vehicles.value("a2"));
            QVERIFY(!result.vehicles.contains("b1"));
            result=CrmVehicleService::primaryStatuses("unknown",local);
            QVERIFY(result.succeeded());QVERIFY(result.vehicles.isEmpty());
            result=CrmVehicleService::primaryStatuses("client-a' OR 1=1 --",local);
            QVERIFY(result.succeeded());QVERIFY(result.vehicles.isEmpty());
        }
        QSqlDatabase::removeDatabase(connection);
        QVERIFY(!QSqlDatabase::contains(connection));
    }
    void supplierDebtApplicationBoundaries() {
        QString error;
        GeneralSettingsService::ElitCredentials credentials;
        QVERIFY(GeneralSettingsService::loadElitCredentials(&credentials));
        credentials.login="application-boundaries";
        credentials.password.clear();credentials.rememberPassword=false;
        QVERIFY(GeneralSettingsService::saveElitCredentials(credentials,&error));
        const auto account=ElitInvoiceStore::accountKey(credentials);
        const auto scope=AccessControlService::currentScopeKey();
        const auto payments=PurchasePaymentService::balances(db,&error);
        QVERIFY(error.isEmpty());
        const auto connections=QSqlDatabase::connectionNames();
        const auto read=SupplierDebtApplicationService::prepareRead(account);
        SupplierInvoiceLedger::Ledger ledger;
        SupplierInvoiceLedger::Summary summary;
        std::atomic_bool cancel{true};
        read(ledger,summary,cancel);
        QCOMPARE(QSqlDatabase::connectionNames(),connections);
        cancel=false;
        for(int repeat=0;repeat<3;++repeat) {
            read(ledger,summary,cancel);
            QCOMPARE(QSqlDatabase::connectionNames(),connections);
        }
        QVERIFY(!SupplierDebtApplicationService::confirmDocuments(account,scope+"stale",{},&error));
        QVERIFY(!error.isEmpty());error.clear();
        credentials.login="different-account";
        QVERIFY(GeneralSettingsService::saveElitCredentials(credentials,&error));
        QVERIFY(!SupplierDebtApplicationService::confirmLinks(account,scope,"revision","invoice",{},&error));
        QVERIFY(!error.isEmpty());error.clear();
        ActivityAuditService::setCurrentActor({"master","Master","master","changed-session"});
        QVERIFY(!SupplierDebtApplicationService::confirmDocuments(account,scope,{},&error));
        QVERIFY(!error.isEmpty());
        const auto denied=SupplierDebtApplicationService::prepareRead(account);
        ledger={};denied(ledger,summary,cancel);
        QVERIFY(!ledger.error.isEmpty());
        QCOMPARE(QSqlDatabase::connectionNames(),connections);
        ActivityAuditService::setCurrentActor({"owner","Owner","owner","test"});
        error.clear();
        QCOMPARE(PurchasePaymentService::balances(db,&error),payments);
        QVERIFY(error.isEmpty());
    }
    void elitDocumentReviewDialog() {
        QString error;QSqlQuery q(db);
        QVERIFY(q.exec("UPDATE work_order_parts SET store='ELIT' WHERE id=1"));
        PartsQueryService::invalidateCache();
        const auto payments=PurchasePaymentService::balances(db,&error);QVERIFY(error.isEmpty());
        GeneralSettingsService::ElitCredentials credentials;
        QVERIFY(GeneralSettingsService::loadElitCredentials(&credentials));
        credentials.login="manual-ui";credentials.password.clear();credentials.rememberPassword=false;
        QVERIFY(GeneralSettingsService::saveElitCredentials(credentials,&error));
        const auto account=ElitInvoiceStore::accountKey(credentials);
        ElitInvoiceReader::Invoice invoice;invoice.number="MANUAL-UI";invoice.currency="UAH";
        invoice.posted=QDate(2026,9,3);invoice.grossCents=20000;invoice.remainingCents=20000;invoice.detailsComplete=true;
        invoice.lines={{"item","OEM","Part",2,20000}};
        auto duplicate=invoice;duplicate.number="MANUAL-UI-2";
        ElitInvoiceReader::Result result;result.complete=true;result.fetchedAt=QDateTime::currentDateTime();result.invoices={invoice,duplicate};
        QVERIFY(ElitInvoiceStore::publish(db,account,result,{},&error));
        SupplierDebtOverview view;view.resize(1200,720);view.show();view.refresh();
        auto* table=view.findChild<QTableView*>("supplierInvoiceLedger");QVERIFY(table);
        QTRY_VERIFY_WITH_TIMEOUT(view.findChild<QPushButton*>("debtTile0")->isEnabled(),10000);
        view.setFilters({},"invoice",{});QCOMPARE(table->model()->rowCount(),2);
        table->selectRow(0);
        bool visited=false;bool saved=false;int attempts=0;
        QTimer driver;driver.setInterval(40);
        connect(&driver,&QTimer::timeout,&view,[&]{
            if(++attempts>100) {
                for(auto* widget:QApplication::topLevelWidgets()) if(auto* modal=qobject_cast<QDialog*>(widget)) modal->reject();
                return;
            }
            auto* modal=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!modal)return;
            if(auto* tree=modal->findChild<QTreeWidget*>("elitDocumentChoices")) {
                visited=true;
                if(tree->topLevelItemCount()!=1){modal->reject();return;}
                tree->topLevelItem(0)->setCheckState(0,Qt::Checked);
                const auto screenshots=qEnvironmentVariable("CS_TEST_DATA_ROOT");
                if(!screenshots.isEmpty()) modal->grab().save(QDir(screenshots).filePath("elit-document-review.png"));
                modal->findChild<QPushButton*>("elitConfirmDocuments")->click();saved=modal->result()==QDialog::Accepted;
            } else if(auto* button=modal->findChild<QPushButton*>("elitDocumentCandidates"))
                QTimer::singleShot(0,button,[button]{button->click();});
        });
        driver.start();view.findChild<QPushButton*>("debtInvoiceDetails")->click();driver.stop();
        QVERIFY(visited);QVERIFY(saved);
        QCOMPARE(SupplierInvoiceLedger::savedLinks(db,account).size(),1);
        QCOMPARE(PurchasePaymentService::balances(db,&error),payments);
    }
    void elitManualLinkDialog() {
        QString error;QSqlQuery q(db);
        QVERIFY(q.exec("UPDATE work_order_parts SET store='ELIT' WHERE id=1"));
        PartsQueryService::invalidateCache();
        const auto payments=PurchasePaymentService::balances(db,&error);QVERIFY(error.isEmpty());
        GeneralSettingsService::ElitCredentials credentials;
        QVERIFY(GeneralSettingsService::loadElitCredentials(&credentials));
        credentials.login="manual-ui";credentials.password.clear();credentials.rememberPassword=false;
        QVERIFY(GeneralSettingsService::saveElitCredentials(credentials,&error));
        const auto account=ElitInvoiceStore::accountKey(credentials);
        ElitInvoiceReader::Invoice invoice;invoice.number="MANUAL-UI";invoice.currency="UAH";
        invoice.posted=QDate(2026,9,1);invoice.grossCents=20000;invoice.remainingCents=20000;invoice.detailsComplete=true;
        invoice.lines={{"item","OEM","Part",2,20000}};
        auto duplicate=invoice;duplicate.number="MANUAL-UI-2";
        ElitInvoiceReader::Result result;result.complete=true;result.fetchedAt=QDateTime::currentDateTime();result.invoices={invoice,duplicate};
        QVERIFY(ElitInvoiceStore::publish(db,account,result,{},&error));
        SupplierDebtOverview view;view.resize(1200,720);view.show();view.refresh();
        auto* table=view.findChild<QTableView*>("supplierInvoiceLedger");QVERIFY(table);
        QTRY_VERIFY_WITH_TIMEOUT(view.findChild<QPushButton*>("debtTile0")->isEnabled(),10000);
        view.setFilters({},"invoice",{});QCOMPARE(table->model()->rowCount(),2);
        table->selectRow(0);
        bool visited=false;bool saved=false;int attempts=0;
        QTimer driver;driver.setInterval(40);
        connect(&driver,&QTimer::timeout,&view,[&]{
            if(++attempts>100) {
                for(auto* widget:QApplication::topLevelWidgets()) if(auto* modal=qobject_cast<QDialog*>(widget)) modal->reject();
                return;
            }
            auto* modal=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!modal)return;
            if(auto* tree=modal->findChild<QTreeWidget*>("elitLinkCandidates")) {
                visited=true;
                if(tree->topLevelItemCount()!=1 || tree->topLevelItem(0)->childCount()!=1){modal->reject();return;}
                tree->topLevelItem(0)->child(0)->setCheckState(0,Qt::Checked);
                const auto screenshots=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
                if(!screenshots.isEmpty()) modal->grab().save(QDir(screenshots).filePath("elit-manual-links.png"));
                modal->findChild<QPushButton*>("elitConfirmLinks")->click();saved=modal->result()==QDialog::Accepted;
            } else if(auto* button=modal->findChild<QPushButton*>("debtReconcileLinks"))
                QTimer::singleShot(0,button,[button]{button->click();});
        });
        driver.start();view.findChild<QPushButton*>("debtInvoiceDetails")->click();driver.stop();
        QVERIFY(visited);QVERIFY(saved);
        QCOMPARE(SupplierInvoiceLedger::savedLinks(db,account).size(),1);
        QCOMPARE(PurchasePaymentService::balances(db,&error),payments);
    }
    void elitAtomicStartupPublication() {
        QSqlQuery q(db); QString error;
        QVERIFY(q.exec("UPDATE work_order_parts SET store='ELIT' WHERE id=1"));
        PartsQueryService::invalidateCache();
        WorkshopInventoryPurchaseInput stock;
        stock.purchasedOn=QDate(2026,9,1); stock.materialName="Stock test"; stock.unit="pcs";
        stock.quantity=1; stock.unitCost=50; stock.supplier="ELIT"; stock.article="STOCK-OEM"; stock.paidAmount=0;
        qint64 stockId=0;
        QVERIFY2(WorkshopInventoryService::addPurchase(db,stock,&stockId,&error),qPrintable(error));
        const auto payments=PurchasePaymentService::balances(db,&error);
        QVERIFY(error.isEmpty());
        for(const auto& table:db.tables()) if(table.startsWith("purchase_payment_")) {
            for(const QString& operation:{QString("INSERT"),QString("UPDATE"),QString("DELETE")})
                QVERIFY2(q.exec(QString("CREATE TEMP TRIGGER forbid_%1_%2 BEFORE %2 ON %1 BEGIN SELECT RAISE(ABORT,'Sync attempted payment mutation'); END").arg(table,operation)),qPrintable(q.lastError().text()));
        }
        ElitInvoiceReader::Invoice invoice;
        invoice.number="LINK-1"; invoice.currency="UAH"; invoice.posted=stock.purchasedOn;
        invoice.grossCents=25000; invoice.remainingCents=10000; invoice.detailsComplete=true;
        invoice.lines={{"internal","OEM","Part",2,20000},{"internal-2","STOCK-OEM","Stock",1,5000}};
        ElitInvoiceReader::Result result; result.complete=true; result.fetchedAt=QDateTime::currentDateTime(); result.invoices={invoice};
        int linked=0,unresolved=0;
        QVERIFY2(SupplierInvoiceLedger::publishAndLink(db,"atomic",result,{},[]{return true;},&linked,&unresolved,&error),qPrintable(error));
        QCOMPARE(linked,1); QCOMPARE(unresolved,0);
        auto saved=ElitInvoiceStore::load(db,"atomic");
        const auto links=SupplierInvoiceLedger::savedLinks(db,"atomic");
        QCOMPARE(links.size(),2);
        QCOMPARE(links.value(PurchasePaymentService::key("part",1,"order-a")).number,QString("LINK-1"));
        QCOMPARE(links.value(PurchasePaymentService::key("inventory",stockId)).number,QString("LINK-1"));
        QVERIFY2(SupplierInvoiceLedger::publishAndLink(db,"atomic",result,saved.revision,[]{return true;},&linked,&unresolved,&error),qPrintable(error));
        QCOMPARE(linked,0);
        saved=ElitInvoiceStore::load(db,"atomic");
        auto partial=result;
        partial.invoices[0].lines.append({"unmatched","UNKNOWN-OEM","Other stock",1,5000});
        partial.invoices[0].grossCents+=5000;
        QVERIFY2(SupplierInvoiceLedger::publishAndLink(db,"partial",partial,{},[]{return true;},&linked,&unresolved,&error),qPrintable(error));
        QCOMPARE(linked,1); QCOMPARE(unresolved,1);
        QCOMPARE(SupplierInvoiceLedger::savedLinks(db,"partial").size(),2);
        QCOMPARE(SupplierInvoiceLedger::debtTotal(SupplierInvoiceLedger::load(db,"partial")),qint64(25000));
        int checks=0;
        QVERIFY(!SupplierInvoiceLedger::publishAndLink(db,"atomic",result,saved.revision,[&]{return ++checks<2;},&linked,&unresolved,&error));
        QCOMPARE(ElitInvoiceStore::load(db,"atomic").revision,saved.revision);
        QCOMPARE(SupplierInvoiceLedger::savedLinks(db,"atomic").size(),2);
        result.complete=false;
        QVERIFY(!SupplierInvoiceLedger::publishAndLink(db,"atomic",result,saved.revision,[]{return true;},&linked,&unresolved,&error));
        QCOMPARE(ElitInvoiceStore::load(db,"atomic").revision,saved.revision);
        error.clear(); QCOMPARE(PurchasePaymentService::balances(db,&error),payments); QVERIFY(error.isEmpty());
        // A changed confirmed local row must not silently bind to another purchase.
        QVERIFY(q.exec("UPDATE work_order_parts SET part_number='OTHER' WHERE id=1"));
        result.complete=true;
        QVERIFY2(SupplierInvoiceLedger::publishAndLink(db,"atomic",result,saved.revision,[]{return true;},&linked,&unresolved,&error),qPrintable(error));
        QCOMPARE(linked,0); QCOMPARE(unresolved,1);
        QVERIFY(SupplierInvoiceLedger::savedLinks(db,"atomic").isEmpty());
        QCOMPARE(PurchasePaymentService::balances(db,&error),payments);
    }
    void apiSettingsNavigation() {
        QWidget owner;
        const QList<QPair<QString,QStringList>> menus{
            {"int.apikeys", {"int.api.odb", "int.api.booking"}},
            {"int.suppliers", {"int.api.elit", "int.supplier.bm", "int.supplier.omega", "int.supplier.utr"}}
        };
        for (const auto& menu : menus) {
        auto* root=SettingsIntegrationsSection::build(menu.first,&owner,{},{},{});
        auto* stack=qobject_cast<QStackedWidget*>(root);
        QVERIFY(stack); QCOMPARE(stack->count(),menu.second.size()+1); QCOMPARE(stack->currentIndex(),0);
        for (const auto& other : menus) if(other.first!=menu.first)
            for (const auto& id : other.second) QVERIFY(!root->findChild<QPushButton*>(id));
        for (const QString& id : menu.second) {
            auto* button=root->findChild<QPushButton*>(id); QVERIFY(button); button->click();
            QVERIFY(stack->currentIndex()>0);
            bool found=false;
            for(auto* back:stack->currentWidget()->findChildren<QPushButton*>())
                if(back->property("stay_in_detail_menu").toBool() && back->text().contains(QString::fromUtf8("Назад"))) {
                    back->click(); found=true; break;
                }
            QVERIFY(found); QCOMPARE(stack->currentIndex(),0);
        }
        }
    }
    void elitInvoiceReadStaging() {
        using namespace ElitInvoiceReader;
        const QByteArray start = "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body><b:getInvoiceListResponse xmlns:b=\"http://buyer.elit.cz/\">";
        const QByteArray end = "</b:getInvoiceListResponse></s:Body></s:Envelope>";
        const QByteArray row = "<return><invoiceNo>UA-1</invoiceNo><currency>UAH</currency><postingDate>2026-10-03T00:00:00</postingDate><amount>100.25</amount><amountVAT>20.05</amountVAT><remainingAmount>40.10</remainingAmount></return>";
        QVector<Invoice> page;
        QString error;
        QVERIFY(parsePage(start + row + end, &page, &error));
        QCOMPARE(page.size(), 1);
        QCOMPARE(page[0].remainingCents, qint64(4010));
        QCOMPARE(page[0].amountCents, qint64(10025));
        QVERIFY(!page[0].due.isValid());
        Collection collection;
        collection.expected = 2;
        QVERIFY(collection.append(page, &error));
        QVERIFY(!collection.append(page, &error));
        QCOMPARE(collection.all.size(), 1);
        auto old = page[0]; old.number = "OLD"; old.posted = QDate(2026,9,1);
        QVERIFY(collection.append({old}, &error));
        QCOMPARE(collection.period(QDate(2026,10,21)).size(), 2);
        collection.all[1].remainingCents = 0;
        QCOMPARE(collection.period(QDate(2026,10,21)).size(), 1);
        collection.all[0].posted = QDate(2026,10,22);
        QCOMPARE(collection.period(QDate(2026,10,21)).size(), 0);
        for (const auto& invalid : {QByteArray(row).replace("<currency>UAH</currency>", ""), QByteArray("<return/>"), QByteArray("<Fault/>")})
            QVERIFY(!parsePage(start + invalid + end, &page, &error));
        QVERIFY(parsePage(start + end, &page, &error));
        QVERIFY(page.isEmpty());
        QVERIFY(!parsePage(start + row, &page, &error));
        QVERIFY(!parsePage("<!DOCTYPE x>" + start + end, &page, &error));
        qint64 cents = 0;
        QVERIFY(!money("nan", &cents));
        QVERIFY(!money("1e100", &cents));
        QVERIFY(!money("", &cents));
        QObject owner;
        bool completed = false;
        ElitInvoiceReader::start(&owner, {}, [&](const Result& result) {
            completed = true; QVERIFY(!result.complete); QVERIFY(result.invoices.isEmpty());
        });
        QVERIFY(completed);
    }
    void elitReadOnlyCheck() {
        GeneralSettingsService::ElitCredentials credentials;
        credentials.login = QStringLiteral("test-user");
        credentials.password = QStringLiteral("secret<&");
        ElitSoapRequest::PreparedRequest request;
        QString error;
        QVERIFY(ElitSoapRequest::prepareAccessCheck(credentials, &request, &error));
        QVERIFY(request.body.contains("buyer:getNumOfInvoices"));
        QVERIFY(!request.body.contains("sendBasket"));
        QVERIFY(!request.body.contains("pageNo"));
        const QByteArray prefix = "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body>";
        const QByteArray suffix = "</s:Body></s:Envelope>";
        const QByteArray response = prefix + "<b:getNumOfInvoicesResponse xmlns:b=\"http://buyer.elit.cz/\"><return>0</return></b:getNumOfInvoicesResponse>" + suffix;
        QVERIFY(ElitConnectionCheck::parseResponse(response, 200, credentials).accessible);
        QVERIFY(!ElitConnectionCheck::parseResponse(response, 403, credentials).accessible);
        QVERIFY(!ElitConnectionCheck::parseResponse("<html>OK</html>", 200, credentials).accessible);
        QVERIFY(!ElitConnectionCheck::parseResponse(response.left(70), 200, credentials).accessible);
        auto invalid = response;
        invalid.replace(">0<", ">-1<");
        QVERIFY(!ElitConnectionCheck::parseResponse(invalid, 200, credentials).accessible);
        invalid = response;
        invalid.replace("getNumOfInvoicesResponse", "getVersionResponse");
        QVERIFY(!ElitConnectionCheck::parseResponse(invalid, 200, credentials).accessible);
        const auto fault = prefix + "<s:Fault><faultcode>s:Server</faultcode><faultstring>Denied test-user secret&lt;&amp;</faultstring></s:Fault>" + suffix;
        const auto result = ElitConnectionCheck::parseResponse(fault, 500, credentials);
        QVERIFY(!result.accessible);
        QVERIFY(!result.message.contains(credentials.password));
        QVERIFY(!result.message.contains(credentials.login));
        QVERIFY(result.message.contains("Denied"));
        QVERIFY(!ElitConnectionCheck::parseResponse(response, 302, credentials).accessible);
        credentials.password.clear();
        bool completed = false;
        QObject owner;
        ElitConnectionCheck::start(&owner, credentials, [&](auto result) {
            completed = true;
            QVERIFY(!result.accessible);
        });
        QVERIFY(completed);
        QVERIFY(owner.findChildren<QNetworkAccessManager*>().isEmpty());
    }
    void elitCredentialsSettings() {
        const QString key = QStringLiteral("integrations/elit/credentials_dpapi");
        QSettings().remove(key);
        GeneralSettingsService::ElitCredentials result;
        QString error;
        QVERIFY(GeneralSettingsService::loadElitCredentials(&result, &error));
        QVERIFY(result.login.isEmpty());
        QCOMPARE(result.company, QStringLiteral("ELIT_UA"));
        QVERIFY(!result.rememberPassword);
        const auto legacy = CloudSyncCredentials::protectToken(
            QStringLiteral("{\"login\":\"old\",\"password\":\"old-password\",\"apiKey\":\"\",\"company\":\"CUSTOM\"}"), &error);
        QVERIFY(!legacy.isEmpty());
        QSettings().setValue(key, legacy);
        QVERIFY(GeneralSettingsService::loadElitCredentials(&result, &error));
        QCOMPARE(result.company, QStringLiteral("CUSTOM"));
        QCOMPARE(result.password, QStringLiteral("old-password"));
        QVERIFY(result.rememberPassword);
        QVERIFY(GeneralSettingsService::saveElitCredentials(
            {QStringLiteral(" elit-user "), QStringLiteral(" secret-password "), QStringLiteral("test-api-secret"), QString(), true}, &error));
        const QString stored = QSettings().value(key).toString();
        QVERIFY(stored.startsWith(QStringLiteral("dpapi:v1:")));
        QVERIFY(!stored.contains(QStringLiteral("secret-password")));
        QVERIFY(!stored.contains(QStringLiteral("test-api-secret")));
        QVERIFY(GeneralSettingsService::loadElitCredentials(&result, &error));
        QCOMPARE(result.login, QStringLiteral("elit-user"));
        QCOMPARE(result.password, QStringLiteral(" secret-password "));
        QCOMPARE(result.apiKey, QStringLiteral("test-api-secret"));
        QWidget owner;
        auto* page = SettingsIntegrationsSection::build(QStringLiteral("int.api.elit"), &owner, {}, {}, {});
        QVERIFY(page);
        for (const auto& name : {"elitLogin", "elitPassword", "elitCompany"})
            QVERIFY2(page->findChild<QLineEdit*>(name), name);
        for (const auto& name : {"elitReadInvoices", "elitCheck", "elitSave"})
            QVERIFY2(page->findChild<QPushButton*>(name), name);
        QVERIFY(page->findChild<QCheckBox*>("elitRememberPassword"));
        QCOMPARE(page->findChild<QLineEdit*>("elitLogin")->text(), result.login);
        QCOMPARE(page->findChild<QLineEdit*>("elitPassword")->echoMode(), QLineEdit::Password);
        QVERIFY(!page->findChild<QLineEdit*>("elitApiKey"));
        QVERIFY(page->findChild<QPushButton*>("elitReadInvoices"));
        QVERIFY(page->findChild<QPushButton*>("elitReadInvoices")->isEnabled());
        QCOMPARE(page->findChild<QLineEdit*>("elitCompany")->text(), QStringLiteral("ELIT_UA"));
        auto* showPassword = page->findChild<QAction*>("elitShowPassword");
        QVERIFY(showPassword);
        showPassword->trigger();
        QCOMPARE(page->findChild<QLineEdit*>("elitPassword")->echoMode(), QLineEdit::Normal);
        showPassword->trigger();
        QCOMPARE(page->findChild<QLineEdit*>("elitPassword")->echoMode(), QLineEdit::Password);
        QVERIFY(page->findChild<QCheckBox*>("elitRememberPassword")->isChecked());
        page->findChild<QLineEdit*>("elitCompany")->setText(QStringLiteral(" TEST "));
        QVERIFY(page->findChild<QPushButton*>("elitCheck")->isEnabled());
        const auto savedBeforeCheck = QSettings().value(key);
        const QString passwordBeforeCheck = page->findChild<QLineEdit*>("elitPassword")->text();
        page->findChild<QLineEdit*>("elitPassword")->clear();
        bool errorShown = false;
        QTimer::singleShot(0, page, [&]() {
            auto* message = page->findChild<QMessageBox*>();
            if (!message) return;
            errorShown = message->textFormat() == Qt::PlainText
                && message->icon() == QMessageBox::Warning && !message->text().isEmpty();
            message->accept();
        });
        page->findChild<QPushButton*>("elitCheck")->click();
        QVERIFY(errorShown);
        QCOMPARE(QSettings().value(key), savedBeforeCheck);
        QVERIFY(page->findChildren<QNetworkAccessManager*>().isEmpty());
        QVERIFY(page->findChild<QPushButton*>("elitCheck")->isEnabled());
        page->findChild<QLineEdit*>("elitPassword")->setText(passwordBeforeCheck);
        page->findChild<QLineEdit*>("elitLogin")->setText(QStringLiteral("replacement"));
        page->findChild<QPushButton*>("elitSave")->click();
        QVERIFY(GeneralSettingsService::loadElitCredentials(&result, &error));
        QCOMPARE(result.login, QStringLiteral("replacement"));
        QCOMPARE(result.company, QStringLiteral("TEST"));
        QCOMPARE(result.apiKey, QStringLiteral("test-api-secret"));
        page->findChild<QCheckBox*>("elitRememberPassword")->setChecked(false);
        page->findChild<QPushButton*>("elitSave")->click();
        QVERIFY(GeneralSettingsService::loadElitCredentials(&result, &error));
        QVERIFY(result.password.isEmpty());
        QVERIFY(!result.rememberPassword);
        QCOMPARE(result.company, QStringLiteral("TEST"));
        ElitSoapRequest::PreparedRequest soap;
        result.password = QStringLiteral("p<&\"secret");
        QVERIFY(ElitSoapRequest::prepareInvoiceList(result, 1, 10, &soap, &error));
        QCOMPARE(soap.request.rawHeader("SOAPAction"), QByteArray("\"\""));
        QVERIFY(!soap.request.hasRawHeader("Authorization"));
        QVERIFY(!soap.body.contains("test-api-secret"));
        QXmlStreamReader xml(soap.body);
        QStringList fields;
        QString password;
        while (!xml.atEnd()) {
            xml.readNext();
            if (!xml.isStartElement()) continue;
            fields.append(xml.name().toString());
            if (xml.name() == QStringLiteral("getInvoiceList"))
                QCOMPARE(xml.namespaceUri().toString(), QStringLiteral("http://buyer.elit.cz/"));
            if (xml.name() == QStringLiteral("password")) password = xml.readElementText();
        }
        QVERIFY(!xml.hasError());
        QCOMPARE(password, result.password);
        QCOMPARE(fields.mid(3), QStringList({"company", "login", "password", "open", "closed", "overdue", "pageNo", "pageSize"}));
        result.company.clear();
        QVERIFY(!ElitSoapRequest::prepareInvoiceList(result, 1, 10, &soap, &error));
        QVERIFY(soap.body.isEmpty());
        QSettings().setValue(key, QStringLiteral("dpapi:v1:invalid"));
        QVERIFY(!GeneralSettingsService::loadElitCredentials(&result, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(result.password.isEmpty());
        QSettings().remove(key);
    }
    void elitMissingCrossReferenceUsesExactCode() {
        QSqlQuery q(db);
        QVERIFY(q.exec("UPDATE work_order_parts SET store='ELIT' WHERE id=1"));
        ElitInvoiceReader::Invoice invoice;
        invoice.number="CODE-1";invoice.currency="UAH";invoice.posted=QDate(2026,9,1);
        invoice.grossCents=20000;invoice.remainingCents=20000;invoice.detailsComplete=true;
        invoice.lines={{"OEM",{},"Part",2,20000}};
        ElitInvoiceReader::Result result;result.complete=true;result.fetchedAt=QDateTime::currentDateTime();result.invoices={invoice};
        QString error;
        QVERIFY2(ElitInvoiceStore::publish(db,"code-test",result,{},&error),qPrintable(error));
        auto ledger=SupplierInvoiceLedger::load(db,"code-test");QVERIFY2(ledger.error.isEmpty(),qPrintable(ledger.error));
        auto matched=[](const SupplierInvoiceLedger::Ledger& value){for(const auto& e:value.entries)if(e.remote)return e.counted;return false;};
        QVERIFY(matched(ledger));
        result.invoices[0].lines[0].reference="OTHER";
        QVERIFY(ElitInvoiceStore::publish(db,"code-test",result,ledger.snapshot.revision,&error));
        ledger=SupplierInvoiceLedger::load(db,"code-test");QVERIFY(!matched(ledger));
        result.invoices[0].lines[0].reference.clear();result.invoices[0].lines[0].itemCode="BRAND OEM";
        QVERIFY(ElitInvoiceStore::publish(db,"code-test",result,ledger.snapshot.revision,&error));
        ledger=SupplierInvoiceLedger::load(db,"code-test");QVERIFY(!matched(ledger));
        result.invoices[0].lines[0].itemCode="";
        QVERIFY(ElitInvoiceStore::publish(db,"code-test",result,ledger.snapshot.revision,&error));
        QVERIFY(!matched(SupplierInvoiceLedger::load(db,"code-test")));
    }
    void elitWorkingCopyAllocationAudit() {
        if(qEnvironmentVariable("CS_TAB_AUDIT_COPY").isEmpty()) QSKIP("Requires isolated working-data copy");
        QLoggingCategory::setFilterRules(QStringLiteral("*.debug=false"));
        QVERIFY(QFileInfo(db.databaseName()).absoluteFilePath().startsWith(App::sessionDir()));
        QString error;
        const auto tables=db.tables();
        auto financialState=[&]() {
            QMap<QString,QList<QByteArray>> state;
            for(const auto& table:{"purchase_payment_balances","purchase_payment_events","purchase_payment_terms",
                                  "workshop_spare_payment_allocations","work_order_parts","workshop_inventory_purchases",
                                  "workshop_spare_movements"}) {
                if(!tables.contains(QString::fromLatin1(table)))continue;
                QSqlQuery q(db);
                if(!q.exec("SELECT * FROM \""+QString::fromLatin1(table)+"\""))return QMap<QString,QList<QByteArray>>{};
                auto& rows=state[QString::fromLatin1(table)];
                while(q.next()) {
                    QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);
                    for(int i=0;i<q.record().count();++i)stream<<q.value(i);
                    rows.append(bytes);
                }
                std::sort(rows.begin(),rows.end());
            }
            return state;
        };
        const auto before=financialState();QVERIFY(!before.isEmpty());
        QSqlQuery accounts(db);
        QVERIFY(accounts.exec("SELECT account FROM elit_invoice_snapshots"));
        QStringList keys;while(accounts.next())keys.append(accounts.value(0).toString());
        QVERIFY(!keys.isEmpty());accounts.finish();
        QVector<SparePaymentAllocation::Distribution> distribution;
        QVERIFY2(SparePaymentAllocation::distribution(db,&distribution,&error),qPrintable(error));
        int issued=0,stock=0;for(const auto& slice:distribution){QVERIFY(slice.quantity>0);slice.partId?++issued:++stock;}
        qInfo()<<"AUDIT_FIFO"<<"issued_slices"<<issued<<"stock_slices"<<stock;
        for(const auto& account:keys) {
            const auto initial=SupplierInvoiceLedger::load(db,account);
            QVERIFY2(initial.error.isEmpty(),qPrintable(initial.error));
            QVERIFY(!initial.snapshot.documents.isEmpty());
            const auto pdfSource=qEnvironmentVariable("CS_ELIT_PDF_AUDIT_SOURCE");
            if(!pdfSource.isEmpty()) {
                QVERIFY(QFileInfo(QSettings().fileName()).absoluteFilePath().startsWith(dir.path()+"/"));
                QVERIFY(QCoreApplication::applicationName().startsWith("Safety-"));
                QSettings production(QSettings::NativeFormat,QSettings::UserScope,App::orgName(),App::appName());
                const auto plain=CloudSyncCredentials::unprotectToken(production.value("integrations/elit/credentials_dpapi").toString(),&error);
                QVERIFY2(!plain.isEmpty(),"Saved account identity unavailable");
                const auto identity=QJsonDocument::fromJson(plain.toUtf8()).object();
                GeneralSettingsService::ElitCredentials auditAccount;
                auditAccount.company=identity.value("company").toString();auditAccount.login=identity.value("login").toString();
                auditAccount.rememberPassword=false;
                QCOMPARE(ElitInvoiceStore::accountKey(auditAccount),account);
                // Only the guarded temporary INI receives the identity, never a password.
                QVERIFY2(GeneralSettingsService::saveElitCredentials(auditAccount,&error),qPrintable(error));
                const auto originalPaths=ElitInvoicePdf::paths(db,account);
                qInfo()<<"AUDIT_PDF_ORIGINAL_CACHE"<<originalPaths.size();
                const auto databasePath=db.databaseName();
                bool saved=true;
                auto* worker=QThread::create([&] {
                    const QString name="pdf_offline_audit";
                    const auto cleanup=qScopeGuard([&]{QSqlDatabase::removeDatabase(name);});
                    {
                        auto copy=QSqlDatabase::addDatabase("QSQLITE",name);copy.setDatabaseName(databasePath);
                        if(!copy.open()){saved=false;error="Cannot open isolated copy";return;}
                        for(const auto& document:initial.snapshot.documents) {
                            QFile input(QDir(pdfSource).filePath(document.invoice.number+".pdf"));
                            if(!input.open(QIODevice::ReadOnly)) {saved=false;error=input.errorString();break;}
                            if(!ElitInvoicePdf::save(copy,account,document,input.readAll(),[]{return true;},&error)) {saved=false;break;}
                        }
                        copy.close();
                    }
                });
                worker->start();QTRY_VERIFY_WITH_TIMEOUT(worker->isFinished(),120000);worker->wait();delete worker;
                QVERIFY2(saved,qPrintable(error));
                const auto paths=ElitInvoicePdf::paths(db,account);
                QCOMPARE(paths.size(),initial.snapshot.documents.size());
                const auto registry=InvoiceRegistryService::load(db,&error);QVERIFY2(error.isEmpty(),qPrintable(error));
                const auto links=SupplierInvoiceLedger::activeLinks(db);
                int confirmed=0,visible=0,opened=0;
                QDesktopServices::setUrlHandler("file",this,"capturePdfUrl");
                const auto resetHandler=qScopeGuard([]{QDesktopServices::unsetUrlHandler("file");});
                QWidget owner;
                QString openError;
                QTimer dismissError;
                connect(&dismissError,&QTimer::timeout,&owner,[&] {
                    if(auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                        openError=box->text();box->reject();
                    }
                });
                dismissError.start(50);
                for(const auto& document:initial.snapshot.documents) {
                    const auto path=paths.value(document.identity);
                    QVERIFY(QFileInfo(path).absoluteFilePath().startsWith(App::sessionDir()));
                    bool linked=false,listed=false;
                    for(const auto& link:links)for(const auto& target:link.documents)
                        if(target.identity==document.identity) {
                            linked=true;QCOMPARE(target.filePath,path);QVERIFY(target.quantity>0);
                        }
                    for(const auto& invoice:registry)if(invoice.filePath==path) {
                        listed=true;QCOMPARE(invoice.number,document.invoice.number);
                        QCOMPARE(invoice.date,document.invoice.posted);
                        QVERIFY(!invoice.parts.isEmpty());
                    }
                    if(linked) {++confirmed;QVERIFY2(listed,qPrintable(document.invoice.number));}
                    if(listed)++visible;
                    // Exercise the same asynchronous PDF validation/open path used by the tab,
                    // capturing the OS handoff instead of launching 27 external viewer windows.
                    openedPdf={};QStandardItemModel model(1,4);const auto index=model.index(0,3);
                    model.setData(index,document.invoice.number);
                    model.setData(index,QVariantMap{{"path",path},{"date",document.invoice.posted.toString("dd.MM.yy")},{"store","ELIT"}},Qt::UserRole);
                    ReportsInteractionSupport::openInvoiceFromReport(&owner,index);
                    QTRY_VERIFY_WITH_TIMEOUT(!openedPdf.isEmpty() || !openError.isEmpty(),10000);
                    QVERIFY2(openError.isEmpty(),qPrintable(openError));
                    QCOMPARE(openedPdf.toLocalFile(),QFileInfo(path).absoluteFilePath());++opened;
                }
                InvoicesPage page;page.resize(1200,600);page.show();page.refresh();
                auto* table=page.findChild<QTableView*>("invoicesTable");QVERIFY(table);
                QTRY_VERIFY_WITH_TIMEOUT(table->isEnabled(),10000);
                QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(),registry.size(),10000);
                int clicked=0;
                for(const auto& invoice:registry)if(paths.values().contains(invoice.filePath)) {
                    int row=-1;
                    for(int i=0;i<table->model()->rowCount();++i)
                        if(table->model()->index(i,1).data().toString()==invoice.number) {row=i;break;}
                    QVERIFY(row>=0);openedPdf={};table->clicked(table->model()->index(row,1));
                    QTRY_VERIFY_WITH_TIMEOUT(!openedPdf.isEmpty(),10000);
                    QCOMPARE(openedPdf.toLocalFile(),QFileInfo(invoice.filePath).absoluteFilePath());++clicked;
                }
                qInfo()<<"AUDIT_PDF_RESULT"<<"validated"<<paths.size()<<"confirmed_documents"<<confirmed
                       <<"registry_documents"<<visible<<"open_handoffs"<<opened<<"tab_clicks"<<clicked;
                QCOMPARE(financialState(),before);
            }
            int elitLocal=0,missingOem=0; qint64 localDebt=0;
            for(const auto& item:initial.local)if(SupplierInvoiceLedger::isElit(item.purchase.supplier)) {
                ++elitLocal; if(item.purchase.oem.trimmed().isEmpty())++missingOem;
                localDebt+=item.purchase.remainingCents();
            }
            qInfo()<<"AUDIT_LOCAL_ELIT"<<"purchases"<<elitLocal<<"missing_oem"<<missingOem<<"debt_cents"<<localDebt;
            int lineCount=0,noCodeMatch=0,noDateMatch=0,noPriceMatch=0;
            for(const auto& document:initial.snapshot.documents)for(const auto& line:document.invoice.lines) {
                ++lineCount;bool code=false,date=false,price=false;
                const auto article=SupplierInvoiceLedger::normalizedArticle(line.reference.trimmed().isEmpty()?line.itemCode:line.reference);
                for(const auto& item:initial.local) {
                    const auto& p=item.purchase;
                    if(!SupplierInvoiceLedger::isElit(p.supplier) || article.isEmpty() || SupplierInvoiceLedger::normalizedArticle(p.oem)!=article)continue;
                    code=true;const auto purchased=PartsQueryService::parseBusinessDate(p.date);
                    if(!purchased.isValid() || qAbs(purchased.daysTo(document.invoice.posted))>31)continue;
                    date=true;
                    if(p.quantity>0 && line.quantity>0 && qAbs(p.totalCents/p.quantity-line.grossCents/line.quantity)<=1.0)price=true;
                }
                if(!code)++noCodeMatch;else if(!date)++noDateMatch;else if(!price)++noPriceMatch;
            }
            qInfo()<<"AUDIT_LINE_DIAGNOSTICS"<<"lines"<<lineCount<<"no_exact_code"<<noCodeMatch
                <<"outside_date_window"<<noDateMatch<<"unit_price_diff"<<noPriceMatch;
            ElitInvoiceReader::Result replay;replay.complete=true;replay.fetchedAt=initial.snapshot.fetchedAt;
            replay.asOf=replay.fetchedAt.date();
            for(const auto& document:initial.snapshot.documents)replay.invoices.append(document.invoice);
            replay.availableCount=replay.invoices.size();
            for(int pass=0;pass<2;++pass) {
                const auto previous=ElitInvoiceStore::load(db,account,&error);QVERIFY2(error.isEmpty(),qPrintable(error));
                int linked=0,pending=0;
                QVERIFY2(SupplierInvoiceLedger::publishAndLink(db,account,replay,previous.revision,[]{return true;},&linked,&pending,&error),qPrintable(error));
                const auto ledger=SupplierInvoiceLedger::load(db,account);QVERIFY2(ledger.error.isEmpty(),qPrintable(ledger.error));
                QHash<QString,PurchaseDebt> slices;
                int matched=0,unresolved=0; qint64 remoteDebt=0,remoteTotal=0;
                for(const auto& entry:ledger.entries) {
                    if(entry.remote) {
                        remoteDebt+=entry.remaining;remoteTotal+=entry.total;
                        entry.counted?++matched:++unresolved;
                        if(pass==0)qInfo().noquote()<<"AUDIT_INVOICE"<<entry.number<<"total_cents"<<entry.total
                            <<"remote_debt_cents"<<entry.remaining<<"local_debt_cents"<<entry.localRemaining
                            <<"counted"<<entry.counted<<"reason"<<entry.reason;
                    }
                    if(!entry.counted)continue;
                    for(const auto& item:entry.items) {
                        auto& sum=slices[item.key];sum.quantity+=item.purchase.quantity;
                        sum.totalCents+=item.purchase.totalCents;sum.paidCents+=item.purchase.paidCents;
                        QVERIFY(item.purchase.quantity>0);
                        if(pass==0 && entry.remote)qInfo()<<"AUDIT_LINK"<<entry.number<<item.key<<item.purchase.quantity<<item.purchase.totalCents<<item.orders;
                    }
                }
                for(const auto& item:ledger.local) {
                    if(item.purchase.totalCents<=0)continue;
                    const auto sum=slices.value(item.key);
                    QVERIFY2(qAbs(sum.quantity-item.purchase.quantity)<0.000001,qPrintable(item.key));
                    QCOMPARE(sum.totalCents,item.purchase.totalCents);
                    QCOMPARE(sum.paidCents,item.purchase.paidCents);
                }
                qInfo()<<"AUDIT_TOTAL"<<"pass"<<pass<<"snapshot"<<ledger.snapshot.fetchedAt.toString(Qt::ISODate)
                    <<"documents"<<replay.invoices.size()<<"matched"<<matched<<"unresolved"<<unresolved
                    <<"remote_total_cents"<<remoteTotal<<"remote_debt_cents"<<remoteDebt
                    <<"ledger_debt_all_suppliers_cents"<<SupplierInvoiceLedger::debtTotal(ledger);
                QCOMPARE(financialState(),before);
                if(pass==1)QCOMPARE(linked,0);
            }
        }
    }
    void fullOpeningWorkingCopy() {
        if (qEnvironmentVariable("CS_TAB_AUDIT_COPY").isEmpty()) QSKIP("Requires isolated working-data copy");
        QLoggingCategory::setFilterRules(QStringLiteral("*.debug=false"));
        QList<QThread*> readers;
        std::atomic_int openFailures{0};
        for (int i = 0; i < 8; ++i) {
            auto* reader = QThread::create([&openFailures]() {
                QString error;
                if (!Database::open(&error)) {
                    ++openFailures;
                    qWarning() << error;
                } else {
                    QSqlQuery query(Database::db());
                    if (!query.exec("SELECT COUNT(*) FROM work_orders") || !query.next()) ++openFailures;
                }
                AppRuntimeService::closeThreadConnection();
            });
            readers.append(reader);
            reader->start();
        }
        for (auto* reader : readers) { QVERIFY(reader->wait(30000)); delete reader; }
        QCOMPARE(openFailures.load(), 0);
        MainWindow window;
        window.showMaximized();
        auto* pages = window.findChild<QStackedWidget*>("pages");
        QVERIFY(pages);
        for (int pass = 0; pass < 2; ++pass) {
            for (int page = 0; page < pages->count(); ++page) {
                QElapsedTimer elapsed; elapsed.start();
                QVERIFY(QMetaObject::invokeMethod(&window,"gotoPage",Qt::DirectConnection,Q_ARG(int,page)));
                QCOMPARE(pages->currentIndex(),page);
                QTest::qWait(250);
                auto busy = [pages]() {
                    if(pages->currentWidget()->property("pageDataLoading").toBool()) return true;
                    for (auto family : {RuntimeJob::Family::CurrentOrders,RuntimeJob::Family::History,RuntimeJob::Family::Parts,RuntimeJob::Family::Reports}) {
                        const auto state = RuntimeJob::RuntimeJobDispatcher::instance().familySnapshot(family);
                        if (state.jobId && (state.status == RuntimeJob::Status::Queued || state.status == RuntimeJob::Status::Running)) return true;
                    }
                    return false;
                };
                while (busy() && elapsed.elapsed()<30000) QTest::qWait(20);
                QVERIFY(!busy());
                QCoreApplication::processEvents();
                const auto screenshot = pages->currentWidget()->grab();
                QVERIFY(!screenshot.isNull());
                qInfo() << "FULL_TAB_OPEN" << pass << page << pages->currentWidget()->metaObject()->className() << elapsed.elapsed();
            }
        }
        QTest::qWait(1500);
    }
    void concurrentPreflightConnections() {
        QSqlQuery schema(db);
        QVERIFY(schema.exec("CREATE TABLE work_order_services(id INTEGER PRIMARY KEY)"));
        QList<QThread*> workers;
        std::atomic_int failures{0};
        const auto path=db.databaseName();
        for(int i=0;i<8;++i) {
            auto* worker=QThread::create([&failures,path]() {
                for(int j=0;j<20;++j) {
                    QString error;
                    if(!RuntimeDbPreflightValidator::validateFile(path,RuntimeDbPreflightValidator::DbRole::Archive,&error)) ++failures;
                }
            });
            workers.append(worker);worker->start();
        }
        for(auto* worker:workers) { QVERIFY(worker->wait(30000));delete worker; }
        QCOMPARE(failures.load(),0);
    }
    void debtSummaryWorkerUsesIndependentConnection() {
        int count = -1;
        double sum = -1;
        bool ok = false;
        const auto databasePath = db.databaseName();
        auto* worker = QThread::create([&, databasePath]() {
            {
                auto connection = QSqlDatabase::addDatabase("QSQLITE", currentConnectionName());
                connection.setDatabaseName(databasePath);
                if (!connection.open()) return;
            }
            ok = MainWindowMetricsService::loadUnpaidPurchasesSummary(count, sum, AccessControlService::OrderScope{});
            AppRuntimeService::closeThreadConnection();
        });
        worker->start();
        QVERIFY(worker->wait(10000));
        delete worker;
        QVERIFY(ok);
        QCOMPARE(count, 1);
        QCOMPARE(sum, 200.0);
        QVERIFY(db.isOpen());
    }
    void navigationDoesNotQueryDebts() {
        MainWindow window;
        auto* pages = window.findChild<QStackedWidget*>("pages");
        QVERIFY(pages);
        // Keep activation deferred: this measures the shared navigation path,
        // independently of each page's asynchronous data loader.
        for (int page = 0; page < 15; ++page) {
            QElapsedTimer elapsed;
            elapsed.start();
            QVERIFY(QMetaObject::invokeMethod(&window, "gotoPage", Qt::DirectConnection, Q_ARG(int, page)));
            QCOMPARE(pages->currentIndex(), page);
            qInfo() << "Navigation request page" << page << "ms" << elapsed.elapsed();
            QVERIFY(elapsed.elapsed() < 200);
        }
        QFile source(cs::tests::projectRoot() + "/src/ui/main/MainWindowRuntime.cpp");
        QVERIFY(source.open(QIODevice::ReadOnly));
        const auto code = source.readAll();
        QVERIFY(code.indexOf("void MainWindow::gotoPage") >= 0);
        QVERIFY(code.indexOf("void MainWindow::requestOpenNewOrder") > code.indexOf("void MainWindow::gotoPage"));
        const auto navigation = code.mid(code.indexOf("void MainWindow::gotoPage"),
            code.indexOf("void MainWindow::requestOpenNewOrder") - code.indexOf("void MainWindow::gotoPage"));
        QVERIFY(!navigation.contains("updateUnpaidBanner()"));
    }
    void suppliedSpareInvoiceImports() {
        const auto files=qEnvironmentVariable("CS_SPARE_INVOICE_TEST_FILES").split(';',Qt::SkipEmptyParts);
        if(files.isEmpty()) QSKIP("Set CS_SPARE_INVOICE_TEST_FILES to verify supplied invoices");
        const QStringList expectedOem{"F 026 400 553","9091706093","9Y0698451AE"};
        const QList<QDate> expectedDates{QDate(2026,8,1),QDate(2026,10,3),QDate(2026,10,10)};
        const QList<double> expectedPrices{700.0,323.0,11004.0};
        const QList<int> expectedCounts{8,1,2};
        QCOMPARE(files.size(),expectedCounts.size());
        for(int index=0;index<files.size();++index) {
            const QString path=dir.filePath(QStringLiteral("supplied-%1.pdf").arg(index));
            QVERIFY(QFile::copy(files[index],path));
            InvoiceImport::ImportJob reader;
            QSignalSpy completed(&reader,&InvoiceImport::ImportJob::finished);
            InvoiceImport::ImportJobRequest request;request.filePath=path;
            QVERIFY(reader.start(request));QVERIFY(completed.wait(60000));
            const auto result=qvariant_cast<InvoiceImport::ImportJobResult>(completed.first().first());
            QVERIFY2(result.ok(),qPrintable(result.error.message));
            qInfo()<<"Supplied invoice"<<index<<"rows"<<result.data.rows.size()
                   <<"manual mapping"<<result.data.manualMappingRequired
                   <<"columns"<<result.data.columnMappingPreview.partNameColumn
                   <<result.data.columnMappingPreview.quantityColumn<<result.data.columnMappingPreview.priceWithVatColumn
                   <<"header"<<result.data.rows.value(result.data.columnMappingPreview.headerRow);
            WorkshopInventoryPage page;page.show();
            QPointer<QDialog> form;bool started=false,filled=false;int ticks=0;
            bool countCorrect=expectedCounts[index]==1 || result.data.parsedPartDrafts.size()==expectedCounts[index];
            QTimer automation;automation.setInterval(50);
            connect(&automation,&QTimer::timeout,&page,[&] {
                auto* active=qobject_cast<QDialog*>(QApplication::activeModalWidget());
                if(!active) return;
                if(++ticks>800) {active->reject();if(form)form->reject();return;}
                if(auto* warning=qobject_cast<QMessageBox*>(active)) {
                    if(auto* yes=warning->button(QMessageBox::Yes)) yes->click();
                    else {qInfo()<<"Import warning"<<warning->text();ticks=801;warning->accept();}
                    return;
                }
                if(auto* picker=qobject_cast<QInputDialog*>(active)) {
                    auto* choices=picker->findChild<QComboBox*>();
                    countCorrect=choices->count()==expectedCounts[index];
                    choices->setCurrentIndex(index==2?1:0);picker->accept();return;
                }
                if(auto* store=active->findChild<QLineEdit*>("invoiceTemplateStore")) {
                    if(store->text().isEmpty()) store->setText("Test Supplier");
                    for(auto* check:active->findChildren<QCheckBox*>()) check->setChecked(true);
                    auto* button=active->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
                    QTimer::singleShot(0,active,[button]{button->click();});return;
                }
                if(!started) {
                    form=active;started=true;
                    auto* job=active->findChild<InvoiceImport::ImportJob*>("spareInvoiceImportJob");
                    QTimer::singleShot(0,active,[job,result,active]{
                        auto* progress=static_cast<InvoiceImportProgressDialog*>(active->findChild<QDialog*>("invoiceImportProgressDialog"));
                        InvoiceImport::ImportJobRequest request; request.filePath="test-injected-invoice.pdf";
                        progress->start(request); job->finished(result);
                    });return;
                }
                auto* name=active->findChild<QLineEdit*>("sparePartName");
                if(!name || name->text().isEmpty()) return;
                filled=active->findChild<QLineEdit*>("sparePartArticle")->text()==expectedOem[index]
                    && active->findChild<QDateEdit*>("spareOrderedDate")->date()==expectedDates[index]
                    && qAbs(active->findChild<QDoubleSpinBox*>("spareUnitCost")->value()-expectedPrices[index])<0.005;
                auto* import=active->findChild<QPushButton*>("spareImportInvoice");
                auto* save=active->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save);
                filled=filled && qAbs(import->mapTo(active,QPoint()).y()-save->mapTo(active,QPoint()).y())<8;
                const QString captures=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
                if(!captures.isEmpty()) active->grab().save(QDir(captures).filePath(QStringLiteral("supplied-invoice-%1.png").arg(index)));
                active->reject();
            });
            automation.start();page.findChild<QPushButton*>("inventoryAddSpare")->click();automation.stop();
            QVERIFY2(filled && countCorrect,qPrintable(QStringLiteral("Invoice %1 failed to fill fields, rows or footer").arg(index)));
        }
    }
    void antifreezeWarehouseAliases() {
        const auto canonical = QString::fromUtf8("Антифриз");
        for (const auto& name : {"Антифріз", "антіфріз", "антіфріза", "АНТИФРИЗ"}) {
            QCOMPARE(WorkshopInventoryService::canonicalMaterialName(QString::fromUtf8(name)), canonical);
            QCOMPARE(WorkshopInventoryService::normalizedMaterialKey(QString::fromUtf8(name)), canonical.toCaseFolded());
        }
        QVERIFY(WorkshopInventoryService::normalizedMaterialKey(QString::fromUtf8("Антифріз G12"))
            != WorkshopInventoryService::normalizedMaterialKey(QString::fromUtf8("Антифриз G13")));
        WorkshopInventoryPurchaseInput purchase;
        purchase.purchasedOn = QDate::currentDate().addDays(-1);
        purchase.materialName = QString::fromUtf8("Антіфріз"); purchase.unit = QString::fromUtf8("л");
        purchase.quantity = 20; purchase.unitCost = 300; purchase.clientPrice = 400;
        QString error;
        qint64 id = 0;
        QVERIFY2(WorkshopInventoryService::addPurchase(db, purchase, &id, &error), qPrintable(error));
        QSqlQuery q(db);
        QVERIFY(q.exec("ALTER TABLE work_orders ADD COLUMN client TEXT DEFAULT ''"));
        // Simulate a historical misspelled purchase without rewriting the stored record.
        q.prepare("UPDATE workshop_inventory_purchases SET material_name=? WHERE id=?");
        q.addBindValue(QString::fromUtf8("Антіфріз")); q.addBindValue(id); QVERIFY(q.exec());
        QTableWidget editor(1, 12);
        for (int col=0; col<12; ++col) editor.setItem(0,col,new QTableWidgetItem());
        editor.item(0,4)->setText(QString::fromUtf8("антіфріза")); editor.item(0,5)->setText("5");
        refreshPartRowClientPricing(&editor,0,4);
        QCOMPARE(editor.item(0,4)->text(),canonical);
        QCOMPARE(editor.item(0,8)->text(),QString::fromUtf8("База"));
        QCOMPARE(editor.item(0,6)->text().remove(' '),QString("400"));
        QCOMPARE(editor.item(0,6)->data(Qt::UserRole+3).toDouble(),300.0);
        PartRowPayloadInput input; input.name=QString::fromUtf8("Антифріз"); input.qty=5;
        const auto part=PartRowPayloadBuilderService::build(input);
        QCOMPARE(part.name,canonical); QCOMPARE(part.clientAmount,2000.0); QCOMPARE(part.purchaseAmount,1500.0);
        q.prepare("INSERT INTO work_order_parts(order_number,name,qty,store,installed_at) VALUES(1,?,5,?,?)");
        q.addBindValue(QString::fromUtf8("Антифріз")); q.addBindValue(QString::fromUtf8("База"));
        q.addBindValue(QDate::currentDate().toString(Qt::ISODate)); QVERIFY2(q.exec(),qPrintable(q.lastError().text()));
        const auto stock=WorkshopInventoryService::buildSnapshot(db,purchase.purchasedOn,QDate::currentDate());
        QVERIFY2(stock.errorText.isEmpty(),qPrintable(stock.errorText));
        QCOMPARE(stock.balanceRows.size(),1); QCOMPARE(stock.balanceRows.first().materialName,canonical);
        QCOMPARE(stock.balanceRows.first().balanceQuantity,15.0);
        QCOMPARE(stock.consumptions.first().quantity,5.0);
        q.prepare("SELECT material_name FROM workshop_inventory_purchases WHERE id=?"); q.addBindValue(id);
        QVERIFY(q.exec()); QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString::fromUtf8("Антіфріз"));
        QVERIFY(q.exec("ALTER TABLE work_orders ADD COLUMN order_year INTEGER DEFAULT 2026"));
        QVERIFY(q.exec("ALTER TABLE work_order_parts ADD COLUMN note TEXT DEFAULT ''"));
        QVERIFY(q.exec("CREATE VIEW router_work_order_parts AS SELECT * FROM work_order_parts"));
        QVERIFY(q.exec("CREATE TABLE work_order_part_attachments(part_id INTEGER,invoice_number TEXT,file_path TEXT,invoice_date TEXT)"));
        purchase.materialName=QString::fromUtf8("Масло 5W40");
        purchase.quantity=10;
        QVERIFY2(WorkshopInventoryService::addPurchase(db,purchase,&id,&error),qPrintable(error));
        preparePartsReportFixture();
        const auto report = ReportsDataLoader::buildPartsSnapshot(db.databaseName(), purchase.purchasedOn.toString(Qt::ISODate),
            QDate::currentDate().toString(Qt::ISODate), 0, 0, {}, {});
        QVERIFY2(report.error.isEmpty(), qPrintable(report.error));
        QCOMPARE(report.tileTitles.value(1), canonical);
        QVERIFY2(report.tileBodies.value(1).contains(QString::fromUtf8("<b>20</b> л")), qPrintable(report.tileBodies.value(1)));
        QVERIFY(report.tileBodies.value(1).contains(QString::fromUtf8("<b>5</b> л")));
        QVERIFY(report.tileBodies.value(1).contains(QString::fromUtf8("<b>15</b> л")));
        QCOMPARE(report.tileTitles.value(0),QString::fromUtf8("Олива 5W40"));
    QVERIFY(report.tileBodies.value(0).contains(QString::fromUtf8("Залишок:</td>")));
    QVERIFY(report.tileBodies.value(0).contains(QString::fromUtf8("<b>10</b> л")));
    QVERIFY(report.tileBodies.value(0).contains("REPORT_TILE_VALUE_GAP"));
    QCOMPARE(report.tileBodies.value(1).count("align='center'"),6);
    QVERIFY(report.tileBodies.value(2).contains("REPORT_TILE_VALUE_GAP"));
    QVERIFY(report.tileBodies.value(3).contains("REPORT_TILE_VALUE_GAP"));
        ReportDashboard dashboard;
        dashboard.resize(1500,100);
        dashboard.configure(3,false);
        dashboard.parts(report.tileTitles,report.tileBodies);
        dashboard.show();QTest::qWait(50);
        for(int i=0;i<4;++i){
            auto* card=dashboard.findChild<QWidget*>(QString("reportPartsTile%1").arg(i));QVERIFY(card);
            for(auto* label:card->findChildren<QLabel*>())if(label->text().contains("font-weight:700")){
                QVERIFY2(label->heightForWidth(label->width())<=label->height(),qPrintable(QString("width=%1 height=%2 required=%3 text=%4").arg(label->width()).arg(label->height()).arg(label->heightForWidth(label->width())).arg(label->text())));
                QVERIFY(label->text().contains(QString("font-size:%1pt").arg(label->font().pointSizeF()+1.0)));
            }
        }
    }
    void oilEightLitresWarehouseMovement() {
        QSqlQuery q(db);
        for (const auto& sql : {
            "ALTER TABLE work_orders ADD COLUMN order_year INTEGER DEFAULT 2026",
            "ALTER TABLE work_orders ADD COLUMN client TEXT DEFAULT ''",
            "ALTER TABLE work_orders ADD COLUMN works_sum REAL DEFAULT 0",
            "ALTER TABLE work_orders ADD COLUMN parts_sum REAL DEFAULT 0",
            "CREATE TABLE work_order_services(id INTEGER PRIMARY KEY,order_number INTEGER,order_uid TEXT,qty REAL,price REAL,amount REAL)",
            "ALTER TABLE work_order_parts ADD COLUMN note TEXT DEFAULT ''",
            "ALTER TABLE work_order_parts ADD COLUMN service_id INTEGER DEFAULT 0",
            "ALTER TABLE work_order_parts ADD COLUMN ordered_by_admin INTEGER DEFAULT 1",
            "ALTER TABLE work_order_parts ADD COLUMN markup_percent REAL DEFAULT 0",
            "ALTER TABLE work_order_parts ADD COLUMN manual_markup_override INTEGER DEFAULT 0",
            "ALTER TABLE work_order_parts ADD COLUMN markup_source TEXT DEFAULT 'policy'",
            "ALTER TABLE work_order_parts ADD COLUMN pricing_updated_at TEXT DEFAULT ''",
            "ALTER TABLE work_order_parts ADD COLUMN pricing_calc_version INTEGER DEFAULT 1"})
            QVERIFY2(q.exec(sql), qPrintable(q.lastError().text()));
        WorkshopInventoryPurchaseInput purchase;
        purchase.purchasedOn = QDate::currentDate().addDays(-1);
        purchase.materialName = QString::fromUtf8("Масло 5W40"); purchase.unit = QString::fromUtf8("л");
        purchase.quantity = 200; purchase.unitCost = 270; purchase.clientPrice = 400;
        QString error;
        QVERIFY2(WorkshopInventoryService::addPurchase(db, purchase, nullptr, &error), qPrintable(error));
        QTableWidget editor(1, 12);
        for (int col = 0; col < 12; ++col) editor.setItem(0, col, new QTableWidgetItem());
        editor.item(0, 4)->setText(QString::fromUtf8("Олива моторна 5W40"));
        editor.item(0, 5)->setText("8");
        refreshPartRowClientPricing(&editor, 0, 4);
        QCOMPARE(editor.item(0, 8)->text(), QString::fromUtf8("База"));
        QCOMPARE(editor.item(0, 6)->data(Qt::UserRole + 3).toDouble(), 270.0);
        QCOMPARE(editor.item(0, 6)->text().remove(' '), QString("400"));
        PartRowPayloadInput input;
        input.name = QString::fromUtf8("Моторна олива 5W-40"); input.qty = 8;
        auto part = PartRowPayloadBuilderService::build(input);
        QCOMPARE(part.store, QString::fromUtf8("База"));
        QCOMPARE(part.purchasePrice, 270.0); QCOMPARE(part.clientPrice, 400.0);
        QCOMPARE(part.purchaseAmount, 2160.0); QCOMPARE(part.clientAmount, 3200.0);
        QCOMPARE(part.installedAt, QDate::currentDate().toString(Qt::ISODate));
        int id = 0;
        QVERIFY2(WorkPartsPersistenceRepositoryFacade::insertPart(db, 1, part, &id, &error), qPrintable(error));
        auto snapshot = WorkshopInventoryService::buildSnapshot(db, purchase.purchasedOn, QDate::currentDate());
        QVERIFY2(snapshot.errorText.isEmpty(), qPrintable(snapshot.errorText));
        QCOMPARE(snapshot.consumptions.size(), 1);
        QCOMPARE(snapshot.consumptions.first().quantity, 8.0);
        QCOMPARE(snapshot.consumptions.first().totalCost, 2160.0);
        QCOMPARE(snapshot.balanceRows.first().balanceQuantity, 192.0);
        QCOMPARE(snapshot.balanceRows.first().stockValue, 51840.0);
        QVERIFY(q.exec("CREATE VIEW router_work_order_parts AS SELECT * FROM work_order_parts"));
        QVERIFY(q.exec("CREATE TABLE work_order_part_attachments(part_id INTEGER,invoice_number TEXT,file_path TEXT,invoice_date TEXT)"));
        preparePartsReportFixture();
        const auto report = ReportsDataLoader::buildPartsSnapshot(db.databaseName(), purchase.purchasedOn.toString(Qt::ISODate),
            QDate::currentDate().toString(Qt::ISODate), 0, 0, {}, {});
        QVERIFY2(report.error.isEmpty(), qPrintable(report.error));
        bool foundStock = false;
        for (const auto& row : report.rows) if (!row.isStoreHeader && row.store == QString::fromUtf8("Склад СТО")) {
            foundStock = true;
            QCOMPARE(row.purchase, QString("2 160")); QCOMPARE(row.client, QString("3 200"));
            QCOMPARE(row.net, QString("1 040"));
        }
        QVERIFY(foundStock);
        WorkshopInventoryPage page; page.resize(1200, 750); page.show(); page.refresh();
        QCoreApplication::processEvents();
        auto* balances = page.findChild<QTableView*>("inventoryBalances"); QVERIFY(balances);
        QCOMPARE(balances->model()->columnCount(), 7);
        QCOMPARE(balances->model()->index(0, 1).data().toString(), QString("1"));
        QCOMPARE(balances->model()->index(0, 3).data().toString(), QString("8"));
        QCOMPARE(balances->model()->index(0, 4).data().toString(), QString("192"));
        QCOMPARE(balances->model()->index(0, 5).data().toString(), QString("2 160"));
        QCOMPARE(balances->model()->index(0, 6).data().toString(), QString("51 840"));
        auto* tabs = page.findChild<QTabWidget*>("inventoryBalanceTabs"); QVERIFY(tabs);
        QCOMPARE(tabs->tabText(0), QString::fromUtf8("Залишки"));
        QCOMPARE(tabs->tabText(1), QString::fromUtf8("Придбання"));
        tabs->setCurrentIndex(1);
        bool openedBalances = false;
        auto* dialog = page.findChild<QDialog*>("workshopInventoryPurchasesTab"); QVERIFY(dialog);
        QTimer::singleShot(100, dialog, [&] { openedBalances = tabs->currentIndex() == 0;
            dialog->grab().save(previousWorkingDirectory + "/build-single-instance-audit/oil-eight-litres-balance.png"); dialog->reject(); });
        page.findChild<QPushButton*>("inventoryBalance")->click();
        QVERIFY(openedBalances);
        const QRect available = page.screen()->availableGeometry();
        QCOMPARE(dialog->width(), qRound(available.width() * 0.64));
        QCOMPARE(dialog->height(), qRound(available.height() * 0.63));
        input.qty = 6; part = PartRowPayloadBuilderService::build(input);
        QVERIFY2(WorkPartsPersistenceRepositoryFacade::updatePart(db, 1, id, part, &error), qPrintable(error));
        snapshot = WorkshopInventoryService::buildSnapshot(db, purchase.purchasedOn, QDate::currentDate());
        QCOMPARE(snapshot.consumptions.size(), 1); QCOMPARE(snapshot.balanceRows.first().balanceQuantity, 194.0);
        input.store = "External shop";
        auto external = PartRowPayloadBuilderService::build(input);
        external.installedAt = QDate::currentDate().toString(Qt::ISODate);
        int externalId = 0;
        QVERIFY2(WorkPartsPersistenceRepositoryFacade::insertPart(db, 1, external, &externalId, &error), qPrintable(error));
        snapshot = WorkshopInventoryService::buildSnapshot(db, purchase.purchasedOn, QDate::currentDate());
        QCOMPARE(snapshot.consumptions.size(), 1);
        QVERIFY2(WorkPartsPersistenceRepositoryFacade::removePart(db, 1, id, &error), qPrintable(error));
        snapshot = WorkshopInventoryService::buildSnapshot(db, purchase.purchasedOn, QDate::currentDate());
        QCOMPARE(snapshot.consumptions.size(), 0); QCOMPARE(snapshot.balanceRows.first().balanceQuantity, 200.0);
        input.name = QString::fromUtf8("Олива моторна 5W30"); input.store.clear();
        QVERIFY(PartRowPayloadBuilderService::build(input).store.isEmpty());
        // Customer-owned oil remains visible but never consumes stock or creates a purchase debt.
        editor.setProperty("csCustomerPartsMode", true);
        refreshPartRowClientPricing(&editor, 0, 5);
        QCOMPARE(editor.item(0, 8)->text(), QString::fromUtf8("База"));
        editor.setRowCount(2);
        for (int col = 0; col < 12; ++col) editor.setItem(1, col, new QTableWidgetItem());
        editor.item(1, 4)->setText(QString::fromUtf8("Олива моторна 5W40"));
        editor.item(1, 5)->setText("8");
        editor.item(1, 6)->setText("999");
        refreshPartRowClientPricing(&editor, 1, 4);
        QCOMPARE(editor.item(1, 8)->text(), QString::fromUtf8("З/Ч клієнта"));
        QVERIFY(editor.item(1, 6)->text().isEmpty());
        QVERIFY(editor.item(1, 7)->text().isEmpty());
        editor.setProperty("csCustomerPartsMode", false);
        refreshPartRowClientPricing(&editor, 1, 5);
        QCOMPARE(editor.item(1, 8)->text(), QString::fromUtf8("З/Ч клієнта"));
        input.name = editor.item(1, 4)->text(); input.store = editor.item(1, 8)->text();
        input.qty = 8; input.purchasePrice = 999; input.manualMarkupOverride = true; input.manualMarkupPercent = 50;
        auto customer = PartRowPayloadBuilderService::build(input);
        QCOMPARE(customer.purchasePrice, 0.0); QCOMPARE(customer.purchaseAmount, 0.0);
        QCOMPARE(customer.clientPrice, 0.0); QCOMPARE(customer.clientAmount, 0.0);
        QCOMPARE(customer.markupSource, QString("customer"));
        customer.installedAt = QDate::currentDate().toString(Qt::ISODate);
        int customerId = 0;
        QVERIFY2(WorkPartsPersistenceRepositoryFacade::insertPart(db, 1, customer, &customerId, &error), qPrintable(error));
        snapshot = WorkshopInventoryService::buildSnapshot(db, purchase.purchasedOn, QDate::currentDate());
        QCOMPARE(snapshot.balanceRows.first().balanceQuantity, 200.0);
        QVERIFY(snapshot.consumptions.isEmpty());
        PartsQueryService::invalidateCache();
        for (const auto& debt : PurchasePaymentService::orderPartDebts(db, 1, &error)) QVERIFY(debt.id != customerId);
        for (const auto& row : PartsQueryService::loadRows(db, {}, {}, {})) if (row.id == customerId) {
            const auto display = PartsPageTableModelAdapter::adaptRow(row, QDate::currentDate().addDays(10), true);
            QVERIFY(!display.overdue); QVERIFY(!display.paymentActionsEnabled);
            QVERIFY(display.priceDisplay.isEmpty()); QVERIFY(display.amountDisplay.isEmpty());
        }
        q.prepare("SELECT store,price,amount,purchase_price,purchase_amount FROM work_order_parts WHERE id=?");
        q.addBindValue(customerId); QVERIFY(q.exec()); QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(), QString::fromUtf8("З/Ч клієнта"));
        for (int col = 1; col <= 4; ++col) QCOMPARE(q.value(col).toDouble(), 0.0);
        editor.setRowCount(3);
        for (int col = 0; col < 12; ++col) editor.setItem(2, col, new QTableWidgetItem());
        editor.item(2, 4)->setText(input.name); editor.item(2, 5)->setText("8");
        refreshPartRowClientPricing(&editor, 2, 4);
        QCOMPARE(editor.item(2, 8)->text(), QString::fromUtf8("База"));
        QCOMPARE(editor.item(2, 6)->data(Qt::UserRole + 3).toDouble(), 270.0);
    }
    void invoiceImportPreviewKeepsScreenBounds() {
        InvoiceImport::ExcelReadResult data;data.documentNumber="TEST-123";data.documentDate="2026-10-28";
        QList<InvoiceImport::ImportedPartDraft> drafts;
        for(int i=0;i<6;++i){InvoiceImport::ImportedPartDraft d;d.name="Filter";d.oemNumber=QString::number(i);d.store="Test";d.qtyText="2";d.priceText="150";drafts.append(d);}
        auto ownedDialog=CurrentOrdersSupport::createInvoiceReview("preview.pdf",data,drafts,nullptr);
        auto& dialog=*ownedDialog;dialog.show();QTest::qWait(100);
        auto* table=dialog.findChild<QTableView*>("invoiceReviewTable");QVERIFY(table);QCOMPARE(table->model()->rowCount(),6);
        const QRect available=dialog.screen()->availableGeometry();
        if(!qEnvironmentVariable("CS_REVIEW_CAPTURE").isEmpty()) {
            QVERIFY(dialog.grab().save(qEnvironmentVariable("CS_REVIEW_CAPTURE")));
            qInfo()<<"REVIEW_GEOMETRY"<<dialog.size()<<"DPR"<<dialog.devicePixelRatioF()<<"SCREEN"<<available;
        }
        QVERIFY(available.contains(dialog.frameGeometry()));
        dialog.findChild<QLineEdit*>("reviewBulkOrder")->setText("403");
        QCOMPARE(table->model()->index(0,2).data().toString(),QString());
        dialog.findChild<QPushButton*>("reviewApply")->click();
        for(int row=0;row<6;++row)QCOMPARE(table->model()->index(row,2).data().toString(),QString("403"));
        for(auto* button:dialog.findChildren<QPushButton*>())if(button->isVisible())QVERIFY(dialog.rect().contains(QRect(button->mapTo(&dialog,QPoint()),button->size())));
        QTimer::singleShot(0,[]{if(auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))box->button(QMessageBox::Yes)->click();});
        dialog.reject();QVERIFY(!dialog.isVisible());
    }
    void manualPartsWindowWorkAreaAndDrag() {
        InvoiceImport::ExcelReadResult data;data.documentDate="2026-10-28";
        QList<InvoiceImport::ImportedPartDraft> drafts;
        for(int i=0;i<8;++i){InvoiceImport::ImportedPartDraft d;d.name="Part";d.qtyText="1";d.priceText="10";drafts.append(d);}
        InvoiceReviewDialog dialog(drafts,data,nullptr,{},true);dialog.useWhiteInputs();dialog.show();QTest::qWait(100);
        const QRect area=dialog.screen()->availableGeometry();
        const int gap=qRound(dialog.screen()->logicalDotsPerInchY()*5.0/25.4);
        QCOMPARE(area.bottom()-dialog.frameGeometry().bottom(),gap);
        dialog.findChild<QPushButton*>("reviewImport")->click();QTest::qWait(100);
        QVERIFY(area.bottom()-dialog.frameGeometry().bottom()>=gap);
        auto* button=dialog.findChild<QPushButton*>("reviewImport");
        QVERIFY(dialog.rect().contains(QRect(button->mapTo(&dialog,QPoint()),button->size())));
        auto* header=dialog.findChild<QWidget*>("reviewHeader");QVERIFY(header);
        const QPoint before=dialog.pos(),local(100,20),global=header->mapToGlobal(local),delta(12,-12);
        QMouseEvent press(QEvent::MouseButtonPress,local,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(header,&press);
        QMouseEvent motion(QEvent::MouseMove,local+delta,global+delta,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(header,&motion);
        QMouseEvent release(QEvent::MouseButtonRelease,local,global+delta,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(header,&release);QCOMPARE(dialog.pos(),before+delta);
        const QString captures=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
        if(!captures.isEmpty())QVERIFY(dialog.grab().save(QDir(captures).filePath("manual-parts-work-area.png")));
    }
    void invoiceReviewSelectionByMouseAndKeyboard() {
        InvoiceImport::ExcelReadResult data;data.documentNumber="TEST-SELECT";data.documentDate="2026-10-30";
        InvoiceImport::ImportedPartDraft first;first.name="First";first.store="Test";first.qtyText="1";first.priceText="10";
        auto second=first;second.name="Second";second.priceText="20";
        for(const QString& order : {QString(),QString("541")}) {
            InvoiceReviewDialog dialog({first,second},data,nullptr,order);
            dialog.show();QTest::qWait(50);
            auto* table=dialog.findChild<QTableView*>("invoiceReviewTable");QVERIFY(table);
            table->setColumnWidth(0,110); // Reproduce the widened checkbox column in the screenshot.
            const auto index=table->model()->index(0,0);
            QTest::mouseClick(table->viewport(),Qt::LeftButton,Qt::NoModifier,table->visualRect(index).center());
            QCOMPARE(dialog.selectedRows(),QList<int>{1});
            QCOMPARE(dialog.selectedDrafts().first().name,second.name);
            table->setCurrentIndex(index);table->setFocus();QTest::keyClick(table,Qt::Key_Space);
            QCOMPARE(dialog.selectedRows().size(),2);
            auto* all=dialog.findChild<QCheckBox*>("reviewSelectAll");QVERIFY(all);
            QTest::mouseClick(all,Qt::LeftButton,Qt::NoModifier,all->rect().center());
            QVERIFY(dialog.selectedRows().isEmpty());
            QVERIFY(!dialog.findChild<QPushButton*>("reviewImport")->isEnabled());
            QTest::mouseClick(all,Qt::LeftButton,Qt::NoModifier,all->rect().center());
            QCOMPARE(dialog.selectedRows().size(),2);
            QTest::mouseClick(table->viewport(),Qt::RightButton,Qt::NoModifier,table->visualRect(index).center());
            QCOMPARE(dialog.selectedRows().size(),2);
            dialog.reject();
        }
    }
    void manualSparePartsPreview() {
        InvoiceImport::ExcelReadResult data;data.documentDate="2026-10-28";
        InvoiceImport::ImportedPartDraft draft;draft.name=QString::fromUtf8("Прокладка глушника");draft.oemNumber="9091706093";draft.store=QString::fromUtf8("Автонова-D");draft.qtyText="1";draft.priceText="323.00";draft.deliveryDate="28.10.26";
        InvoiceReviewDialog dialog({draft},data,nullptr,{},true);dialog.useWhiteInputs();dialog.show();QTest::qWait(100);
        auto* table=dialog.findChild<QTableView*>("invoiceReviewTable");QVERIFY(table);
        QCOMPARE(table->model()->index(0,1).data().toString(),draft.deliveryDate);
        dialog.findChild<QPushButton*>("reviewAddPartRow")->click();QCOMPARE(table->model()->rowCount(),2);
        table->setCurrentIndex(table->model()->index(1,5));dialog.findChild<QPushButton*>("reviewRemovePartRow")->click();QCOMPARE(table->model()->rowCount(),1);
        const QString captures=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
        table->setCurrentIndex(table->model()->index(0,5));table->edit(table->model()->index(0,5));QTest::qWait(50);
        auto* editor=table->findChild<QLineEdit*>();QVERIFY(editor);QVERIFY(editor->isVisible());
        QVERIFY(editor->height()>=editor->fontMetrics().height()+4);
        QVERIFY(table->visualRect(table->model()->index(0,5)).contains(editor->geometry()));
        editor->setText(QString::fromUtf8("Рульова рейка"));
        if(!captures.isEmpty())QVERIFY(dialog.grab().save(QDir(captures).filePath("spare-parts-editing-text.png")));
        QTest::keyClick(editor,Qt::Key_Return);
        QTRY_COMPARE(table->model()->index(0,5).data().toString(),QString::fromUtf8("Рульова рейка"));
        table->model()->setData(table->model()->index(0,5),draft.name);
        if(!captures.isEmpty())QVERIFY(dialog.grab().save(QDir(captures).filePath("spare-parts-editor.png")));
        QVERIFY(dialog.screen()->availableGeometry().contains(dialog.frameGeometry()));
        int commits=0;dialog.commit=[&](QString*){++commits;return true;};
        dialog.findChild<QPushButton*>("reviewImport")->click();QCOMPARE(commits,1);QCOMPARE(dialog.result(),int(QDialog::Accepted));
        QCOMPARE(dialog.selectedDrafts().first().name,draft.name);
        InvoiceReviewDialog cancelled({draft},data,nullptr,{},true);cancelled.commit=[&](QString*){++commits;return true;};cancelled.reject();QCOMPARE(commits,1);
    }
    void invoicesSearchByVehicleAndOrder() {
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE work_order_part_attachments(part_id INTEGER PRIMARY KEY,invoice_number TEXT,invoice_date TEXT,file_path TEXT)"));
        QVERIFY(q.exec("INSERT INTO work_order_part_attachments VALUES(1,'INV-TEST','01.09.26','invoice.pdf')"));
        InvoicesPage page;
        page.refresh();
        auto* table = page.findChild<QTableView*>("invoicesTable"); QVERIFY(table);
        auto* search = page.findChild<QLineEdit*>("invoiceSearch"); QVERIFY(search);
        QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(), 1, 10000);
        for (const QString& query : {QString("audi"), QString("AA 1234 AA"), QString("1234"),
                 QString("1"), QString("INV-TEST"), QString("store"), QString("Audi 1234")}) {
            search->setText(query);
            QVERIFY2(table->model()->rowCount() == 1, qPrintable(query));
        }
        search->setText("missing-vehicle"); QCOMPARE(table->model()->rowCount(), 0);
        search->clear(); QCOMPARE(table->model()->rowCount(), 1);
        auto* status = page.findChild<QComboBox*>(); QVERIFY(status);
        status->setCurrentIndex(2); QCOMPARE(table->model()->rowCount(), 0);
        status->setCurrentIndex(1); search->setText("Audi"); QCOMPARE(table->model()->rowCount(), 1);
    }
    void settingsCaptionInsets() {
        SettingsPage page;
        page.resize(1300,850);
        page.show();
        QTest::qWait(150);
        auto verifyInsets = [&page]() {
            for (const char* name : {"hdrTitle", "footerText"}) {
                auto* label = page.findChild<QLabel*>(name);
                QVERIFY(label);
                const int inset = qRound(2.0 * page.logicalDpiX() / 25.4);
                QCOMPARE(label->parentWidget()->layout()->contentsMargins().left(), inset);
                QCOMPARE(label->x(), inset);
            }
        };
        verifyInsets();
        for (auto* button : page.findChildren<QPushButton*>()) {
            if (button->text() == QString::fromUtf8("Зовнішній екран") ||
                button->text() == QString::fromUtf8("Персонал")) {
                button->click();
                QTest::qWait(50);
                verifyInsets();
            }
        }
        page.refreshThemeStyles();
        QTest::qWait(50);
        verifyInsets();
    }

    void debtBannerPinnedLayout() {
        MainWindow window;
        window.resize(1400, 900);
        window.show();
        QTest::qWait(100);
        auto* banner = window.findChild<UnpaidBannerWidget*>("sidebar_debt_banner");
        auto* scroll = window.findChild<QScrollArea*>("sidebar_top_scroll");
        QVERIFY(banner && scroll);
        QCOMPARE(banner->parentWidget(), window.findChild<QFrame*>("sidebar"));
        QCOMPARE(banner->height(), qRound(28.0 * banner->logicalDpiY() / 25.4));
        const auto origin = banner->mapTo(&window, QPoint());
        QCOMPARE(scroll->mapTo(&window, QPoint()).y() - origin.y() - banner->height(),
                 qRound(10.0 * banner->logicalDpiY() / 25.4));
        QCOMPARE(banner->width(), scroll->width());
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
        banner->setDebt(false, "0", "0");
        QTest::qWait(30);
        QCOMPARE(banner->mapTo(&window, QPoint()), origin);
    }
    void debtBannerRectangularOutline() {
        UnpaidBannerWidget banner;banner.resize(220,140);banner.show();
        for(bool debt:{true,false}) {
            banner.setDebt(debt,"35","80 305.86");
            QTest::qWait(20);
            const auto image=banner.grab().toImage();
            const auto scale=image.devicePixelRatio();
            for(const auto point:{QPoint(0,0),QPoint(banner.width()-1,0),QPoint(0,banner.height()-1),QPoint(banner.width()-1,banner.height()-1)})
                QCOMPARE(image.pixelColor(qRound(point.x()*scale),qRound(point.y()*scale)),QColor(Qt::black));
            const auto output=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
            if(!output.isEmpty()) {
                QVERIFY(QDir().mkpath(output));
                QVERIFY(image.save(output+(debt?"/banner-debt.png":"/banner-paid.png")));
            }
        }
    }
    void legacySupplierNameDisplaysCanonically() {
        QSqlQuery query(db);
        query.prepare("UPDATE work_order_parts SET store=?");
        query.addBindValue(QString::fromUtf8("Автонова-Д"));
        QVERIFY(query.exec());
        QString error;
        const auto rows = PartsQueryService::loadRows(db, QString(), {}, {}, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!rows.isEmpty());
        for (const auto& row : rows) QCOMPARE(row.store, QString::fromUtf8("Автонова-D"));
        const auto debts = PurchasePaymentService::debts(db, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!debts.isEmpty());
        for (const auto& debt : debts) QCOMPARE(debt.supplier, QString::fromUtf8("Автонова-D"));
        QVERIFY(query.exec("SELECT DISTINCT store FROM work_order_parts"));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(), QString::fromUtf8("Автонова-Д"));
    }
    void debtBannerOpensDetails() {
        UnpaidBannerWidget banner;
        banner.show();
        QSignalSpy clicks(&banner, &UnpaidBannerWidget::clicked);
        QTest::mouseClick(&banner, Qt::LeftButton);
        QTest::keyClick(&banner, Qt::Key_Return);
        QCOMPARE(clicks.count(), 2);
        bool inspected = false;
        QTimer::singleShot(100, [&] {
            auto* dialog = qobject_cast<QDialog*>(qApp->activeModalWidget());
            if (!dialog) return;
            auto* summary = dialog->findChild<QTextBrowser*>("supplierDebtDetailsSummary");
            const auto closeDialog = qScopeGuard([dialog] { dialog->accept(); });
            auto* page=dialog->findChild<QWidget*>("purchaseDebtsPage");
            QVERIFY(page);
            QTRY_VERIFY_WITH_TIMEOUT(!page->property("debtDataLoading").toBool(),30000);
            auto* table = dialog->findChild<QTableView*>("purchaseDebtsTable");
            inspected = summary && !summary->toPlainText().isEmpty() && table && table->model()->rowCount() > 0;
            if (summary && table) {
                const auto originalHtml = summary->toHtml();
                const auto originalSize = dialog->size();
                const auto available = dialog->screen()->availableGeometry().adjusted(32, 32, -32, -32);
                QCOMPARE(originalSize, QSize(qMin(975, available.width()), qMin(550, available.height())));
                QVERIFY(dialog->grab().save(previousWorkingDirectory + "/build-single-instance-audit/debt-reference-initial.png"));
                dialog->resize(dialog->width(), qMin(760, available.height()));
                summary->setHtml(QString("<p>Supplier debt summary</p>").repeated(12));
                QTest::qWait(150);
                QVERIFY(summary->height() > 240);
                QCOMPARE(summary->verticalScrollBar()->maximum(), 0);
                QVERIFY(table->height() > 80);
                dialog->resize(dialog->width(), 540);
                QTest::qWait(150);
                QVERIFY(summary->height() <= dialog->height() - 240);
                QVERIFY(table->height() > 50);
                dialog->resize(dialog->width(), qMin(760, available.height()));
                QTest::qWait(150);
                QCOMPARE(summary->verticalScrollBar()->maximum(), 0);
                summary->setHtml(originalHtml);
                dialog->resize(originalSize);
                QTest::qWait(100);
            }
            dialog->grab().save(previousWorkingDirectory + "/build-single-instance-audit/debt-banner-details.png");
            dialog->accept();
        });
        PurchaseDebtsPage::showDetails(&banner);
        QVERIFY(inspected);
    }
    void externalPlanningFitsBothPanels() {
        QWidget owner;
        owner.setGeometry(100, 100, 500, 400);
        const QRect ownerGeometry = owner.geometry();
        ExternalDisplayController displays(&owner, [](int id, QWidget* parent) {
            auto* page = new PlanningPage(parent);
            page->setDailyView(id == 100);
            return ExternalDisplayController::Page{page, [page] { page->refresh(); }};
        }, [](int) { return true; });
        QString error;
        QVERIFY2(displays.display(qApp->primaryScreen(), {0, 100}, 1, &error), qPrintable(error));
        auto* window = owner.findChild<QWidget*>("externalDisplayWindow"); QVERIFY(window);
        QCOMPARE(owner.geometry(), ownerGeometry);
        for (int mode = 0; mode < 2; ++mode) {
            if (mode) window->findChild<QSplitter*>("externalOuterSplitter")->setSizes({350, 650});
            QTest::qWait(600);
            const auto views = window->findChildren<QGraphicsView*>("externalFittedPage");
            QCOMPARE(views.size(), 2);
            for (auto* view : views) {
                QVERIFY(view->viewport()->rect().contains(view->mapFromScene(view->sceneRect()).boundingRect()));
                auto* proxy = qgraphicsitem_cast<QGraphicsProxyWidget*>(view->scene()->items().last()); QVERIFY(proxy);
                auto* page = proxy->widget(); QVERIFY(page);
                auto* date = page->findChild<QLabel*>("externalCurrentDate"); QVERIFY(date);
                QVERIFY(date->text().contains(QString::number(QDate::currentDate().year())));
                QVERIFY(page->findChild<QPushButton*>("planningCurrentMode")->isHidden());
                auto* scroll = page->findChild<QScrollArea*>("planning_scroll");
                if (!scroll->isHidden()) {
                    QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
                    QCOMPARE(scroll->verticalScrollBar()->maximum(), 0);
                    const QDate today = QDate::currentDate();
                    const QDate monday = today.addDays(1 - today.dayOfWeek());
                    PlanningDayCell* current = nullptr;
                    PlanningDayCell* other = nullptr;
                    int firstTop = INT_MAX;
                    for (auto* cell : page->findChildren<PlanningDayCell*>()) {
                        if (cell->isHidden()) continue;
                        QVERIFY(cell->date() >= monday);
                        firstTop = qMin(firstTop, cell->geometry().top());
                        if (cell->date() == today) current = cell;
                        else if (cell->date() >= monday && cell->date() < monday.addDays(6)) other = cell;
                    }
                    if (today.dayOfWeek() != 7) {
                        QVERIFY(current); QVERIFY(other);
                        QCOMPARE(current->geometry().top(), firstTop);
                        const double widthRatio = double(current->width()) / other->width();
                        const double heightRatio = double(current->height()) / other->height();
                        QVERIFY2(widthRatio >= 1.10 && widthRatio <= 1.15, qPrintable(QString::number(widthRatio)));
                        QVERIFY2(heightRatio >= 1.10 && heightRatio <= 1.15, qPrintable(QString::number(heightRatio)));
                    }
                } else {
                    auto* table = page->findChild<QTableWidget*>("planningDailyTable"); QVERIFY(table);
                    QCOMPARE(table->rowCount(), 11);
                    QCOMPARE(table->verticalScrollBar()->maximum(), 0);
                    QVERIFY(qAbs(table->verticalHeader()->length() - table->viewport()->height()) <= 2);
                }
            }
        }
        QVERIFY(window->grab().save(previousWorkingDirectory + "/build-single-instance-audit/external-planning-fit.png"));
        auto* settings = displays.settingsWidget(&owner);
        settings->findChild<QComboBox*>("externalLayoutSelector")->setCurrentIndex(1);
        for (int i = 0; i < 2; ++i) {
            auto* selector = settings->findChild<QComboBox*>("externalPanel" + QString::number(i));
            selector->setCurrentIndex(selector->findData(i == 0 ? 0 : 100));
        }
        settings->findChild<QPushButton*>("configureExternalDisplay")->click();
        auto* editor = owner.findChild<QWidget*>("externalDisplayEditor"); QVERIFY(editor);
        editor->resize(1360, 768); QTest::qWait(800);
        QVERIFY(editor->grab().save(previousWorkingDirectory + "/build-single-instance-audit/external-editor-planning.png"));
        QCOMPARE(owner.geometry(), ownerGeometry);
        editor->close();
        displays.stopAll();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    void externalDisplayThreePanelLayouts() {
        QWidget owner;
        ExternalDisplayController displays(&owner, [](int id, QWidget* parent) {
            return ExternalDisplayController::Page{new QLabel(QString::number(id), parent), {}};
        }, [](int) { return true; });
        auto* screen = qApp->primaryScreen(); QVERIFY(screen);
        auto* settings = displays.settingsWidget(&owner);
        auto* layout = settings->findChild<QComboBox*>("externalLayoutSelector"); QVERIFY(layout);
        QCOMPARE(layout->count(), 7);
        for (int mode : {4, 5, 6}) {
            layout->setCurrentIndex(mode);
            for (int i = 0; i < 3; ++i) {
                auto* choice = settings->findChild<QComboBox*>("externalPanel" + QString::number(i));
                choice->setCurrentIndex(choice->findData(i == 2 ? 3 : i));
                QVERIFY(!choice->parentWidget()->isHidden());
            }
            QVERIFY(settings->findChild<QComboBox*>("externalPanel3")->parentWidget()->isHidden());
            settings->findChild<QPushButton*>("showExternalDisplay")->click();
            auto* window = owner.findChild<QWidget*>("externalDisplayWindow"); QVERIFY(window);
            auto* outer = window->findChild<QSplitter*>("externalOuterSplitter"); QVERIFY(outer);
            QCOMPARE(window->findChildren<QGraphicsView*>("externalFittedPage").size(), 3);
            if (mode == 4) {
                QCOMPARE(outer->orientation(), Qt::Horizontal);
                QCOMPARE(outer->count(), 3);
            } else {
                QCOMPARE(outer->orientation(), Qt::Vertical);
                QCOMPARE(outer->count(), 2);
                auto* top = window->findChild<QSplitter*>("externalRow0"); QVERIFY(top);
                auto* bottom = window->findChild<QSplitter*>("externalRow1"); QVERIFY(bottom);
                QCOMPARE(top->count(), mode == 5 ? 2 : 1);
                QCOMPARE(bottom->count(), mode == 5 ? 1 : 2);
            }
            auto* reopened = displays.settingsWidget(&owner);
            QCOMPARE(reopened->findChild<QComboBox*>("externalLayoutSelector")->currentIndex(), mode);
            delete reopened;
            displays.stopAll();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QString error;
            QVERIFY(!displays.display(screen, {0, 1, 3, 4}, mode, &error));
        }
    }
    void externalDisplayLayoutAndLifecycle() {
        QWidget owner; int refreshed = 0;
        ExternalDisplayController displays(&owner, [&](int id, QWidget* parent) {
            auto* label = new QLabel(QString::fromUtf8("Тестовий розділ %1").arg(id), parent);
            return ExternalDisplayController::Page{label, [&] { ++refreshed; }};
        }, [](int id) { return id != 6; });
        QString error; auto* screen = qApp->primaryScreen(); QVERIFY(screen);
        QVERIFY(!ExternalDisplayController::screenKey(screen).isEmpty());
        QVERIFY(!displays.display(nullptr, {0}, 0, &error));
        QVERIFY(!displays.display(screen, {0, 1}, 0, &error));
        QVERIFY(displays.display(screen, {0, 0}, 1, &error));
        displays.stop(screen); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); refreshed = 0;
        QVERIFY(!displays.display(screen, {6}, 0, &error));
        auto* settings = displays.settingsWidget(&owner); settings->resize(900, 760); settings->show();
        auto* left = settings->findChild<QComboBox*>("externalPanel0"); QVERIFY(left); QCOMPARE(left->count(), 8);
        auto* right = settings->findChild<QComboBox*>("externalPanel1"); QVERIFY(right);
        auto* selector = settings->findChild<QComboBox*>("externalScreenSelector"); QVERIFY(selector); QCOMPARE(selector->count(), qApp->screens().size());
        left->setCurrentIndex(left->findData(0)); right->setCurrentIndex(right->findData(100));
        auto* layout = settings->findChild<QComboBox*>("externalLayoutSelector"); QVERIFY(layout); layout->setCurrentIndex(1);
        auto* show = settings->findChild<QPushButton*>("showExternalDisplay"); QVERIFY(show);
        auto* back = settings->findChild<QPushButton*>("externalBack"); QVERIFY(back);
        auto* stop = settings->findChild<QPushButton*>("stopExternalDisplay"); QVERIFY(stop);
        QTest::qWait(100);
        QCOMPARE(show->geometry().center().y(), back->geometry().center().y());
        QCOMPARE(stop->geometry().center().y(), back->geometry().center().y());
        QCOMPARE(show->text(), QString::fromUtf8("Відобразити")); show->click();
        auto* reopened = displays.settingsWidget(&owner);
        QCOMPARE(reopened->findChild<QComboBox*>("externalLayoutSelector")->currentIndex(), 1);
        QCOMPARE(reopened->findChild<QComboBox*>("externalPanel0")->currentData().toInt(), 0);
        QCOMPARE(reopened->findChild<QComboBox*>("externalPanel1")->currentData().toInt(), 100);
        auto* previewButton = settings->findChild<QPushButton*>("refreshExternalPreview"); QVERIFY(previewButton);
        previewButton->click(); QTest::qWait(1700); QVERIFY(previewButton->isEnabled());
        QVERIFY(settings->findChild<QWidget*>("externalLayoutPreview")->height() < 270);
        displays.stop(screen); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        const QString output = qEnvironmentVariable("CS_EXTERNAL_DISPLAY_TEST_OUTPUT");
        if (!output.isEmpty()) QVERIFY(settings->grab().save(QDir(output).filePath("external-display-settings.png")));
        QVERIFY2(displays.display(screen, {0, 1, 3, 4}, 3, &error), qPrintable(error));
        auto* window = owner.findChild<QWidget*>("externalDisplayWindow"); QVERIFY(window);
        QCOMPARE(window->findChildren<QSplitter*>().size(), 3);
        QCOMPARE(window->windowHandle()->screen(), screen);
        QCOMPARE(refreshed, 8); displays.refresh(); QCOMPARE(refreshed, 12);
        QTest::qWait(100);
        if (!output.isEmpty()) QVERIFY(window->grab().save(QDir(output).filePath("external-display-grid.png")));
        displays.stop(screen); QVERIFY(!window->isVisible());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!owner.findChild<QWidget*>("externalDisplayWindow"));
        auto* configure = settings->findChild<QPushButton*>("configureExternalDisplay"); QVERIFY(configure);
        configure->click(); QTest::qWait(300);
        auto* editor = owner.findChild<QWidget*>("externalDisplayEditor"); QVERIFY(editor);
        QVERIFY(!editor->isFullScreen());
        auto* font = editor->findChild<QSpinBox*>("externalFontSize"); QVERIFY(font); font->setValue(30);
        const QString appearanceKey = "externalDisplays/" + ExternalDisplayController::screenKey(screen) + "/appearance/fontSize";
        QCOMPARE(QSettings().value(appearanceKey).toInt(), 30);
        if (!output.isEmpty()) QVERIFY(editor->grab().save(QDir(output).filePath("external-display-editor.png")));
        editor->close(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        configure->click(); QTest::qWait(100);
        editor = owner.findChild<QWidget*>("externalDisplayEditor"); QVERIFY(editor);
        QCOMPARE(editor->findChild<QSpinBox*>("externalFontSize")->value(), 30);
        editor->close(); QSettings().remove(appearanceKey);
        QVERIFY(displays.display(screen, {0, 1}, 2, &error));
        window = owner.findChild<QWidget*>("externalDisplayWindow"); QVERIFY(window);
        auto* splitter = window->findChild<QSplitter*>("externalOuterSplitter"); QVERIFY(splitter); QCOMPARE(splitter->orientation(), Qt::Vertical);
        owner.close(); QVERIFY(!window->isVisible());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    void externalDisplayLiveReadersAndRoleChange() {
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE work_order_part_attachments(part_id INTEGER PRIMARY KEY,invoice_number TEXT,invoice_date TEXT,file_path TEXT)"));
        QVERIFY(q.exec("INSERT INTO work_order_part_attachments VALUES(1,'INV-1','01.09.26','invoice.pdf'),(2,'INV-2','02.09.26','second.pdf')"));
        QWidget owner;
        // These live readers use the fixture's explicit database path rather
        // than the complete encrypted runtime required by order-list workers.
        ExternalDisplayController displays(&owner, [](int, QWidget* parent) {
            auto* page = new InvoicesPage(parent); QPointer<InvoicesPage> guard(page);
            return ExternalDisplayController::Page{page, [guard] { if (guard) guard->refresh(); }};
        }, [](int) { return true; });
        QString error; QVERIFY2(displays.display(qApp->primaryScreen(), {1, 4}, 1, &error), qPrintable(error));
        auto* window = owner.findChild<QWidget*>("externalDisplayWindow"); QVERIFY(window);
        QList<QWidget*> pages;
        for (auto* view : window->findChildren<QGraphicsView*>("externalFittedPage"))
            for (auto* item : view->scene()->items())
                if (auto* proxy = qgraphicsitem_cast<QGraphicsProxyWidget*>(item); proxy && proxy->widget()->objectName() == "invoicesPage") pages << proxy->widget();
        QCOMPARE(pages.size(), 2);
        for (auto* page : pages) {
            auto* table = page->findChild<QTableView*>(); QVERIFY(table);
            QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(), 2, 10000);
        }
        auto* settings = displays.settingsWidget(&owner); settings->resize(900, 620); settings->show();
        auto* layout = settings->findChild<QComboBox*>("externalLayoutSelector"); layout->setCurrentIndex(1);
        for (int i = 0; i < 2; ++i) {
            auto* selector = settings->findChild<QComboBox*>("externalPanel" + QString::number(i));
            selector->setCurrentIndex(selector->findData(i == 0 ? 1 : 4));
        }
        settings->findChild<QPushButton*>("refreshExternalPreview")->click(); QTest::qWait(1800);
        int thumbnails = 0;
        for (auto* label : settings->findChildren<QLabel*>()) if (!label->pixmap().isNull()) ++thumbnails;
        QCOMPARE(thumbnails, 2);
        const QString output = qEnvironmentVariable("CS_EXTERNAL_DISPLAY_TEST_OUTPUT");
        if (!output.isEmpty()) QVERIFY(window->grab().save(QDir(output).filePath("external-display-live.png")));
        if (!output.isEmpty()) QVERIFY(settings->grab().save(QDir(output).filePath("external-display-live-preview.png")));
        ActivityAuditService::setCurrentActor({"master", "Master", "master", "test"});
        QPointer<QWidget> guard(window);
        QTRY_VERIFY_WITH_TIMEOUT(!guard || !guard->isVisible(), 3000);
        QTest::qWait(200);
    }
    void invoiceGroupsOrdersAndSettlesAllPositions() {
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE work_order_part_attachments(part_id INTEGER PRIMARY KEY,invoice_number TEXT,invoice_date TEXT,file_path TEXT)"));
        QVERIFY(q.exec("INSERT INTO work_order_part_attachments VALUES(1,'INV-1','01.09.26','invoice.pdf'),(2,'INV-1','01.09.26','copy.pdf')"));
        QVERIFY(q.exec("INSERT INTO work_orders VALUES(2,22,'order-b','BB1234BB','Test','','closed','2026-09-01','2026-09-05')"));
        QVERIFY(prepareFixtureOrderState(2));
        QVERIFY(q.exec("INSERT INTO work_order_parts SELECT 3,2,'order-b',ordered_at,delivery_date,installed_at,store,part_number,manufacturer,name,qty,price,amount,purchase_price,purchase_amount,0 FROM work_order_parts WHERE id=1"));
        QVERIFY(q.exec("INSERT INTO work_order_part_attachments VALUES(3,'INV-1','01.09.26','copy-two.pdf')"));
        QString error;
        auto invoices = InvoiceRegistryService::load(db, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error)); QCOMPARE(invoices.size(), 1);
        QCOMPARE(invoices.first().orders.size(), 2);
        QCOMPARE(invoices.first().totalCents, 50000);
        QCOMPARE(invoices.first().remainingCents(), 40000);
        QVERIFY(!PurchasePaymentService::payInvoiceOrders(db, invoices.first(), {999}, &error));
        QVERIFY2(PurchasePaymentService::payInvoiceOrders(db, invoices.first(), {2}, &error), qPrintable(error));
        const auto partial = InvoiceRegistryService::load(db, &error).first();
        QCOMPARE(partial.remainingCents(), 20000);
        QVERIFY(!partial.paid());
        QVERIFY(q.exec("SELECT paid FROM work_order_parts WHERE id=1")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 0);
        QVERIFY(q.exec("SELECT paid FROM work_order_parts WHERE id=3")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 1);
        QVERIFY(!PurchasePaymentService::payInvoice(db, invoices.first(), &error));
        invoices = InvoiceRegistryService::load(db, &error);
        QVERIFY2(PurchasePaymentService::payInvoice(db, invoices.first(), &error), qPrintable(error));
        QVERIFY(PurchasePaymentService::debts(db, &error).isEmpty());
        QVERIFY(q.exec("SELECT SUM(paid) FROM work_order_parts")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 3);
        QVERIFY(q.exec("SELECT status FROM work_orders WHERE number=2")); QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), QString("closed"));
        QVERIFY(!PurchasePaymentService::payInvoice(db, invoices.first(), &error));
        invoices = InvoiceRegistryService::load(db, &error); QVERIFY(invoices.first().paid());
        QVERIFY(PurchasePaymentService::payInvoice(db, invoices.first(), &error));
        QVERIFY(q.exec("SELECT COUNT(*) FROM purchase_payment_events")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 2);
    }
    void invoicePaymentRejectsChangesAndRollsBack() {
        QSqlQuery q(db);
        QVERIFY(q.exec("UPDATE work_order_parts SET paid=0 WHERE id=2"));
        QVERIFY(q.exec("CREATE TABLE work_order_part_attachments(part_id INTEGER PRIMARY KEY,invoice_number TEXT,invoice_date TEXT,file_path TEXT)"));
        QVERIFY(q.exec("INSERT INTO work_order_part_attachments VALUES(1,'INV-1','01.09.26','invoice.pdf'),(2,'INV-1','01.09.26','invoice.pdf')"));
        QString error;
        auto invoice = InvoiceRegistryService::load(db, &error).first();
        QVERIFY(q.exec("UPDATE work_order_parts SET purchase_amount=210 WHERE id=1"));
        QVERIFY(!PurchasePaymentService::payInvoice(db, invoice, &error));
        invoice = InvoiceRegistryService::load(db, &error).first();
        QVERIFY(q.exec("CREATE TRIGGER fail_invoice_payment BEFORE UPDATE OF paid ON work_order_parts WHEN NEW.id=2 BEGIN SELECT RAISE(ABORT,'test failure'); END"));
        QVERIFY(!PurchasePaymentService::payInvoice(db, invoice, &error));
        QVERIFY(q.exec("SELECT SUM(paid) FROM work_order_parts")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 0);
        QVERIFY(q.exec("SELECT COUNT(*) FROM purchase_payment_events")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 0);
        ActivityAuditService::setCurrentActor({"master","Master","master","test"});
        QVERIFY(!PurchasePaymentService::payInvoice(db, invoice, &error));
    }
    void invoiceNumbersDoNotMergeDifferentSuppliersOrDates() {
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE work_order_part_attachments(part_id INTEGER PRIMARY KEY,invoice_number TEXT,invoice_date TEXT,file_path TEXT)"));
        QVERIFY(q.exec("INSERT INTO work_order_part_attachments VALUES(1,'INV-1','01.09.26','invoice.pdf'),(2,'INV-1','02.09.26','second.pdf')"));
        QString error; QCOMPARE(InvoiceRegistryService::load(db, &error).size(), 2);
        QVERIFY(q.exec("UPDATE work_order_part_attachments SET invoice_date='01.09.26'"));
        QVERIFY(q.exec("UPDATE work_order_parts SET store='Other Store' WHERE id=2"));
        PartsQueryService::invalidateCache(); QCOMPARE(InvoiceRegistryService::load(db, &error).size(), 2);
    }
    void invoiceEvidenceMismatchBlocksPayment() {
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE work_order_part_attachments(part_id INTEGER PRIMARY KEY,invoice_number TEXT,invoice_date TEXT,file_path TEXT)"));
        QVERIFY(q.exec("INSERT INTO work_order_part_attachments VALUES(1,'INV-1','01.09.26','invoice.pdf')"));
        QVERIFY(q.exec("CREATE TABLE invoice_part_evidence(document_hash TEXT,order_uid TEXT,part_id INTEGER,invoice_number TEXT,invoice_date TEXT,supplier TEXT,article TEXT,name TEXT,quantity REAL,purchase_cents INTEGER,file_path TEXT,active INTEGER)"));
        QVERIFY(q.exec("INSERT INTO invoice_part_evidence SELECT 'hash',order_uid,id,'INV-1','01.09.26',store,part_number,name,qty,19900,'invoice.pdf',1 FROM work_order_parts WHERE id=1"));
        QString error;
        const auto invoices = InvoiceRegistryService::load(db, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error)); QCOMPARE(invoices.size(), 1);
        QVERIFY(invoices.first().amountMismatch); QCOMPARE(invoices.first().totalCents, 19900);
        QVERIFY(!PurchasePaymentService::payInvoice(db, invoices.first(), &error));
        QVERIFY(q.exec("SELECT paid FROM work_order_parts WHERE id=1")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 0);
        QVERIFY(q.exec("SELECT COUNT(*) FROM purchase_payment_events")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 0);
    }
    void partialStockInvoiceDistribution() {
        QString error;QSqlQuery q(db);
        QVERIFY(WorkshopSpareStockService::ensureSchema(db,&error));
        QVERIFY(q.exec("INSERT INTO workshop_spare_orders(id,name,brand,model,plate,master_id,master_name,supplier,ordered_on,quantity,unit_cost) VALUES(99,'Filter','Opel','Vectra','AA1234BA','m1','Master','ELIT','2026-09-01',7,10.01)"));
        WorkshopInventoryPurchaseInput stock;stock.purchasedOn=QDate(2026,9,1);stock.materialName="Filter";stock.unit="pcs";
        stock.quantity=3;stock.unitCost=10.01;stock.supplier="ELIT";stock.article="FILTER";stock.document="FIFO-A";stock.paidAmount=0;
        qint64 a=0,b=0;QVERIFY(WorkshopInventoryService::addPurchase(db,stock,&a,&error));
        stock.quantity=4;stock.document="FIFO-B";QVERIFY(WorkshopInventoryService::addPurchase(db,stock,&b,&error));
        auto movement=[&](const QString& operation,double quantity,qint64 purchase,int part,int order){
            q.prepare("INSERT INTO workshop_spare_movements(spare_id,operation,quantity,moved_on,purchase_id,part_id,order_number) VALUES(99,?,?,'2026-09-01',?,?,?)");
            q.addBindValue(operation);q.addBindValue(quantity);q.addBindValue(purchase?QVariant(purchase):QVariant());q.addBindValue(part);q.addBindValue(order);return q.exec();
        };
        QVERIFY(movement("receipt",3,a,0,0));QVERIFY(movement("receipt",4,b,0,0));
        QVERIFY(movement("issue",2,0,1,1));QVERIFY(movement("issue",3,0,999,2)); // Archived/missing live part still consumes stock.
        QVERIFY(q.exec("CREATE TABLE work_order_part_attachments(part_id INTEGER PRIMARY KEY,invoice_number TEXT,invoice_date TEXT,file_path TEXT)"));
        QVERIFY(q.exec("INSERT INTO work_order_part_attachments VALUES(1,'FIFO-A','2026-09-01','')"));
        PartsQueryService::invalidateCache();
        // Archived/missing live parts do not erase the receipt's supplier debt.
        // Clicking the report must still open the invoice.
        const auto before=PurchasePaymentService::balances(db,&error);QVERIFY(error.isEmpty());
        QVERIFY(q.exec("SAVEPOINT archived_part_ui"));
        QVERIFY(q.exec("DELETE FROM work_order_parts WHERE id=1"));PartsQueryService::invalidateCache();
        QVERIFY(PurchasePaymentService::orderPartDebts(db,1,&error).isEmpty());QVERIFY(error.isEmpty());
        const auto uiBefore=PurchasePaymentService::balances(db,&error);QVERIFY(error.isEmpty());
        {
            ReportsPage reports;reports.resize(1400,800);reports.show();
            auto* tabs=reports.findChild<QTabWidget*>();QVERIFY(tabs);tabs->setCurrentIndex(1);QTest::qWait(300);
            auto* orders=tabs->widget(1)->findChild<QTableView*>();QVERIFY(orders);
            auto* model=dynamic_cast<ReportsTableSupport::ReportsSnapshotTableModel*>(orders->model());QVERIFY(model);
            OrdersSnapshot snapshot;OrdersSnapshotRow row;row.internalOrderNumber=1;row.orderNumber="1";
            row.paymentStatus=QString::fromUtf8("Не сплачено повністю");snapshot.rows.append(row);
            ReportsTableSupport::applyOrdersSnapshot(orders,model,snapshot);
            bool invoiceShown=false;
            QTimer::singleShot(30,&reports,[&]{
                if(auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
                    invoiceShown=dialog->objectName()=="purchasePaymentDialog";dialog->reject();
                }
            });
            const auto index=model->index(0,6);orders->setCurrentIndex(index);orders->clicked(index);
            QVERIFY(invoiceShown);
            QCOMPARE(PurchasePaymentService::balances(db,&error),uiBefore);
        }
        QVERIFY(q.exec("ROLLBACK TO archived_part_ui"));QVERIFY(q.exec("RELEASE archived_part_ui"));PartsQueryService::invalidateCache();
        for(const auto& table:db.tables()) if(table.startsWith("purchase_payment_") || table=="workshop_spare_payment_allocations")
            for(const QString& operation:{QString("INSERT"),QString("UPDATE"),QString("DELETE")})
                QVERIFY(q.exec(QString("CREATE TEMP TRIGGER distribution_forbid_%1_%2 BEFORE %2 ON %1 BEGIN SELECT RAISE(ABORT,'Distribution wrote payment'); END").arg(table,operation)));
        QVector<SparePaymentAllocation::Distribution> slices;
        QVERIFY2(SparePaymentAllocation::distribution(db,&slices,&error),qPrintable(error));QCOMPARE(slices.size(),4);
        QHash<qint64,qint64> totals;QHash<qint64,double> quantities;
        for(const auto& slice:slices){totals[slice.purchaseId]+=slice.total;quantities[slice.purchaseId]+=slice.quantity;}
        QCOMPARE(totals[a],qint64(3003));QCOMPARE(totals[b],qint64(4004));QCOMPARE(quantities[a],3.0);QCOMPARE(quantities[b],4.0);
        const auto invoices=InvoiceRegistryService::load(db,&error);QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(invoices.size(),2);
        for(const auto& invoice:invoices) {
            QCOMPARE(invoice.parts.size(),1);QCOMPARE(invoice.parts[0].source,QString("inventory"));QCOMPARE(invoice.distribution.size(),2);
            QCOMPARE(invoice.totalCents,invoice.number=="FIFO-A"?qint64(3003):qint64(4004));
            QCOMPARE(invoice.paidCents,qint64(0));
            if(invoice.number=="FIFO-B"){QVERIFY(!invoice.orders.contains(1));QVERIFY(invoice.orders.contains(2));}
        }
        const auto local=SupplierInvoiceLedger::localItems(db,&error);QVERIFY(error.isEmpty());
        for(const auto& item:local)if(item.purchase.source=="inventory" && item.purchase.id==b)QCOMPARE(item.orders,QVector<int>{2});
        QCOMPARE(PurchasePaymentService::balances(db,&error),before);
        InvoicesPage page;page.resize(1200,700);page.show();page.refresh();auto* table=page.findChild<QTableView*>("invoicesTable");QVERIFY(table);
        QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(),2,10000);
        bool shown=false;
        QTimer::singleShot(50,[&]{auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)return;
            auto* allocation=dialog->findChild<QTableView*>("invoiceDistribution");shown=allocation && allocation->model()->rowCount()==2;
            const auto captures=qEnvironmentVariable("CS_TEST_SCREENSHOTS");if(!captures.isEmpty())dialog->grab().save(QDir(captures).filePath("partial-invoice-distribution.png"));
            dialog->reject();});
        table->clicked(table->model()->index(0,1));QVERIFY(shown);
        GeneralSettingsService::ElitCredentials credentials;
        QVERIFY(GeneralSettingsService::loadElitCredentials(&credentials));
        credentials.login="partial-distribution";credentials.password.clear();credentials.rememberPassword=false;
        QVERIFY(GeneralSettingsService::saveElitCredentials(credentials,&error));
        ElitInvoiceReader::Invoice remote;remote.number="FIFO-A";remote.currency="UAH";remote.posted=stock.purchasedOn;
        remote.grossCents=3003;remote.remainingCents=0;remote.detailsComplete=true;
        remote.lines={{"internal-a","FILTER","Filter",3,3003}};
        ElitInvoiceReader::Result remoteResult;remoteResult.complete=true;remoteResult.fetchedAt=QDateTime::currentDateTime();remoteResult.invoices={remote};
        remote.number="FIFO-B";remote.grossCents=4004;remote.lines={{"internal-b","FILTER","Filter",4,4004}};remoteResult.invoices.append(remote);
        int linked=0,pending=0;
        QVERIFY2(SupplierInvoiceLedger::publishAndLink(db,ElitInvoiceStore::accountKey(credentials),remoteResult,{},[]{return true;},&linked,&pending,&error),qPrintable(error));
        QCOMPARE(linked,2);
        // With no local document number, the active-account associations must
        // retain the same receipt split and must not import remote paid status.
        QVERIFY(q.exec("UPDATE workshop_inventory_purchases SET metadata_json='{\"article\":\"FILTER\"}'"));
        const auto remoteInvoices=InvoiceRegistryService::load(db,&error);QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(remoteInvoices.size(),2);
        for(const auto& invoice:remoteInvoices){QCOMPARE(invoice.parts.size(),1);QCOMPARE(invoice.parts.first().source,QString("inventory"));QCOMPARE(invoice.paidCents,qint64(0));QCOMPARE(invoice.distribution.size(),2);}
        QCOMPARE(PurchasePaymentService::balances(db,&error),before);
        // Fractions are rounded cumulatively; issued value plus stock always
        // equals the receipt value, even when a slice is below one kopeck.
        QVERIFY(q.exec(QString("UPDATE workshop_inventory_purchases SET unit_cost=0.01 WHERE id=%1").arg(b)));
        QVERIFY(movement("issue",0.5,0,998,3));
        QVERIFY(SparePaymentAllocation::distribution(db,&slices,&error));
        qint64 rounded=0;double quantity=0;
        for(const auto& slice:slices)if(slice.purchaseId==b){rounded+=slice.total;quantity+=slice.quantity;}
        QCOMPARE(rounded,qint64(4));QCOMPARE(quantity,4.0);
        QCOMPARE(PurchasePaymentService::balances(db,&error),before);
        // An over-issue must not produce a partially valid report.
        QVERIFY(movement("issue",3,0,998,3));
        QVERIFY(!SparePaymentAllocation::distribution(db,&slices,&error));QVERIFY(slices.isEmpty());
        QVERIFY(InvoiceRegistryService::load(db,&error).isEmpty());QVERIFY(!error.isEmpty());
    }
    void stockOnlyInvoiceRegistry() {
        QString error;
        WorkshopInventoryPurchaseInput stock;
        stock.purchasedOn=QDate(2026,9,1);stock.materialName="Stock filter";stock.unit="pcs";
        stock.quantity=2;stock.unitCost=100;stock.supplier="ELIT";stock.article="STOCK-ONLY";stock.paidAmount=0;
        qint64 id=0;QVERIFY2(WorkshopInventoryService::addPurchase(db,stock,&id,&error),qPrintable(error));
        GeneralSettingsService::ElitCredentials credentials;
        QVERIFY(GeneralSettingsService::loadElitCredentials(&credentials));
        credentials.login="stock-registry";credentials.password.clear();credentials.rememberPassword=false;
        QVERIFY(GeneralSettingsService::saveElitCredentials(credentials,&error));
        ElitInvoiceReader::Invoice invoice;invoice.number="STOCK-INV";invoice.currency="UAH";invoice.posted=stock.purchasedOn;
        invoice.grossCents=20000;invoice.remainingCents=0;invoice.detailsComplete=true;
        invoice.lines={{"internal","STOCK-ONLY","Stock filter",2,20000}};
        ElitInvoiceReader::Result result;result.complete=true;result.fetchedAt=QDateTime::currentDateTime();result.invoices={invoice};
        int linked=0,pending=0;
        QVERIFY2(SupplierInvoiceLedger::publishAndLink(db,ElitInvoiceStore::accountKey(credentials),result,{},[]{return true;},&linked,&pending,&error),qPrintable(error));
        QCOMPARE(linked,1);
        const auto balances=PurchasePaymentService::balances(db,&error);QVERIFY(error.isEmpty());
        QSqlQuery q(db);
        for(const auto& table:db.tables()) if(table.startsWith("purchase_payment_"))
            for(const QString& operation:{QString("INSERT"),QString("UPDATE"),QString("DELETE")})
                QVERIFY(q.exec(QString("CREATE TEMP TRIGGER registry_forbid_%1_%2 BEFORE %2 ON %1 BEGIN SELECT RAISE(ABORT,'Registry wrote payment'); END").arg(table,operation)));
        auto invoices=InvoiceRegistryService::load(db,&error);QVERIFY2(error.isEmpty(),qPrintable(error));
        QCOMPARE(invoices.size(),1);QCOMPARE(invoices[0].number,invoice.number);
        QCOMPARE(invoices[0].parts.size(),1);QCOMPARE(invoices[0].parts[0].source,QString("inventory"));
        QCOMPARE(invoices[0].totalCents,qint64(20000));QCOMPARE(invoices[0].paidCents,qint64(0));
        QVERIFY(!invoices[0].paid()); // Remote observation does not rewrite local payment status.
        InvoicesPage page;page.resize(1200,600);page.show();page.refresh();
        auto* table=page.findChild<QTableView*>("invoicesTable");QVERIFY(table);
        QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(),1,10000);
        QVERIFY(table->model()->index(0,3).data().toString().contains(QString::fromUtf8("Склад СТО")));
        qint64 destination=0;bool checked=false;
        page.inventoryRequested=[&](qint64 value){destination=value;};
        QTimer::singleShot(50,[&]{
            auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)return;
            auto guard=qScopeGuard([&]{dialog->reject();});
            auto* pay=dialog->findChild<QPushButton*>("payInvoice");
            auto* open=dialog->findChild<QPushButton*>("invoiceOpenStock");
            checked=pay && !pay->isEnabled() && open;
            if(open)open->click();
        });
        // The number column opens the remote PDF; the location column opens stock details.
        table->clicked(table->model()->index(0,3));
        QVERIFY(checked);QTRY_COMPARE(destination,id);
        QCOMPARE(PurchasePaymentService::balances(db,&error),balances);
        credentials.login="another-stock-account";QVERIFY(GeneralSettingsService::saveElitCredentials(credentials,&error));
        QVERIFY(InvoiceRegistryService::load(db,&error).isEmpty());
        // Local document metadata remains visible without an ELIT account.
        QVERIFY(q.exec("UPDATE workshop_inventory_purchases SET metadata_json='{\"document\":\"LOCAL-STOCK\"}'"));
        invoices=InvoiceRegistryService::load(db,&error);QVERIFY(error.isEmpty());
        QCOMPARE(invoices.size(),1);QCOMPARE(invoices[0].number,QString("LOCAL-STOCK"));
        ActivityAuditService::setCurrentActor({"master-stock","Master","master","test"});
        QVERIFY(InvoiceRegistryService::load(db,&error).isEmpty());
    }
    void invoiceRegistryPdfClick() {
        const auto source=qEnvironmentVariable("CS_ELIT_PDF_AUDIT_SOURCE");
        QVERIFY(qEnvironmentVariable("CS_TAB_AUDIT_COPY").isEmpty());
        const auto path=dir.filePath("invoice.pdf");
        QString invoiceNumber="TEST-INVOICE-001";
        if(source.isEmpty()) {
            QPdfWriter writer(path);QPainter painter(&writer);
            QVERIFY(painter.isActive());
            painter.drawText(100,300,invoiceNumber);painter.end();
        } else {
            const auto files=QDir(source).entryInfoList({"*.pdf"},QDir::Files);QVERIFY(!files.isEmpty());
            QVERIFY(QFile::copy(files.first().absoluteFilePath(),path));
            invoiceNumber=files.first().completeBaseName();
        }
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE work_order_part_attachments(part_id INTEGER PRIMARY KEY,invoice_number TEXT,invoice_date TEXT,file_path TEXT)"));
        q.prepare("INSERT INTO work_order_part_attachments VALUES(1,?,'23.10.26',?)");
        q.addBindValue(invoiceNumber);q.addBindValue(path);QVERIFY(q.exec());
        QString error;const auto before=PurchasePaymentService::balances(db,&error);QVERIFY(error.isEmpty());
        InvoicesPage page;page.resize(1200,600);page.show();page.refresh();
        auto* table=page.findChild<QTableView*>("invoicesTable");QVERIFY(table);
        QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(),1,10000);
        openedPdf={};QDesktopServices::setUrlHandler("file",this,"capturePdfUrl");
        const auto reset=qScopeGuard([]{QDesktopServices::unsetUrlHandler("file");});
        QTimer dismiss;QString failure;bool previewSeen=false;
        connect(&dismiss,&QTimer::timeout,&page,[&] {
            if(auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){failure=box->text();box->reject();}
            else if(auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
                if(dialog->objectName()!="invoicePreviewDialog")return;
                auto* preview=dialog->findChild<QScrollArea*>("orderReportPdfPreview");
                if(!preview || preview->findChildren<QLabel*>("orderReportPdfPage").isEmpty())return;
                if(preview->findChildren<QLabel*>("orderReportPdfPage").first()->pixmap().isNull())return;
                previewSeen=dialog->findChild<QPushButton*>("payInvoice")->isVisible()
                    && !dialog->findChild<QPushButton*>("payInvoiceOrders")->isVisible();
                dialog->grab().save(dir.filePath(QStringLiteral("invoice-embedded-preview.png")));
                dialog->reject();
            }
        });dismiss.start(50);
        for(int column:{0,2,4,6})table->clicked(table->model()->index(0,column));
        QTest::qWait(100);QVERIFY(openedPdf.isEmpty());QVERIFY(!QApplication::activeModalWidget());
        table->clicked(table->model()->index(0,1));
        QTRY_VERIFY_WITH_TIMEOUT(previewSeen || !failure.isEmpty(),35000);
        QVERIFY2(failure.isEmpty(),qPrintable(failure));
        QVERIFY(previewSeen);QVERIFY(openedPdf.isEmpty());
        QCOMPARE(PurchasePaymentService::balances(db,&error),before);
    }
    void partsInvoiceDisplayAndManualWidths() {
        ThemeManager::applySavedTheme();
        for(int pass=0;pass<2;++pass) {
            ReportsPage page;page.resize(1500,800);page.show();
            auto* tabs=page.findChild<QTabWidget*>();QVERIFY(tabs);tabs->setCurrentIndex(3);
            auto* table=tabs->widget(3)->findChild<QTableView*>();QVERIFY(table);
            QTest::qWait(400);
            auto* header=table->horizontalHeader();
            QCOMPARE(header->sectionResizeMode(3),QHeaderView::Interactive);
            if(pass==1)QCOMPARE(header->sectionSize(3),243);
            header->resizeSection(3,243);
            auto* model=dynamic_cast<ReportsTableSupport::ReportsSnapshotTableModel*>(table->model());QVERIFY(model);
            PartsSnapshot snapshot;PartsSnapshotRow row;
            row.invoiceNumber=QString::fromUtf8("Видаткова накладна №А-04884142 від 06.10.26");
            row.invoicePath="original.pdf";snapshot.rows.append(row);
            ReportsTableSupport::applyPartsSnapshot(table,model,snapshot);
            QCOMPARE(model->index(0,3).data().toString(),QString::fromUtf8("А-04884142"));
            QCOMPARE(model->index(0,3).data(Qt::UserRole).toMap().value("path").toString(),QString("original.pdf"));
            page.resize(1300,800);QTest::qWait(100);QCOMPARE(header->sectionSize(3),243);
        }
    }
    void statisticsAsyncCancellationAndReopen() {
        StatsPage page(nullptr,StatsPage::View::WorkLog);
        page.show();page.markDirty();page.refresh();
        QVERIFY(page.property("statisticsLoading").toBool());
        const auto before=page.property("events");
        page.close();
        QTRY_VERIFY_WITH_TIMEOUT(!page.property("statisticsLoading").toBool(),10000);
        QCOMPARE(page.property("events"),before);
        page.show();page.markDirty();page.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(!page.property("statisticsLoading").toBool(),10000);
        QVERIFY2(page.property("statisticsError").toString().isEmpty(),qPrintable(page.property("statisticsError").toString()));
        auto* transient=new StatsPage(nullptr,StatsPage::View::WorkLog);
        transient->show();transient->refresh();delete transient;
        QTest::qWait(100);
    }
    void statisticsCompactDates() {
        ThemeManager::applySavedTheme();
        StatsPage page;page.resize(1500,800);page.show();
        QTRY_VERIFY_WITH_TIMEOUT(!page.property("statisticsLoading").toBool(),10000);
        QTest::qWait(150);
        const auto dates=page.findChildren<QDateEdit*>();QVERIFY(dates.size()>=2);
        for(auto* date:dates) {
            QCOMPARE(date->height(),qRound(7.0*page.logicalDpiY()/25.4));
            QVERIFY(qAbs(date->width()-dates.first()->width())<=1);
        }
        QVERIFY(dates.first()->mapTo(&page,QPoint()).x()<80);
        QVERIFY(dates[1]->mapTo(&page,QPoint()).x()<400);
        QWidget* previous=nullptr;
        for(const auto* name:{"statsPeriodTile","statsQuickTile","statsActionsTile"}) {
            auto* tile=page.findChild<QWidget*>(name);QVERIFY(tile);
            if(previous) {
                QCOMPARE(tile->y(),previous->y());
                QCOMPARE(tile->height(),previous->height());
                QVERIFY(tile->x()>previous->geometry().right());
            }
            for(auto* button:tile->findChildren<QPushButton*>()) {
                QCOMPARE(button->height(),qRound(7.0*page.logicalDpiY()/25.4));
                QVERIFY(tile->rect().contains(QRect(button->mapTo(tile,QPoint()),button->size())));
            }
            previous=tile;
        }
        page.grab().save(dir.filePath(QStringLiteral("statistics-compact-dates.png")));
        auto* tabs=page.findChild<QTabWidget*>();QVERIFY(tabs);
        auto* masterTile=page.findChild<QWidget*>("statsMasterTile");QVERIFY(masterTile);
        QVERIFY(!masterTile->isVisible());
        tabs->setCurrentIndex(1);QTest::qWait(150);
        QVERIFY(masterTile->isVisible());
        previous=nullptr;
        for(const auto* name:{"statsPeriodTile","statsQuickTile","statsMasterTile","statsActionsTile"}) {
            auto* tile=page.findChild<QWidget*>(name);QVERIFY(tile);
            if(previous) {
                QCOMPARE(tile->y(),previous->y());
                QCOMPARE(tile->height(),previous->height());
                QVERIFY(tile->x()>previous->geometry().right());
            }
            previous=tile;
        }
        auto* master=masterTile->findChild<QComboBox*>();QVERIFY(master);
        QCOMPARE(master->height(),qRound(7.0*page.logicalDpiY()/25.4));
        QVERIFY(masterTile->rect().contains(QRect(master->mapTo(masterTile,QPoint()),master->size())));
        page.grab().save(dir.filePath(QStringLiteral("statistics-masters-tiles.png")));
        tabs->setCurrentIndex(2);QTest::qWait(150);
        auto* searchTile=page.findChild<QWidget*>("statsSearchTile");QVERIFY(searchTile);
        QVERIFY(searchTile->isVisible());
        QVERIFY(!masterTile->isVisible());
        previous=nullptr;
        for(const auto* name:{"statsPeriodTile","statsSearchTile","statsQuickTile","statsActionsTile"}) {
            auto* tile=page.findChild<QWidget*>(name);QVERIFY(tile);
            if(previous) {
                QCOMPARE(tile->y(),previous->y());
                QCOMPARE(tile->height(),previous->height());
                QVERIFY(tile->x()>previous->geometry().right());
            }
            previous=tile;
        }
        auto* search=searchTile->findChild<QLineEdit*>();QVERIFY(search);
        QCOMPARE(search->height(),qRound(7.0*page.logicalDpiY()/25.4));
        QVERIFY(searchTile->rect().contains(QRect(search->mapTo(searchTile,QPoint()),search->size())));
        page.grab().save(dir.filePath(QStringLiteral("statistics-parts-tiles.png")));
        tabs->setCurrentIndex(0);QTest::qWait(50);
        QVERIFY(!searchTile->isVisible());
        QVERIFY(!masterTile->isVisible());
    }
    void popupCaptionsExcludeApplicationName() {
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName()!=QStringLiteral("windows"))
            QSKIP("Native Windows caption test");
        ThemeManager::applySavedTheme();
        const auto previousDisplayName=QApplication::applicationDisplayName();
        QApplication::setApplicationDisplayName(QStringLiteral("CarServicePointG_Sergiy&Soul"));
        QDialog dialog;
        dialog.setWindowTitle(QStringLiteral("Invoice 123"));
        dialog.show();
        QTest::qWait(100);
        wchar_t caption[512]{};
        GetWindowTextW(reinterpret_cast<HWND>(dialog.winId()),caption,512);
        QCOMPARE(QString::fromWCharArray(caption),dialog.windowTitle());
        dialog.setWindowTitle(QStringLiteral("Invoice 456"));
        QTest::qWait(100);
        GetWindowTextW(reinterpret_cast<HWND>(dialog.winId()),caption,512);
        QCOMPARE(QString::fromWCharArray(caption),dialog.windowTitle());
        QDialog nested(&dialog);
        nested.setWindowTitle(QString::fromUtf8("Фактичне розміщення наряду"));
        nested.setModal(true);nested.show();
        QTest::qWait(100);
        GetWindowTextW(reinterpret_cast<HWND>(nested.winId()),caption,512);
        QCOMPARE(QString::fromWCharArray(caption),nested.windowTitle());
        nested.hide();nested.show();nested.activateWindow();
        QTest::qWait(100);
        GetWindowTextW(reinterpret_cast<HWND>(nested.winId()),caption,512);
        QCOMPARE(QString::fromWCharArray(caption),nested.windowTitle());
        QWidget secondary(&dialog,Qt::Window);
        secondary.setWindowTitle(QStringLiteral("Preview"));secondary.show();
        QTest::qWait(100);
        GetWindowTextW(reinterpret_cast<HWND>(secondary.winId()),caption,512);
        QCOMPARE(QString::fromWCharArray(caption),secondary.windowTitle());
        QApplication::setApplicationDisplayName(previousDisplayName);
#endif
    }

    void compactInputDialogsHaveUniformWidth() {
        ThemeManager::applySavedTheme();
        for(int mode=0;mode<3;++mode) {
            QInputDialog dialog;
            dialog.setLabelText(QString::fromUtf8("Оплата"));
            if(mode==0)dialog.setComboBoxItems({QString::fromUtf8("Сплатити за наряд")});
            if(mode==1)dialog.setComboBoxItems({QString(160,'X'),"Second"});
            if(mode==2)dialog.setInputMode(QInputDialog::IntInput);
            dialog.setMinimumWidth(800);
            dialog.show();QTest::qWait(80);
            QCOMPARE(dialog.width(),qRound(90.0*dialog.logicalDpiX()/25.4));
            for(auto* field:dialog.findChildren<QWidget*>())
                if(field->isVisible() && (qobject_cast<QComboBox*>(field) || qobject_cast<QDialogButtonBox*>(field)))
                    QVERIFY(dialog.rect().contains(QRect(field->mapTo(&dialog,QPoint()),field->size())));
            if(mode==0)dialog.grab().save(dir.filePath(QStringLiteral("compact-payment-dialog.png")));
            dialog.close();
        }
    }
    void reportOrderSummaryRejectsPartialReads() {
        QString error="stale";
        QVERIFY(InvoiceReportService::canViewOrder(1));
        const auto initial=InvoiceReportService::orderSummary(1,&error);
        QVERIFY2(error.isEmpty(),qPrintable(error));
        QCOMPARE(initial.remainingCents,qint64(20000));
        QVERIFY(!initial.paymentDateKnown);
        QVERIFY2(PurchasePaymentService::payOrderParts(1,initial.debts,&error,5700),qPrintable(error));
        QCOMPARE(InvoiceReportService::orderSummary(1,&error).remainingCents,qint64(14300));
        QVERIFY2(error.isEmpty(),qPrintable(error));
        QSqlQuery q(db);
        QVERIFY(q.exec("ALTER TABLE purchase_payment_balances RENAME COLUMN paid_cents TO broken_paid"));q.finish();
        const auto failed=InvoiceReportService::orderSummary(1,&error);
        QVERIFY(!error.isEmpty());QVERIFY(failed.debts.isEmpty());QVERIFY(failed.invoices.isEmpty());
        QVERIFY(!failed.paymentDateKnown);
        QVERIFY(InvoiceReportService::orderSummary(1,nullptr).debts.isEmpty());
        QVERIFY(q.exec("ALTER TABLE purchase_payment_balances RENAME COLUMN broken_paid TO paid_cents"));q.finish();
        QCOMPARE(InvoiceReportService::orderSummary(1,&error).remainingCents,qint64(14300));
        QVERIFY2(error.isEmpty(),qPrintable(error));
        QVERIFY(q.exec("CREATE TABLE work_order_part_attachments(part_id INTEGER PRIMARY KEY,invoice_number TEXT,invoice_date TEXT,file_path TEXT)"));
        QVERIFY(q.exec("INSERT INTO work_order_part_attachments VALUES(1,'SUMMARY-1','01.09.26','invoice.pdf'),(2,'SUMMARY-1','01.09.26','invoice.pdf')"));q.finish();
        const auto documented=InvoiceReportService::orderSummary(1,&error);
        QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(documented.invoices.size(),1);
        QCOMPARE(documented.dueDate,QDate(2026,9,8));
        QVERIFY2(PurchasePaymentService::payOrderParts(1,documented.debts,&error,14300),qPrintable(error));
        QCOMPARE(InvoiceReportService::orderSummary(1,&error).remainingCents,qint64(0));
        QVERIFY2(error.isEmpty(),qPrintable(error));
        // An invoice read failure after the debt read succeeds must not look paid.
        QVERIFY(q.exec("ALTER TABLE work_order_part_attachments RENAME COLUMN file_path TO broken_path"));q.finish();
        const auto invoiceFailure=InvoiceReportService::orderSummary(1,&error);
        QVERIFY(!error.isEmpty());QVERIFY(invoiceFailure.invoices.isEmpty());QVERIFY(invoiceFailure.debts.isEmpty());
        QVERIFY(!invoiceFailure.paymentDateKnown);
        QVERIFY(q.exec("ALTER TABLE work_order_part_attachments RENAME COLUMN broken_path TO file_path"));q.finish();
        QCOMPARE(InvoiceReportService::orderSummary(1,&error).invoices.size(),1);
        QVERIFY2(error.isEmpty(),qPrintable(error));
        QVERIFY(!InvoiceReportService::canViewOrder(0));
        QVERIFY(InvoiceReportService::orderSummary(0,&error).debts.isEmpty());QVERIFY(!error.isEmpty());
    }
    void invoiceReportServiceGuardsContextAndMissingDatabase() {
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE work_order_part_attachments(part_id INTEGER PRIMARY KEY,invoice_number TEXT,invoice_date TEXT,file_path TEXT)"));
        QVERIFY(q.exec("INSERT INTO work_order_part_attachments VALUES(1,'BOUNDARY-1','01.09.26','invoice.pdf')"));q.finish();
        const auto context=InvoiceReportService::context();
        QVERIFY(context.allowed);QVERIFY(InvoiceReportService::isCurrent(context));
        const auto before=QSqlDatabase::connectionNames();
        QString error;
        const auto invoices=InvoiceReportService::load(context,&error);
        QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(invoices.size(),1);
        QCOMPARE(invoices.first().number,QString("BOUNDARY-1"));
        auto changed=context;changed.scope+="changed";QVERIFY(!InvoiceReportService::isCurrent(changed));
        changed=context;changed.account+="changed";QVERIFY(!InvoiceReportService::isCurrent(changed));
        changed=context;changed.databasePath=dir.filePath("not-created-report.sqlite");
        QVERIFY(!InvoiceReportService::isCurrent(changed));
        QVERIFY(InvoiceReportService::load(changed,&error).isEmpty());QVERIFY(!error.isEmpty());
        QVERIFY(!QFileInfo::exists(changed.databasePath));
        changed=context;changed.allowed=false;
        QVERIFY(InvoiceReportService::load(changed,&error).isEmpty());QVERIFY(!error.isEmpty());
        QVERIFY(!InvoiceReportService::isCurrent(changed));
        const auto after=QSqlDatabase::connectionNames();
        QCOMPARE(QSet<QString>(after.begin(),after.end()),QSet<QString>(before.begin(),before.end()));
    }
    void elitPdfOpenOwnsConnectionAndRejectsLateResults() {
        GeneralSettingsService::ElitCredentials credentials;
        QVERIFY(GeneralSettingsService::loadElitCredentials(&credentials));
        credentials.login="pdf-boundary-test";credentials.password.clear();credentials.rememberPassword=false;
        QString error;QVERIFY(GeneralSettingsService::saveElitCredentials(credentials,&error));
        const auto account=ElitInvoiceStore::accountKey(credentials);
        ElitInvoiceReader::Invoice invoice;invoice.number="PDF-BOUNDARY";invoice.currency="UAH";
        invoice.posted=QDate(2026,10,23);invoice.grossCents=10000;invoice.remainingCents=10000;invoice.detailsComplete=true;
        invoice.lines={{"boundary","OEM-TEST","Test item",1,10000}};
        ElitInvoiceReader::Result published;published.complete=true;published.fetchedAt=QDateTime::currentDateTime();published.invoices={invoice};
        QVERIFY2(ElitInvoiceStore::publish(db,account,published,QString(),&error),qPrintable(error));
        const auto snapshot=ElitInvoiceStore::load(db,account,&error);
        QCOMPARE(snapshot.documents.size(),1);
        ElitPdfOpenService::Request request;
        QVERIFY2(ElitPdfOpenService::prepare(snapshot.documents.first().identity,&request,&error),qPrintable(error));
        QVERIFY(ElitPdfOpenService::isCurrent(request));
        const auto beforeNames=QSqlDatabase::connectionNames();
        const auto before=QSet<QString>(beforeNames.begin(),beforeNames.end());
        const auto missing=ElitPdfOpenService::execute(request,[]{return true;});
        QVERIFY(missing.path.isEmpty());QVERIFY(!missing.error.isEmpty());
        const QByteArray bytes="%PDF-test-cached-file";
        const auto path=dir.filePath("cached-boundary.pdf");QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(bytes),qint64(bytes.size()));file.close();
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE IF NOT EXISTS elit_invoice_pdf_files(account TEXT,invoice_id TEXT,file_path TEXT,sha256 TEXT,PRIMARY KEY(account,invoice_id))"));
        q.prepare("INSERT INTO elit_invoice_pdf_files VALUES(?,?,?,?)");q.addBindValue(account);q.addBindValue(request.identity);q.addBindValue(path);q.addBindValue(ElitInvoicePdf::digest(bytes));QVERIFY(q.exec());q.finish();
        const auto cached=ElitPdfOpenService::execute(request,[]{return true;});
        QCOMPARE(cached.path,path);QVERIFY(cached.error.isEmpty());
        int checks=0;
        const auto late=ElitPdfOpenService::execute(request,[&]{return ++checks<3;});
        QVERIFY(checks>=3);QVERIFY(late.path.isEmpty());
        QVERIFY(ElitPdfOpenService::execute(request,[]{return false;}).path.isEmpty());
        auto stale=request;stale.account="another-account";
        QVERIFY(!ElitPdfOpenService::isCurrent(stale));QVERIFY(ElitPdfOpenService::execute(stale,[]{return true;}).path.isEmpty());
        auto absent=request;absent.databasePath=dir.filePath("not-created.sqlite");
        QVERIFY(!ElitPdfOpenService::execute(absent,[]{return true;}).error.isEmpty());
        QVERIFY(!QFileInfo::exists(absent.databasePath));
        const auto afterNames=QSqlDatabase::connectionNames();
        QCOMPARE(QSet<QString>(afterNames.begin(),afterNames.end()),before);
    }
    void remoteInvoiceStatusClickDoesNotOpenPdf() {
        GeneralSettingsService::ElitCredentials credentials;
        QVERIFY(GeneralSettingsService::loadElitCredentials(&credentials));
        credentials.login="invoice-status-test";credentials.password.clear();credentials.rememberPassword=false;
        QString error;QVERIFY(GeneralSettingsService::saveElitCredentials(credentials,&error));
        ElitInvoiceReader::Invoice invoice;invoice.number="REMOTE-PAID";invoice.currency="UAH";
        invoice.posted=QDate(2026,10,23);invoice.grossCents=10000;invoice.remainingCents=0;invoice.detailsComplete=true;
        invoice.lines={{"status-test","REMOTE-ONLY","Test item",1,10000}};
        ElitInvoiceReader::Result result;result.complete=true;result.fetchedAt=QDateTime::currentDateTime();result.invoices={invoice};
        int linked=0,pending=0;
        QVERIFY2(SupplierInvoiceLedger::publishAndLink(db,ElitInvoiceStore::accountKey(credentials),result,{},[]{return true;},&linked,&pending,&error),qPrintable(error));
        const auto before=PurchasePaymentService::balances(db,&error);QVERIFY(error.isEmpty());
        InvoicesPage page;page.resize(1200,650);page.show();page.refresh();
        auto* table=page.findChild<QTableView*>("invoicesTable");QVERIFY(table);
        QTRY_VERIFY_WITH_TIMEOUT(table->isEnabled(),10000);
        page.searchField()->setText("REMOTE-PAID");QCOMPARE(table->model()->rowCount(),1);
        for(int column:{0,2,4,6})table->clicked(table->model()->index(0,column));
        QVERIFY(!page.findChild<QWidget*>("elitPdfOpenProgress"));
        QVERIFY(!QApplication::activeModalWidget());
        bool previewOpened=false;
        QTimer::singleShot(20,[&]{
            if(auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
                previewOpened=dialog->objectName()=="invoicePreviewDialog";
                dialog->reject();
            }
        });
        table->clicked(table->model()->index(0,3));
        QVERIFY(previewOpened);
        QVERIFY(!page.findChild<QWidget*>("elitPdfOpenProgress"));
        QCOMPARE(PurchasePaymentService::balances(db,&error),before);
        bool paid=false;
        QTimer::singleShot(20,[&]{if(auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            paid=box->text().contains(QString::fromUtf8("сплачена"));box->accept();
        }});
        table->clicked(table->model()->index(0,5));QVERIFY(paid);
        QVERIFY(!page.findChild<QWidget*>("elitPdfOpenProgress"));
        QCOMPARE(PurchasePaymentService::balances(db,&error),before);
    }
    void mandatoryOutboxFailureRollsBackAuditAndBusinessChange() {
        QString error;QVERIFY2(CloudSyncSchemaService::ensureSchema(db,&error),qPrintable(error));
        QVERIFY2(ActivityAuditService::ensureSchema(db,&error),qPrintable(error));
        QVERIFY(Database::setSetting("settings.cloudsync.capture_outbox_while_disabled",true));
        const auto disable=qScopeGuard([]{Database::setSetting("settings.cloudsync.capture_outbox_while_disabled",false);});
        QSqlQuery q(db);
        MutationExecutor::Request request;request.sourceModule="OutboxAtomicityTest";
        request.entityType="work_order_part";request.entityId="1";request.operation="rename";
        request.changedTables={"work_order_parts"};
        auto rename=[&](const QString& name){return MutationExecutor::execute(db,request,[&](QSqlDatabase& tx,QString* error){
            QSqlQuery update(tx);update.prepare("UPDATE work_order_parts SET name=? WHERE id=1");update.addBindValue(name);
            const bool ok=update.exec();if(!ok && error)*error=update.lastError().text();return ok;
        });};
        const auto success=rename("FIRST");QVERIFY2(success.ok,qPrintable(success.errorText));
        q.prepare("SELECT COUNT(*) FROM sync_outbox WHERE operation_id=?");q.addBindValue(success.operationId);QVERIFY(q.exec());QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),1);q.finish();
        QVERIFY(q.exec("SELECT COUNT(*) FROM activity_audit_events"));QVERIFY(q.next());const int events=q.value(0).toInt();q.finish();
        QVERIFY(q.exec("CREATE TRIGGER outbox_write_failure BEFORE INSERT ON sync_outbox BEGIN SELECT RAISE(ABORT,'injected outbox failure'); END"));
        expectRejectedTransaction("injected outbox failure Unable to fetch row");
        const auto failed=rename("SECOND");QVERIFY(!failed.ok);QVERIFY(!failed.errorText.isEmpty());
        QVERIFY(q.exec("SELECT name FROM work_order_parts WHERE id=1"));QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("FIRST"));q.finish();
        QVERIFY(q.exec("SELECT COUNT(*) FROM activity_audit_events"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),events);q.finish();
        q.prepare("SELECT COUNT(*) FROM sync_outbox WHERE operation_id=?");q.addBindValue(failed.operationId);QVERIFY(q.exec());QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),0);
    }
    void mandatoryAuditFailureRollsBackBusinessChange() {
        QString error;QVERIFY2(ActivityAuditService::ensureSchema(db,&error),qPrintable(error));
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TRIGGER audit_write_failure BEFORE INSERT ON activity_audit_events BEGIN SELECT RAISE(ABORT,'injected audit failure'); END"));
        expectRejectedTransaction(QString::fromUtf8("Не вдалося зберегти обов'язковий аудит. Зміни скасовано: injected audit failure Unable to fetch row"));
        MutationExecutor::Request request;request.sourceModule="AuditAtomicityTest";
        request.entityType="work_order_part";request.entityId="1";request.operation="rename";
        request.changedTables={"work_order_parts"};
        const auto result=MutationExecutor::execute(db,request,[](QSqlDatabase& tx,QString* error){
            QSqlQuery update(tx);const bool ok=update.exec("UPDATE work_order_parts SET name='CHANGED' WHERE id=1");
            if(!ok && error)*error=update.lastError().text();return ok;
        });
        QVERIFY(!result.ok);QVERIFY(!result.errorText.isEmpty());
        QVERIFY(q.exec("SELECT name FROM work_order_parts WHERE id=1"));QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("Part"));
    }
    void financialTotalsDistinguishErrorsAndRoundRows() {
        using namespace OrderFinancialService;
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE IF NOT EXISTS work_order_services(id INTEGER PRIMARY KEY,order_number INTEGER,order_uid TEXT,qty REAL,price REAL,amount REAL)"));
        QVERIFY(q.exec("DELETE FROM work_order_parts"));
        auto result=readTotals(db,1);QVERIFY2(result.ok(),qPrintable(result.error));
        QCOMPARE(result.worksCents,qint64(0));QCOMPARE(result.partsCents,qint64(0));
        QCOMPARE(readTotals(db,999).status,TotalsStatus::NotFound);
        QVERIFY(q.exec("INSERT INTO work_order_parts(id,order_number,order_uid,qty,price,amount) VALUES(1,1,'order-a',0.5,0.01,NULL),(2,1,'order-a',0.5,0.01,NULL)"));
        result=readTotals(db,1);QVERIFY2(result.ok(),qPrintable(result.error));QCOMPARE(result.partsCents,qint64(2));
        for(const auto& column:QStringList{"works_sum REAL", "parts_sum REAL"})
            if(db.record("work_orders").indexOf(column.section(' ',0,0))<0)
                QVERIFY(q.exec("ALTER TABLE work_orders ADD COLUMN "+column));
        QString error;QVERIFY2(refreshOrderFinancialSums(db,1,&error),qPrintable(error));
        QVERIFY(q.exec("SELECT parts_sum FROM work_orders WHERE number=1"));QVERIFY(q.next());QCOMPARE(q.value(0).toDouble(),0.02);q.finish();
        QVERIFY(q.exec("INSERT INTO work_order_parts(id,order_number,order_uid,qty,price,amount) VALUES(3,1,'other-order',1,999,999),(4,1,'',1,100,42)"));
        result=readTotals(db,1);QVERIFY(result.ok());QCOMPARE(result.partsCents,qint64(4202));
        QVERIFY(q.exec("UPDATE work_order_parts SET amount=1.005 WHERE id=4"));
        QCOMPARE(readTotals(db,1).status,TotalsStatus::InvalidData);
        QVERIFY(q.exec("SELECT amount FROM work_order_parts WHERE id=4"));QVERIFY(q.next());QCOMPARE(q.value(0).toDouble(),1.005);q.finish();
        QVERIFY(q.exec("DROP TABLE work_order_services"));
        result=readTotals(db,1);QCOMPARE(result.status,TotalsStatus::DatabaseError);QVERIFY(!result.error.isEmpty());
    }
    void recalculationPreservesFractionalQuantity_data() {
        QTest::addColumn<double>("quantity");
        QTest::addColumn<double>("expected");
        QTest::newRow("millilitre") << 0.001 << 0.10;
        QTest::newRow("half") << 0.5 << 50.0;
        QTest::newRow("one") << 1.0 << 100.0;
        QTest::newRow("two") << 2.0 << 200.0;
        QTest::newRow("zero-rejected") << 0.0 << -1.0;
        QTest::newRow("negative-rejected") << -0.5 << -1.0;
    }
    void recalculationPreservesFractionalQuantity() {
        QFETCH(double,quantity); QFETCH(double,expected);
        QSqlQuery q(db);
        for(const auto& column:QStringList{"works_sum REAL", "parts_sum REAL"})
            if(db.record("work_orders").indexOf(column.section(' ',0,0))<0)
                QVERIFY(q.exec("ALTER TABLE work_orders ADD COLUMN "+column));
        for(const auto& column:QStringList{"markup_percent REAL", "manual_markup_override INTEGER DEFAULT 0", "markup_source TEXT", "pricing_calc_version INTEGER", "pricing_updated_at TEXT"})
            if(db.record("work_order_parts").indexOf(column.section(' ',0,0))<0)
                QVERIFY(q.exec("ALTER TABLE work_order_parts ADD COLUMN "+column));
        QVERIFY(q.exec("CREATE TABLE IF NOT EXISTS work_order_services(id INTEGER PRIMARY KEY,order_number INTEGER,order_uid TEXT,qty REAL,price REAL,amount REAL)"));
        QVERIFY(q.exec("UPDATE work_order_parts SET manual_markup_override=1 WHERE id=2"));
        q.prepare("UPDATE work_order_parts SET qty=?,price=100,amount=123.45,purchase_price=100,purchase_amount=123.45,store='',markup_percent=0 WHERE id=1");
        q.addBindValue(quantity);QVERIFY(q.exec());q.finish();
        if(expected<0) {
            QVERIFY(q.exec("INSERT INTO work_order_parts(id,order_number,order_uid,name,qty,price,amount,purchase_price,purchase_amount,manual_markup_override) VALUES(0,1,'order-a','Valid preceding row',0.5,100,123.45,100,123.45,0)"));
        }
        QString error;
        if(expected<0) expectRejectedTransaction(QString::fromUtf8("Некоректна кількість запчастини ID 1. Перерахунок скасовано без збереження змін."));
        const bool ok=PricingRecalculationService::recalcAllPartMarkupsFromSettings(db,&error);
        if(expected<0) {QVERIFY(!ok);QVERIFY(!error.isEmpty());}
        else QVERIFY2(ok,qPrintable(error));
        QVERIFY(q.exec("SELECT qty,amount,purchase_amount FROM work_order_parts WHERE id=1"));QVERIFY(q.next());
        QCOMPARE(q.value(0).toDouble(),quantity);
        QCOMPARE(q.value(1).toDouble(),expected<0?123.45:expected);
        QCOMPARE(q.value(2).toDouble(),expected<0?123.45:expected);
        q.finish();
        if(expected<0) {
            QVERIFY(q.exec("SELECT amount,purchase_amount FROM work_order_parts WHERE id=0"));QVERIFY(q.next());
            QCOMPARE(q.value(0).toDouble(),123.45);QCOMPARE(q.value(1).toDouble(),123.45);
        }
    }
    void invoiceRegistryUiLoadsAndFilters() {
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE work_order_part_attachments(part_id INTEGER PRIMARY KEY,invoice_number TEXT,invoice_date TEXT,file_path TEXT)"));
        QVERIFY(q.exec("INSERT INTO work_order_part_attachments VALUES(1,'INV-1','01.09.26','invoice.pdf'),(2,'INV-2','02.09.26','second.pdf')"));
        InvoicesPage page; page.resize(1200, 600); page.show(); page.refresh();
        auto* table = page.findChild<QTableView*>("invoicesTable"); QVERIFY(table);
        QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(), 2, 10000);
        QCOMPARE(table->model()->columnCount(), 7);
        auto* filter = page.findChild<QComboBox*>(); QVERIFY(filter);
        filter->setCurrentIndex(1); QCOMPARE(table->model()->rowCount(), 1);
        filter->setCurrentIndex(2); QCOMPARE(table->model()->rowCount(), 1);
        QCOMPARE(table->model()->index(0, 5).data().toString(), QString::fromUtf8("Сплачена"));
        filter->setCurrentIndex(0);
        bool paymentDialogChecked = false;
        QTimer::singleShot(50, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) return;
            const auto close = qScopeGuard([&]{ if(dialog->isVisible()) dialog->reject(); });
            auto* pay = dialog->findChild<QPushButton*>("payInFull");
            auto* balance = dialog->findChild<QLabel*>("paymentBalance");
            QString displayed;
            for(auto* label:dialog->findChildren<QLabel*>()) displayed += label->text()+"\n";
            paymentDialogChecked = dialog->objectName()=="purchasePaymentDialog"
                && pay && pay->isEnabled() && balance
                && displayed.contains("INV-1")
                && displayed.contains(FormatUtils::money(200.0,2));
            dialog->grab().save(dir.filePath(QStringLiteral("invoice-payment-dialog.png")));
            dialog->reject();
        });
        int unpaidRow=0;
        while(unpaidRow<table->model()->rowCount() && table->model()->index(unpaidRow,5).data().toString()==QString::fromUtf8("Сплачена"))++unpaidRow;
        QVERIFY(unpaidRow<table->model()->rowCount());
        const auto beforeCancel = PurchasePaymentService::detail(db,"part",1,"order-a");
        const auto eventsBeforeCancel = PurchasePaymentService::history(db,beforeCancel).size();
        table->clicked(table->model()->index(unpaidRow, 5));
        QVERIFY(paymentDialogChecked);
        const auto afterCancel = PurchasePaymentService::detail(db,"part",1,"order-a");
        QCOMPARE(afterCancel.paidCents,beforeCancel.paidCents);
        QCOMPARE(afterCancel.remainingCents(),beforeCancel.remainingCents());
        QCOMPARE(PurchasePaymentService::history(db,afterCancel).size(),eventsBeforeCancel);
        bool paidMessageChecked=false;
        QTimer::singleShot(20,[&] {
            auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if(!dialog) return;
            const auto close=qScopeGuard([&]{if(dialog->isVisible())dialog->reject();});
            auto* pay=dialog->findChild<QPushButton*>("payInFull");
            if(!pay)return;
            QTimer::singleShot(0,[&]{
                auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                if(!box)return;
                paidMessageChecked=box->windowTitle()==QString::fromUtf8("Оплату здійснено")
                    && box->text()==QString::fromUtf8("Оплату %1 грн успішно збережено.").arg(FormatUtils::moneyCents(beforeCancel.remainingCents()));
                box->accept();
            });
            pay->click();
        });
        table->clicked(table->model()->index(unpaidRow,5));
        QVERIFY(paidMessageChecked);
        const auto afterPayment=PurchasePaymentService::detail(db,"part",1,"order-a");
        QCOMPARE(afterPayment.remainingCents(),qint64(0));
        QCOMPARE(afterPayment.paidCents,beforeCancel.paidCents+beforeCancel.remainingCents());
        QCOMPARE(PurchasePaymentService::history(db,afterPayment).size(),eventsBeforeCancel+1);
        QTRY_VERIFY_WITH_TIMEOUT(table->model()->index(unpaidRow,5).data().toString()==QString::fromUtf8("Сплачена"),10000);
        const QString capture = qEnvironmentVariable("CS_INVOICE_UI_TEST_OUTPUT");
        if (!capture.isEmpty()) QVERIFY(page.grab().save(capture));
        QTest::qWait(150);
    }
    void invoicePaymentDateAndLayoutPersistence() {
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE work_order_part_attachments(part_id INTEGER PRIMARY KEY,invoice_number TEXT,invoice_date TEXT,file_path TEXT)"));
        QVERIFY(q.exec("INSERT INTO work_order_part_attachments VALUES(1,'DATE-1','01.09.26',''),(2,'DATE-2','02.09.26','')"));
        QString error;auto debt=PurchasePaymentService::detail(db,"part",1,"order-a");
        QVERIFY2(PurchasePaymentService::pay(db,debt,debt.remainingCents(),&error),qPrintable(error));
        QVERIFY(q.exec("UPDATE purchase_payment_events SET created_at='2026-10-21 12:34:00'"));
        auto invoices=InvoiceRegistryService::load(db,&error);QVERIFY(error.isEmpty());
        QCOMPARE(invoices.size(),2);
        QVERIFY(!invoices[0].paidAt.isValid());
        QCOMPARE(invoices[1].paidAt.date(),QDate(2026,10,21));
        {
            InvoicesPage page;page.resize(1200,650);page.show();page.refresh();
            auto* table=page.findChild<QTableView*>("invoicesTable");QVERIFY(table);
            QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(),2,10000);
            auto paidRow=[&]{for(int row=0;row<table->model()->rowCount();++row)
                if(table->model()->index(row,1).data().toString()=="DATE-1")return row;return -1;};
            QVERIFY(paidRow()>=0);
            QCOMPARE(table->model()->index(paidRow(),6).data().toString(),QString("21.10.26"));
            auto* header=table->horizontalHeader();header->resizeSection(1,287);header->moveSection(header->visualIndex(2),0);
            page.findChild<QComboBox*>("invoiceSort")->setCurrentIndex(2);
            QCOMPARE(header->length(),table->viewport()->width());QCOMPARE(header->visualIndex(2),0);
            for(const int width:{900,1500,1200}) {
                page.resize(width,650);QTest::qWait(30);
                QCOMPARE(header->length(),table->viewport()->width());
                QCOMPARE(table->horizontalScrollBar()->maximum(),0);
            }
            bool paidMessage=false;
            QTimer::singleShot(20,[&]{
                auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                if(box){paidMessage=box->text().contains("21.10.2026");box->accept();}
            });
            table->clicked(table->model()->index(paidRow(),5));QVERIFY(paidMessage);
            QVERIFY(page.grab().save(dir.filePath(QStringLiteral("invoices-updated.png"))));
            page.setPeriod(QDate(2026,9,1),QDate(2026,9,1));
            QCOMPARE(table->model()->rowCount(),1);
            QCOMPARE(table->model()->index(0,1).data().toString(),QString("DATE-1"));
            page.setPeriod(QDate(2026,10,1),QDate(2026,10,31));
            QCOMPARE(table->model()->rowCount(),0);
            bool expanded=false;
            page.periodExpanded=[&](QDate from,QDate to){expanded=from==QDate(2026,9,1) && to==QDate(2026,10,31);};
            page.selectInvoice(invoices[1].key);
            QTRY_VERIFY_WITH_TIMEOUT(expanded,10000);
            QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(),2,10000);
        }
        InvoicesPage reopened;
        auto* header=reopened.findChild<QTableView*>("invoicesTable")->horizontalHeader();
        QCOMPARE(header->visualIndex(2),0);
    }
    void supplierStagingWorkingCopyAudit() {
        const auto source=qEnvironmentVariable("CS_SUPPLIER_AUDIT_SOURCE");
        if(source.isEmpty())QSKIP("Requires an explicit disposable working-data copy");
        QTemporaryDir folder;QString error;SupplierInvoiceStaging::Plan plan;
        const auto destination=folder.filePath("staging.sqlite");
        QVERIFY2(SupplierInvoiceStaging::auditCopy(source,destination,"bm","offline-audit",{},&plan,&error),qPrintable(error));
        const auto name=QString("supplier-copy-audit-inspect");
        {
            auto copy=QSqlDatabase::addDatabase("QSQLITE",name);copy.setDatabaseName(destination);QVERIFY(copy.open());
            const auto local=SupplierInvoiceLedger::localItems(copy,&error);QVERIFY2(error.isEmpty(),qPrintable(error));
            QVERIFY(!local.isEmpty());
            {
                auto original=QSqlDatabase::addDatabase("QSQLITE",name+"-original");original.setDatabaseName(source);original.setConnectOptions("QSQLITE_OPEN_READONLY");QVERIFY(original.open());
                for(const auto& table:original.tables())if(table.contains("payment") || table.startsWith("workshop_") || table=="work_order_parts") {
                    QStringList fields;const auto record=original.record(table);
                    for(int i=0;i<record.count();++i)fields.append('"'+record.fieldName(i)+'"');
                    const auto sql="SELECT "+fields.join(',')+" FROM \""+table+"\"";
                    auto rows=[&](QSqlDatabase connection){QSqlQuery q(connection);QStringList values;
                        if(!q.exec(sql))return QStringList{q.lastError().text()};
                        while(q.next()){QJsonArray row;for(int i=0;i<record.count();++i)row.append(QJsonValue::fromVariant(q.value(i)));values.append(QString::fromUtf8(QJsonDocument(row).toJson(QJsonDocument::Compact)));}
                        values.sort();return values;
                    };
                    QCOMPARE(rows(copy),rows(original));
                }
                original.close();
            }
            QSqlDatabase::removeDatabase(name+"-original");
            QMap<QString,int> counts;for(const auto& item:local)++counts[SupplierInvoiceStaging::supplierKey(item.purchase.supplier)];
            qInfo()<<"Working-copy candidates:"<<local.size()<<"BM:"<<counts.value("bm")<<"Omega:"<<counts.value("omega")<<"UTR:"<<counts.value("utr");
            copy.close();
        }
        QSqlDatabase::removeDatabase(name);
    }
    void supplierInvoiceStagingCopyOnly() {
        using namespace SupplierInvoiceStaging;
        const auto raw=QJsonDocument::fromJson(R"({"document_uuid":"bm-doc-1","number":"BM-1","created_at":"2026-09-01T12:00:00Z","total":200,"currency":{"name":"UAH"},"products":[{"quantity":2,"total":200,"product":{"article":"OEM","name":"Part"}}]})").object();
        QVector<Document> documents;QString error;
        QVERIFY2(parse("bm",QJsonArray{raw},&documents,&error),qPrintable(error));
        QVERIFY(documents[0].complete);QCOMPARE(documents[0].total,20000);
        QSqlQuery q(db);QVERIFY(q.exec("UPDATE work_order_parts SET store='BM Parts' WHERE id=1"));q.finish();
        const auto local=SupplierInvoiceLedger::localItems(db,&error);QVERIFY2(error.isEmpty(),qPrintable(error));
        auto result=plan(documents,local);QCOMPARE(result.links.size(),1);
        auto duplicate=documents[0];duplicate.id="bm-doc-2";
        result=plan({documents[0],duplicate},local);QCOMPARE(result.links.size(),0);QCOMPARE(result.unresolved,2);
        duplicate=documents[0];duplicate.supplier="omega";
        QCOMPARE(plan({duplicate},local).links.size(),0);
        auto wrong=raw;wrong.insert("total",201);QVERIFY(!parse("bm",QJsonArray{wrong},&documents,&error));QVERIFY(documents.isEmpty());
        QVERIFY(parse("bm",QJsonArray{raw},&documents,&error));
        const auto source=db.databaseName();
        auto hash=[&]{QFile f(source);if(!f.open(QIODevice::ReadOnly))return QByteArray();return QCryptographicHash::hash(f.readAll(),QCryptographicHash::Sha256);};
        const auto before=hash();QVERIFY(!before.isEmpty());
        QTemporaryDir copies;const auto destination=copies.filePath("audit.sqlite");
        QVERIFY2(auditCopy(source,destination,"bm","test-account",documents,&result,&error),qPrintable(error));
        QCOMPARE(result.links.size(),1);QCOMPARE(before,hash());
        QVERIFY(!db.tables().contains("supplier_staged_links"));
        QVERIFY(!auditCopy(source,destination,"bm","test-account",documents,&result,&error));
        QVERIFY(!saveCopy(db,"bm","test-account",documents,&result,&error));
        const auto utr=QJsonDocument::fromJson(R"({"documentNumber":"UT-1","accountingNumber":"123","date":"2026-09-01T12:00:00"})").object();
        QVERIFY(parse("utr",QJsonArray{utr},&documents,&error));QVERIFY(!documents[0].complete);
        QCOMPARE(plan(documents,local).links.size(),0);
        const auto omega=QJsonDocument::fromJson(R"({"Id":"omega-1","Number":"OM-1","Date":"2026-09-01","Summ":200,"Products":[{"Code":"OEM","Count":"2","PiceWithVAT":100}]})").object();
        QVERIFY(parse("omega",QJsonArray{omega},&documents,&error));QVERIFY(!documents[0].complete);
        QVERIFY(parse("omega",QJsonArray{omega},&documents,&error,"UAH"));QVERIFY(documents[0].complete);
        auto stock=local.first();stock.purchase.supplier=QString::fromUtf8("Омега");stock.purchase.source="inventory";stock.key="inventory:test";
        const auto stockPlan=plan(documents,{stock});QCOMPARE(stockPlan.links.size(),1);QCOMPARE(stockPlan.links[0].purchase,stock.key);
    }
    void partsSearchAlignedWithDate() {
        ThemeManager::applySavedTheme();qApp->setFont(QFont("Times New Roman",10));
        PartsPage page;page.setStyleSheet("#partsArchivePage {background:#4b5968;}");page.resize(1500,800);page.show();QTest::qWait(800);
        auto* dashboard=page.m_supplierDashboard;QVERIFY(dashboard);
        auto* filters=page.findChild<QWidget*>("partsFilterFrame");QVERIFY(filters);
        const auto dates=filters->findChildren<QDateEdit*>();QCOMPARE(dates.size(),2);
        QVERIFY(page.findChild<QWidget*>("partsSearchTile"));
        for(int count:{0,2,3,4,7}) {
            QMap<QString,qint64> balances;
            for(int i=0;i<count;++i)balances.insert(QString::fromUtf8("Постачальник %1").arg(i+1),123456+i);
            dashboard->setBalances(balances);
            QCOMPARE(dashboard->findChildren<QWidget*>("partsSupplierTile").size(),count);
            QCOMPARE(dashboard->findChildren<QWidget*>("partsTotalTile").size(),count<=3?1:0);
            for(int width:{1500,1200,1700}) {
                page.resize(width,900);QTest::qWait(100);
                QList<QWidget*> cards;
                for(auto* w:dashboard->findChildren<QWidget*>(QString(),Qt::FindDirectChildrenOnly))if(w->property("partsSupplierTile").toBool())cards.append(w);
                for(int i=0;i<cards.size();++i){
                    QCOMPARE(cards[i]->y(),0);
                    QCOMPARE(cards[i]->height(),qRound(15.0*dashboard->logicalDpiY()/25.4));
                    QCOMPARE(dashboard->height(),cards[i]->height());
                    QVERIFY(dashboard->rect().contains(cards[i]->geometry()));
                    for(int j=i+1;j<cards.size();++j)QVERIFY(!cards[i]->geometry().intersects(cards[j]->geometry()));
                }
                for(auto* date:dates){
                    auto* edit=date->findChild<QLineEdit*>();QVERIFY(edit);
                    QVERIFY(edit->width()>=edit->fontMetrics().horizontalAdvance("25.10.26"));
                    QVERIFY(filters->rect().contains(date->geometry()));
                }
                QVERIFY(page.m_search->parentWidget()->rect().contains(page.m_search->geometry()));
            }
            const auto output=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
            if(!output.isEmpty())QVERIFY(page.grab().save(QDir(output).filePath(QString("parts-supplier-%1.png").arg(count))));
        }
    }
    void invoiceDashboardNarrowLayout() {
        ThemeManager::applySavedTheme();qApp->setFont(QFont("Times New Roman",10));
        QWidget host;
        QWidget controls(&host);
        auto* status=new QComboBox(&controls);status->setObjectName("invoiceStatusFilter");status->addItem(QString::fromUtf8("Усі накладні"));
        auto* sort=new QComboBox(&controls);sort->setObjectName("invoiceSort");sort->addItem(QString::fromUtf8("За датою ↓"));
        auto* refresh=new QPushButton(QString::fromUtf8("Оновити"),&controls);refresh->setObjectName("invoiceRefresh");
        ReportDashboard dashboard(&host);dashboard.setInvoiceControls(&controls);
        host.show();dashboard.show();
        for(int width:{1230,1366,1500}){
            dashboard.configure(2,false);
            dashboard.values({"28","9","19",QString::fromUtf8("27 365,86 грн")});
            dashboard.from->setDate(QDate(2026,10,16));dashboard.to->setDate(QDate(2026,10,25));
            host.resize(width,200);dashboard.setFixedSize(width,dashboard.compactHeight());QTest::qWait(150);
            QList<QWidget*> cards;
            for(auto* card:dashboard.findChildren<QWidget*>(QString(),Qt::FindDirectChildrenOnly))if(card->isVisible()&&card->property("reportTile").toBool())cards.append(card);
            QCOMPARE(cards.size(),9);
            for(auto* card:cards){
                QVERIFY2(dashboard.rect().contains(card->geometry()),qPrintable(card->objectName()));
                for(auto* other:cards)if(other!=card)QVERIFY2(!card->geometry().intersects(other->geometry()),qPrintable(QString("%1 / %2: dashboard %3 minimum %4 date %5 search %6").arg(card->objectName(),other->objectName()).arg(dashboard.width()).arg(dashboard.minimumSizeHint().width()).arg(dashboard.from->width()).arg(dashboard.search->width())));
                for(auto* field:card->findChildren<QWidget*>(QString(),Qt::FindDirectChildrenOnly))if(field->isVisible())QVERIFY2(card->rect().contains(field->geometry()),qPrintable(card->objectName()));
            }
            for(auto* date:{dashboard.from,dashboard.to}){
                auto* editor=date->findChild<QLineEdit*>();QVERIFY(editor);
                QVERIFY(editor->width()>=editor->fontMetrics().horizontalAdvance("25.10.26"));
            }
            const auto output=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
            if(!output.isEmpty())QVERIFY(dashboard.grab().save(QDir(output).filePath(QString("invoice-narrow-%1.png").arg(width))));
        }
    }
    void expenseDashboardTextFits() {
        qApp->setFont(QFont("Times New Roman",10));
        QWidget host;host.resize(1600,200);host.show();
        ReportDashboard dashboard(&host);
        dashboard.configure(5,false);
        dashboard.values({QString::fromUtf8("0 грн"),QString::fromUtf8("0 грн"),QString::fromUtf8("Витрати не внесені\nПеревірте записи та обрані дати")});
        dashboard.show();
        for(bool light:{false,true})for(int width:{1200,1400,1600}) {
            dashboard.configure(5,light);dashboard.resize(width,180);QTest::qWait(100);
            auto* card=dashboard.findChild<QWidget*>("reportMetric2");QVERIFY(card);
            auto* value=dashboard.findChild<QLabel*>("reportMetricValue2");QVERIFY(value);
            QCOMPARE(value->font().pointSizeF(),10.0);
            QVERIFY(card->rect().contains(value->geometry()));
            QVERIFY2(value->heightForWidth(value->width())<=value->height(),qPrintable(QString("width=%1 needed=%2 available=%3").arg(width).arg(value->heightForWidth(value->width())).arg(value->height())));
            for(auto* label:card->findChildren<QLabel*>())if(label!=value)
                QVERIFY(!value->geometry().intersects(label->geometry()));
            QCOMPARE(dashboard.findChild<QLabel*>("reportMetricValue0")->font().pointSizeF(),15.0);
        }
        dashboard.configure(5,false);dashboard.resize(1200,180);QTest::qWait(100);
        const auto output=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
        if(!output.isEmpty())QVERIFY(dashboard.grab().save(QDir(output).filePath("expenses-text-fit.png")));
    }
    void totalExpenseDashboardMetrics() {
        QString error;
        QVERIFY2(AdditionalExpenseService::ensureSchema(db,&error),qPrintable(error));
        const QDate date(2090,1,15);
        QVERIFY2(AdditionalExpenseService::add(date,QString::fromUtf8("Нова пошта"),"test",250,nullptr,&error),qPrintable(error));
        QVERIFY2(AdditionalExpenseService::add(date,QString::fromUtf8("Інше"),"test",75.25,nullptr,&error),qPrintable(error));
        QVERIFY(AdditionalExpenseService::canWrite());
        QVERIFY(AdditionalExpenseService::categories().contains(QString::fromUtf8("Інше")));
        QSqlQuery fault(db);
        QVERIFY(fault.exec("CREATE TRIGGER refuse_expense_delete BEFORE DELETE ON additional_expenses BEGIN SELECT RAISE(ABORT,'test rollback'); END"));
        QVERIFY(fault.exec("SELECT id FROM additional_expenses ORDER BY id LIMIT 1"));QVERIFY(fault.next());
        const qint64 expenseId=fault.value(0).toLongLong();fault.finish();
        QVERIFY(!AdditionalExpenseService::remove(expenseId,&error));
        QVERIFY(fault.exec("DROP TRIGGER refuse_expense_delete"));error.clear();
        const auto snapshot=ReportsDataLoader::buildTotalExpensesSnapshot(db.databaseName(),"2090-01-01","2090-01-31");
        QVERIFY2(snapshot.error.isEmpty(),qPrintable(snapshot.error));
        QCOMPARE(snapshot.rows.size(),2);
        QCOMPARE(snapshot.additionalCents,qint64(32525));
        QCOMPARE(snapshot.novaPoshtaCents,qint64(25000));
        const auto empty=ReportsDataLoader::buildTotalExpensesSnapshot(db.databaseName(),"2090-02-01","2090-02-28");
        QVERIFY(empty.error.isEmpty());QVERIFY(empty.rows.isEmpty());
        QCOMPARE(empty.novaPoshtaCents,qint64(0));
    }
    void invoiceSortPopupFitsLabels() {
        InvoicesPage page;
        auto* sort = page.findChild<QComboBox*>("invoiceSort");
        QVERIFY(sort);
        sort->setFixedWidth(110);
        page.show();
        for (int points : {11, 18}) {
            QFont font = sort->font();
            font.setPointSize(points);
            sort->setFont(font);
            sort->showPopup();
            QCoreApplication::processEvents();
            for (int row = 0; row < sort->count(); ++row)
                QVERIFY(sort->view()->viewport()->width() > sort->view()->fontMetrics().horizontalAdvance(sort->itemText(row)));
            QCOMPARE(sort->width(), 110);
            sort->hidePopup();
        }
    }

    void orderInvoiceLabelsAndPaymentEmphasis() {
        QTableView table;
        ReportsTableSupport::ReportsSnapshotTableModel model;
        table.setModel(&model);
        OrdersSnapshot snapshot;
        OrdersSnapshotRow row;
        row.invoiceNumbers={QString::fromUtf8("Видаткова накладна №А-04872750 від 03.10.26"),QStringLiteral("KAM26F058793")};
        row.invoiceKeys={QStringLiteral("original-key-1"),QStringLiteral("original-key-2")};
        for(const auto& status:{QString::fromUtf8("Сплачено"),QString::fromUtf8("Не сплачено"),QString::fromUtf8("Не сплачено повністю")}) {
            row.paymentStatus=status;snapshot.rows.append(row);
        }
        ReportsTableSupport::applyOrdersSnapshot(&table,&model,snapshot);
        QCOMPARE(model.cellText(0,3),QString::fromUtf8("А-04872750\nKAM26F058793"));
        QCOMPARE(model.cellUserValue(0,3).toStringList(),row.invoiceKeys);
        QVERIFY(!model.data(model.index(0,6),Qt::FontRole).value<QFont>().bold());
        QVERIFY(model.data(model.index(1,6),Qt::FontRole).value<QFont>().bold());
        QVERIFY(model.data(model.index(2,6),Qt::FontRole).value<QFont>().bold());
        QCOMPARE(snapshot.rows.first().invoiceNumbers,row.invoiceNumbers);
    }

    void reportDashboardLayout() {
        ThemeManager::applySavedTheme();qApp->setFont(QFont("Times New Roman",10));
        ReportsPage page;page.setStyleSheet(page.styleSheet()+"#reportsPage {background:#526070;}");page.resize(1500,850);page.show();
        auto* tabs=page.findChild<QTabWidget*>();QVERIFY(tabs);
        auto* dashboard=page.findChild<QWidget*>("reportDashboard");QVERIFY(dashboard);
        for(auto* value:dashboard->findChildren<QLabel*>())if(value->property("reportValue").toBool())
            QCOMPARE(value->alignment(),Qt::Alignment(Qt::AlignCenter));
        auto* from=page.findChild<QDateEdit*>("dashboardFrom");QVERIFY(from);
        auto* to=page.findChild<QDateEdit*>("dashboardTo");QVERIFY(to);
        from->setDate(QDate(2026,10,16));to->setDate(QDate(2026,10,25));
        const auto output=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
        int dashboardHeight=-1;
        for(int index:{0,1,2,4,5,7,3,6,8,9,2,1}) {
            tabs->setCurrentIndex(index);page.refresh();QTest::qWait(1200);
            const bool supported=index>=0&&index<=7;
            QCOMPARE(dashboard->isVisible(),supported);
            if(!supported)continue;
            auto* top=page.findChild<QWidget*>("reportsTopHost");QVERIFY(top);
            if(dashboardHeight<0)dashboardHeight=top->height();
            QCOMPARE(top->height(),dashboardHeight);
            auto* dateTile=dashboard->findChild<QWidget*>("reportDateTile");QVERIFY(dateTile);
            QCOMPARE(dateTile->height(),qRound(7.0*dashboard->logicalDpiY()/25.4)+qRound(9.0*dashboard->logicalDpiY()/25.4));
            for(auto* card:dashboard->findChildren<QWidget*>())if(card->isVisible()&&card->property("reportTile").toBool()){
                QCOMPARE(card->height(),dateTile->height());
                QCOMPARE(card->mapTo(dashboard,QPoint()).y(),dateTile->mapTo(dashboard,QPoint()).y());
                for(auto* rule:card->findChildren<QFrame*>())if(rule->frameShape()==QFrame::HLine&&rule->isVisible())
                    QCOMPARE(rule->mapTo(card,QPoint()).y()+1,qRound(7.0*dashboard->logicalDpiY()/25.4));
            }
            auto* tabBar=tabs->findChild<QTabBar*>();QVERIFY(tabBar);
            const int gap=tabBar->mapTo(&page,QPoint(0,0)).y()-dateTile->mapTo(&page,QPoint(0,dateTile->height())).y();
            QCOMPARE(gap,qRound(page.logicalDpiY()/25.4));
            for(auto* field:dashboard->findChildren<QWidget*>(QString(),Qt::FindDirectChildrenOnly)) {
                if(!field->isVisible())continue;
                QVERIFY2(dashboard->rect().contains(field->geometry()),qPrintable(field->objectName()));
            }
            QCOMPARE(from->height(),qRound(7.0*page.logicalDpiY()/25.4));
            QCOMPARE(from->y(),to->y());
            for(int i=0;i<4;++i){
                auto* card=dashboard->findChild<QWidget*>(QString("reportMetric%1").arg(i));
                if(!card||!card->isVisible())continue;
                for(auto* label:card->findChildren<QLabel*>())if(label->isVisible()&&!label->text().isEmpty())
                    QVERIFY2(label->heightForWidth(label->width())<=label->height(),qPrintable(label->text()));
            }
            QVERIFY(to->x()>from->geometry().right());
            QCOMPARE(from->accessibleName(),QString::fromUtf8("Початок періоду"));
            QCOMPARE(to->accessibleName(),QString::fromUtf8("Кінець періоду"));
            for(auto* label:dateTile->findChildren<QLabel*>())if(label->isVisible()){
                QVERIFY(label->text()!=QString::fromUtf8("З:"));
                QVERIFY(label->text()!=QString::fromUtf8("По:"));
            }
            if(index==3){
                static_cast<ReportDashboard*>(dashboard)->parts(
                    {QString::fromUtf8("Олива моторна 5W40"),QString::fromUtf8("Антифриз"),QString::fromUtf8("Запчастини · постачання"),QString::fromUtf8("Запчастини · використання")},
                    {QString::fromUtf8("Продаж: 0 л | Залишок: 146 л"),QString::fromUtf8("Немає даних"),QString::fromUtf8("На складі: 0 од.<br/>Замовлено: 0 од."),QString::fromUtf8("Видано в наряди: 0 од.<br/>Встановлено: 0 од.")});
                QTest::qWait(100);
                QCOMPARE(dashboard->findChild<QWidget*>("reportPartsTile0")->width(),qRound(60.0*dashboard->logicalDpiX()/25.4));
                QCOMPARE(dashboard->findChild<QWidget*>("reportPartsTile1")->width(),qRound(60.0*dashboard->logicalDpiX()/25.4));
                QVERIFY(qAbs(dashboard->findChild<QWidget*>("reportPartsTile2")->width()-dashboard->findChild<QWidget*>("reportPartsTile3")->width())<=1);
                for(int i=0;i<4;++i){
                    auto* card=dashboard->findChild<QWidget*>(QString("reportPartsTile%1").arg(i));QVERIFY(card);
                    QVERIFY(card->isVisible());QCOMPARE(card->height(),dateTile->height());
                    for(auto* label:card->findChildren<QLabel*>())if(!label->text().isEmpty())
                        QVERIFY2(label->heightForWidth(label->width())<=label->height(),qPrintable(QString("%1 size %2x%3 needed %4").arg(label->text()).arg(label->width()).arg(label->height()).arg(label->heightForWidth(label->width()))));
                }
            }
            if(index==6){
                auto* actions=dashboard->findChild<QWidget*>("additionalExpensesFormScroll");QVERIFY(actions);
                auto* add=actions->findChild<QPushButton*>("addAdditionalExpense");QVERIFY(add);
                auto* remove=actions->findChild<QPushButton*>("deleteAdditionalExpense");QVERIFY(remove);
                QVERIFY(actions->property("reportTile").toBool());
                QCOMPARE(actions->height(),dateTile->height());
                QCOMPARE(actions->y(),dateTile->y());
                QCOMPARE(add->y(),remove->y());
                QVERIFY(remove->x()>add->geometry().right());
                QCOMPARE(add->height(),qRound(7.0*dashboard->logicalDpiY()/25.4));
                QCOMPARE(remove->height(),add->height());
                QVERIFY(!add->icon().isNull());QVERIFY(!remove->icon().isNull());
                for(int i=0;i<3;++i){
                    auto* card=dashboard->findChild<QWidget*>(QString("reportMetric%1").arg(i));QVERIFY(card);
                    for(auto* label:card->findChildren<QLabel*>())if(label->isVisible()&&!label->text().isEmpty())
                        QVERIFY2(label->heightForWidth(label->width())<=label->height(),qPrintable(label->text()));
                }
            }
            for(auto* button:dashboard->findChildren<QPushButton*>())if(button->isVisible()) {
                QCOMPARE(button->height(),qRound(7.0*dashboard->logicalDpiY()/25.4));
                QVERIFY(button->parentWidget()->rect().contains(button->geometry()));
                QVERIFY2(button->width()>=button->fontMetrics().horizontalAdvance(button->text())+10,qPrintable(button->text()));
            }
            auto* search=page.findChild<QLineEdit*>("dashboardSearch");QVERIFY(search);
            QCOMPARE(search->isVisible(),index==1||index==2);
            if(index==1||index==2){
                QCOMPARE(from->mapTo(dashboard,QPoint()).y(),search->mapTo(dashboard,QPoint()).y());
                QCOMPARE(from->height(),search->height());
                auto* dates=dashboard->findChild<QWidget*>("reportDateTile");QVERIFY(dates);
                auto* searchTile=dashboard->findChild<QWidget*>("reportSearchTile");QVERIFY(searchTile);
                auto* following=dashboard->findChild<QWidget*>(index==1?"reportActionsHost":"reportMetricsHost");QVERIFY(following);
                if(index==1){
                    QCOMPARE(searchTile->x(),dates->geometry().right()+7);
                    QCOMPARE(following->x(),searchTile->geometry().right()+7);
                }else{
                    QCOMPARE(searchTile->x(),dates->geometry().right()+7);
                    auto* previous=searchTile;
                    for(int i=0;i<4;++i){
                        auto* card=dashboard->findChild<QWidget*>(QString("reportMetric%1").arg(i));QVERIFY(card);
                        QCOMPARE(card->x(),previous->geometry().right()+7);
                        QVERIFY(card->width()>0);
                        previous=card;
                    }
                    QCOMPARE(from->y(),to->y());
                    QVERIFY(to->x()>from->x());
                    for(int i=0;i<3;++i){
                        auto* card=dashboard->findChild<QWidget*>(QString("reportInvoiceControl%1").arg(i));QVERIFY(card);
                        QVERIFY(card->isVisible());QCOMPARE(card->y(),dates->y());
                        QCOMPARE(card->x(),previous->geometry().right()+7);previous=card;
                    }
                    static_cast<ReportDashboard*>(dashboard)->values({"28","9","19",QString::fromUtf8("27 365,86 грн")});
                    QTest::qWait(50);
                    for(int i=0;i<4;++i){
                        auto* card=dashboard->findChild<QWidget*>(QString("reportMetric%1").arg(i));
                        for(auto* label:card->findChildren<QLabel*>())if(label->isVisible()&&!label->text().isEmpty()){
                            QVERIFY2(label->heightForWidth(label->width())<=label->height(),qPrintable(label->text()));
                        }
                    }
                }
                QCOMPARE(searchTile->y(),dates->y());
                search->setText("477");QTest::qWait(500);
                QCOMPARE(page.findChild<QLineEdit*>(index==1?"reportsOrdersSearch":"invoiceSearch")->text(),QString("477"));
                search->clear();QTest::qWait(500);
            }
            if(!output.isEmpty())QVERIFY(page.grab().save(QDir(output).filePath(QString("dashboard-%1.png").arg(index))));
        }
        QVERIFY(!page.findChild<QPushButton*>("reportAction0_0"));
        from->setDate(QDate::currentDate());to->setDate(QDate::currentDate());
        QCOMPARE(from->date(),QDate::currentDate());QCOMPARE(to->date(),QDate::currentDate());
        from->setDate(QDate::currentDate().addDays(-6));QCOMPARE(from->date(),QDate::currentDate().addDays(-6));
        page.findChild<QPushButton*>("reportAction1_2")->click();
        QVERIFY(page.findChild<QPushButton*>("reportAction1_2")->isChecked());
        page.findChild<QPushButton*>("reportAction2_1")->click();
        QVERIFY(page.findChild<QPushButton*>("reportAction1_0")->isChecked());
        QCOMPARE(from->date(),QDate::currentDate().addDays(-9));
        QTest::qWait(500);
    }
    void allReportFiltersOneMillimetreOffset() {
        ReportsPage page;page.resize(1500,800);page.show();
        auto* tabs=page.findChild<QTabWidget*>();QVERIFY(tabs);
        auto* row=page.findChild<QWidget*>("reportDashboard");QVERIFY(row);
        auto* top=page.findChild<QWidget*>("reportsTopHost");QVERIFY(top);
        const int offset=qRound(page.logicalDpiY()/25.4);
        for(int index:{3,8,9,3}) {
            tabs->setCurrentIndex(index);QTest::qWait(700);
            if(index>=8) {QVERIFY(!top->isVisible());continue;}
            auto* target=row;
            QCOMPARE(target->layout()->contentsMargins().top(),offset);
            const auto tabRect=tabs->geometry();
            const auto margins=target->layout()->contentsMargins();
            QList<QWidget*> fields;
            for(auto* card:row->findChildren<QWidget*>(QString(),Qt::FindDirectChildrenOnly))
                if(card->isVisible() && card->property("reportTile").toBool())fields.append(card);
            QVERIFY(!fields.isEmpty());
            QList<QRect> shifted;
            for(auto* field:fields) {QVERIFY(field);shifted.append(QRect(field->mapTo(&page,QPoint()),field->size()));}
            target->layout()->setContentsMargins(margins.left(),0,margins.right(),margins.bottom());
            target->layout()->activate();QTest::qWait(20);
            for(int i=0;i<fields.size();++i)
                QCOMPARE(shifted[i],QRect(fields[i]->mapTo(&page,QPoint()),fields[i]->size()).translated(0,offset));
            QCOMPARE(tabs->geometry(),tabRect);
            target->layout()->setContentsMargins(margins);target->layout()->activate();
        }
    }
    void ordersFilterOneMillimetreOffset() {
        ReportsPage page;page.resize(1500,800);page.show();
        auto* tabs=page.findChild<QTabWidget*>();QVERIFY(tabs);
        tabs->setCurrentIndex(1);QTest::qWait(500);
        auto* row=page.findChild<QWidget*>("reportDashboard");QVERIFY(row);
        auto* dates=page.findChild<QWidget*>("reportDateTile");QVERIFY(dates);
        auto* search=page.findChild<QWidget*>("reportSearchTile");QVERIFY(search);
        QCOMPARE(dates->mapTo(row,QPoint()).y(),search->mapTo(row,QPoint()).y());
        QCOMPARE(dates->height(),search->height());
        QVERIFY(page.findChild<QLineEdit*>("dashboardSearch")->isVisible());
        const auto output=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
        if(!output.isEmpty())QVERIFY(page.grab().save(QDir(output).filePath("orders-offset.png")));
        QTest::qWait(1500);
    }
    void invoiceSearchMatchesOrdersToolbar() {
        ReportsPage page;page.resize(1300,750);
        auto* tabs=page.findChild<QTabWidget*>();QVERIFY(tabs);
        tabs->setCurrentIndex(2);page.show();QTest::qWait(150);
        auto* top=page.findChild<QWidget*>("reportsTopHost");QVERIFY(top);
        auto* search=page.findChild<QLineEdit*>("dashboardSearch");QVERIFY(search);
        const QList<QDateEdit*> dates={page.findChild<QDateEdit*>("dashboardFrom"),page.findChild<QDateEdit*>("dashboardTo")};
        for(auto* date:dates) {
            QVERIFY(date->isVisible());QVERIFY(date->isEnabled());
            QVERIFY(date->mapTo(top,QPoint(date->width(),0)).x()<search->mapTo(top,QPoint()).x());
            QCOMPARE(date->height(),search->height());
        }
        QVERIFY(search->isVisible());
        search->setText("invoice-only");
        QCOMPARE(page.findChild<QLineEdit*>("invoiceSearch")->text(),QString("invoice-only"));
        QVERIFY(page.grab().save(dir.filePath(QStringLiteral("invoice-search-toolbar.png"))));
        tabs->setCurrentIndex(1);QTest::qWait(150);
        QVERIFY(search->text().isEmpty());
        search->setText("order-only");
        QCOMPARE(page.findChild<QLineEdit*>("reportsOrdersSearch")->text(),QString("order-only"));
        tabs->setCurrentIndex(2);QTest::qWait(150);
        QCOMPARE(search->text(),QString("invoice-only"));
        QTest::qWait(1500);
    }
    void loginDropdownUsesActivePersonnel() {
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE masters(id TEXT PRIMARY KEY,name TEXT,role TEXT,active INTEGER)"));
        QVERIFY(q.exec("INSERT INTO masters VALUES('current','Current worker','master',1),('old1','Duplicate','master',0),('old2','Duplicate','master',0)"));
        QVERIFY(q.exec("DELETE FROM staff_accounts"));
        QVERIFY(q.exec("INSERT INTO staff_accounts(id,master_id,display_name,role,active,pin_hash) VALUES"
                       "('current','current','Current worker','owner',1,'configured'),"
                       "('old1','old1','Duplicate','master',1,''),('old2','old2','Duplicate','master',1,''),"
                       "('admin','','Administrator','owner',1,'')"));
        StaffSessionDialog dialog; dialog.show();
        auto* list=dialog.findChild<QComboBox*>(); QVERIFY(list);
        QCOMPARE(list->count(),1);
        QCOMPARE(list->itemData(0).toString(),QString("current"));
        QVERIFY(list->itemText(0).contains("Current worker"));
        QTest::qWait(50);
        QVERIFY(dialog.grab().save(dir.filePath(QStringLiteral("staff-login-filtered.png"))));
    }
    void staffBatchServiceRollsBackAndRejectsStaleForms() {
        QString error;
        QVERIFY2(StaffAccountService::ensureSchema(db,&error),qPrintable(error));
        const auto read=[&](const QString& id) {
            const auto rows=AccessControlService::accounts(&error);
            for(const auto& row:rows) if(row.id==id) return row;
            return AccessAccountRow{};
        };
        const auto first=read("account-1"),second=read("account-2");
        QVERIFY2(error.isEmpty(),qPrintable(error));QVERIFY(!first.id.isEmpty());QVERIFY(!second.id.isEmpty());
        QVector<AccessControlService::AccountEdit> edits{{first,"First changed","master"},{second,"Second changed","administrator"}};
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TRIGGER fail_second_staff BEFORE UPDATE ON staff_accounts WHEN OLD.id='account-2' BEGIN SELECT RAISE(ABORT,'test write failure'); END"));q.finish();
        bool changed=true;
        QVERIFY(!AccessControlService::saveAccounts(edits,&changed,&error));QVERIFY(!changed);QVERIFY(!error.isEmpty());
        QCOMPARE(read(first.id).displayName,first.displayName);QCOMPARE(read(second.id).roleId,second.roleId);
        QVERIFY(q.exec("DROP TRIGGER fail_second_staff"));q.finish();
        QVERIFY2(AccessControlService::saveAccounts(edits,&changed,&error),qPrintable(error));QVERIFY(changed);
        QCOMPARE(read(first.id).displayName,QString("First changed"));QCOMPARE(read(second.id).roleId,QString("administrator"));
        // The old snapshot must fail, including an unchanged requested value.
        QVERIFY(!AccessControlService::saveAccounts(edits,&changed,&error));QVERIFY(!changed);QVERIFY(!error.isEmpty());
        auto current=read(first.id);
        edits={{current,current.displayName,current.roleId}};
        QVERIFY2(AccessControlService::saveAccounts(edits,&changed,&error),qPrintable(error));QVERIFY(!changed);
        edits.append(edits.first());
        QVERIFY(!AccessControlService::saveAccounts(edits,&changed,&error));QVERIFY(!error.isEmpty());
        edits.resize(1);
        QVERIFY2(AccessControlService::deactivateAccount(first.id,&error),qPrintable(error));
        QVERIFY(!AccessControlService::saveAccounts(edits,&changed,&error));QVERIFY(!error.isEmpty());
        ActivityAuditService::setCurrentActor({"account-2","Second changed","administrator","test"});
        QVERIFY(!AccessControlService::saveAccounts({},&changed,&error));QVERIFY(!changed);QVERIFY(!error.isEmpty());
        ActivityAuditService::setCurrentActor({"owner","Owner","owner","test"});
        QVERIFY(q.exec("PRAGMA integrity_check"));QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("ok"));
    }
    void staffPageSavesThroughBatchService() {
        QString error;
        QVERIFY2(StaffAccountService::ensureSchema(db,&error),qPrintable(error));
        AccessControlPage page;
        auto* table=page.findChild<QTableWidget*>("accessAccountsTable");QVERIFY(table);
        int row=-1;
        for(int i=0;i<table->rowCount();++i)
            if(table->item(i,0)->data(Qt::UserRole).toString()=="account-1") row=i;
        QVERIFY(row>=0);
        auto* name=table->cellWidget(row,0)->findChild<QLineEdit*>();
        auto* role=table->cellWidget(row,1)->findChild<QComboBox*>();
        QVERIFY(name);QVERIFY(role);name->setText("Batch UI worker");
        const int roleIndex=role->findData("administrator");QVERIFY(roleIndex>=0);role->setCurrentIndex(roleIndex);
        QPushButton* save=nullptr;
        for(auto* button:page.findChildren<QPushButton*>())
            if(button->text()==QString::fromUtf8("Зберегти працівників")) save=button;
        QVERIFY(save);save->click();
        QSqlQuery q(db);
        QVERIFY(q.exec("SELECT display_name,role FROM staff_accounts WHERE id='account-1'"));QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(),QString("Batch UI worker"));QCOMPARE(q.value(1).toString(),QString("administrator"));q.finish();
        // Current UI snapshot is now stale after another form changes the name.
        QVERIFY(q.exec("UPDATE staff_accounts SET display_name='Concurrent name' WHERE id='account-1'"));q.finish();
        save->click();
        QVERIFY(q.exec("SELECT display_name FROM staff_accounts WHERE id='account-1'"));QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(),QString("Concurrent name"));
    }
    void independentStaffAccounts() {
        ThemeManager::applySavedTheme();
        QString error;QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE masters(id TEXT PRIMARY KEY,name TEXT,role TEXT,active INTEGER)"));
        QVERIFY(q.exec("INSERT INTO masters VALUES('obsolete','Old master','master',1)"));
        // Existing active account-2 is linked to m2; login intentionally excludes
        // orphaned staff links, while the management page keeps them repairable.
        QVERIFY(q.exec("INSERT INTO masters VALUES('m2','Existing master','master',1)"));
        QVERIFY2(StaffAccountService::ensureSchema(db,&error),qPrintable(error));
        QVERIFY2(AccessControlService::saveAccount(db,"account-1","Renamed worker","administrator",&error),qPrintable(error));
        QVERIFY2(AccessControlService::saveAccount(db,{},"New worker","master",&error),qPrintable(error));
        AccessControlPage page;page.resize(1200,700);page.show();
        bool inspectedDialog=false;
        QTimer::singleShot(0,&page,[&] {
            auto* dialog=qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
            if(dialog) {
                const int expectedWidth=qMin(qRound(90.0*dialog->logicalDpiX()/25.4),dialog->screen()->availableGeometry().width()-24);
                auto* editor=dialog->findChild<QLineEdit*>();
                inspectedDialog=dialog->width()==expectedWidth && editor && editor->isVisible()
                    && dialog->rect().contains(QRect(editor->mapTo(dialog,QPoint()),editor->size()));
                dialog->reject();
            }
        });
        page.findChild<QPushButton*>("addStaffAccount")->click();QVERIFY(inspectedDialog);
        auto* table=page.findChild<QTableWidget*>("accessAccountsTable");QVERIFY(table);
        int target=-1;for(int i=0;i<table->rowCount();++i)if(table->item(i,0)->data(Qt::UserRole).toString()=="account-1")target=i;
        QVERIFY(target>=0);
        table->cellWidget(target,0)->findChild<QLineEdit*>()->setText("Saved name");
        QPushButton* save=nullptr;for(auto* button:page.findChildren<QPushButton*>())if(button->text()==QString::fromUtf8("Зберегти працівників"))save=button;
        QVERIFY(save);save->click();
        QVERIFY(q.exec("SELECT display_name,role FROM staff_accounts WHERE id='account-1'"));QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(),QString("Saved name"));QCOMPARE(q.value(1).toString(),QString("administrator"));q.finish();
        QVERIFY2(AccessControlService::deactivateAccount(db,"account-1",&error),qPrintable(error));
        for(int i=0;i<3;++i) {
            const auto rows=StaffAccountService::accounts(&error);QVERIFY2(error.isEmpty(),qPrintable(error));
            for(const auto& row:rows){QVERIFY(row.id!="account-1");QVERIFY(row.masterId!="obsolete");}
            const auto security=AccessControlService::accounts(db,&error);
            QCOMPARE(rows.size(),security.size());
        }
        QVERIFY(!AccessControlService::deactivateAccount(db,"owner",&error));
        QTest::qWait(50);
        for(int row=0;row<table->rowCount();++row)for(int col:{0,1,4}) {
            auto* cell=table->cellWidget(row,col);
            for(auto* control:cell->findChildren<QWidget*>(QString(),Qt::FindDirectChildrenOnly))
                QVERIFY(cell->rect().contains(control->geometry()));
        }
        QVERIFY(page.grab().save(dir.filePath(QStringLiteral("staff-updated.png"))));
    }
    void invoiceAgingBoundariesAndPayment() {
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE work_order_part_attachments(part_id INTEGER PRIMARY KEY,invoice_number TEXT,invoice_date TEXT,file_path TEXT)"));
        QVERIFY(q.exec("INSERT INTO work_order_part_attachments VALUES(1,'INV-1','','invoice.pdf')"));
        InvoicesPage page; page.resize(1200, 600); page.show();
        auto* table = page.findChild<QTableView*>("invoicesTable"); QVERIFY(table);
        for (int days : {-1, 0, 2, 3, 5, 6}) {
            q.prepare("UPDATE work_order_part_attachments SET invoice_date=?");
            q.addBindValue(QDate::currentDate().addDays(-days).toString("yyyy-MM-dd")); QVERIFY(q.exec());
            page.refresh(); QTRY_VERIFY_WITH_TIMEOUT(table->isEnabled(), 10000);
            QCOMPARE(table->model()->rowCount(), 1);
            const auto fill = table->model()->index(0, 0).data(Qt::BackgroundRole);
            QCOMPARE(fill.isValid(), days >= 3);
            if (days >= 3) {
                const QColor expected(days > 5 ? "#ff9f9f" : "#ffe28a");
                QCOMPARE(fill.value<QBrush>().color(), expected);
                QTest::qWait(50);
                const QRect cell = table->visualRect(table->model()->index(0, 0));
                const auto pixels = table->viewport()->grab().toImage();
                QCOMPARE(pixels.pixelColor(cell.left()+12, cell.top()+5), expected);
                QVERIFY(page.grab().save(dir.filePath(QStringLiteral("invoice-aging-%1.png").arg(days))));
            }
        }
        QString error; const auto debts = PurchasePaymentService::orderPartDebts(db, 1, &error);
        QVERIFY2(PurchasePaymentService::payOrderParts(db, 1, debts, &error), qPrintable(error));
        page.refresh(); QTRY_VERIFY_WITH_TIMEOUT(table->isEnabled(), 10000);
        QVERIFY(!table->model()->index(0, 0).data(Qt::BackgroundRole).isValid());
        QCOMPARE(table->model()->index(0, 5).data().toString(), QString::fromUtf8("Сплачена"));
    }
    void workshopDispatchQueueAndResources() {
        using namespace PlanningDispatch;
        QSqlQuery sql(db);
        QVERIFY(sql.exec("CREATE TABLE planning_calendar(day TEXT PRIMARY KEY,note TEXT)"));
        const QDate day=QDate::currentDate();
        PlanningRepository repo;
        repo.setDayMap(day,{{8,"Oil change"}});
        repo.setDayReminderActive(day,true);
        Entry a;a.vehicle="Audi Q7";a.work="Service";a.mechanic="Test Master";a.mechanicId="m1";
        a.start=540;a.minutes=90;a.bay=1;
        QString error;
        QVERIFY2(save({},a,&error),qPrintable(error));
        auto pending=queue(&error);QCOMPARE(pending.size(),1);
        a.day=day;
        QVERIFY2(save(pending[0],a,&error),qPrintable(error));
        QVERIFY(queue(&error).isEmpty());QVERIFY(!save(pending[0],a,&error));
        auto scheduled=decode(day,9,repo.getDayMap(day).value(9));
        QCOMPARE(scheduled.vehicle,a.vehicle);QCOMPARE(scheduled.minutes,90);
        Entry other=a;other.start=600;other.bay=2;
        QVERIFY(!save({},other,&error));other.mechanicId="m2";other.bay=1;
        QVERIFY(!save({},other,&error));other.bay=2;
        QVERIFY2(save({},other,&error),qPrintable(error));
        a.day=day.addDays(1);
        QVERIFY2(save(scheduled,a,&error),qPrintable(error));
        QVERIFY(repo.isDayReminderActive(day));QCOMPARE(repo.getDayMap(day).value(8),QString("Oil change"));
        QVERIFY(!repo.getDayMap(day).contains(9));QVERIFY(repo.getDayMap(a.day).contains(9));
        PlanningPage page(nullptr,true);page.resize(1360,880);page.show();
        auto* desk=page.findChild<QWidget*>("planningDesk");QVERIFY(desk);
        QCOMPARE(desk->findChildren<QFrame*>("planningMetricCard").size(),4);
        auto* grid=desk->findChild<QTableWidget*>("dispatchSchedule");QVERIFY(grid);
        QCOMPARE(grid->rowCount(),1);QVERIFY(grid->columnCount()==6 || grid->columnCount()==7);
        auto click=[&](QString text){for(auto* b:desk->findChildren<QPushButton*>())if(b->text()==text){b->click();return true;}return false;};
        const auto capture=qEnvironmentVariable("CS_DISPATCH_QA_DIR");
        if(!capture.isEmpty())QVERIFY(page.grab().save(capture+"/week.png"));
        auto* selectedWeekDay=qobject_cast<PlanningDayCell*>(grid->cellWidget(0,day.dayOfWeek()-1));
        QVERIFY(selectedWeekDay);QTest::mouseClick(selectedWeekDay,Qt::LeftButton);
        QTRY_VERIFY(grid->columnCount()>=41);
        QCOMPARE(grid->columnSpan(0,1),4);
        QVERIFY(!grid->findChildren<QPushButton*>("dispatchAppointment").isEmpty());
        QVERIFY(click(QStringLiteral("День")));QVERIFY(grid->columnCount()>=41);
        if(!capture.isEmpty())QVERIFY(page.grab().save(capture+"/day.png"));
        QVERIFY(click(QStringLiteral("Місяць")));QCOMPARE(grid->columnCount(),7);QVERIFY(grid->rowCount()>=4);
        if(!capture.isEmpty())QVERIFY(page.grab().save(capture+"/month.png"));
    }
    void planningDailyAndWeeklyViews() {
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE planning_calendar(day TEXT PRIMARY KEY,note TEXT)"));
        PlanningRepository repo;
        repo.setDayMap(QDate::currentDate(), {{7,"Legacy early"}, {10, QString::fromUtf8("Тестовий запис")}, {19,"Legacy late"}});
        PlanningPage page; page.resize(1400, 850); page.show();
        auto* current = page.findChild<QPushButton*>("planningCurrentMode");
        auto* weekly = page.findChild<QPushButton*>("planningWeeklyMode");
        auto* table = page.findChild<QTableWidget*>("planningDailyTable");
        QVERIFY(current); QVERIFY(weekly); QVERIFY(table);
        QVERIFY(table->isHidden()); current->click();
        QVERIFY(!table->isHidden()); QCOMPARE(table->rowCount(), 11);
        QCOMPARE(table->item(0,0)->text(), QString("08:00"));
        QCOMPARE(table->item(10,0)->text(), QString("18:00"));
        QCOMPARE(table->item(2, 1)->text(), QString::fromUtf8("Тестовий запис"));
        QVERIFY(current->isChecked()); QVERIFY(!weekly->isChecked());
        QVERIFY(page.grab().save(dir.filePath(QStringLiteral("planning-daily.png"))));
        weekly->click(); QVERIFY(table->isHidden()); QVERIFY(weekly->isChecked());
        QCOMPARE(repo.getDayMap(QDate::currentDate()).value(10), QString::fromUtf8("Тестовий запис"));
        QCOMPARE(repo.getDayMap(QDate::currentDate()).value(7), QString("Legacy early"));
        QCOMPARE(repo.getDayMap(QDate::currentDate()).value(19), QString("Legacy late"));
    }
    void planningLiveHourAndDayEmphasis() {
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE planning_calendar(day TEXT PRIMARY KEY,note TEXT)"));
        QVERIFY(q.exec("ALTER TABLE work_orders ADD COLUMN plate_norm TEXT"));
        QVERIFY(q.exec("UPDATE work_orders SET plate='AA 1234 BB',plate_norm='AA1234BB',status='in_progress',closed_at='' WHERE number=1"));
        PlanningRepository repo;
        const auto today = QDate::currentDate();
        repo.setDayMap(today, {{10, "AA 1234 BB | Oil service"}});
        PlanningPage page; page.resize(1100, 1000); page.show(); page.setDailyView(true);
        QTest::qWait(100);
        auto* table = page.findChild<QTableWidget*>("planningDailyTable"); QVERIFY(table);
        page.updatePlanningClock(QDateTime(today, QTime(10, 15)));
        QVERIFY(table->rowHeight(2) > table->rowHeight(1) * 1.4);
        QVERIFY(qAbs(table->verticalHeader()->length() - table->viewport()->height()) <= 2);
        QCOMPARE(table->item(2, 1)->toolTip(), QString::fromUtf8("Зараз · наряд у роботі"));
        const auto rendered = table->viewport()->grab().toImage();
        const auto cellRect = table->visualItemRect(table->item(2, 1));
        const auto sample = rendered.pixelColor(QPoint(cellRect.right() - 20, cellRect.center().y()) * table->devicePixelRatioF());
        QVERIFY(sample.green() > sample.red() + 15);
        QVERIFY(page.grab().save(previousWorkingDirectory + "/build-single-instance-audit/planning-live-hour.png"));
        page.updatePlanningClock(QDateTime(today, QTime(11, 0)));
        QVERIFY(table->rowHeight(3) > table->rowHeight(2));
        QVERIFY(!table->item(2, 1)->font().bold());
        QVERIFY(q.exec("UPDATE work_orders SET status='closed',closed_at='2026-10-11' WHERE number=1"));
        page.updatePlanningClock(QDateTime(today, QTime(10, 15)));
        QCOMPARE(table->item(2, 1)->toolTip(), QString::fromUtf8("Поточна година"));
        page.updatePlanningClock(QDateTime(today, QTime(22, 0)));
        for (int row = 0; row < 11; ++row) QVERIFY(!table->item(row, 0)->font().bold());
        page.setDailyView(false);
        const auto cells = page.findChildren<PlanningDayCell*>();
        PlanningDayCell* target = nullptr;
        for (auto* cell : cells) if (!cell->isHidden()) { target = cell; break; }
        QVERIFY(target);
        page.updatePlanningClock(QDateTime(target->date(), QTime(10, 0)));
        QVERIFY(target->property("current").toBool()); QVERIFY(target->graphicsEffect());
        QVERIFY(target->minimumHeight() > 150);
        QCoreApplication::processEvents();
        QVERIFY(page.grab().save(previousWorkingDirectory + "/build-single-instance-audit/planning-live-day.png"));
        page.updatePlanningClock(QDateTime(target->date().addDays(1), QTime(0, 0)));
        QVERIFY(!target->property("current").toBool()); QVERIFY(!target->graphicsEffect());
        QCOMPARE(target->minimumHeight(), 150);
    }
    void orderPartsPaymentIsAtomicAndPreservesCustomerStatus() {
        QString error;
        auto expected = PurchasePaymentService::orderPartDebts(db, 1, &error);
        QCOMPARE(expected.size(), 1);
        QVERIFY2(PurchasePaymentService::pay(db, expected.first(), 5000, &error), qPrintable(error));
        expected = PurchasePaymentService::orderPartDebts(db, 1, &error);
        QCOMPARE(expected.first().remainingCents(), 15000);
        QVERIFY2(PurchasePaymentService::payOrderParts(db, 1, expected, &error), qPrintable(error));
        QVERIFY(PurchasePaymentService::orderPartDebts(db, 1, &error).isEmpty());
        QSqlQuery q(db);
        QVERIFY(q.exec("SELECT paid FROM work_order_parts WHERE id=1")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 1);
        QVERIFY(q.exec("SELECT status,closed_at FROM work_orders WHERE number=1")); QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(), QString("open")); QCOMPARE(q.value(1).toString(), QString(""));
        QVERIFY(q.exec("SELECT COUNT(*) FROM purchase_payment_events")); QVERIFY(q.next()); const int events = q.value(0).toInt();
        QVERIFY(!PurchasePaymentService::payOrderParts(db, 1, expected, &error));
        QVERIFY(PurchasePaymentService::payOrderParts(db, 1, {}, &error));
        QVERIFY(q.exec("SELECT COUNT(*) FROM purchase_payment_events")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), events);
    }
    void groupedPartialPaymentsAndHistory() {
        QString error;
        auto expected = PurchasePaymentService::orderPartDebts(db, 1, &error);
        QVERIFY(!PurchasePaymentService::payOrderParts(db, 1, expected, &error, 20001));
        QVERIFY(!PurchasePaymentService::payOrderParts(db, 1, expected, &error, 0));
        QVERIFY(PurchasePaymentService::payOrderParts(db, 1, expected, &error, 5700));
        QVERIFY(!PurchasePaymentService::payOrderParts(db, 1, expected, &error, 5700));
        auto current = PurchasePaymentService::orderPartDebts(db, 1, &error);
        QCOMPARE(current.first().remainingCents(), 14300);
        QCOMPARE(PurchasePaymentService::history(db, current.first()).last().amountCents, 5700);
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE work_order_part_attachments(part_id INTEGER PRIMARY KEY,invoice_number TEXT,invoice_date TEXT,file_path TEXT)"));
        QVERIFY(q.exec("INSERT INTO work_order_part_attachments VALUES(1,'INV-PARTIAL','01.09.26','invoice.pdf'),(2,'INV-PARTIAL','01.09.26','invoice.pdf')"));
        auto invoice = InvoiceRegistryService::load(db, &error).first();
        QVERIFY(!PurchasePaymentService::payInvoice(db, invoice, &error, 14301));
        QVERIFY(PurchasePaymentService::payInvoice(db, invoice, &error, 3000));
        QVERIFY(!PurchasePaymentService::payInvoice(db, invoice, &error, 3000));
        invoice = InvoiceRegistryService::load(db, &error).first();
        QCOMPARE(invoice.remainingCents(), 11300);
        QVERIFY(PurchasePaymentService::payInvoice(db, invoice, &error, 11300));
        QCOMPARE(InvoiceRegistryService::load(db, &error).first().remainingCents(), 0);
        QCOMPARE(PurchasePaymentService::history(db, current.first()).size(), 3);
        QVERIFY(q.exec("SELECT status FROM work_orders WHERE number=1")); QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(), QString("open"));
    }
    void groupedPaymentDialogUsesSharedForm() {
        QString error;
        // This scenario covers an existing, empty journal; missing journal is an error.
        QVERIFY2(PurchasePaymentService::ensureSchema(db,&error),qPrintable(error));
        const auto debts = PurchasePaymentService::orderPartDebts(db, 1);
        bool inspected = false;
        QTimer::singleShot(0, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            const auto close = qScopeGuard([&]{if(dialog->isVisible())dialog->reject();});
            QCOMPARE(dialog->objectName(), QString("purchasePaymentDialog"));
            dialog->findChild<QCheckBox*>("partialPayment")->setChecked(true);
            auto* value = dialog->findChild<QDoubleSpinBox*>("partialPaymentAmount");
            QVERIFY(value); value->setValue(57);
            auto* historyButton = dialog->findChild<QPushButton*>("togglePaymentHistory");
            QVERIFY(historyButton); historyButton->click();
            QVERIFY(dialog->findChild<QTableWidget*>("paymentHistory")->isVisible());
            QCoreApplication::processEvents();
            auto* remaining = dialog->findChild<QLabel*>("paymentRemaining");
            auto* heading = dialog->findChild<QLabel*>("paymentHeading");
            QVERIFY(remaining); QVERIFY(heading);
            QVERIFY(remaining->parentWidget()->layout()->indexOf(remaining) >= 0);
            const QRect remainingRect(remaining->mapTo(dialog, QPoint()), remaining->size());
            const QRect headingRect(heading->mapTo(dialog, QPoint()), heading->size());
            QVERIFY(!remainingRect.intersects(headingRect));
            const auto output = qEnvironmentVariable("CS_TEST_SCREENSHOTS");
            if (!output.isEmpty()) {
                QCoreApplication::processEvents();
                QVERIFY(dialog->grab().save(QDir(output).filePath("grouped-payment.png")));
            }
            inspected = true;
            dialog->findChild<QPushButton*>("payInFull")->click();
        });
        qint64 amount = 0;
        QVERIFY(chooseGroupedPurchasePayment(nullptr, QString::fromUtf8("Оплата наряду №1"), debts, &amount));
        QVERIFY(inspected); QCOMPARE(amount, 5700);
        QCOMPARE(PurchasePaymentService::orderPartDebts(db, 1).first().remainingCents(), 20000);
        QTimer::singleShot(0, [] {
            if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) dialog->reject();
        });
        QVERIFY(!chooseGroupedPurchasePayment(nullptr, QString::fromUtf8("Оплата наряду №1"), debts, &amount));
        QCOMPARE(PurchasePaymentService::orderPartDebts(db, 1).first().remainingCents(), 20000);
    }
    void paidPurchaseShowsInformationInsteadOfPaymentForm() {
        auto debt = PurchasePaymentService::orderPartDebts(db, 1).first();
        debt.paidCents = debt.totalCents;
        bool informed = false;
        QTimer::singleShot(0, [&] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            QVERIFY(box);
            informed = box->text().contains(QString::fromUtf8("Оплата вже здійснена"));
            box->accept();
        });
        qint64 amount = 0;
        QVERIFY(!choosePurchasePayment(nullptr, debt, false, &amount));
        QVERIFY(informed);
        QCOMPARE(PurchasePaymentService::orderPartDebts(db, 1).first().remainingCents(), 20000);
        informed = false;
        QTimer::singleShot(0, [&] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            QVERIFY(box);
            informed = box->text().contains(QString::fromUtf8("успішно збережено"));
            box->accept();
        });
        showPurchasePaymentSaved(nullptr, 5700);
        QVERIFY(informed);
    }
    void orderPartsPaymentRollsBackAndRejectsStaleSnapshot() {
        QString error;
        auto expected = PurchasePaymentService::orderPartDebts(db, 1, &error);
        QSqlQuery q(db);
        QVERIFY(q.exec("UPDATE work_order_parts SET paid=0 WHERE id=2"));
        QVERIFY(!PurchasePaymentService::payOrderParts(db, 1, expected, &error));
        QVERIFY(q.exec("SELECT paid FROM work_order_parts WHERE id=1")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 0);
        expected = PurchasePaymentService::orderPartDebts(db, 1, &error);
        QCOMPARE(expected.size(), 2);
        QVERIFY(q.exec("CREATE TRIGGER fail_second_payment BEFORE UPDATE OF paid ON work_order_parts WHEN NEW.id=2 BEGIN SELECT RAISE(ABORT,'test failure'); END"));
        QVERIFY(!PurchasePaymentService::payOrderParts(db, 1, expected, &error));
        QVERIFY(q.exec("SELECT SUM(paid) FROM work_order_parts")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 0);
        QVERIFY(q.exec("SELECT COUNT(*) FROM purchase_payment_events")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 0);
        ActivityAuditService::setCurrentActor({"master","Master","master","test"});
        QVERIFY(!PurchasePaymentService::payOrderParts(db, 1, expected, &error));
    }
    void warehousePurchaseIsNotPaidTwiceFromOrder() {
        QSqlQuery q(db);
        QVERIFY(q.exec("UPDATE work_order_parts SET store='Склад СТО' WHERE id=1"));
        QString error;
        QVERIFY(PurchasePaymentService::orderPartDebts(db, 1, &error).isEmpty());
        QVERIFY(PurchasePaymentService::payOrderParts(db, 1, {}, &error));
        QVERIFY(q.exec("SELECT paid FROM work_order_parts WHERE id=1")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 0);
    }
    void closedMobileOrderMarkerIsShownInHistoryWithoutChangingStoredStatus() {
        HistoryPage history;
        auto* model=history.findChild<TabularSnapshotTableModel*>();
        auto* table=history.findChild<QTableView*>();
        QVERIFY(model); QVERIFY(table);
        const QString closedAt="2026-10-02T21:30:00+03:00";
        const QString state=QStringLiteral("{\"workflow\":{\"ready_completed_at\":\"2026-10-02T21:30:00+03:00\",\"ready_completed_by\":\"m1\",\"ready_completed_role\":\"master\",\"ready_completed_source\":\"mobile\"}}");
        model->setSnapshot({{QString::fromUtf8("№"),QString::fromUtf8("Відкрито"),QString::fromUtf8("Закрито"),QString::fromUtf8("Статус"),
            QString::fromUtf8("Номер"),QString::fromUtf8("Авто"),QString::fromUtf8("Клієнт"),QString::fromUtf8("Майстер"),
            QString::fromUtf8("Роботи"),QString::fromUtf8("Запчастини"),"__mobile_state","__internal_number"},
            {{468,"2026-09-23",closedAt,"completed","AA1234AA","Volvo","Test","Master",3800,1300,state,1}}, {}});
        QCOMPARE(model->data(model->index(0,3)).toString(),QStringLiteral("completed"));
        QVERIFY(table->model()->data(table->model()->index(0,3)).toString().contains(QString::fromUtf8("Закрито майстром")));
        QVERIFY(table->model()->data(table->model()->index(0,3),Qt::ToolTipRole).toString().contains(QString::fromUtf8("Закрито майстром")));
        bool menuSeen=false;
        QTimer::singleShot(50,&history,[&]{
            auto* menu=history.findChild<QMenu*>("historyStatusMenu");
            if(!menu)return;
            menuSeen=menu->actions().size()==2
                && menu->actions()[0]->text()==QString::fromUtf8("Відкритий")
                && menu->actions()[1]->text()==QString::fromUtf8("В роботі");
            menu->close();
        });
        table->clicked(table->model()->index(0,3));
        QVERIFY(menuSeen);
        QSqlQuery unchanged(db);QVERIFY(unchanged.exec("SELECT status FROM work_orders WHERE number=1"));
        QVERIFY(unchanged.next());QCOMPARE(unchanged.value(0).toString(),QString("open"));
    }
    void splashHandsOffToMainWindow() {
        QLoggingCategory::setFilterRules("*.debug=false");
        SplashScreen splash;
        splash.start(6000);
        splash.repaint();
        MainWindow window;
        bool hiddenAtHandoff=false;
        int handoffs=0;
        connect(&splash,&SplashScreen::finished,&window,[&]{
            hiddenAtHandoff=!splash.isVisible();
            ++handoffs;
            window.showMaximized();
        });
        QVERIFY(!window.isVisible());
        splash.setStartupReady();
        QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(),15000);
        QVERIFY(hiddenAtHandoff);
        QCOMPARE(handoffs,1);
        QVERIFY(!splash.isVisible());
        auto* pages=window.findChild<QStackedWidget*>("pages");
        QVERIFY(pages && pages->currentWidget());
        const auto output=qEnvironmentVariable("CS_SPLASH_OUTPUT");
        if(!output.isEmpty()) {
            QDir().mkpath(output);
            QVERIFY(window.grab().save(output+"/04-main-window.png"));
        }
        QTest::qWait(200);
        QCOMPARE(handoffs,1);
        window.close();
    }

    void debtWindowAsyncLifecycle() {
        QLoggingCategory::setFilterRules("*.debug=false");
        QString error;
        auto debts=PurchasePaymentService::debts(db,&error);
        auto facts=SupplierDebtReconciliationService::load(db,&error);
        debts=SupplierInvoiceLedger::reportingDebts(db,debts,&facts,&error);
        QVERIFY2(error.isEmpty(),qPrintable(error));
        QMap<QString,QString> expected;
        for(const auto& d:debts) expected[PurchasePaymentService::key(d.source,d.id,d.orderUid)]=FormatUtils::money(d.remainingCents()/100.0,2);
        PurchaseDebtsPage page;
        page.refresh();
        QPointer<QThread> first=page.m_worker;
        QVERIFY(first);
        for(int i=0;i<8;++i) { page.refresh(); QCOMPARE(page.m_worker.data(),first.data()); }
        QTRY_VERIFY_WITH_TIMEOUT(!page.property("debtDataLoading").toBool(),30000);
        QVERIFY2(page.property("debtLoadError").toString().isEmpty(),qPrintable(page.property("debtLoadError").toString()));
        QMap<QString,QString> actual;
        auto* table=page.findChild<QTableView*>("purchaseDebtsTable");
        for(int i=0;i<table->model()->rowCount();++i) {
            const auto item=table->model()->index(i,0);
            if(!item.data(Qt::UserRole).isValid())continue;
            actual[PurchasePaymentService::key(item.data(Qt::UserRole+1).toString(),item.data(Qt::UserRole).toLongLong(),item.data(Qt::UserRole+2).toString())]=table->model()->index(i,7).data().toString();
        }
        QCOMPARE(actual,expected);
        PartsPage totalsPage;
        totalsPage.updateDebtBanner();
        QTRY_VERIFY_WITH_TIMEOUT(!totalsPage.property("debtDataLoading").toBool(),10000);
        QVERIFY(totalsPage.property("debtLoadError").toString().isEmpty());
        const auto totals=SupplierInvoiceLedger::summary(db,debts,facts,&error).totals;
        QStringList expectedAmounts,actualAmounts;
        qint64 total=0;
        for(auto it=totals.begin();it!=totals.end();++it) if(it.value()>0) {
            total+=it.value(); expectedAmounts<<FormatUtils::money(it.value()/100.0,2)+QString::fromUtf8(" грн");
        }
        if(expectedAmounts.size()<=3) expectedAmounts<<FormatUtils::money(total/100.0,2)+QString::fromUtf8(" грн");
        for(auto* label:totalsPage.findChildren<QLabel*>("partsSupplierAmount")) actualAmounts<<label->text();
        expectedAmounts.sort(); actualAmounts.sort(); QCOMPARE(actualAmounts,expectedAmounts);
        // An owner result must not reappear after a session switch.
        page.refresh();
        ActivityAuditService::setCurrentActor({"unlinked","Unlinked","master","new-session"});
        page.refresh();
        QCOMPARE(table->model()->rowCount(),0);
        QTRY_VERIFY_WITH_TIMEOUT(!page.property("debtDataLoading").toBool(),30000);
        QCOMPARE(table->model()->rowCount(),0);
        ActivityAuditService::setCurrentActor({"owner","Owner","owner","test"});
        // Close while reads are in flight: no join on the GUI thread or dangling UI callback.
        auto* closing=new PurchaseDebtsPage;
        closing->refresh();
        QPointer<QThread> debtWorker=closing->m_worker;
        delete closing;
        QTRY_VERIFY_WITH_TIMEOUT(debtWorker.isNull(),10000);
        auto* parts=new PartsPage;
        parts->updateDebtBanner();
        QPointer<QThread> partsWorker=parts->m_debtWorker;
        QVERIFY(partsWorker);
        for(int i=0;i<8;++i) { parts->updateDebtBanner(); QCOMPARE(parts->m_debtWorker.data(),partsWorker.data()); }
        delete parts;
        QTRY_VERIFY_WITH_TIMEOUT(partsWorker.isNull(),10000);
    }

    void performanceAuditWorkingCopy() {
        if(qEnvironmentVariable("CS_TAB_AUDIT_COPY").isEmpty() || qEnvironmentVariable("CS_PERF_OUTPUT").isEmpty())
            QSKIP("Explicit isolated performance dataset and output required");
        QVERIFY(QFileInfo(db.databaseName()).absoluteFilePath().startsWith(App::sessionDir()));
        QCOMPARE(PerformanceAudit::runAudit(db,qEnvironmentVariable("CS_PERF_OUTPUT")),0);
    }
    void initTestCase() {
        if(qEnvironmentVariable("CS_FINANCIAL_AUDIT")=="1")
            QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::HttpProxy,QStringLiteral("127.0.0.1"),9));
        for(const auto& name:{"segoeui.ttf","times.ttf"}) {
            const auto path=qEnvironmentVariable("WINDIR")+"/Fonts/"+name;
            if(QFile::exists(path)) QVERIFY(QFontDatabase::addApplicationFont(path)>=0);
        }
        previousWorkingDirectory = QDir::currentPath();
        QVERIFY(QDir::setCurrent(dir.path()));
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("CarServiceStage2Regression");
        QCoreApplication::setApplicationName("Safety-" + QUuid::createUuid().toString(QUuid::Id128));
        isolatedRuntimeProfiles.append(App::dataDir());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
        if(!qEnvironmentVariable("CS_TEST_DATA_ROOT").isEmpty()) {
            const auto root=QFileInfo(qEnvironmentVariable("CS_TEST_DATA_ROOT")).canonicalFilePath();
            QVERIFY(!root.isEmpty());
            QVERIFY(QFileInfo(dir.path()).canonicalFilePath().startsWith(root+"/"));
            QVERIFY(QFileInfo(App::dataDir()).canonicalFilePath().startsWith(root+"/"));
            QVERIFY(QFileInfo(QSettings().fileName()).absoluteFilePath().startsWith(root+"/"));
            qInfo()<<"ISOLATION_VERIFIED"<<root<<dir.path()<<App::dataDir()<<QSettings().fileName();
        }
    }
    void init() {
        qputenv("CS_ELIT_DISABLE_STARTUP","1");
        ActivityAuditService::clearCurrentActor();
        PartsQueryService::invalidateCache();
        const QString auditCopy=qEnvironmentVariable("CS_TAB_AUDIT_COPY");
        if(!auditCopy.isEmpty()) {
            QVERIFY(QDir().mkpath(App::sessionDir()));
            for(const auto& file:QDir(auditCopy).entryList({"*.db","*.db-wal","*.db-shm"},QDir::Files))
                QVERIFY(QFile::copy(QDir(auditCopy).filePath(file),QDir(App::sessionDir()).filePath(file)));
            for(const auto& file:QDir(App::sessionDir()).entryList({"*.db"},QDir::Files)) {
                {
                    auto local=QSqlDatabase::addDatabase("QSQLITE","audit_relocate");
                    local.setDatabaseName(QDir(App::sessionDir()).filePath(file));
                    QVERIFY(local.open());
                    const auto tables=local.tables();
                    for(const auto& table:tables) {
                        const auto record=local.record(table);
                        for(const auto& column:{QString("storage_path"),QString("source_db")}) {
                            if(record.indexOf(column)<0) continue;
                            QSqlQuery paths(local);
                            QVERIFY(paths.exec("SELECT DISTINCT \""+column+"\" FROM \""+table+"\" WHERE \""+column+"\"<>''"));
                            QStringList oldPaths;
                            while(paths.next()) oldPaths.append(paths.value(0).toString());
                            paths.finish();
                            for(const auto& old:oldPaths) {
                                const auto replacement=QDir(App::sessionDir()).filePath(QFileInfo(old).fileName());
                                QVERIFY2(QFileInfo::exists(replacement),qPrintable("Missing copied database: "+QFileInfo(old).fileName()));
                                QSqlQuery update(local);
                                update.prepare("UPDATE \""+table+"\" SET \""+column+"\"=? WHERE \""+column+"\"=?");
                                update.addBindValue(replacement);update.addBindValue(old);QVERIFY(update.exec());
                            }
                        }
                    }
                }
                QSqlDatabase::removeDatabase("audit_relocate");
            }
            QString error;
            QVERIFY2(Database::prepareRuntime(&error),qPrintable(error));
            QVERIFY2(Database::openPrepared(&error),qPrintable(error));
            if(!qEnvironmentVariable("CS_PERF_OUTPUT").isEmpty())
                QVERIFY2(Database::runStartupSchemaCheck(&error),qPrintable(error));
            db=Database::db();
            Database::setSetting("settings.booking.sync.enabled",false);
            Database::setSetting("settings.cloudsync.enabled",false);
            Database::setSetting("settings.cloudsync.api_token",QString());
            QSettings().setValue("EmailSync/enabled",false);
            QSqlQuery owner(db);
            QVERIFY(owner.exec("SELECT id FROM staff_accounts WHERE role='owner' AND active=1 LIMIT 1"));
            QVERIFY(owner.next());
            ActivityAuditService::setCurrentActor({owner.value(0).toString(),"Audit","owner","test"});
            return;
        }
        // UI workers must open the same prepared runtime as the main thread.
        // Each scenario still starts with an independent empty fixture.
        QCoreApplication::setApplicationName("Safety-Purchase-" + QUuid::createUuid().toString(QUuid::Id128));
        isolatedRuntimeProfiles.append(App::dataDir());
        QString runtimeError;
        QVERIFY2(Database::prepareRuntime(&runtimeError),qPrintable(runtimeError));
        QVERIFY2(Database::openPrepared(&runtimeError),qPrintable(runtimeError));
        db=Database::db();
        QCOMPARE(QFileInfo(db.databaseName()).absoluteFilePath(),QFileInfo(App::activeDbFilePath()).absoluteFilePath());
        QSqlQuery q(db);
        const QStringList sql={
            "CREATE TABLE work_orders(number INTEGER PRIMARY KEY,order_number INTEGER,order_uid TEXT,plate TEXT,car TEXT,vin TEXT,status TEXT,opened_at TEXT,closed_at TEXT)",
            "INSERT INTO work_orders VALUES(1,1,'order-a','AA1234AA','Audi','','open','2026-09-01','')",
            "CREATE VIEW router_work_orders AS SELECT * FROM work_orders",
            "CREATE TABLE settings(key TEXT PRIMARY KEY,value TEXT)",
            "CREATE TABLE expenses(id INTEGER PRIMARY KEY,period_from TEXT,period_to TEXT,elec_cost REAL,elec_curr REAL,elec_prev REAL,elec_tariff REAL,cleaning REAL,sw_opendatabot REAL,sw_tecalliance REAL,heating REAL,amortization REAL,rent REAL,other_amount REAL,cleaning_note TEXT,software_note TEXT,heating_note TEXT,amortization_note TEXT,rent_note TEXT,other_note TEXT)",
            "CREATE TABLE staff_accounts(id TEXT PRIMARY KEY,master_id TEXT DEFAULT '',display_name TEXT DEFAULT '',role TEXT DEFAULT 'master',pin_hash TEXT DEFAULT '',active INTEGER DEFAULT 1,created_at TEXT DEFAULT '',updated_at TEXT DEFAULT '')",
            "INSERT INTO staff_accounts(id,master_id,role) VALUES('owner','','owner'),('test-owner','','owner'),('master','','master'),('account-1','m1','master'),('account-2','m2','master'),('unlinked','','master')",
            "CREATE TABLE work_order_parts(id INTEGER PRIMARY KEY,order_number INTEGER,order_uid TEXT,ordered_at TEXT,delivery_date TEXT,installed_at TEXT,store TEXT,part_number TEXT,manufacturer TEXT,name TEXT,qty REAL,price REAL,amount REAL,purchase_price REAL,purchase_amount REAL,paid INTEGER)",
            "INSERT INTO work_order_parts VALUES(1,1,'order-a','2026-09-01','','','Store','OEM','Brand','Part',2,125,250,100,200,0)",
            "INSERT INTO work_order_parts VALUES(2,1,'order-a','2026-09-01','','','Store','','','Paid part',1,200,200,100,100,1)"
        };
        for(const auto& statement:sql) QVERIFY2(q.exec(statement),qPrintable(q.lastError().text()));
        // These partial DAO fixtures still represent a prepared runtime order.
        QVERIFY(prepareFixtureOrderState(1));
        QString error;
        QVERIFY2(AccessControlService::ensureSchema(db,&error),qPrintable(error));
        ActivityAuditService::setCurrentActor({"owner","Owner","owner","test"});
        QVERIFY2(WorkshopInventoryService::ensureSchema(db,&error),qPrintable(error));
        QVERIFY(Database::setSetting("settings.sounds.elit_voice",false));
        QVERIFY(Database::setSetting("settings.booking.sync.enabled",false));
        QVERIFY(Database::setSetting("settings.cloudsync.enabled",false));
        QVERIFY(Database::setSetting("settings.booking.api.token",QString()));
        QSettings().setValue("EmailSync/enabled",false);
        ThemeManager::applySavedTheme();
    }
    void cleanup() {
        // Page destruction requests cancellation; its readers finish asynchronously.
        // Keep their profile and actor alive until the workers actually release it.
        QTRY_COMPARE(PerfWatchdog::activeAsyncJobs(), 0);
        ActivityAuditService::clearCurrentActor();
        PartsQueryService::invalidateCache();
        db.close(); db={}; Database::close();
    }
    void cleanupTestCase() {
        QVERIFY(QDir::setCurrent(previousWorkingDirectory));
        const QString root = App::dataDir();
        if(qEnvironmentVariable("CS_TEST_DATA_ROOT").isEmpty())
            QVERIFY(root == QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
        else QVERIFY(QFileInfo(root).canonicalFilePath().startsWith(QFileInfo(qEnvironmentVariable("CS_TEST_DATA_ROOT")).canonicalFilePath()+"/"));
        QVERIFY(QFileInfo(root).fileName().startsWith("Safety-"));
        if(!qEnvironmentVariable("CS_TAB_AUDIT_COPY").isEmpty()) {
            for(const auto& name:QSqlDatabase::connectionNames()) {
                { auto connection=QSqlDatabase::database(name,false);connection.close(); }
                QSqlDatabase::removeDatabase(name);
            }
        }
        QVERIFY(QDir(root).removeRecursively());
        for (const QString& profile : isolatedRuntimeProfiles) {
            if (!QFileInfo::exists(profile)) continue;
            const QFileInfo info(profile);
            QVERIFY(info.fileName().startsWith("Safety-"));
            QVERIFY(info.absolutePath() == QFileInfo(root).absolutePath());
            QVERIFY(QDir(profile).removeRecursively());
        }
    }
    void selectedOrderSettlementFollowsRow() {
        QSqlQuery q(db);
        for (const auto& sql : QStringList{
            "ALTER TABLE work_orders ADD COLUMN discount REAL DEFAULT 0",
            "ALTER TABLE work_orders ADD COLUMN discount_mode TEXT DEFAULT 'uah'",
            "CREATE TABLE work_order_services(order_uid TEXT,order_number INTEGER,qty REAL,price REAL,amount REAL)",
            "CREATE VIEW router_work_order_services AS SELECT * FROM work_order_services",
            "CREATE VIEW router_work_order_parts AS SELECT * FROM work_order_parts",
            "CREATE TABLE client_payments(order_uid TEXT,amount_cents INTEGER)",
            "INSERT INTO client_payments VALUES('order-a',15000)",
            "INSERT INTO work_orders(number,order_number,order_uid,status) VALUES(2,2,'order-b','open')"})
            QVERIFY2(q.exec(sql),qPrintable(q.lastError().text()));
        QVERIFY(prepareFixtureOrderState(2));
        CurrentOrdersPage page;
        auto* model=page.findChild<TabularSnapshotTableModel*>();
        auto* table=page.findChild<QTableView*>("premiumCurrentOrdersTable");
        QVERIFY(model && table);
        QSignalSpy summary(&page,&CurrentOrdersPage::settlementChanged);
        model->setSnapshot({{"№","__internal_number","__order_uid"},{{1,1,"order-a"},{2,2,"order-b"}},{}});
        table->selectRow(0);
        QVERIFY(!summary.isEmpty());
        const auto text=summary.last().first().toString();
        QVERIFY2(text.contains("450,00 грн") && text.contains("150,00 грн") && text.contains("300,00 грн"),qPrintable(text));
        table->selectRow(1);
        QVERIFY(summary.last().first().toString().startsWith("№2"));
        QVERIFY(summary.last().first().toString().contains("немає даних"));
        table->clearSelection();
        QVERIFY(summary.last().first().toString().contains("Оберіть"));
    }
    void masterToolbarHidesPdfAndImport() {
        for (const auto& role : {QString("master"),QString("owner")}) {
            ActivityAuditService::setCurrentActor({role,role,role,"test"});
            CurrentOrdersPage page;
            int checked = 0;
            for (auto* button : page.findChildren<QToolButton*>()) {
                if (button->text().contains("PDF") || button->text() == QString::fromUtf8("Імпорт накладної")) {
                    QCOMPARE(button->isHidden(),role == "master");
                    ++checked;
                }
            }
            QCOMPARE(checked,2);
        }
    }
    void currentStatusReadAndDeletionGuards() {
        QString status,error;
        QVERIFY2(OrderDialogReadQueryService::loadCurrentStatus(1,&status,&error),qPrintable(error));
        QVERIFY(!OrderDialogReadQueryService::loadCurrentStatus(99999,&status,&error));
        QVERIFY(status.isEmpty()); QVERIFY(!error.isEmpty());
        QSqlQuery query(db);
        QVERIFY(query.exec("ALTER TABLE work_orders RENAME COLUMN status TO unavailable_status"));
        QVERIFY(!OrderDialogReadQueryService::loadCurrentStatus(1,&status,&error));
        QVERIFY(status.isEmpty()); QVERIFY(!error.isEmpty());
        QVERIFY(query.exec("ALTER TABLE work_orders RENAME COLUMN unavailable_status TO status"));
        QVERIFY(OrderDialogReadQueryService::loadCurrentStatus(1,&status,&error));
        QVERIFY(OrderClosureApplicationService::deletionBlockReason("order-a").isEmpty());
        QVERIFY(query.exec("CREATE TABLE IF NOT EXISTS order_closure_commands(order_uid TEXT,state TEXT)"));
        QVERIFY(query.exec("INSERT INTO order_closure_commands(order_uid,state) VALUES('order-a','pending')"));
        QVERIFY(!OrderClosureApplicationService::deletionBlockReason("order-a").isEmpty());
        QVERIFY(query.exec("DELETE FROM order_closure_commands WHERE order_uid='order-a'"));
        QVERIFY(OrderClosureApplicationService::deletionBlockReason("order-a").isEmpty());
        ActivityAuditService::setCurrentActor({"master","Master","master","test"});
        const auto reply=OrderClosureApplicationService::localRequest("snapshot",{});
        QVERIFY(!reply.ok); QCOMPARE(reply.status,403);
    }
    void supplierDebtReadBoundaryIsolationAndFailure() {
        QSqlQuery q(db);QString error;
        QVERIFY(q.exec("ALTER TABLE work_orders ADD COLUMN master_id TEXT"));
        QVERIFY(q.exec("UPDATE work_orders SET master_id='m1'"));
        QVERIFY(q.exec("INSERT INTO work_orders(number,order_uid,master_id,status) VALUES(2,'order-b','m2','open')"));
        QVERIFY(prepareFixtureOrderState(2));
        const auto payments=PurchasePaymentService::balances(db,&error);QVERIFY(error.isEmpty());
        const auto connections=QSqlDatabase::connectionNames();
        std::atomic_bool cancel(false);
        auto banner=SupplierDebtReadService::prepareBanner();
        const auto totals=banner(cancel,&error);QVERIFY2(error.isEmpty(),qPrintable(error));
        QCOMPARE(totals.value("Store"),qint64(20000));
        ActivityAuditService::setCurrentActor({"account-2","Master 2","master","read"});
        auto restricted=SupplierDebtReadService::prepare();
        auto snapshot=restricted(cancel);QVERIFY2(snapshot.error.isEmpty(),qPrintable(snapshot.error));
        QVERIFY(snapshot.restricted);QVERIFY(snapshot.debts.isEmpty());
        QVERIFY(snapshot.reconciliations.isEmpty());QVERIFY(snapshot.ledger.totals.isEmpty());
        ActivityAuditService::setCurrentActor({"account-1","Master 1","master","read"});
        auto scopedTask=SupplierDebtReadService::prepare();
        snapshot=scopedTask(cancel);QVERIFY2(snapshot.error.isEmpty(),qPrintable(snapshot.error));
        QCOMPARE(snapshot.debts.size(),1);QCOMPARE(snapshot.debts.first().orderUid,QString("order-a"));
        QVERIFY(!SupplierDebtReadService::reconciliationInput().error.isEmpty());
        ActivityAuditService::setCurrentActor({"owner","Owner","owner","read"});
        // Prepared scope is immutable even when the UI session changes.
        snapshot=restricted(cancel);QVERIFY(snapshot.debts.isEmpty());QVERIFY(snapshot.restricted);
        cancel.store(true);snapshot=scopedTask(cancel);QVERIFY(snapshot.debts.isEmpty());
        QVERIFY(banner(cancel,&error).isEmpty());cancel.store(false);
        QVERIFY(q.exec("ALTER TABLE work_order_parts RENAME COLUMN qty TO unavailable_qty"));
        PartsQueryService::invalidateCache();
        snapshot=scopedTask(cancel);QVERIFY(!snapshot.error.isEmpty());QVERIFY(snapshot.debts.isEmpty());
        QVERIFY(banner(cancel,&error).isEmpty());QVERIFY(!error.isEmpty());
        QVERIFY(q.exec("ALTER TABLE work_order_parts RENAME COLUMN unavailable_qty TO qty"));
        PartsQueryService::invalidateCache();
        QCOMPARE(QSqlDatabase::connectionNames(),connections);
        error.clear();QCOMPARE(PurchasePaymentService::balances(db,&error),payments);QVERIFY(error.isEmpty());
    }
    void masterScopeAndDebtTotals() {
        QSqlQuery q(db);
        QVERIFY(q.exec("ALTER TABLE work_orders ADD COLUMN master_id TEXT"));
        QVERIFY(q.exec("UPDATE work_orders SET master_id='m1'"));
        QVERIFY(q.exec("INSERT INTO work_orders(number,order_uid,master_id,status) VALUES(2,'order-b','m2','open')"));
        QVERIFY(prepareFixtureOrderState(2));
        ActivityAuditService::setCurrentActor({"account-1","Master 1","master","test"});
        const auto scope = AccessControlService::currentOrderScope();
        QVERIFY(scope.restricted);
        QString status,error;
        QVERIFY(OrderDialogReadQueryService::loadCurrentStatus(1,&status,&error));
        QVERIFY(!OrderDialogReadQueryService::loadCurrentStatus(2,&status,&error));
        QVERIFY(status.isEmpty()); QVERIFY(!error.isEmpty());
        QVERIFY(scope.contains("order-a",1));
        QVERIFY(!scope.contains("order-b",1)); // A reused display number must not grant access.
        QVERIFY(!scope.contains("",2));
        QVERIFY(scope.contains("",1));
        int count = -1; double sum = -1;
        QVERIFY(MainWindowMetricsService::loadUnpaidPurchasesSummary(count,sum));
        QCOMPARE(count,1); QCOMPARE(sum,200.0);
        ActivityAuditService::setCurrentActor({"account-2","Master 2","master","test"});
        // A captured worker scope must not change when the active user changes.
        QVERIFY(MainWindowMetricsService::loadUnpaidPurchasesSummary(count,sum,scope));
        QCOMPARE(count,1); QCOMPARE(sum,200.0);
        QVERIFY(MainWindowMetricsService::loadUnpaidPurchasesSummary(count,sum));
        QCOMPARE(count,0); QCOMPARE(sum,0.0);
        QVERIFY(!AccessControlService::masterOrderScope(db,"unlinked").contains("order-a",1));
        QVERIFY(!AccessControlService::masterOrderScope(db,"missing").contains("order-a",1));
        ActivityAuditService::setCurrentActor({"owner","Owner","owner","test"});
        QVERIFY(!AccessControlService::currentOrderScope().restricted);
        QVERIFY(MainWindowMetricsService::loadUnpaidPurchasesSummary(count,sum));
        QCOMPARE(count,1); QCOMPARE(sum,200.0);
    }
    void sessionChangeClearsOrderModelsImmediately() {
        CurrentOrdersPage current;
        HistoryPage history;
        for (auto* page : {static_cast<QWidget*>(&current),static_cast<QWidget*>(&history)}) {
            auto* model=page->findChild<TabularSnapshotTableModel*>();
            QVERIFY(model);
            model->setSnapshot({{"Foreign order"},{{"Other employee data"}}, {}});
            QCOMPARE(model->rowCount(),1);
        }
        ActivityAuditService::setCurrentActor({"account-2","Second","master","new-session"});
        current.refresh(); history.refresh();
        QCOMPARE(current.findChild<TabularSnapshotTableModel*>()->rowCount(),0);
        QCOMPARE(history.findChild<TabularSnapshotTableModel*>()->rowCount(),0);
    }
    void hiddenPartsRefreshDoesNotDisablePainting_data() {
        QTest::addColumn<QString>("role");
        QTest::newRow("owner") << QString("owner");
        QTest::newRow("master") << QString("master");
    }
    void hiddenPartsRefreshDoesNotDisablePainting() {
        QFETCH(QString,role);
        QSqlQuery account(db);
        account.prepare("UPDATE staff_accounts SET role=? WHERE id='account-1'"); account.addBindValue(role);
        QVERIFY(account.exec());
        ActivityAuditService::setCurrentActor({"account-1","First",role,"test"});
        QStackedWidget stack;
        auto* page = new PartsPage;
        stack.addWidget(page);
        stack.addWidget(new QWidget);
        stack.resize(1280,800);
        stack.show();
        const QVector<PartRowSnapshot> rows{PartsQueryService::loadRowById(db,1)};
        page->m_lastAppliedFrom = QDate(2026,9,1);
        page->m_lastAppliedTo = QDate(2026,10,2);
        for (int cycle=0; cycle<12; ++cycle) {
            stack.setCurrentIndex(1);
            PageFreezeController::apply(&stack,1);
            QVERIFY(!page->m_table->updatesEnabled());
            // A queued worker result arrives after navigating away.
            bool applied = false;
            QTimer::singleShot(0,page,[&]() {
                page->applyAsyncSnapshot(rows,{},QString(),1,1,"order-a",0,0);
                QVERIFY(page->applyIncrementalPaidToggle(1,false));
                applied = true;
            });
            QTRY_VERIFY(applied);
            stack.setCurrentIndex(0);
            PageFreezeController::apply(&stack,0);
            QVERIFY(page->m_table->updatesEnabled());
            QVERIFY(page->m_table->viewport()->updatesEnabled());
            QVERIFY(page->m_table->horizontalHeader()->updatesEnabled());
            QVERIFY(page->m_table->isVisible());
            QCOMPARE(page->m_table->model()->rowCount(),1);
            QVERIFY(!page->m_table->testAttribute(Qt::WA_ForceUpdatesDisabled));
        }
        // An intentional table-local update lock must still be preserved.
        page->m_table->setUpdatesEnabled(false);
        page->populateRows(rows);
        QVERIFY(page->m_table->testAttribute(Qt::WA_ForceUpdatesDisabled));
        page->m_table->setUpdatesEnabled(true);
    }
    void masterPartsKeepDeadlineHighlighting() {
        auto row = PartsQueryService::loadRowById(db,1);
        row.orderStatus = "open";
        row.orderClosedAtRaw.clear();
        row.deliveryDateRaw = "2026-10-01";
        row.dateRaw = "2026-10-01";
        auto warning = PartsPageTableModelAdapter::adaptRow(row,QDate(2026,10,4),false);
        auto critical = PartsPageTableModelAdapter::adaptRow(row,QDate(2026,10,7),false);
        QCOMPARE(warning.overdueSeverity,1);
        QCOMPARE(critical.overdueSeverity,2);
        row.paid = true;
        QCOMPARE(PartsPageTableModelAdapter::adaptRow(row,QDate(2026,10,7),false).overdueSeverity,2);
        QCOMPARE(PartsPageTableModelAdapter::adaptRow(row,QDate(2026,10,7),true).overdueSeverity,0);
        row.installedAtRaw = "2026-10-02";
        QCOMPARE(PartsPageTableModelAdapter::adaptRow(row,QDate(2026,10,7),false).overdueSeverity,0);
    }
    void planningHidesOtherMastersAndPreservesOccupiedSlots() {
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE masters(id TEXT PRIMARY KEY,name TEXT)"));
        QVERIFY(q.exec("INSERT INTO masters VALUES('m1','First'),('m2','Second')"));
        QVERIFY(q.exec("CREATE TABLE planning_calendar(day TEXT PRIMARY KEY,note TEXT)"));
        QVERIFY(q.exec("INSERT INTO planning_calendar VALUES('02.10.26','09:00 AA | Car | 2000 | Client | Phone | Work | First | normal | note' || char(10) || '10:00 BB | Car | 2000 | Client | Phone | Work | Second | normal | note' || char(10) || '11:00 Unassigned old record')"));
        PlanningRepository repo;
        const QDate day(2026,10,2);
        ActivityAuditService::setCurrentActor({"account-1","First","master","test"});
        QCOMPARE(repo.visibleDayMap(day).size(),1);
        QVERIFY(repo.visibleDayMap(day).contains(9));
        QCOMPARE(repo.visibleMonthMaps(2026,10).value(day).size(),1);
        auto mine=repo.visibleDayMap(day);
        mine[10]="attempt to overwrite";
        repo.setVisibleDayMap(day,mine);
        QVERIFY(repo.getDayMap(day).value(10).contains("Second"));
        mine.remove(10); mine[12]="New appointment";
        repo.setVisibleDayMap(day,mine);
        QCOMPARE(repo.visibleDayMap(day).size(),2);
        QCOMPARE(repo.getDayMap(day).size(),4);
        repo.setVisibleDayMap(day,{});
        QCOMPARE(repo.getDayMap(day).size(),2);
        QVERIFY(repo.getDayMap(day).contains(10)); QVERIFY(repo.getDayMap(day).contains(11));
        ActivityAuditService::setCurrentActor({"account-2","Second","master","test"});
        QCOMPARE(repo.visibleDayMap(day).size(),1); QVERIFY(repo.visibleDayMap(day).contains(10));
        ActivityAuditService::setCurrentActor({"owner","Owner","owner","test"});
        QCOMPARE(repo.visibleDayMap(day).size(),2);
    }
    void planningUsesExplicitMasterIdEvenWithDuplicateNames() {
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE masters(id TEXT PRIMARY KEY,name TEXT)"));
        QVERIFY(q.exec("INSERT INTO masters VALUES('m1','Same'),('m2','Same')"));
        QVERIFY(q.exec("CREATE TABLE planning_calendar(day TEXT PRIMARY KEY,note TEXT)"));
        q.prepare("INSERT INTO planning_calendar VALUES('02.10.26',?)");
        q.addBindValue(QString::fromUtf8("09:00 Own | Джерело: мобільний застосунок | Майстер ID: m1\n10:00 Other | Джерело: мобільний застосунок | Майстер ID: m2\n11:00 Old | Майстер: Same"));
        QVERIFY(q.exec());
        PlanningRepository repo;
        const QDate day(2026,10,2);
        ActivityAuditService::setCurrentActor({"account-1","Same","master","test"});
        QCOMPARE(repo.visibleDayMap(day).size(),1);
        QVERIFY(repo.visibleDayMap(day).contains(9));
        ActivityAuditService::setCurrentActor({"account-2","Same","master","test"});
        QCOMPARE(repo.visibleDayMap(day).size(),1);
        QVERIFY(repo.visibleDayMap(day).contains(10));
    }
    void legacyStatesAndPartialSettlement() {
        QSqlQuery cache(db);
        QVERIFY(cache.exec("CREATE TABLE router_cache_work_order_parts AS SELECT * FROM work_order_parts"));
        QString error;
        auto d=PurchasePaymentService::detail(db,"part",1,"order-a",&error);
        QVERIFY2(error.isEmpty(),qPrintable(error));
        QCOMPARE(d.totalCents,20000); QCOMPARE(d.remainingCents(),20000);
        QCOMPARE(PurchasePaymentService::detail(db,"part",2,"order-a").remainingCents(),0);
        QVERIFY2(PurchasePaymentService::pay(db,d,7501,&error),qPrintable(error));
        auto after=PurchasePaymentService::detail(db,"part",1,"order-a");
        QCOMPARE(after.paidCents,7501); QCOMPARE(after.remainingCents(),12499);
        QVERIFY(!PurchasePaymentService::pay(db,d,7501,&error));
        QVERIFY(!PurchasePaymentService::pay(db,after,-1,&error));
        QVERIFY(!PurchasePaymentService::pay(db,after,12500,&error));
        QCOMPARE(PurchasePaymentService::history(db,after).size(),1);
        int count=0;
        double total=0;
        QVERIFY(Database::unpaidPurchasesSummary(count,total));
        QCOMPARE(total,124.99); QCOMPARE(count,1);
        auto row=PartsQueryService::loadRowById(db,1);
        QVERIFY(!row.paid); QCOMPARE(row.paidAmount,75.01);
        QVERIFY(PartsPageTableModelAdapter::adaptRow(row,QDate::currentDate(),true).partiallyPaid);
        QVERIFY2(PurchasePaymentService::pay(db,after,12499,&error),qPrintable(error));
        QCOMPARE(PurchasePaymentService::detail(db,"part",1,"order-a").remainingCents(),0);
        QSqlQuery q(db); QVERIFY(q.exec("SELECT paid FROM work_order_parts WHERE id=1")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),1);
        QCOMPARE(PurchasePaymentService::history(db,after).size(),2);
        QCOMPARE(PartsQueryService::loadRows(db,{}, {}, {}).size(),2);
        QVERIFY(Database::setPartPaid(1,false));
        QCOMPARE(PurchasePaymentService::detail(db,"part",1,"order-a").remainingCents(),20000);
        QVERIFY(Database::setPartPaid(1,true));
        QCOMPARE(PurchasePaymentService::detail(db,"part",1,"order-a").remainingCents(),0);
    }
    void inventoryPaymentDetailIsReadOnly() {
        QString openError;
        ScopedSqliteConnection connection(dir.filePath("legacy-payment-detail.db"),"payment-detail-readonly",&openError);
        QVERIFY2(connection.isOpen(),qPrintable(openError));
        auto db=connection.db();
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE workshop_inventory_purchases(id INTEGER PRIMARY KEY,purchased_on TEXT,material_name TEXT,quantity REAL,unit_cost REAL,supplier TEXT,note TEXT,unit TEXT)"));
        QVERIFY(q.exec("INSERT INTO workshop_inventory_purchases VALUES(1,'2026-09-16','Oil',10,25,'Store','','l')"));
        auto schema=[&]{QStringList rows;QSqlQuery read(db);if(read.exec("SELECT name,sql FROM sqlite_master ORDER BY name"))while(read.next())rows.append(read.value(0).toString()+read.value(1).toString());return rows;};
        const auto before=schema();
        QVERIFY(q.exec("PRAGMA query_only=ON"));
        QString error="stale error";
        auto detail=PurchasePaymentService::detail(db,"inventory",1,{},&error);
        QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(detail.id,1);
        QCOMPARE(detail.totalCents,25000);
        // Preserve existing legacy inventory semantics; do not invent ledger events.
        QCOMPARE(detail.paidCents,25000);
        QCOMPARE(schema(),before);
        QVERIFY(q.exec("PRAGMA query_only=OFF"));
        QVERIFY(q.exec("CREATE TABLE returned_inventory_purchases(purchase_id INTEGER PRIMARY KEY,spare_id INTEGER)"));
        QVERIFY(q.exec("CREATE TABLE purchase_payment_balances(payment_key TEXT PRIMARY KEY,paid_cents INTEGER)"));
        q.prepare("INSERT INTO purchase_payment_balances VALUES(?,5000)");
        q.addBindValue(PurchasePaymentService::key("inventory",1));QVERIFY(q.exec());
        QVERIFY(q.exec("PRAGMA query_only=ON"));
        detail=PurchasePaymentService::detail(db,"inventory",1,{},&error);
        QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(detail.paidCents,5000);
        QCOMPARE(detail.remainingCents(),20000);
        QVERIFY(q.exec("PRAGMA query_only=OFF"));
        QVERIFY(q.exec("INSERT INTO returned_inventory_purchases VALUES(1,1)"));
        detail=PurchasePaymentService::detail(db,"inventory",1,{},&error);
        QCOMPARE(detail.id,0);QVERIFY(!error.isEmpty());
        QVERIFY(q.exec("DELETE FROM returned_inventory_purchases"));
        QVERIFY(q.exec("ALTER TABLE purchase_payment_balances RENAME COLUMN paid_cents TO broken_amount"));
        detail=PurchasePaymentService::detail(db,"inventory",1,{},&error);
        QCOMPARE(detail.id,0);QVERIFY(!error.isEmpty());
        QCOMPARE(PurchasePaymentService::detail(db,"inventory",1,{}).id,0);
        QVERIFY(q.exec("ALTER TABLE purchase_payment_balances RENAME COLUMN broken_amount TO paid_cents"));
        QVERIFY(q.exec("CREATE TABLE purchase_payment_terms(broken_key TEXT)"));
        detail=PurchasePaymentService::detail(db,"inventory",1,{},&error);
        QCOMPARE(detail.id,0);QVERIFY(!error.isEmpty());
    }
    void partDetailReadErrorsAreExplicit() {
        QString error;
        QVERIFY2(PurchasePaymentService::ensureSchema(db,&error),qPrintable(error));
        QSqlQuery q(db);
        q.prepare("INSERT INTO purchase_payment_balances(payment_key,paid_cents) VALUES(?,5000)");
        q.addBindValue(PurchasePaymentService::key("part",1,"order-a"));QVERIFY(q.exec());
        auto row=PartsQueryService::loadRowById(db,1,&error);
        QCOMPARE(row.id,1);QVERIFY(error.isEmpty());QCOMPARE(row.paidAmount,50.0);
        QVERIFY(q.exec("ALTER TABLE purchase_payment_balances RENAME COLUMN paid_cents TO broken_amount"));
        QCOMPARE(PartsQueryService::loadRowById(db,1,&error).id,0);QVERIFY(!error.isEmpty());
        QCOMPARE(PartsQueryService::loadRowById(db,1).id,0);
        QVERIFY(q.exec("ALTER TABLE purchase_payment_balances RENAME COLUMN broken_amount TO paid_cents"));
        QCOMPARE(PartsQueryService::loadRowById(db,1,&error).paidAmount,50.0);QVERIFY(error.isEmpty());
        QVERIFY(q.exec("DROP VIEW router_work_orders"));
        QCOMPARE(PartsQueryService::loadRowById(db,1,&error).id,1);QVERIFY(error.isEmpty());
        QVERIFY(q.exec("CREATE VIEW router_work_orders AS SELECT * FROM nonexistent_orders"));
        QCOMPARE(PartsQueryService::loadRowById(db,1,&error).id,0);QVERIFY(!error.isEmpty());
        QVERIFY(q.exec("DROP VIEW router_work_orders"));
        QVERIFY(q.exec("CREATE VIEW router_work_orders AS SELECT * FROM work_orders"));
        QCOMPARE(PartsQueryService::loadRowById(db,1,&error).id,1);QVERIFY(error.isEmpty());
        QCOMPARE(PartsQueryService::loadRowById(db,999999,&error).id,0);QVERIFY(error.isEmpty());
    }
    void partsReadFailureDoesNotPoisonCache() {
        QString error;
        QVERIFY2(PurchasePaymentService::ensureSchema(db,&error),qPrintable(error));
        QSqlQuery q(db);
        q.prepare("INSERT INTO purchase_payment_balances(payment_key,paid_cents) VALUES(?,5000)");
        q.addBindValue(PurchasePaymentService::key("part",1,"order-a"));QVERIFY(q.exec());
        PartsQueryService::invalidateCache();
        QVERIFY(q.exec("ALTER TABLE purchase_payment_balances RENAME COLUMN paid_cents TO broken_amount"));
        QVERIFY(PartsQueryService::loadRows(db,{},{},{},&error).isEmpty());
        QVERIFY(error.contains("paid_cents"));
        QVERIFY(PartsQueryService::loadRows(db,{},{},{}).isEmpty());
        QVERIFY(q.exec("ALTER TABLE purchase_payment_balances RENAME COLUMN broken_amount TO paid_cents"));
        // No invalidation here: neither failed call may have published a cached result.
        const auto rows=PartsQueryService::loadRows(db,{},{},{},&error);
        QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(rows.size(),2);
        bool found=false;
        for(const auto& row:rows)if(row.id==1){found=true;QCOMPARE(row.paidAmount,50.0);}
        QVERIFY(found);
        error="old diagnostic";
        const auto cached=PartsQueryService::loadRows(db,{},{},{},&error);
        QVERIFY(error.isEmpty());QCOMPARE(cached.size(),rows.size());
        for(const auto& row:cached)if(row.id==1)QCOMPARE(row.paidAmount,50.0);
    }
    void debtReadErrorsNeverReturnPartialBalances() {
        QString error;
        QVERIFY2(PurchasePaymentService::ensureSchema(db,&error),qPrintable(error));
        auto read=[&]{PartsQueryService::invalidateCache();return PurchasePaymentService::debts(db,&error);};
        const auto valid=read();QVERIFY2(error.isEmpty(),qPrintable(error));QVERIFY(!valid.isEmpty());
        QSqlQuery q(db);
        QVERIFY(q.exec("ALTER TABLE purchase_payment_terms RENAME COLUMN due_date TO broken_date"));
        QVERIFY(read().isEmpty());QVERIFY(error.contains("due_date"));
        // Omitting the diagnostic output must not expose the partial part debts either.
        PartsQueryService::invalidateCache();QVERIFY(PurchasePaymentService::debts(db).isEmpty());
        QVERIFY(q.exec("ALTER TABLE purchase_payment_terms RENAME COLUMN broken_date TO due_date"));
        QCOMPARE(read().size(),valid.size());QVERIFY(error.isEmpty());
        QVERIFY(q.exec("ALTER TABLE workshop_inventory_purchases RENAME COLUMN material_name TO broken_name"));
        QVERIFY(read().isEmpty());QVERIFY(!error.isEmpty());
        QVERIFY(q.exec("ALTER TABLE workshop_inventory_purchases RENAME COLUMN broken_name TO material_name"));
        QCOMPARE(read().size(),valid.size());QVERIFY(error.isEmpty());
        QVERIFY(q.exec("ALTER TABLE purchase_payment_balances RENAME COLUMN paid_cents TO broken_amount"));
        QVERIFY(read().isEmpty());QVERIFY(!error.isEmpty());
        QVERIFY(q.exec("ALTER TABLE purchase_payment_balances RENAME COLUMN broken_amount TO paid_cents"));
        QCOMPARE(read().size(),valid.size());QVERIFY(error.isEmpty());
    }
    void inventoryAndCombinedDebts() {
        WorkshopInventoryPurchaseInput input;
        input.purchasedOn=QDate(2026,9,16); input.materialName="Oil"; input.unit="l";
        input.quantity=10; input.unitCost=25; input.supplier="Store";
        QString error; qint64 oldId=0,newId=0;
        QVERIFY2(WorkshopInventoryService::addPurchase(db,input,&oldId,&error),qPrintable(error));
        QCOMPARE(PurchasePaymentService::detail(db,"inventory",oldId,{}).remainingCents(),0);
        input.paidAmount=50;
        QVERIFY2(WorkshopInventoryService::addPurchase(db,input,&newId,&error),qPrintable(error));
        auto d=PurchasePaymentService::detail(db,"inventory",newId,{});
        QCOMPARE(d.paidCents,5000); QCOMPARE(d.remainingCents(),20000);
        error.clear(); const auto debts=PurchasePaymentService::debts(db,&error);
        QVERIFY2(error.isEmpty(),qPrintable(error)); QCOMPARE(debts.size(),2);
        QCOMPARE(debts[0].remainingCents()+debts[1].remainingCents(),40000);
        QVERIFY2(PurchasePaymentService::pay(db,d,20000,&error),qPrintable(error));
        QCOMPARE(PurchasePaymentService::debts(db).size(),1);
        input.paidAmount=251;
        QVERIFY(!WorkshopInventoryService::addPurchase(db,input,nullptr,&error));
        QSqlQuery q(db); QVERIFY(q.exec("SELECT COUNT(*) FROM workshop_inventory_purchases")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),2);
    }
    void paymentRollsBackOnAuditFailureAndMasterDenied() {
        QString error; QVERIFY(PurchasePaymentService::ensureSchema(db,&error));
        QSqlQuery q(db);
        QVERIFY2(q.exec("CREATE TRIGGER reject_payment BEFORE INSERT ON activity_audit_events BEGIN SELECT RAISE(ABORT,'test failure'); END"),qPrintable(q.lastError().text()));
        const auto d=PurchasePaymentService::detail(db,"part",1,"order-a");
        QVERIFY(!PurchasePaymentService::pay(db,d,100,&error));
        QVERIFY(PurchasePaymentService::balances(db).isEmpty());
        QCOMPARE(PurchasePaymentService::detail(db,"part",1,"order-a").paidCents,0);
        ActivityAuditService::setCurrentActor({"master-id","Master","master","session"});
        QVERIFY(!PurchasePaymentService::canPay(db,"part"));
        QVERIFY(!PurchasePaymentService::canPay(db,"inventory"));
        QVERIFY(!PurchasePaymentService::pay(db,d,100,&error));
    }
    void dialogPartialAndCancel() {
        const auto d=PurchasePaymentService::detail(db,"part",1,"order-a");
        qint64 amount=-1;
        QTimer::singleShot(50,[&] {
            auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()); QVERIFY(dialog);
            const auto close=qScopeGuard([&] { if(dialog->isVisible()) dialog->reject(); });
            auto* full=dialog->findChild<QPushButton*>("payInFull"); QVERIFY(full); QVERIFY(!full->autoDefault());
            const auto buttonImage=full->grab().toImage();
            int greenPixels=0;
            for(int y=0;y<buttonImage.height();++y)for(int x=0;x<buttonImage.width();++x) {
                const auto color=buttonImage.pixelColor(x,y);
                if(color.green()>color.red()+20 && color.green()>color.blue()+20)++greenPixels;
            }
            // A single sampled pixel can land on the white caption glyph.
            QVERIFY(greenPixels>buttonImage.width()*buttonImage.height()/4);
            QVERIFY(!dialog->findChild<QPushButton*>("payPartially"));
            dialog->findChild<QCheckBox*>("partialPayment")->setChecked(true);
            auto* input=dialog->findChild<QDoubleSpinBox*>("partialPaymentAmount"); QVERIFY(input); input->setValue(75.01);
            QCoreApplication::processEvents();
            QVERIFY(dialog->grab().save("purchase-payment-dialog.png"));
            dialog->findChild<QPushButton*>("payInFull")->click();
        });
        QVERIFY(choosePurchasePayment(nullptr,d,false,&amount)); QCOMPARE(amount,7501);
        QCOMPARE(PurchasePaymentService::detail(db,"part",1,"order-a").paidCents,0);
        QTimer::singleShot(0,[]{qobject_cast<QDialog*>(QApplication::activeModalWidget())->reject();});
        QVERIFY(!choosePurchasePayment(nullptr,d,true,&amount));
        QTimer::singleShot(0,[]{
            auto* dialog=QApplication::activeModalWidget();
            dialog->findChild<QCheckBox*>("deferPayment")->setChecked(true);
            dialog->findChild<QPushButton*>("payInFull")->click();
        });
        QVERIFY(choosePurchasePayment(nullptr,d,true,&amount)); QCOMPARE(amount,0);
        PurchaseDebtsPage page; page.resize(1000,500); page.refresh(); page.show();
        QTRY_VERIFY_WITH_TIMEOUT(!page.property("debtDataLoading").toBool(),10000);
        auto* table=page.findChild<QTableView*>("purchaseDebtsTable"); QVERIFY(table); QCOMPARE(table->model()->rowCount(),2);
        QVERIFY(page.grab().save("purchase-debts-page.png"));
        const auto beforePayment = PurchasePaymentService::detail(db,"part",1,"order-a");
        const auto historyCount = PurchasePaymentService::history(db,beforePayment).size();
        bool savedMessageChecked = false;
        QTimer paymentReady;
        connect(&paymentReady,&QTimer::timeout,[&]{
            auto* payment = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if(!payment || payment->objectName()!=QStringLiteral("purchasePaymentDialog"))return;
            paymentReady.stop();
            const auto closePayment = qScopeGuard([&]{ if(payment->isVisible()) payment->reject(); });
            auto* pay = payment->findChild<QPushButton*>("payInFull"); QVERIFY(pay);
            // This timer is consumed by the success message's nested event loop.
            QTimer::singleShot(0,[&]{
                auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                if(!box || box->windowTitle()!=QString::fromUtf8("Оплату здійснено")) return;
                const QString expected = QString::fromUtf8("Оплату %1 грн успішно збережено.")
                    .arg(FormatUtils::moneyCents(beforePayment.remainingCents()));
                savedMessageChecked = box->text()==expected;
                box->accept();
            });
            pay->click();
        });
        paymentReady.start(10);
        table->doubleClicked(table->model()->index(1,3));
        QVERIFY(savedMessageChecked);
        const auto afterPayment = PurchasePaymentService::detail(db,"part",1,"order-a");
        QCOMPARE(afterPayment.remainingCents(),0);
        QCOMPARE(afterPayment.paidCents,beforePayment.paidCents+beforePayment.remainingCents());
        QCOMPARE(PurchasePaymentService::history(db,afterPayment).size(),historyCount+1);
        QTRY_VERIFY_WITH_TIMEOUT(!page.property("debtDataLoading").toBool(),10000);
        QCOMPARE(table->model()->rowCount(),0);
    }
    void paymentButtonsAndReportsTab() {
        PartsPage page;
        auto* table=page.findChild<QTableView*>("partsArchiveTable"); QVERIFY(table);
        table->setParent(nullptr); QScopedPointer<QTableView> owned(table);
        PartsPageTableModelAdapter::PartsArchiveTableModel model;
        auto unpaid=PartsPageTableModelAdapter::adaptRow(PartsQueryService::loadRowById(db,1));
        auto partial=unpaid; partial.partiallyPaid=true;
        auto paid=unpaid; paid.paid=true;
        model.setRows({unpaid,partial,paid}); table->setModel(&model);
        for(int col=0;col<model.columnCount()-1;++col) table->hideColumn(col);
        table->setColumnWidth(model.columnCount()-1,240);
        for(int row=0;row<3;++row) table->setRowHeight(row,55);
        table->resize(270,240); table->show(); QTest::qWait(30);
        QVERIFY(table->grab().save("purchase-payment-states.png"));
        const auto partialCell=model.index(1,model.columnCount()-1);
        const auto captured=table->viewport()->grab();
        const auto point=table->visualRect(partialCell).center()+QPoint(45,5);
        const auto color=captured.toImage().pixelColor(point*captured.devicePixelRatio());
        owned.take()->setParent(&page);
        QVERIFY2(color.green()>color.blue()+30,qPrintable(color.name()));
        QVERIFY(model.data(partialCell,Qt::UserRole+4).toBool());
        QString error;
        QVERIFY(PurchasePaymentService::pay(db,PurchasePaymentService::detail(db,"part",1,"order-a"),5000,&error,7));
        WorkshopInventoryPurchaseInput input;
        input.purchasedOn=QDate::currentDate(); input.materialName="Oil"; input.unit="l";
        input.quantity=10; input.unitCost=25; input.supplier=QString::fromUtf8("Постачальник матеріалів"); input.paidAmount=0;
        QVERIFY(WorkshopInventoryService::addPurchase(db,input,nullptr,&error));
        input.paidAmount=50;
        QVERIFY(WorkshopInventoryService::addPurchase(db,input,nullptr,&error));
        ReportsPage reports;
        auto* tabs=reports.findChild<QTabWidget*>(); QVERIFY(tabs); QCOMPARE(tabs->count(),10);
        QCOMPARE(tabs->tabText(2), QString::fromUtf8("Накладні"));
        tabs->setCurrentIndex(8); reports.resize(1300,650); reports.show(); QTest::qWait(150);
        auto* top=reports.findChild<QWidget*>("reportsTopHost"); QVERIFY(top);
        QVERIFY(!top->isVisible());
        QVERIFY(!reports.findChild<QScrollArea*>("reportsDebtSummaryArea"));
        QVERIFY(reports.grab().save("purchase-reports-tab.png"));
        QCOMPARE(tabs->currentWidget()->objectName(),QString("purchaseDebtsPage"));
        tabs->setCurrentIndex(9); QTest::qWait(100);
        QCOMPARE(tabs->currentWidget()->objectName(),QString("purchaseDebtAnalysisPage"));
        QCOMPARE(tabs->tabText(9),QString::fromUtf8("Аналіз боргів"));
        auto* analysis=reports.findChild<QTextBrowser*>("debtAnalysisReport"); QVERIFY(analysis);
        reports.findChild<QTabBar*>("debtAnalysisViews")->setCurrentIndex(1);
        QVERIFY(analysis->toPlainText().contains(QString::fromUtf8("Обставини заборгованості")));
        QVERIFY(!top->isVisible());
        QVERIFY(tabs->geometry().top()<30);
        auto* heading=reports.findChild<QLabel*>("debtAnalysisHeading"); QVERIFY(heading);
        auto* pdf=reports.findChild<QPushButton*>("debtAnalysisPdf"); QVERIFY(pdf);
        QVERIFY(qAbs(heading->geometry().center().y()-pdf->geometry().center().y())<12);
        auto* search=reports.findChild<QLineEdit*>("debtAnalysisSearch"); QVERIFY(search);
        auto* supplier=reports.findChild<QComboBox*>("debtAnalysisSupplier"); QVERIFY(supplier);
        QVERIFY(qAbs(search->geometry().center().y()-supplier->geometry().center().y())<8);
        QVERIFY(!reports.findChild<QLabel*>("debtAnalysisExpenses")->isVisible());
        QVERIFY(reports.grab().save("purchase-debt-analysis-tab.png"));
        const QString qaDir=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
        if (!qaDir.isEmpty()) {
            QVERIFY(QDir().mkpath(qaDir));
            QVERIFY(reports.grab().save(QDir(qaDir).filePath("debt-analysis.png")));
            reports.findChild<QTabBar*>("debtAnalysisViews")->setCurrentIndex(0);
            for(const QSize size:{QSize(1280,900),QSize(1920,1080)}) {
                reports.resize(size); QTest::qWait(80);
                QVERIFY(reports.grab().save(QDir(qaDir).filePath(QString("debt-overview-%1.png").arg(size.width()))));
                QVERIFY(analysis->horizontalScrollBar()->maximum()<=1);
            }
        }
        for(int index:{8,9,1,9,8,9}) {
            tabs->setCurrentIndex(index); QTest::qWait(40);
            QCOMPARE(top->isVisible(),index==1);
            if(index==9) QVERIFY(tabs->geometry().top()<30);
        }
        tabs->setCurrentIndex(1); QTest::qWait(100);
        QVERIFY(top->isVisible());
    }
    void zeroAndDeferredPayments() {
        QString error;
        auto d=PurchasePaymentService::detail(db,"part",1,"order-a");
        QVERIFY2(PurchasePaymentService::pay(db,d,0,&error,14),qPrintable(error));
        auto after=PurchasePaymentService::detail(db,"part",1,"order-a");
        QCOMPARE(after.paidCents,0);
        QCOMPARE(after.dueDate,QDate::currentDate().addDays(14));
        QVERIFY(!PurchasePaymentService::pay(db,d,100,&error));
        db.close(); QVERIFY(db.open()); PartsQueryService::invalidateCache();
        after=PurchasePaymentService::detail(db,"part",1,"order-a");
        QCOMPARE(after.dueDate,QDate::currentDate().addDays(14));
        QCOMPARE(PurchasePaymentService::history(db,after).last().dueDate,after.dueDate);
        QVERIFY(PurchasePaymentService::pay(db,after,5000,&error));
        after=PurchasePaymentService::detail(db,"part",1,"order-a");
        QVERIFY(PurchasePaymentService::pay(db,after,0,&error,3));
        after=PurchasePaymentService::detail(db,"part",1,"order-a");
        QCOMPARE(after.paidCents,5000);
        QCOMPARE(after.dueDate,QDate::currentDate().addDays(3));
        QVERIFY(!PurchasePaymentService::pay(db,after,0,&error,3651));
        QCOMPARE(PurchasePaymentService::history(db,after).size(),3);
        QVERIFY(PurchasePaymentService::pay(db,after,0,&error,0));
        after=PurchasePaymentService::detail(db,"part",1,"order-a");
        QVERIFY(!after.dueDate.isValid());
        QCOMPARE(after.paidCents,5000);
        QVERIFY(PurchasePaymentService::pay(db,after,15000,&error,7));
        after=PurchasePaymentService::detail(db,"part",1,"order-a");
        QCOMPARE(after.remainingCents(),0);
        QVERIFY(!after.dueDate.isValid());
    }
    void inventoryDeferralAndSummary() {
        WorkshopInventoryPurchaseInput input;
        input.purchasedOn=QDate::currentDate(); input.materialName="Oil"; input.unit="l";
        input.quantity=10; input.unitCost=25; input.supplier="Store & Co";
        input.paidAmount=0; input.deferredDays=10;
        QString error; qint64 id=0;
        QVERIFY2(WorkshopInventoryService::addPurchase(db,input,&id,&error),qPrintable(error));
        auto d=PurchasePaymentService::detail(db,"inventory",id,{});
        QCOMPARE(d.paidCents,0); QCOMPARE(d.dueDate,QDate::currentDate().addDays(10));
        QVERIFY(PurchasePaymentService::pay(db,d,5001,&error));
        const auto debts=PurchasePaymentService::debts(db,&error);
        QCOMPARE(debts.size(),2);
        int count=0; double sum=0;
        QVERIFY(Database::unpaidPurchasesSummary(count,sum));
        QCOMPARE(count,2); QCOMPARE(sum,399.99);
        const auto html=PurchaseDebtsPage::summaryHtml(debts);
        QVERIFY(html.contains(QString::fromUtf8("Склад СТО / Store &amp; Co")));
        QVERIFY(html.contains(QString::fromUtf8("Частково сплачено")));
        QVERIFY(html.contains(QString::fromUtf8("Борги: не сплачено")));
        QVERIFY(html.contains("50.01")); QVERIFY(html.contains("199.99"));
        QVERIFY(html.contains("font-size:10pt"));
        QVERIFY(html.contains(QString::fromUtf8("<b>Разом</b>")));
        QCOMPARE(html.count("<hr />"),2);
        QVERIFY(html.contains("colspan='3'"));
        QVERIFY(html.contains("width='27'"));
        input.deferredDays=3651;
        QVERIFY(!WorkshopInventoryService::addPurchase(db,input,nullptr,&error));
        QCOMPARE(PurchasePaymentService::debts(db).size(),2);
        auto part=PurchasePaymentService::detail(db,"part",1,"order-a");
        QVERIFY(PurchasePaymentService::pay(db,part,part.remainingCents(),&error));
        QVERIFY(Database::unpaidPurchasesSummary(count,sum));
        QCOMPARE(count,1); QCOMPARE(sum,199.99);
    }
    void debtAnalysisUsesOrderDateForUnpaidClosedOrders() {
        const QDate today(2026,10,2);
        PurchaseDebt debt;
        debt.source="part"; debt.id=1; debt.orderUid="order-a"; debt.orderNumber=9;
        debt.supplier="Store & Co"; debt.name="<Part>";
        debt.totalCents=20000; debt.paidCents=5000; debt.dueDate=today.addDays(-7);
        auto other=debt; other.id=2; other.supplier="Other"; other.paidCents=0; other.dueDate={};
        PartRowSnapshot part; part.id=1; part.orderUid="order-a"; part.orderStatus="open";
        part.dateRaw=today.addDays(-5).toString("dd.MM.yy"); part.paid=false;
        auto second=part; second.id=2; second.dateRaw=today.addDays(-3).toString("dd.MM.yy");
        second.installedAtRaw=today.addDays(-1).toString("dd.MM.yy");
        const auto html=PurchaseDebtAnalysisPage::analysisHtml({debt,other},{part,second},today);
        QVERIFY(html.contains(FormatUtils::money(350,2)+QString::fromUtf8(" грн")));
        QVERIFY(html.contains(QString::fromUtf8("Не сплата 5 днів: 1")));
        QVERIFY(!html.contains(QString::fromUtf8("Не сплата 3 дні: 0")));
        QVERIFY(html.contains("Store &amp; Co")); QVERIFY(html.contains("&lt;Part&gt;"));
        QVERIFY(!html.contains("<Part>"));
        QVERIFY(html.contains(QString::fromUtf8("Не встановлено; оплата не завершена")));
        QVERIFY(html.contains(QString::fromUtf8("Встановлено; оплата не завершена")));
        auto filtered=PurchaseDebtAnalysisPage::analysisHtml({debt,other},{part,second},today,"Other");
        QVERIFY(!filtered.contains("Store &amp; Co")); QVERIFY(filtered.contains(FormatUtils::money(200,2)+QString::fromUtf8(" грн")));
        part.orderClosedAtRaw=today.toString("dd.MM.yy");
        auto closed=PurchaseDebtAnalysisPage::analysisHtml({debt},{part},today);
        QVERIFY(!closed.contains(QString::fromUtf8("Не сплачено клієнтом")));
        QVERIFY(!closed.contains(QString::fromUtf8("Оплата клієнтом: немає підтверджених даних")));
        QVERIFY(closed.contains(QString::fromUtf8("Не сплата 5 днів: 1")));
        QVERIFY(!html.contains(QString::fromUtf8("Часткові, грн")));
        QVERIFY(!html.contains(QString::fromUtf8("Днів після строку оплати")));
        QVERIFY(!html.contains(QString::fromUtf8("Що перевірити")));
        QVERIFY(html.contains(QString::fromUtf8("Оплата прострочена, дн")));
        QVERIFY(html.contains(QString::fromUtf8("Вік закупівлі, дн")));
        QVERIFY(html.contains(QString::fromUtf8("Затримка оплати, дн")));
        auto unknown=PurchaseDebtAnalysisPage::analysisHtml({debt},{},today);
        QVERIFY(!unknown.contains(QString::fromUtf8("Не визначено стан встановлення")));
        QVERIFY(PurchaseDebtAnalysisPage::analysisHtml({}, {}, today).contains(QString::fromUtf8("Непогашених боргів немає")));
        QVector<PurchaseDebt> many;
        for (int i=0;i<101;++i) { auto item=debt; item.id=i+1; item.name=QString("ITEM-%1-END").arg(i); many.append(item); }
        auto page0=PurchaseDebtAnalysisPage::analysisHtml(many,{},today);
        auto page1=PurchaseDebtAnalysisPage::analysisHtml(many,{},today,{},1);
        QVERIFY(page0.contains("ITEM-0-END")); QVERIFY(!page0.contains("ITEM-100-END"));
        QVERIFY(page1.contains("ITEM-100-END")); QVERIFY(!page1.contains("ITEM-0-END"));
        QVERIFY(page0.contains(QString::fromUtf8("Позицій: 101")));
        QVERIFY(page1.contains(QString::fromUtf8("Позицій: 101")));
    }
    void debtAgeIgnoresDeliveryDeadlineAndContinuesAfterClosure() {
        PurchaseDebt debt; debt.totalCents=25000; debt.paidCents=5000;
        debt.date="2026-09-01"; debt.dueDate=QDate(2026,12,1);
        PartRowSnapshot part; part.dateRaw="01.09.26";
        part.deliveryDateRaw="25.09.26"; part.orderStatus="closed"; part.orderClosedAtRaw="20.09.26";
        QCOMPARE(PurchasePaymentService::unpaidDays(debt,&part,QDate(2026,10,2)),31);
        QCOMPARE(PurchasePaymentService::unpaidDays(debt,&part,QDate(2026,10,3)),32);
        debt.paidCents=debt.totalCents;
        QCOMPARE(PurchasePaymentService::unpaidDays(debt,&part,QDate(2026,10,3)),0);
        debt.paidCents=0; part.dateRaw="";
        QCOMPARE(PurchasePaymentService::unpaidDays(debt,&part,QDate(2026,10,3)),-1);
        part.dateRaw="04.10.26";
        QCOMPARE(PurchasePaymentService::unpaidDays(debt,&part,QDate(2026,10,3)),0);
    }
    void debtAgingBucketsAreExclusiveAndHeadersSeparateTimelines() {
        const QDate today(2026,10,2);
        QVector<PurchaseDebt> debts;
        for(int days:{2,3,4,5,7,8}) {
            PurchaseDebt d; d.source="inventory"; d.id=days; d.supplier="Shop";
            d.date=today.addDays(-days).toString("dd.MM.yy"); d.totalCents=10000;
            d.dueDate=today.addDays(-days);
            debts.append(d);
        }
        const auto html=PurchaseDebtAnalysisPage::analysisHtml(debts,{},today);
        QVERIFY(html.contains(QString::fromUtf8("Не сплата 3 дні: 2 · Не сплата 5 днів: 2 · Не сплата більше 7 днів: 1")));
        QTextDocument document; document.setHtml(html);
        const auto frames=document.rootFrame()->childFrames();
        QVERIFY(frames.size()>=4);
        QCOMPARE(qobject_cast<QTextTable*>(frames.first())->columns(),2);
        QCOMPARE(qobject_cast<QTextTable*>(frames[1])->columns(),8);
        QCOMPARE(qobject_cast<QTextTable*>(frames.last())->columns(),10);
        const auto overview=PurchaseDebtAnalysisPage::analysisHtml(debts,{},today,{},0,{}, {},0);
        QVERIFY(!overview.contains(QString::fromUtf8("Вік неоплачених запасів")));
        QVERIFY(!overview.contains(QString::fromUtf8("<h3>Потребують дії</h3>")));
        QVERIFY(overview.contains(QString::fromUtf8("Закупівлі, що потребують уваги")));
        document.setHtml(overview);
        const auto overviewFrames=document.rootFrame()->childFrames();
        QCOMPARE(qobject_cast<QTextTable*>(overviewFrames[0])->columns(),3);
        QCOMPARE(qobject_cast<QTextTable*>(overviewFrames[1])->columns(),7);
        QVERIFY(overview.contains("bgcolor='#bd4750'"));
        QVERIFY(overview.contains("<!--EXPENSES-->"));
        QVERIFY(overview.contains(QString::fromUtf8("Разом за обліком")));
    }
    void debtAnalysisModesAndFiltersUseOneSnapshot() {
        PurchaseDebtAnalysisPage page; page.resize(1440,900); page.show(); page.refresh();
        auto* views=page.findChild<QTabBar*>("debtAnalysisViews");
        auto* source=page.findChild<QComboBox*>("debtAnalysisSource");
        auto* search=page.findChild<QLineEdit*>("debtAnalysisSearch");
        auto* report=page.findChild<QTextBrowser*>("debtAnalysisReport");
        QVERIFY(views && source && search && report); QCOMPARE(views->count(),4);
        QCOMPARE(views->currentIndex(),3);views->setCurrentIndex(0);
        auto* expenses=page.findChild<QLabel*>("debtAnalysisExpenses"); QVERIFY(expenses);
        QTRY_VERIFY(expenses->text().contains(QString::fromUtf8("сукупні")));
        QVERIFY(report->toPlainText().contains(QString::fromUtf8("Постачальники та зобов'язання")));
        QVERIFY(!report->toPlainText().contains(QString::fromUtf8("Єдиний реєстр боргів")));
        const auto qa=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
        for(int mode=0;mode<3;++mode) {
            views->setCurrentIndex(mode); QTest::qWait(40);
            if(!qa.isEmpty()) { QDir().mkpath(qa); QVERIFY(page.grab().save(QDir(qa).filePath(QString("debt-view-%1.png").arg(mode)))); }
        }
        QVERIFY(report->toPlainText().contains("Part"));
        source->setCurrentIndex(source->findData("invoice"));
        QVERIFY(!report->toPlainText().contains("Part"));
        source->setCurrentIndex(source->findData("manual"));
        QVERIFY(report->toPlainText().contains("Part"));
        search->setText("NOT-FOUND");
        QTRY_VERIFY(!report->toPlainText().contains("Part"));
        search->clear(); QTRY_VERIFY(report->toPlainText().contains("Part"));
        source->setCurrentIndex(source->findData("inventory"));
        QVERIFY(!report->toPlainText().contains("Part"));
    }
    void debtAnalysisExpensePeriodDoesNotDuplicateLiabilities() {
        QSqlQuery query(db);
        QVERIFY(query.exec("INSERT INTO expenses(period_from,period_to,rent) VALUES('2026-09-01','2026-09-30',1234)"));
        PurchaseDebtAnalysisPage page; page.setExpensePeriod(QDate(2026,9,1),QDate(2026,9,30)); page.refresh();
        page.findChild<QTabBar*>("debtAnalysisViews")->setCurrentIndex(0);
        auto* expenses=page.findChild<QLabel*>("debtAnalysisExpenses"); QVERIFY(expenses);
        QTRY_VERIFY(expenses->text().contains(FormatUtils::money(1234,2)));
        auto* report=page.findChild<QTextBrowser*>("debtAnalysisReport"); QVERIFY(report);
        QVERIFY(report->toPlainText().contains(QString::fromUtf8("Витрати та рух коштів")));
        QVERIFY(report->toPlainText().contains(FormatUtils::money(1234,2)));
        QVERIFY(!report->toPlainText().contains("<!--EXPENSES-->"));
        QCOMPARE(PurchasePaymentService::debts(db).size(),1);
        page.setExpensePeriod(QDate(2027,1,1),QDate(2027,1,31)); page.refresh();
        QTRY_VERIFY(expenses->text().contains(QString::fromUtf8("сукупні 0 грн")));
        QVERIFY(!expenses->text().contains(FormatUtils::money(1234,2)));
    }
    void debtTimelinesAndLinksKeepUnknownValuesSeparate() {
        const QDate today(2026,10,3);
        PurchaseDebt debt; debt.source="inventory"; debt.id=1; debt.supplier="S & %20"; debt.name=debt.supplier;
        debt.date="2026-09-03"; debt.dueDate=today.addDays(-4); debt.totalCents=12300; debt.paidCents=300;
        auto unknown=debt; unknown.id=2; unknown.name=""; unknown.dueDate={};
        const auto overview=PurchaseDebtAnalysisPage::analysisHtml({debt,unknown},{},today,{},0,{}, {},0);
        QVERIFY(overview.contains(QString::fromUtf8("font-weight:600;'>4</span>")));
        QVERIFY(overview.contains("<td>30</td>"));
        const auto registerHtml=PurchaseDebtAnalysisPage::analysisHtml({debt,unknown},{},today,{},0,{}, {},2);
        QCOMPARE(registerHtml.count("href='record:"),2);
        QVERIFY(registerHtml.contains(QString::fromUtf8("Без назви")));
        QVERIFY(registerHtml.contains("<td>4</td>"));
        QVERIFY(!registerHtml.contains(QString::fromUtf8("Не визначено")));
        QVERIFY(!registerHtml.contains(QString::fromUtf8("Постачальники та зобов'язання")));
    }
    void supplierReconciliationPreservesPaymentsAndRejectsStaleOrUnauthorizedEdits() {
        QVERIFY(SupplierDebtReconciliationService::canReconcile());
        QString error;
        const auto before=PurchasePaymentService::detail(db,"part",1,"order-a");
        SupplierDebtReconciliation row; row.supplier="Store"; row.computedCents=20000; row.actualCents=23050;
        QVERIFY2(SupplierDebtReconciliationService::save(db,{row},&error),qPrintable(error));
        const auto saved=SupplierDebtReconciliationService::load(db,&error);
        QCOMPARE(saved.value("Store").actualCents,23050LL);
        QVERIFY(saved.value("Store").differs()); QVERIFY(!saved.value("Store").revision.isEmpty());
        QCOMPARE(PurchasePaymentService::detail(db,"part",1,"order-a").paidCents,before.paidCents);
        QCOMPARE(PurchasePaymentService::detail(db,"part",1,"order-a").totalCents,before.totalCents);
        const auto html=PurchaseDebtAnalysisPage::analysisHtml({}, {},QDate(2026,10,2),{},0,saved);
        QVERIFY(html.contains(QString::fromUtf8("Розбіжність за звіркою")));
        QVERIFY(!SupplierDebtReconciliationService::save(db,{row},&error));
        row=saved.value("Store"); row.computedCents=19999;
        QVERIFY(!SupplierDebtReconciliationService::save(db,{row},&error));
        QCOMPARE(SupplierDebtReconciliationService::load(db).value("Store").actualCents,23050LL);
        db.close(); QVERIFY(db.open());
        QCOMPARE(SupplierDebtReconciliationService::load(db).value("Store").actualCents,23050LL);
        ActivityAuditService::setCurrentActor({"master","Master","master","test"});
        QVERIFY(!SupplierDebtReconciliationService::save(db,{row},&error));
        QVERIFY(SupplierDebtReconciliationService::load(db).isEmpty());
        QVERIFY(!SupplierDebtReconciliationService::canReconcile());
    }
    void supplierReconciliationDialogSavesEditedAmount() {
        QVERIFY(Database::setSetting("settings.fin.store_markups", "{\"Store\":0}"));
        PurchaseDebtsPage page; page.resize(950,550); page.show(); page.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(!page.property("debtDataLoading").toBool(),10000);
        auto* button=page.findChild<QPushButton*>("reconcileSupplierDebts"); QVERIFY(button);
        bool submitted=false;
        QTimer modalTimer;
        connect(&modalTimer,&QTimer::timeout,this,[&] {
            auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if(!dialog) return;
            modalTimer.stop();
            auto* table=dialog->findChild<QTableView*>("supplierReconciliationTable");
            auto* save=dialog->findChild<QPushButton*>("saveSupplierReconciliation");
            if(!table || !save || table->model()->rowCount()!=1) { dialog->reject(); return; }
            auto* value=qobject_cast<QDoubleSpinBox*>(table->indexWidget(table->model()->index(0,2)));
            if(!value) { dialog->reject(); return; }
            value->setValue(250.25);
            const QString qaDir=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
            if(!qaDir.isEmpty()) {
                QDir().mkpath(qaDir); dialog->grab().save(QDir(qaDir).filePath("supplier-reconciliation.png"));
            }
            submitted=true; save->click();
        });
        modalTimer.start(20);
        button->click();
        page.reconcileDebts(); // A repeated request must not open a second dialog.
        QTRY_VERIFY_WITH_TIMEOUT(submitted && !page.m_reconciliationPending,10000);
        const auto entries=SupplierDebtReconciliationService::load(db);
        QCOMPARE(entries.value("Store").actualCents,25025LL);
        int count=0; double amount=0;
        QVERIFY(MainWindowMetricsService::loadUnpaidPurchasesSummary(count,amount));
        QCOMPARE(amount,250.25);
        QString banner;
        page.summaryChanged=[&](const QString& html) { banner=html; };
        page.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(banner.contains(FormatUtils::money(250.25,2)),10000);
        bool restored=false;
        QTimer reopenTimer;
        connect(&reopenTimer,&QTimer::timeout,this,[&] {
            auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if(!dialog) return;
            reopenTimer.stop();
            auto* table=dialog->findChild<QTableView*>("supplierReconciliationTable");
            auto* value=table?qobject_cast<QDoubleSpinBox*>(table->indexWidget(table->model()->index(0,2))):nullptr;
            restored=value && value->value()==250.25;
            dialog->reject();
        });
        reopenTimer.start(20);
        button->click();
        QTRY_VERIFY_WITH_TIMEOUT(restored && !page.m_reconciliationPending,10000);
        PurchaseDebtAnalysisPage analysis; analysis.resize(1500,900); analysis.refresh(); analysis.show();
        analysis.findChild<QTabBar*>("debtAnalysisViews")->setCurrentIndex(0);
        QTest::qWait(80);
        const QString qaDir=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
        if(!qaDir.isEmpty()) QVERIFY(analysis.grab().save(QDir(qaDir).filePath("reconciled-analysis.png")));
        QTRY_VERIFY(analysis.findChild<QTextBrowser*>("debtAnalysisReport")->toPlainText().contains(QString::fromUtf8("Розбіжність за звіркою")));
    }
    void reconciliationSuppliersComeOnlyFromFinance() {
        QSqlQuery q(db);
        QVERIFY(q.exec("UPDATE work_order_parts SET store='G' WHERE id=2"));
        QString error;
        SupplierDebtReconciliation old;
        old.supplier="Old shop"; old.actualCents=100;
        QVERIFY(SupplierDebtReconciliationService::save(db,{old},&error));
        const auto before=PurchasePaymentService::balances(db,&error);
        QVERIFY(Database::setSetting("settings.fin.store_markups", "{\"Store\":10,\"Configured without purchases\":0}"));
        QVERIFY(Database::setSetting("settings.fin.oil_store_markups", "[{\"store\":\"Oil shop\",\"pct\":5},{\"store\":\"Store\",\"pct\":5}]"));
        const auto names=SupplierDebtReconciliationService::suppliers(db,&error);
        QVERIFY2(error.isEmpty(),qPrintable(error));
        QCOMPARE(names,QStringList({"Configured without purchases","Oil shop","Store"}));
        QVERIFY(!names.contains("G")); QVERIFY(!names.contains("Old shop"));
        QCOMPARE(PurchasePaymentService::balances(db,&error),before);
        QCOMPARE(SupplierDebtReconciliationService::load(db).value("Old shop").actualCents,100);
        QVERIFY(q.exec("SELECT store FROM work_order_parts WHERE id=2")); QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(),QString("G"));
        QVERIFY(Database::setSetting("settings.fin.store_markups", "{}"));
        QVERIFY(Database::setSetting("settings.fin.oil_store_markups", "[]"));
        QVERIFY(SupplierDebtReconciliationService::suppliers(db,&error).isEmpty());
    }
    void supplierReconciliationBatchRollsBackAndListsPaidSuppliers() {
        QVERIFY(Database::setSetting("settings.fin.store_markups", "{\"Store\":0,\"Paid shop\":0}"));
        QSqlQuery q(db);
        QVERIFY(q.exec("UPDATE work_order_parts SET store='Paid shop' WHERE id=2"));
        const auto names=SupplierDebtReconciliationService::suppliers(db);
        QVERIFY(names.contains("Paid shop")); QVERIFY(names.contains("Store"));
        SupplierDebtReconciliation row; row.supplier="Store"; row.computedCents=20000; row.actualCents=22000;
        auto invalid=row; invalid.supplier="Paid shop"; invalid.actualCents=-1; invalid.computedCents=0;
        QString error;
        QVERIFY(!SupplierDebtReconciliationService::save(db,{row,invalid},&error));
        QVERIFY(SupplierDebtReconciliationService::load(db).isEmpty());
        ActivityAuditService::setCurrentActor({"master","Master","master","test"});
        QVERIFY(!SupplierDebtReconciliationService::canReconcile());
        QVERIFY(!SupplierDebtReconciliationService::canReconcile(db));
        QVERIFY(!SupplierDebtReconciliationService::save(db,{row},&error));
        ActivityAuditService::setCurrentActor({"owner","Owner","owner","test"});
        QVERIFY(SupplierDebtReconciliationService::canReconcile(db));
        invalid.actualCents=300;
        QVERIFY2(SupplierDebtReconciliationService::save(db,{row,invalid},&error),qPrintable(error));
        QCOMPARE(SupplierDebtReconciliationService::load(db).size(),2);
    }
    void reconciledTotalsFollowSubsequentLedgerChanges() {
        PurchaseDebt debt; debt.supplier="Shop"; debt.totalCents=20000;
        SupplierDebtReconciliation r; r.supplier="Shop"; r.computedCents=20000; r.actualCents=25000;
        QMap<QString,SupplierDebtReconciliation> saved{{"Shop",r}};
        QCOMPARE(SupplierDebtReconciliationService::effectiveTotals({debt},saved).value("Shop"),25000LL);
        debt.paidCents=5000;
        QCOMPARE(SupplierDebtReconciliationService::effectiveTotals({debt},saved).value("Shop"),20000LL);
        r.actualCents=0; saved["Shop"]=r;
        QCOMPARE(SupplierDebtReconciliationService::effectiveTotals({debt},saved).value("Shop"),0LL);
        debt.totalCents=30000;
        QCOMPARE(SupplierDebtReconciliationService::effectiveTotals({debt},saved).value("Shop"),5000LL);
    }
    void debtPdfButtonCreatesAndOpensWithoutSaveDialog_data() {
        QTest::addColumn<QString>("selection");
        QTest::newRow("full")<<QString("fullDebtReport");
        QTest::newRow("brief")<<QString("briefDebtReport");
        QTest::newRow("cancel")<<QString();
    }
    void debtPdfButtonCreatesAndOpensWithoutSaveDialog() {
        QFETCH(QString,selection);
        QVERIFY(Database::setSetting("settings.int.directories.reports_dir",dir.filePath("reports")));
        openedPdf={};
        QDesktopServices::setUrlHandler("file",this,"capturePdfUrl");
        const auto reset=qScopeGuard([] { QDesktopServices::unsetUrlHandler("file"); });
        PurchaseDebtAnalysisPage page; page.refresh();
        auto* button=page.findChild<QPushButton*>("debtAnalysisPdf"); QVERIFY(button);
        QTRY_VERIFY(button->isEnabled());
        bool modal=false, unexpected=false;
        QTimer timer;
        connect(&timer,&QTimer::timeout,this,[&] {
            if(auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
                modal=true;
                if(dialog->objectName()!="debtReportChoice") { unexpected=true; dialog->reject(); return; }
                QCOMPARE(dialog->windowTitle(),QString::fromUtf8("Який звіт підготувати?"));
                const auto qa=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
                if(!qa.isEmpty() && selection=="fullDebtReport") {
                    QVERIFY(QDir().mkpath(qa));
                    QVERIFY(dialog->grab().save(QDir(qa).filePath("debt-report-choice.png")));
                }
                if(selection.isEmpty()) { dialog->reject(); return; }
                auto* option=dialog->findChild<QPushButton*>(selection); QVERIFY(option); option->click();
            }
        });
        timer.start(100);
        button->click();
        QVERIFY(modal); QVERIFY(!unexpected);
        if(selection.isEmpty()) { QVERIFY(openedPdf.isEmpty()); return; }
        QVERIFY(openedPdf.isLocalFile());
        QVERIFY(QFileInfo(openedPdf.toLocalFile()).absoluteFilePath().startsWith(dir.path()));
        QFile pdf(openedPdf.toLocalFile()); QVERIFY(pdf.open(QIODevice::ReadOnly));
        QCOMPARE(pdf.read(5),QByteArray("%PDF-"));
    }
    void installationTimelineStopsAtInstallationAndRejectsUnconfirmedLegacyYears() {
        PurchaseDebt debt; debt.source="part"; debt.id=1; debt.orderUid="timeline";
        debt.supplier="Shop"; debt.totalCents=10000;
        PartRowSnapshot part; part.id=1; part.orderUid=debt.orderUid;
        part.dateRaw="2026-07-27"; part.deliveryDateRaw="2026-07-27";
        part.orderOpenedAtRaw="2026-06-09"; part.orderClosedAtRaw="2026-07-27"; part.orderStatus="closed";
        part.installedAtRaw="2026-08-13";
        auto html=PurchaseDebtAnalysisPage::analysisHtml({debt},{part},QDate(2026,10,3));
        QVERIFY(html.contains(QString::fromUtf8("Встановлення: 13.08.2026")));
        QVERIFY(html.contains("<td>68</td>")); // Purchase age is distinct from the completed installation cycle.
        QVERIFY(html.contains(QString::fromUtf8("після доставки: 17 дн")));
        QVERIFY(!html.contains(QString::fromUtf8("прострочення оплати не визначено")));
        QVERIFY(!html.contains(QString::fromUtf8("Не сплачено клієнтом")));
        part.installedAtRaw="2026-09-03";
        html=PurchaseDebtAnalysisPage::analysisHtml({debt},{part},QDate(2026,10,3));
        QVERIFY(html.contains(QString::fromUtf8("після доставки: 38 дн")));
        part.dateRaw="2020-08-28"; part.deliveryDateRaw="2020-08-28";
        part.orderOpenedAtRaw="2026-08-31"; part.installedAtRaw="";
        html=PurchaseDebtAnalysisPage::analysisHtml({debt},{part},QDate(2026,10,3));
        QVERIFY(!html.contains("<td>2227</td>"));
        QVERIFY(!html.contains(QString::fromUtf8("не підтверджена")));
        part.dateRaw="2026-08-28"; part.deliveryDateRaw="2026-08-28";
        html=PurchaseDebtAnalysisPage::analysisHtml({debt},{part},QDate(2026,10,3));
        QVERIFY(!html.contains(QString::fromUtf8("кінцеву подію не підтверджено")));
    }
    void supplierReconciliationDialogDefaultsToComputedBalanceAndCanCancel() {
        QVERIFY(Database::setSetting("settings.fin.store_markups", "{\"Store\":0}"));
        PurchaseDebtsPage page; page.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(!page.property("debtDataLoading").toBool(),10000);
        auto* button=page.findChild<QPushButton*>("reconcileSupplierDebts"); QVERIFY(button);
        bool observed=false;
        QTimer modalTimer;
        connect(&modalTimer,&QTimer::timeout,this,[&] {
            auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if(!dialog) return;
            modalTimer.stop();
            auto* table=dialog->findChild<QTableView*>("supplierReconciliationTable");
            if(table && table->model()->rowCount()==1) {
                auto* value=qobject_cast<QDoubleSpinBox*>(table->indexWidget(table->model()->index(0,2)));
                observed=value && value->value()==200.0;
            }
            dialog->reject();
        });
        modalTimer.start(20);
        button->click();
        QTRY_VERIFY_WITH_TIMEOUT(observed && !page.m_reconciliationPending,10000);
        QVERIFY(SupplierDebtReconciliationService::load(db).isEmpty());
        QVERIFY(SupplierDebtReconciliationService::canReconcile());
        page.reconcileDebts();
        QVERIFY(page.m_reconciliationPending);
        ActivityAuditService::setCurrentActor({"master","Master","master","test"});
        QTRY_VERIFY_WITH_TIMEOUT(!page.m_reconciliationPending,10000);
        QVERIFY(!QApplication::activeModalWidget());
        QVERIFY(SupplierDebtReconciliationService::load(db).isEmpty());
        QVERIFY(!SupplierDebtReconciliationService::canReconcile());
    }
    void debtSummarySpacerHasPhysicalMinimum() {
        QTextDocument document;
        document.setHtml(PurchaseDebtsPage::summaryHtml({},40));
        document.setTextWidth(1000);
        auto* table=qobject_cast<QTextTable*>(document.rootFrame()->childFrames().value(0));
        QVERIFY(table); QCOMPARE(table->columns(),6);
        const auto constraints=table->format().columnWidthConstraints();
        QCOMPARE(constraints[3].type(),QTextLength::FixedLength);
        QVERIFY(constraints[3].rawValue()>=40);
        const auto block=table->cellAt(0,3).firstCursorPosition().block();
        QVERIFY(document.documentLayout()->blockBoundingRect(block).width()>=40);
    }
    void debtAnalysisDoesNotExposeForeignOrdersOrUnassignedInventory() {
        QSqlQuery q(db);
        QVERIFY(q.exec("ALTER TABLE work_orders ADD COLUMN master_id TEXT"));
        QVERIFY(q.exec("UPDATE work_orders SET master_id='m1'"));
        QVERIFY(q.exec("INSERT INTO work_orders(number,order_uid,master_id,status) VALUES(2,'order-b','m2','open')"));
        QVERIFY(prepareFixtureOrderState(2));
        QVERIFY(q.exec("INSERT INTO work_order_parts VALUES(3,2,'order-b','2026-09-01','','','Secret shop','','','FOREIGN PART',1,100,100,100,100,0)"));
        WorkshopInventoryPurchaseInput input;
        input.purchasedOn=QDate::currentDate(); input.materialName="PRIVATE INVENTORY"; input.unit="l";
        input.quantity=1; input.unitCost=100; input.supplier="Secret inventory"; input.paidAmount=0;
        QString error;
        QVERIFY(WorkshopInventoryService::addPurchase(db,input,nullptr,&error));
        ActivityAuditService::setCurrentActor({"account-1","Master 1","master","test"});
        PurchaseDebtAnalysisPage page; page.refresh();
        auto* report=page.findChild<QTextBrowser*>("debtAnalysisReport"); QVERIFY(report);
        page.findChild<QTabBar*>("debtAnalysisViews")->setCurrentIndex(2);
        QTRY_VERIFY(report->toPlainText().contains("Part"));
        QVERIFY(!report->toPlainText().contains("FOREIGN PART"));
        QVERIFY(!report->toPlainText().contains("PRIVATE INVENTORY"));
        ActivityAuditService::setCurrentActor({"account-2","Master 2","master","test"});
        page.refresh(); QTRY_VERIFY(report->toPlainText().contains("FOREIGN PART"));
        QVERIFY(!report->toPlainText().contains("PRIVATE INVENTORY"));
    }
    void oldPaymentHistoryMigration() {
        QSqlQuery q(db);
        QVERIFY(q.exec("DROP TABLE IF EXISTS purchase_payment_events"));
        QVERIFY(q.exec("CREATE TABLE purchase_payment_events(id INTEGER PRIMARY KEY,payment_key TEXT,amount_cents INTEGER,actor TEXT,created_at TEXT)"));
        QVERIFY(q.exec("INSERT INTO purchase_payment_events VALUES(1,'part:1:order-a',123,'Admin','2026-09-01 10:00:00')"));
        QString error;
        QVERIFY2(PurchasePaymentService::ensureSchema(db,&error),qPrintable(error));
        QVERIFY(PurchasePaymentService::ensureSchema(db,&error));
        const auto d=PurchasePaymentService::detail(db,"part",1,"order-a");
        const auto history=PurchasePaymentService::history(db,d);
        QCOMPARE(history.size(),1); QCOMPARE(history[0].amountCents,123);
        QVERIFY(!history[0].dueDate.isValid());
    }
    void stockPurchaseNavigationSelectsOldReceipt() {
        QSqlQuery schema(db);
        QVERIFY(schema.exec("ALTER TABLE work_orders ADD COLUMN client TEXT DEFAULT ''"));
        WorkshopInventoryPurchaseInput input;
        input.purchasedOn=QDate::currentDate().addYears(-1); input.materialName="Oil"; input.unit="l";
        input.quantity=8; input.unitCost=25; input.supplier="Store";
        QString error; qint64 id=0;
        QVERIFY2(WorkshopInventoryService::addPurchase(db,input,&id,&error),qPrintable(error));
        WorkshopInventoryPage stock;
        stock.show();
        stock.focusPurchase(id);
        QTest::qWait(120);
        QVERIFY(!QApplication::activeModalWidget());
        auto* tabs=stock.findChild<QTabWidget*>("workshopStockKinds");
        QVERIFY(tabs); QCOMPARE(tabs->currentIndex(),1);
        auto* table=stock.findChild<QTableView*>("inventoryMovementJournal");
        QVERIFY(table); QVERIFY(table->selectionModel()->hasSelection());
    }
    void debtRecordPaymentShortcut() {
        PurchaseDebtAnalysisPage page;
        page.refresh(); page.show();
        QTest::qWait(100);
        auto* report=page.findChild<QTextBrowser*>("debtAnalysisReport");
        QVERIFY(report);
        bool opened=false;
        QTimer::singleShot(0,[&] {
            auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            const auto close=qScopeGuard([&] { if(dialog->isVisible()) dialog->reject(); });
            QCOMPARE(dialog->objectName(),QString("purchaseDebtRecordDialog"));
            auto* go=dialog->findChild<QPushButton*>("goToPurchaseSource");
            QVERIFY(go); QVERIFY(go->isEnabled());
            auto* browser=dialog->findChild<QTextBrowser*>();
            QCOMPARE(browser->lineWrapMode(),QTextEdit::WidgetWidth);
            QVERIFY(browser->toHtml().contains("8pt"));
            QTest::qWait(50);
            QCOMPARE(browser->horizontalScrollBar()->maximum(),0);
            QVERIFY(dialog->grab().save(dir.filePath(QStringLiteral("debt-record-navigation.png"))));
            QTimer paymentReady;
            connect(&paymentReady,&QTimer::timeout,[&] {
                auto* payment=qobject_cast<QDialog*>(QApplication::activeModalWidget());
                if(payment && payment->objectName()=="purchasePaymentDialog") {
                    paymentReady.stop();
                    opened=true;
                    QTimer::singleShot(0,[] {
                        auto* notice=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                        QVERIFY(notice);
                        QCOMPARE(notice->windowTitle(),QString::fromUtf8("Оплату здійснено"));
                        notice->accept();
                    });
                    payment->findChild<QPushButton*>("payInFull")->click();
                }
            });
            paymentReady.start(10);
            dialog->findChild<QPushButton*>("payDebtRecord")->click();
        });
        QVERIFY(QMetaObject::invokeMethod(report,"anchorClicked",Qt::DirectConnection,
            Q_ARG(QUrl,QUrl("record:part:1:order-a"))));
        QVERIFY(opened);
        QCOMPARE(PurchasePaymentService::detail(db,"part",1,"order-a").remainingCents(),0LL);
    }
    void dialogZeroAndDeferral() {
        const auto d=PurchasePaymentService::detail(db,"part",1,"order-a");
        qint64 amount=-1; int days=-1;
        QTimer::singleShot(0,[&] {
            auto* dialog=QApplication::activeModalWidget(); QVERIFY(dialog);
            dialog->findChild<QCheckBox*>("deferPayment")->setChecked(true);
            dialog->findChild<QSpinBox*>("paymentDeferredDays")->setValue(21);
            auto* input=dialog->findChild<QDoubleSpinBox*>("partialPaymentAmount");
            QCOMPARE(input->minimum(),0.0); QCOMPARE(input->value(),0.0);
            QVERIFY(!input->isEnabled());
            QCoreApplication::processEvents();
            QVERIFY(dialog->grab().save("purchase-payment-deferral.png"));
            dialog->findChild<QPushButton*>("payInFull")->click();
        });
        QVERIFY(choosePurchasePayment(nullptr,d,false,&amount,&days));
        QCOMPARE(amount,0); QCOMPARE(days,21);
        QCOMPARE(PurchasePaymentService::detail(db,"part",1,"order-a").paidCents,0);
    }
    void inventoryPaymentCheckboxModes() {
        for (bool defer : {false, true}) for (bool partial : {false, true}) {
            PurchaseDebt purchase;
            purchase.source="inventory"; purchase.name="Antifreeze";
            purchase.totalCents=600000;
            qint64 amount=-1; int days=-1;
            QTimer::singleShot(0,[&] {
                auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
                QVERIFY(dialog);
                const auto close=qScopeGuard([&] { if(dialog->isVisible()) dialog->reject(); });
                dialog->findChild<QCheckBox*>("deferPayment")->setChecked(defer);
                dialog->findChild<QSpinBox*>("paymentDeferredDays")->setValue(8);
                dialog->findChild<QCheckBox*>("partialPayment")->setChecked(partial);
                auto* value=dialog->findChild<QDoubleSpinBox*>("partialPaymentAmount");
                QCOMPARE(value->isEnabled(),partial);
                if(partial) value->setValue(1500);
                dialog->findChild<QPushButton*>("payInFull")->click();
            });
            QVERIFY(choosePurchasePayment(nullptr,purchase,true,&amount,&days));
            QCOMPARE(amount,partial ? 150000LL : (defer ? 0LL : 600000LL));
            QCOMPARE(days,defer ? 8 : 0);
            WorkshopInventoryPurchaseInput input;
            input.purchasedOn=QDate::currentDate(); input.materialName="Antifreeze";
            input.unit="l"; input.quantity=20; input.unitCost=300;
            input.paidAmount=amount/100.0; input.deferredDays=days;
            QString error; qint64 id=0;
            QVERIFY2(WorkshopInventoryService::addPurchase(db,input,&id,&error),qPrintable(error));
            const auto saved=PurchasePaymentService::detail(db,"inventory",id,{});
            QCOMPARE(saved.paidCents,amount);
            QCOMPARE(saved.remainingCents(),600000-amount);
            QCOMPARE(saved.dueDate,defer ? QDate::currentDate().addDays(8) : QDate());
            const auto debts=PurchasePaymentService::debts(db,&error);
            const auto found=std::find_if(debts.cbegin(),debts.cend(),[&](const auto& d) { return d.source=="inventory" && d.id==id; });
            QCOMPARE(found!=debts.cend(),amount<600000);
        }
    }
    void purchasePaymentPreparationOwnsDatabaseBoundary() {
        QString error;
        const auto expected=PurchasePaymentService::orderPartDebts(db,1,&error);
        QVERIFY(error.isEmpty());
        const auto preparation=PurchasePaymentService::prepareOrderPayment(1,&error);
        QVERIFY2(error.isEmpty(),qPrintable(error));
        QCOMPARE(preparation.expectedDebts.size(),expected.size());
        QCOMPARE(preparation.displayedPurchases.size(),2);
        for(int i=0;i<expected.size();++i) {
            QCOMPARE(preparation.expectedDebts[i].id,expected[i].id);
            QCOMPARE(preparation.expectedDebts[i].remainingCents(),expected[i].remainingCents());
        }
        const auto task=SupplierDebtReadService::prepareOrderPayment(1);
        std::atomic_bool cancel{false};
        PurchasePaymentService::OrderPaymentPreparation background;
        QString backgroundError;
        auto* worker=QThread::create([&]{background=task(cancel,&backgroundError);});
        worker->start();
        QTRY_VERIFY_WITH_TIMEOUT(worker->isFinished(),10000);
        worker->wait();delete worker;
        QVERIFY2(backgroundError.isEmpty(),qPrintable(backgroundError));
        QCOMPARE(background.expectedDebts.size(),preparation.expectedDebts.size());
        QCOMPARE(background.displayedPurchases.size(),preparation.displayedPurchases.size());
        cancel.store(true);
        QVERIFY(task(cancel,&backgroundError).expectedDebts.isEmpty());
        QWidget owner;
        bool sawProgress=false,duplicateRejected=false;
        QTimer::singleShot(0,[&]{
            auto* progress=owner.findChild<QProgressDialog*>("orderPaymentPreparation");
            sawProgress=progress!=nullptr;
            duplicateRejected=!openOrderPartsPayment(&owner,1);
            if(progress)progress->reject();
        });
        QVERIFY(!openOrderPartsPayment(&owner,1));
        QVERIFY(sawProgress);QVERIFY(duplicateRejected);
        // A cancelled read must not open payment or change its balance later.
        QTest::qWait(100);
        QVERIFY(!QApplication::activeModalWidget());
        QCOMPARE(PurchasePaymentService::detail("part",1,"order-a",&error).paidCents,0);
        sawProgress=false;duplicateRejected=false;
        QTimer::singleShot(0,[&]{
            auto* progress=owner.findChild<QProgressDialog*>("purchasePaymentPreparation");
            sawProgress=progress!=nullptr;
            duplicateRejected=!openPurchasePayment(&owner,"part",1,"order-a");
            if(progress)progress->reject();
        });
        QVERIFY(!openPurchasePayment(&owner,"part",1,"order-a"));
        QVERIFY(sawProgress);QVERIFY(duplicateRejected);
        QTest::qWait(100);
        QVERIFY(!QApplication::activeModalWidget());
        QCOMPARE(PurchasePaymentService::detail("part",1,"order-a",&error).paidCents,0);
        QVERIFY2(PurchasePaymentService::payOrderParts(1,preparation.expectedDebts,&error,5700),qPrintable(error));
        QCOMPARE(PurchasePaymentService::detail("part",1,"order-a",&error).paidCents,5700);
        RegisteredInvoice empty;QVERIFY(!PurchasePaymentService::canPayInvoice(empty));
        QSqlQuery q(db);
        QVERIFY(q.exec("ALTER TABLE purchase_payment_balances RENAME COLUMN paid_cents TO broken_paid"));
        const auto failed=PurchasePaymentService::prepareOrderPayment(1,&error);
        QVERIFY(!error.isEmpty());QVERIFY(failed.expectedDebts.isEmpty());QVERIFY(failed.displayedPurchases.isEmpty());
        QVERIFY(PurchasePaymentService::prepareOrderPayment(1,nullptr).displayedPurchases.isEmpty());
    }
    void reportPdfPresentationPreservesResultHandling() {
        QDesktopServices::setUrlHandler("file",this,"capturePdfUrl");
        const auto reset=qScopeGuard([]{QDesktopServices::unsetUrlHandler("file");});
        ReportsPdfRenderer::ExportResult result;
        openedPdf={};
        QVERIFY(!ReportsPdfPresentation::openExportedPdf(nullptr,result));
        QVERIFY(openedPdf.isEmpty());
        QWidget parent;
        for(const auto& error:QStringList{QString(),QString::fromUtf8("Не вдалося записати файл")}) {
            result.errorText=error;QString message;
            QTimer::singleShot(0,[&]{
                auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                if(box){message=box->text();box->accept();}
            });
            QVERIFY(!ReportsPdfPresentation::openExportedPdf(&parent,result));
            QCOMPARE(message,error.isEmpty()?QString::fromUtf8("Не вдалося підготувати PDF"):error);
            QVERIFY(openedPdf.isEmpty());
        }
        result.success=true;result.path=" ";
        QVERIFY(!ReportsPdfPresentation::openExportedPdf(nullptr,result));
        QVERIFY(openedPdf.isEmpty());
        result.path=dir.filePath(QString::fromUtf8("Звіт з пробілами.pdf"));
        QVERIFY(ReportsPdfPresentation::openExportedPdf(&parent,result));
        QCOMPARE(openedPdf,QUrl::fromLocalFile(result.path));
    }
    void paymentEntryUsesDisplayedDecimal() {
        for(const auto& locale:QList<QLocale>{QLocale(QLocale::Ukrainian,QLocale::Ukraine),QLocale(QLocale::English,QLocale::UnitedStates)}) {
            for(const auto& example:QList<QPair<double,qint64>>{{0.01,1},{75.01,7501},{90000000000000.0,9000000000000000LL}}) {
                PurchaseDebt debt;debt.source="part";debt.totalCents=9000000000000100LL;
                QVector<PurchaseDebt> group{debt};qint64 amount=-1;bool previewChecked=false;
                QTimer::singleShot(0,[&]{
                    auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());QVERIFY(dialog);
                    const auto close=qScopeGuard([&]{if(dialog->isVisible())dialog->reject();});
                    auto* partial=dialog->findChild<QCheckBox*>("partialPayment");
                    auto* value=dialog->findChild<QDoubleSpinBox*>("partialPaymentAmount");
                    auto* remaining=dialog->findChild<QLabel*>("paymentRemaining");
                    auto* pay=dialog->findChild<QPushButton*>("payInFull");
                    QVERIFY(partial && value && remaining && pay);
                    value->setLocale(locale);partial->setChecked(true);value->setValue(example.first);
                    previewChecked=remaining->text().contains(FormatUtils::moneyCents(debt.totalCents-example.second));
                    pay->click();
                });
                QVERIFY(choosePurchasePayment(nullptr,debt,false,&amount,nullptr,&group));
                QCOMPARE(amount,example.second);QVERIFY(previewChecked);
            }
        }
    }
    void paymentSuccessMessageKeepsExactCents() {
        for(const auto& example:QList<QPair<qint64,QString>>{
                {1,QStringLiteral("0,01")},{1005,QStringLiteral("10,05")},
                {std::numeric_limits<qint64>::max(),QStringLiteral("92 233 720 368 547 758,07")}}) {
            QString displayed;
            QTimer::singleShot(0,[&]{
                auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                if(box){displayed=box->text();box->accept();}
            });
            showPurchasePaymentSaved(nullptr,example.first);
            QCOMPARE(displayed,QString::fromUtf8("Оплату %1 грн успішно збережено.").arg(example.second));
        }
    }
    void groupedHistoryConcurrentSnapshots() {
        QString error;
        const QString path=dir.filePath("history-concurrent.db");
        ScopedSqliteConnection connection(path,"history-concurrent-reader",&error);
        QVERIFY2(connection.isOpen(),qPrintable(error));
        auto local=connection.db();QSqlQuery q(local);
        QVERIFY(q.exec("PRAGMA journal_mode=WAL"));q.finish();
        QVERIFY(q.exec("CREATE TABLE purchase_payment_events(id INTEGER PRIMARY KEY,payment_key TEXT,amount_cents INTEGER,actor TEXT,created_at TEXT)"));
        QVERIFY(local.transaction());
        QVector<PurchaseDebt> selected;
        for(int i=1;i<=1500;++i) {
            PurchaseDebt debt;debt.source="inventory";debt.id=i;selected.append(debt);
            q.prepare("INSERT INTO purchase_payment_events VALUES(?,?,1,'Test','2026-09-16')");
            q.addBindValue(i);q.addBindValue(PurchasePaymentService::key(debt.source,debt.id));
            QVERIFY(q.exec());
        }
        QVERIFY(local.commit());q.finish();
        selected.append(selected.first()); // Repeated selection must not duplicate events.
        const auto connections=QSqlDatabase::connectionNames();
        std::atomic_bool stop{false};std::atomic_int commits{0};
        QString writerError;QSemaphore ready;
        std::thread writer([&]{
            {
                ScopedSqliteConnection writeConnection(path,"history-concurrent-writer",&writerError);
                auto write=writeConnection.db();
                ready.release();
                for(int generation=2;writeConnection.isOpen() && writerError.isEmpty() && !stop.load();++generation) {
                    if(!write.transaction()){writerError=write.lastError().text();break;}
                    QSqlQuery change(write);
                    change.prepare("UPDATE purchase_payment_events SET amount_cents=?");
                    change.addBindValue(generation);
                    if(!change.exec())writerError=change.lastError().text();
                    change.finish();
                    if(!writerError.isEmpty()){write.rollback();break;}
                    if(!write.commit()){writerError=write.lastError().text();write.rollback();break;}
                    ++commits;QThread::msleep(1);
                }
            }
        });
        ready.acquire();
        // Collect evidence first; do not return from a test with a joinable thread.
        QList<QVector<PurchasePaymentEntry>> snapshots;QStringList errors;
        QElapsedTimer timer;timer.start();
        for(int i=0;i<40;++i) {
            snapshots.append(PurchasePaymentService::groupHistory(local,selected,&error));
            errors.append(error);
        }
        const auto elapsed=timer.elapsed();stop=true;writer.join();
        QVERIFY2(writerError.isEmpty(),qPrintable(writerError));QVERIFY(commits.load()>0);
        QSet<qint64> observed;
        for(int i=0;i<snapshots.size();++i) {
            QVERIFY2(errors[i].isEmpty(),qPrintable(errors[i]));
            QCOMPARE(snapshots[i].size(),1500);
            const auto generation=snapshots[i].first().amountCents;observed.insert(generation);
            for(const auto& row:snapshots[i])QCOMPARE(row.amountCents,generation);
        }
        QVERIFY(observed.size()>1);
        QCOMPARE(QSqlDatabase::connectionNames(),connections);
        QVERIFY(q.exec("PRAGMA integrity_check"));QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("ok"));
        qInfo()<<"Grouped history snapshots:"<<snapshots.size()<<"events each:1500 commits:"<<commits.load()<<"generations:"<<observed.size()<<"read elapsed ms:"<<elapsed;
    }
    void groupedPaymentHistoryAndExactFormatting() {
        QCOMPARE(FormatUtils::moneyCents(0),QStringLiteral("0,00"));
        QCOMPARE(FormatUtils::moneyCents(1005),QStringLiteral("10,05"));
        QCOMPARE(FormatUtils::moneyCents(-1005),QStringLiteral("-10,05"));
        QCOMPARE(FormatUtils::moneyCents(std::numeric_limits<qint64>::max()),QStringLiteral("92 233 720 368 547 758,07"));
        QCOMPARE(FormatUtils::moneyCents(std::numeric_limits<qint64>::min()),QStringLiteral("-92 233 720 368 547 758,08"));
        QString error;
        ScopedSqliteConnection connection(dir.filePath("group-history.db"),"group-history",&error);
        QVERIFY2(connection.isOpen(),qPrintable(error));
        auto local=connection.db();QSqlQuery q(local);
        QVERIFY(q.exec("CREATE TABLE purchase_payment_events(id INTEGER PRIMARY KEY,payment_key TEXT,amount_cents,actor TEXT,created_at TEXT)"));
        PurchaseDebt first;first.source="inventory";first.id=1;
        auto second=first;second.id=2;
        QVERIFY(q.exec("INSERT INTO purchase_payment_events VALUES(1,'inventory:1:',1005,'Test','2026-09-16'),(2,'inventory:2:',-1005,'Test','2026-09-16'),(3,'inventory:1:',0,'Test','2026-09-16'),(4,'unrelated','bad','Test','2026-09-16')"));
        auto rows=PurchasePaymentService::groupHistory(local,{first,second,first},&error);
        QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(rows.size(),3);
        QCOMPARE(rows[0].amountCents,1005);QCOMPARE(rows[1].amountCents,-1005);QCOMPARE(rows[2].amountCents,0);
        QVERIFY(q.exec("UPDATE purchase_payment_events SET amount_cents='bad' WHERE id=2"));
        QVERIFY(PurchasePaymentService::groupHistory(local,{first,second},&error).isEmpty());
        QVERIFY(error.contains("№2"));
        QVERIFY(PurchasePaymentService::groupHistory(local,{first,second}).isEmpty());
        QCOMPARE(PurchasePaymentService::groupHistory(local,{first},&error).size(),2);QVERIFY(error.isEmpty());
        error="stale";QVERIFY(PurchasePaymentService::groupHistory(local,{},&error).isEmpty());QVERIFY(error.isEmpty());
    }
    void paymentHistoryRejectsInvalidAmounts() {
        QString error;
        ScopedSqliteConnection connection(dir.filePath("history-amounts.db"),"history-amounts",&error);
        QVERIFY2(connection.isOpen(),qPrintable(error));
        auto local=connection.db();QSqlQuery q(local);
        // No affinity: exercise the actual stored SQLite type, not implicit coercion.
        QVERIFY(q.exec("CREATE TABLE purchase_payment_events(id INTEGER PRIMARY KEY,payment_key TEXT,amount_cents,actor TEXT,created_at TEXT)"));
        PurchaseDebt debt;debt.source="inventory";debt.id=1;
        const auto paymentKey=PurchasePaymentService::key(debt.source,debt.id);
        auto insert=[&](int id,const QString& literal){q.prepare("INSERT INTO purchase_payment_events VALUES(?, ?,"+literal+",'Test','2026-09-16 12:00:00')");q.addBindValue(id);q.addBindValue(paymentKey);return q.exec();};
        QVERIFY(insert(1,"1005"));QVERIFY(insert(2,"0"));QVERIFY(insert(3,"-1005"));
        auto rows=PurchasePaymentService::history(local,debt,&error);
        QVERIFY(error.isEmpty());QCOMPARE(rows.size(),3);
        QCOMPARE(rows[0].amountCents,1005);QCOMPARE(rows[1].amountCents,0);QCOMPARE(rows[2].amountCents,-1005);
        for(const auto& invalid:QStringList{"1.5","NULL","'bad'","'100'","X'3130'","9223372036854775808"}) {
            QVERIFY(insert(4,invalid));
            QVERIFY(PurchasePaymentService::history(local,debt,&error).isEmpty());
            QVERIFY2(error.contains("№4"),qPrintable(error));
            QVERIFY(PurchasePaymentService::history(local,debt).isEmpty());
            QVERIFY(q.exec("DELETE FROM purchase_payment_events WHERE id=4"));
            QCOMPARE(PurchasePaymentService::history(local,debt,&error).size(),3);QVERIFY(error.isEmpty());
        }
        QVERIFY(q.exec("INSERT INTO purchase_payment_events VALUES(5,'unrelated','bad','Test','2026-09-16')"));
        QVERIFY(insert(6,"9223372036854775807"));QVERIFY(insert(7,"-9223372036854775808"));
        rows=PurchasePaymentService::history(local,debt,&error);
        QVERIFY(error.isEmpty());QCOMPARE(rows.size(),5);
        QCOMPARE(rows[3].amountCents,std::numeric_limits<qint64>::max());
        QCOMPARE(rows[4].amountCents,std::numeric_limits<qint64>::min());
    }
    void paymentHistoryFailureAndRetry() {
        QString error;
        auto debt=PurchasePaymentService::detail(db,"part",1,"order-a");
        QVERIFY2(PurchasePaymentService::pay(db,debt,100,&error),qPrintable(error));
        debt=PurchasePaymentService::detail(db,"part",1,"order-a");
        QSqlQuery q(db);
        QVERIFY(q.exec("ALTER TABLE purchase_payment_events RENAME COLUMN amount_cents TO broken_amount"));
        QVERIFY(PurchasePaymentService::history(db,debt,&error).isEmpty());QVERIFY(!error.isEmpty());
        QTimer::singleShot(0,[&] {
            auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());QVERIFY(dialog);
            const auto close=qScopeGuard([dialog]{dialog->reject();});
            auto* button=dialog->findChild<QPushButton*>("togglePaymentHistory");
            auto* table=dialog->findChild<QTableWidget*>("paymentHistory");
            auto* message=dialog->findChild<QLabel*>("paymentHistoryError");
            QVERIFY(button && table && message);
            button->click();
            QVERIFY(message->isVisible());QVERIFY(message->text().contains("Не вдалося"));
            QVERIFY(!table->isVisible());QCOMPARE(table->rowCount(),0);QVERIFY(!button->isChecked());
            QVERIFY(q.exec("ALTER TABLE purchase_payment_events RENAME COLUMN broken_amount TO amount_cents"));
            button->click();
            QVERIFY(!message->isVisible());QVERIFY(table->isVisible());QCOMPARE(table->rowCount(),1);
        });
        qint64 amount=0;QVERIFY(!choosePurchasePayment(nullptr,debt,false,&amount));
        QCOMPARE(PurchasePaymentService::history(db,debt,&error).size(),1);QVERIFY(error.isEmpty());
        QVERIFY(q.exec("ALTER TABLE purchase_payment_events RENAME TO hidden_payment_events"));
        QVERIFY(PurchasePaymentService::history(db,debt,&error).isEmpty());QVERIFY(error.contains("Журнал"));
        QVERIFY(q.exec("ALTER TABLE hidden_payment_events RENAME TO purchase_payment_events"));
        debt.id=999999;
        QVERIFY(PurchasePaymentService::history(db,debt,&error).isEmpty());QVERIFY(error.isEmpty());
    }
    void dialogFontsAndDates() {
        QString error;
        auto d=PurchasePaymentService::detail(db,"part",1,"order-a");
        QVERIFY(PurchasePaymentService::pay(db,d,100,&error,7));
        d=PurchasePaymentService::detail(db,"part",1,"order-a");
        QTimer::singleShot(0,[&] {
            auto* dialog=QApplication::activeModalWidget(); QVERIFY(dialog);
            const auto close=qScopeGuard([dialog] {qobject_cast<QDialog*>(dialog)->reject();});
            bool foundDate=false;
            for(auto* label:dialog->findChildren<QLabel*>()) {
                if(label->text()=="01.09.26") foundDate=true;
                QCOMPARE(label->font().family(),QString("Times New Roman"));
                const int expected=label->objectName()=="paymentHeading" ? 14 : label->objectName()=="paymentBalance" ? 24 : 9;
                QCOMPARE(label->font().pointSize(),expected);
            }
            QVERIFY(foundDate);
            for(auto* button:dialog->findChildren<QPushButton*>()) {
                QCOMPARE(button->font().family(),QString("Times New Roman"));
                QCOMPARE(button->font().pointSize(),9);
            }
            auto* history=dialog->findChild<QTableWidget*>("paymentHistory");
            QVERIFY(history); QVERIFY(!history->isVisible());
            dialog->findChild<QPushButton*>("togglePaymentHistory")->click();
            QVERIFY(history->isVisible()); QCOMPARE(history->rowCount(),1);
            dialog->findChild<QPushButton*>("togglePaymentHistory")->click();
            QTest::qWait(100);
            QVERIFY(dialog->width()<=480+qRound(dialog->logicalDpiX()*20.0/25.4));
            QVERIFY(dialog->height()<=300);
            const auto frame=dialog->frameGeometry();
            const auto screen=dialog->screen()->availableGeometry();
            QVERIFY(qAbs(frame.center().x()-screen.center().x())<=2);
            QVERIFY(qAbs(frame.center().y()-screen.center().y())<=2);
            QVERIFY(dialog->grab().save(dir.filePath(QStringLiteral("purchase-payment-cash.png"))));
            qobject_cast<QDialog*>(dialog)->reject();
        });
        qint64 amount=0;
        QVERIFY(!choosePurchasePayment(nullptr,d,false,&amount));
        QCOMPARE(PurchasePaymentService::history(db,d).size(),1);
    }
    void paymentHistoryInStatistics() {
        QString error;
        QSqlQuery setup(db);
        QVERIFY(setup.exec("CREATE TABLE masters(id TEXT PRIMARY KEY,name TEXT,role TEXT,active INTEGER)"));
        QVERIFY2(StaffAccountService::ensureSchema(db,&error),qPrintable(error));
        ActivityAuditService::setCurrentActor({"test-owner", "Test Owner", "owner", "test-session"});
        auto debt=PurchasePaymentService::detail(db,"part",1,"order-a");
        QVERIFY(PurchasePaymentService::pay(db,debt,10025,&error,7));
        StatsPage page(nullptr,StatsPage::View::WorkLog);
        page.resize(1600,900); page.show(); page.refresh();
        QTest::qWait(100);
        QVERIFY(page.grab().save(dir.filePath(QStringLiteral("work-log-updated.png"))));
        QComboBox* category=nullptr;
        for(auto* combo:page.findChildren<QComboBox*>()) if(combo->findData("payments")>=0) category=combo;
        QVERIFY(category);
        QTRY_VERIFY_WITH_TIMEOUT(!page.property("statisticsLoading").toBool(),10000);
        category->setCurrentIndex(category->findData("payments"));
        QTRY_COMPARE_WITH_TIMEOUT(page.property("filteredEvents").toList().size(),1,10000);
        QTableView* activity=nullptr;
        for(auto* table:page.findChildren<QTableView*>()) {
            if(table->model()->columnCount()==7 && table->model()->rowCount()==1
                && table->model()->index(0,3).data().toString()==QString::fromUtf8("Оплати")) { activity=table; break; }
        }
        QVERIFY(activity);
        QTimer::singleShot(0,[&] {
            auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()); QVERIFY(dialog);
            const auto close=qScopeGuard([dialog] {dialog->reject();});
            auto* table=dialog->findChild<QTableWidget*>(); QVERIFY(table);
            QCOMPARE(table->rowCount(),5);
            QCOMPARE(table->item(1,0)->text(),QString::fromUtf8("Платіж"));
            QCOMPARE(table->item(1,2)->text(),QString::fromUtf8("100.25 грн"));
            QCOMPARE(table->item(4,2)->text(),QDate::currentDate().addDays(7).toString("dd.MM.yy"));
        });
        activity->doubleClicked(activity->model()->index(0,0));
    }
    void spareStockReceiptIssueAndRollback() {
        QSqlQuery q(db);
        for(const auto& sql:QStringList{
            "CREATE TABLE masters(id TEXT PRIMARY KEY,name TEXT,active INTEGER,role TEXT,admin_percent REAL DEFAULT 0,active_from TEXT,active_to TEXT)",
            "INSERT INTO masters(id,name,active,role) VALUES('m1','Test master',1,'master')",
            "ALTER TABLE work_orders ADD COLUMN client TEXT DEFAULT ''",
            "ALTER TABLE work_orders ADD COLUMN works_sum REAL DEFAULT 0",
            "ALTER TABLE work_orders ADD COLUMN parts_sum REAL DEFAULT 0",
            "ALTER TABLE work_orders ADD COLUMN order_year INTEGER DEFAULT 2026",
            "CREATE TABLE work_order_services(id INTEGER PRIMARY KEY,order_number INTEGER,order_uid TEXT,qty REAL,price REAL,amount REAL)",
            "ALTER TABLE work_order_parts ADD COLUMN note TEXT DEFAULT ''",
            "ALTER TABLE work_order_parts ADD COLUMN service_id INTEGER DEFAULT 0",
            "ALTER TABLE work_order_parts ADD COLUMN ordered_by_admin INTEGER DEFAULT 1",
            "ALTER TABLE work_order_parts ADD COLUMN markup_percent REAL DEFAULT 0",
            "ALTER TABLE work_order_parts ADD COLUMN manual_markup_override INTEGER DEFAULT 0",
            "ALTER TABLE work_order_parts ADD COLUMN markup_source TEXT DEFAULT 'policy'",
            "ALTER TABLE work_order_parts ADD COLUMN pricing_updated_at TEXT DEFAULT ''",
            "ALTER TABLE work_order_parts ADD COLUMN pricing_calc_version INTEGER DEFAULT 1"}) QVERIFY2(q.exec(sql),qPrintable(q.lastError().text()));
        WorkshopSpareOrder input;input.name="Brake disc";input.brand="Audi";input.model="Q7";input.plate="AA 1234 AA";
        input.masterId="m1";input.supplier="Spare shop";input.orderedOn=QDate::currentDate().addDays(-2);
        input.quantity=2;input.unitCost=250;input.article="OEM-123";input.dueOn=QDate::currentDate().addDays(12);
        QVERIFY(Database::setSetting("settings.int.directories.invoices_dir",dir.filePath("invoices")));
        QFile invoice(dir.filePath("spare-invoice.pdf"));
        QVERIFY(invoice.open(QIODevice::WriteOnly)); invoice.write("%PDF-1.4 spare invoice"); invoice.close();
        input.invoicePath=invoice.fileName(); input.invoiceName="Supplier invoice.pdf"; input.document="INV-42";
        QVERIFY(q.exec("CREATE TABLE work_order_part_attachments(part_id INTEGER PRIMARY KEY,file_path TEXT,display_name TEXT,invoice_number TEXT,invoice_date TEXT)"));
        QString error;qint64 id=0;
        QVERIFY2(WorkshopSpareStockService::addOrder(db,input,&id,&error),qPrintable(error));
        auto reserve=WorkshopSpareStockService::orders(&error).first();
        QVERIFY(reserve.invoicePath!=input.invoicePath); QVERIFY(QFileInfo::exists(reserve.invoicePath));
        QCOMPARE(reserve.invoiceName,input.invoiceName);
        QVERIFY(invoice.remove()); // The stored copy survives removal of the original file.
        const QString storedInvoice=reserve.invoicePath;
        QVERIFY(!WorkshopSpareStockService::issue(db,reserve,1,1,&error));
        {
            WorkshopInventoryPage receiving;receiving.show();receiving.refresh();
            bool importAvailable=false;
            QTimer reviewAutomation;
            connect(&reviewAutomation,&QTimer::timeout,&receiving,[]{
                auto* preview=QApplication::activeModalWidget();if(!preview||preview->objectName()!="invoiceReviewDialog")return;
                auto* number=preview->findChild<QLineEdit*>("reviewInvoiceNumber");if(number->text().isEmpty())number->setText("TEST-123");
                auto* table=preview->findChild<QTableView*>("invoiceReviewTable");for(int r=0;r<table->model()->rowCount();++r)if(table->model()->index(r,3).data().toString().isEmpty())table->model()->setData(table->model()->index(r,3),"Test Supplier");
                preview->findChild<QPushButton*>("reviewImport")->click();
                if(preview->isVisible()&&!preview->property("reviewDiagnosticWritten").toBool()){
                    preview->setProperty("reviewDiagnosticWritten",true);
                    for(auto* label:preview->findChildren<QLabel*>())if(label->isVisible())qInfo()<<"REVIEW_DIAGNOSTIC"<<label->text();
                }
            });reviewAutomation.start(25);
            QTimer::singleShot(50,&receiving,[&] {
                auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
                importAvailable=dialog && dialog->findChild<QPushButton*>("spareImportInvoice")
                    && dialog->findChild<QLabel*>("spareInvoiceFile");
                if(dialog) {
                    const auto closeDialog=qScopeGuard([dialog]{dialog->reject();});
                    auto* dragHeader=dialog->findChild<QWidget*>("spareHeader");QVERIFY(dragHeader);
                    const QPoint beforeDrag=dialog->pos(),dragLocal(100,20),dragGlobal=dragHeader->mapToGlobal(dragLocal),dragDelta(12,-12);
                    QMouseEvent dragPress(QEvent::MouseButtonPress,dragLocal,dragGlobal,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                    QApplication::sendEvent(dragHeader,&dragPress);
                    QMouseEvent dragMove(QEvent::MouseMove,dragLocal+dragDelta,dragGlobal+dragDelta,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
                    QApplication::sendEvent(dragHeader,&dragMove);
                    QMouseEvent dragRelease(QEvent::MouseButtonRelease,dragLocal,dragGlobal+dragDelta,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                    QApplication::sendEvent(dragHeader,&dragRelease);QCOMPARE(dialog->pos(),beforeDrag+dragDelta);
                    dialog->move(beforeDrag);
                    const QString compactCapture=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
                    if(!compactCapture.isEmpty())dialog->grab().save(QDir(compactCapture).filePath("spare-compact-form.png"));
                    auto* brand=dialog->findChild<QLineEdit*>("spareVehicleBrand");
                    auto* model=dialog->findChild<QLineEdit*>("spareVehicleModel");
                    auto* plate=dialog->findChild<QLineEdit*>("spareVehiclePlate");
                    auto* number=dialog->findChild<QLineEdit*>("spareInvoiceNumber");
                    QVERIFY(brand && model && plate && number);
                    brand->setText(QString::fromUtf8("опель")); model->setText(QString::fromUtf8("вектра"));
                    plate->setText(QString::fromUtf8("аа1234ва"));
                    QMetaObject::invokeMethod(plate,"editingFinished");
                    QCOMPARE(brand->text(),QStringLiteral("OPEL"));
                    QCOMPARE(model->text(),QStringLiteral("VECTRA"));
                    QCOMPARE(plate->text(),QString::fromUtf8("АА 1234 ВА"));
                    number->setText("MANUAL-123");
                    auto* job=dialog->findChild<InvoiceImport::ImportJob*>("spareInvoiceImportJob");
                    importAvailable=importAvailable && job;
                    if(job) {
                        auto* progress=static_cast<InvoiceImportProgressDialog*>(dialog->findChild<QDialog*>("invoiceImportProgressDialog"));
                        InvoiceImport::ImportJobRequest request; request.filePath="test-injected-invoice.pdf"; progress->start(request);
                        InvoiceImport::ImportJobResult result; result.status=InvoiceImport::ImportJobStatus::Succeeded;
                        result.filePath=storedInvoice; result.data.documentDate="2026-10-03";
                        InvoiceImport::ImportedPartDraft draft; draft.name="Brake disc"; draft.oemNumber="OEM-123";draft.qtyText="2";draft.priceText="250";
                        result.data.parsedPartDrafts.append(draft);
                        // This fixture supplies a parsed invoice. Let the missing-file
                        // request finish first so its queued error cannot race the fixture.
                        QTRY_VERIFY_WITH_TIMEOUT(!job->isRunning(),5000);
                        job->finished(result);
                        QTRY_COMPARE_WITH_TIMEOUT(dialog->findChild<QLineEdit*>("sparePartName")->text(),draft.name,5000);
                        importAvailable=importAvailable
                            && dialog->findChild<QLineEdit*>("sparePartName")->text()==draft.name
                            && dialog->findChild<QLineEdit*>("sparePartArticle")->text()==draft.oemNumber
                            && dialog->findChild<QDateEdit*>("spareOrderedDate")->date()==QDate(2026,10,3);
                        draft.name="Oil filter"; draft.oemNumber="OEM-456";draft.orderedAt="02.10.26";
                        result.data.parsedPartDrafts.append(draft);
                        progress->start(request);
                        QTRY_VERIFY_WITH_TIMEOUT(!job->isRunning(),5000);
                        job->finished(result);
                        auto* rows=dialog->findChild<QTableWidget*>("spareAdditionalParts");
                        QVERIFY(rows);
                        QTRY_COMPARE_WITH_TIMEOUT(rows->rowCount(),1,5000);
                        importAvailable=importAvailable
                            && dialog->findChild<QLineEdit*>("sparePartName")->text()=="Brake disc"
                            && rows && rows->rowCount()==1 && rows->item(0,0)->text()==draft.name
                            && rows->item(0,1)->text()==draft.oemNumber
                            && qobject_cast<QComboBox*>(rows->cellWidget(0,4))->findData(1)>=0;
                        dialog->findChild<QPushButton*>("spareReviewParts")->click();
                        QCOMPARE(dialog->findChild<QLineEdit*>("sparePartName")->text(),QString("Brake disc"));
                        QCOMPARE(rows->rowCount(),1);QCOMPARE(rows->item(0,0)->text(),QString("Oil filter"));
                    }
                    dialog->resize(1000,800);
                    const QString captures=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
                    if(!captures.isEmpty()) dialog->grab().save(QDir(captures).filePath("spare-invoice-form.png"));
                    dialog->reject();
                }
            });
            receiving.findChild<QPushButton*>("inventoryAddSpare")->click(); QVERIFY(importAvailable);
            auto* table=receiving.findChild<QTableView*>("workshopSpareTable");QVERIFY(table);
            bool accepted=false;
            QTimer::singleShot(50,&receiving,[&] {
                auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
                auto* receive=dialog?dialog->findChild<QPushButton*>("receiveSpareToStock"):nullptr;
                accepted=receive && receive->isEnabled() && dialog->findChild<QPushButton*>("openSpareInvoice");
                if(accepted) receive->click();else if(dialog) dialog->reject();
            });
            table->doubleClicked(table->model()->index(0,1));QVERIFY(accepted);
            bool alreadyReceived=false;
            QTimer::singleShot(50,&receiving,[&] {
                auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
                auto* receive=dialog?dialog->findChild<QPushButton*>("receiveSpareToStock"):nullptr;
                alreadyReceived=receive && !receive->isEnabled() && receive->text()==QString::fromUtf8("Вже прийнято на склад")
                    && dialog->findChild<QLabel*>("spareReceiptInfo");
                if(dialog) dialog->reject();
            });
            table->doubleClicked(table->model()->index(0,1));QVERIFY(alreadyReceived);
        }
        reserve=WorkshopSpareStockService::orders(&error).first();QCOMPARE(reserve.debtCents,qint64(50000));
        QCOMPARE(reserve.unpaidDays,0);QCOMPARE(reserve.deliveredOn,QDate::currentDate());
        QCOMPARE(WorkshopSpareStockService::eligibleOrders(reserve,&error).size(),1);
        QVector<WorkshopSpareStockService::ImportTarget> targets;
        QVERIFY2(WorkshopSpareStockService::importTargets(&targets,&error),qPrintable(error));
        QCOMPARE(targets.size(),1);QCOMPARE(targets.first().number,1);
        QVERIFY(q.exec("ALTER TABLE work_orders RENAME COLUMN car TO unavailable_car"));
        QVERIFY(!WorkshopSpareStockService::importTargets(&targets,&error));
        QVERIFY(targets.isEmpty());QVERIFY(!error.isEmpty());
        QVERIFY(q.exec("ALTER TABLE work_orders RENAME COLUMN unavailable_car TO car"));
        error.clear();

        QVERIFY(q.exec("CREATE TRIGGER fail_spare_attachment BEFORE INSERT ON work_order_part_attachments BEGIN SELECT RAISE(ABORT,'attachment failure'); END"));
        QVERIFY(!WorkshopSpareStockService::issue(db,reserve,1,1,&error));
        QCOMPARE(WorkshopSpareStockService::orders(&error).first().issued,0.0);
        QVERIFY(q.exec("SELECT COUNT(*) FROM work_order_parts")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),2);
        QVERIFY(q.exec("DROP TRIGGER fail_spare_attachment"));
        QVERIFY2(WorkshopSpareStockService::issue(db,reserve,1,1,&error),qPrintable(error));
        QVERIFY(q.exec("SELECT part_id,file_path,display_name,invoice_number FROM work_order_part_attachments")); QVERIFY(q.next());
        QCOMPARE(q.value(1).toString(),storedInvoice); QCOMPARE(q.value(2).toString(),input.invoiceName);
        QCOMPARE(q.value(3).toString(),input.document);
        QVERIFY(!WorkshopSpareStockService::issue(db,reserve,1,1,&error)); // stale repeated click
        reserve=WorkshopSpareStockService::orders(&error).first();QCOMPARE(reserve.issued,1.0);
        QVERIFY(!WorkshopSpareStockService::issue(db,reserve,2,1,&error));
        QVERIFY(q.exec("SELECT qty,store,installed_at FROM work_order_parts WHERE id>2"));QVERIFY(q.next());
        QCOMPARE(q.value(0).toDouble(),1.0);QCOMPARE(q.value(1).toString(),input.supplier);QVERIFY(q.value(2).toString().isEmpty());
        error.clear();
        const auto debts=PurchasePaymentService::debts(db,&error);QVERIFY2(error.isEmpty(),qPrintable(error));
        int receivedDebts=0;for(const auto& debt:debts) if(debt.supplier=="Spare shop") {++receivedDebts;QCOMPARE(debt.remainingCents(),qint64(50000));}
        QCOMPARE(receivedDebts,1);
        const auto material=WorkshopInventoryService::buildSnapshot(db,QDate::currentDate().addDays(-5),QDate::currentDate());
        QVERIFY2(material.errorText.isEmpty(),qPrintable(material.errorText));QVERIFY(material.balanceRows.isEmpty());
        QVERIFY(q.exec("UPDATE work_orders SET plate='OTHER' WHERE number=1"));
        QVERIFY(!WorkshopSpareStockService::issue(db,reserve,1,1,&error));
        QVERIFY(q.exec("UPDATE work_orders SET plate='AA1234AA',closed_at='2026-01-01' WHERE number=1"));
        QVERIFY(!WorkshopSpareStockService::issue(db,reserve,1,1,&error));
        QCOMPARE(WorkshopSpareStockService::orders(&error).first().issued,1.0);
        QVERIFY(q.exec("CREATE TRIGGER fail_spare_receipt BEFORE INSERT ON workshop_spare_movements BEGIN SELECT RAISE(ABORT,'test receipt failure'); END"));
        input.invoicePath=storedInvoice;
        QVERIFY(!WorkshopSpareStockService::addOrder(db,input,nullptr,&error,QDate::currentDate()));
        QVERIFY(q.exec("SELECT COUNT(*) FROM workshop_spare_orders"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),1);
        QVERIFY(q.exec("DROP TRIGGER fail_spare_receipt"));
        QVERIFY2(WorkshopSpareStockService::addOrder(db,input,nullptr,&error,QDate::currentDate()),qPrintable(error));
        QCOMPARE(WorkshopSpareStockService::orders(&error).first().received,2.0);

        WorkshopInventoryPurchaseInput materialInput;
        materialInput.materialName=input.name;materialInput.unit=QString::fromUtf8("шт.");
        materialInput.purchasedOn=QDate::currentDate();materialInput.quantity=10;materialInput.unitCost=10;
        QVERIFY2(WorkshopInventoryService::addPurchase(db,materialInput,nullptr,&error),qPrintable(error));
        QVERIFY(q.exec("UPDATE work_order_parts SET installed_at=date('now') WHERE id>2"));
        const auto independentMaterial=WorkshopInventoryService::buildSnapshot(db,QDate::currentDate(),QDate::currentDate());
        QVERIFY2(independentMaterial.errorText.isEmpty(),qPrintable(independentMaterial.errorText));
        QCOMPARE(independentMaterial.balanceRows.size(),1);
        QCOMPARE(independentMaterial.balanceRows.first().balanceQuantity,10.0);
        QVERIFY(q.exec("UPDATE work_orders SET closed_at='',status='open' WHERE number=1"));
        QVERIFY(q.exec("SELECT part_number,note,purchase_price FROM work_order_parts WHERE id>2"));QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(),input.article);QVERIFY(q.value(1).toString().contains(input.supplier));QCOMPARE(q.value(2).toDouble(),250.0);
        q.finish();
        QVERIFY(q.exec("SELECT part_id FROM workshop_spare_movements WHERE operation='issue'"));
        QVERIFY(q.next()); const int issuedPart = q.value(0).toInt(); q.finish();
        QVERIFY(q.exec("CREATE TRIGGER fail_spare_cancel BEFORE UPDATE OF revision ON workshop_spare_orders "
                       "BEGIN SELECT RAISE(ABORT,'fixture cancel revision failure'); END"));
        error.clear();
        QVERIFY(!WorkshopSpareStockService::cancelDraftIssue(db,id,1,&error));
        QVERIFY(q.exec("SELECT part_id FROM workshop_spare_movements WHERE operation='issue'"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),issuedPart); q.finish();
        QVERIFY(q.exec(QString("SELECT COUNT(*) FROM work_order_parts WHERE id=%1").arg(issuedPart)));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),1); q.finish();
        QVERIFY2(error.contains("fixture cancel revision failure"),qPrintable(error));
        QVERIFY(q.exec("DROP TRIGGER fail_spare_cancel"));
        error.clear();
        QVERIFY2(WorkshopSpareStockService::cancelDraftIssue(db,id,1,&error),qPrintable(error));
        QVERIFY(QFileInfo::exists(storedInvoice));
        for(const auto& order:WorkshopSpareStockService::orders(&error)) if(order.id==id) {
            QCOMPARE(order.issued,0.0);QCOMPARE(order.received,2.0);QCOMPARE(order.debtCents,qint64(50000));
        }
        WorkshopInventoryPage page;page.resize(1400,800);page.show();
        auto* stock=page.findChild<QTableView*>("workshopSpareTable");QVERIFY(stock);
        page.findChild<QTabWidget*>("workshopStockKinds")->setCurrentIndex(0);
        for(int days:{0,2,3,4,5,7,8}) {
            q.prepare("UPDATE workshop_spare_movements SET moved_on=? WHERE spare_id=? AND operation='receipt'");
            q.addBindValue(QDate::currentDate().addDays(-days).toString(Qt::ISODate));q.addBindValue(id);QVERIFY(q.exec());
            for(const auto& order:WorkshopSpareStockService::orders(&error)) if(order.id==id) QCOMPARE(order.unpaidDays,days);
            page.markDirty();page.refresh();
            QCOMPARE(stock->model()->headerData(9,Qt::Horizontal).toString(),QString::fromUtf8("Протерміновано"));
            int row=0;while(row<stock->model()->rowCount() && stock->model()->index(row,0).data().toLongLong()!=id) ++row;
            QVERIFY(row<stock->model()->rowCount());QCOMPARE(stock->model()->index(row,9).data().toInt(),days);
            if(days>=3) {
                QImage image(160,40,QImage::Format_RGB32);image.fill(Qt::white);QPainter painter(&image);
                QStyleOptionViewItem option;option.rect=QRect(0,0,160,40);option.state=QStyle::State_Enabled|QStyle::State_Selected;
                stock->itemDelegate()->paint(&painter,option,stock->model()->index(row,9));painter.end();
                QCOMPARE(image.pixelColor(150,10),days>=5?QColor(255,160,160):QColor(255,235,120));
            }
            if(days==5) {
                QCoreApplication::processEvents();
                QVERIFY(page.grab().save(QCoreApplication::applicationDirPath()+"/spare-overdue.png"));
            }
        }
        const auto receipt=WorkshopSpareStockService::receipts(id,&error).first();
        auto debt=PurchasePaymentService::detail(db,"inventory",receipt.purchaseId,{},&error);
        QVERIFY2(PurchasePaymentService::pay(db,debt,10000,&error),qPrintable(error));
        for(const auto& order:WorkshopSpareStockService::orders(&error)) if(order.id==id) QCOMPARE(order.unpaidDays,8);
        debt=PurchasePaymentService::detail(db,"inventory",receipt.purchaseId,{},&error);
        QVERIFY2(PurchasePaymentService::pay(db,debt,debt.remainingCents(),&error),qPrintable(error));
        for(const auto& order:WorkshopSpareStockService::orders(&error)) if(order.id==id) {QCOMPARE(order.unpaidDays,-1);QCOMPARE(order.debtCents,qint64(0));}
        page.markDirty();page.refresh();
        for(int row=0;row<stock->model()->rowCount();++row) if(stock->model()->index(row,0).data().toLongLong()==id)
            QCOMPARE(stock->model()->index(row,9).data().toString(),QString::fromUtf8("—"));
        auto* journal=page.findChild<QTableView*>("inventoryMovementJournal");QVERIFY(journal);
        QCOMPARE(stock->horizontalHeader()->sectionResizeMode(1),QHeaderView::Interactive);
        QCOMPARE(journal->horizontalHeader()->sectionResizeMode(3),QHeaderView::Interactive);
        stock->setColumnWidth(1,317);journal->setColumnWidth(3,289);
        const int narrow=journal->horizontalHeader()->minimumSectionSize();journal->setColumnWidth(4,narrow);
        page.markDirty();page.refresh();page.resize(1100,700);QCoreApplication::processEvents();
        QCOMPARE(stock->columnWidth(1),317);QCOMPARE(journal->columnWidth(3),289);
        QCOMPARE(journal->columnWidth(4),narrow);
        {
            WorkshopInventoryPage reopened;reopened.show();reopened.refresh();QCoreApplication::processEvents();
            QCOMPARE(reopened.findChild<QTableView*>("workshopSpareTable")->columnWidth(1),317);
            QCOMPARE(reopened.findChild<QTableView*>("inventoryMovementJournal")->columnWidth(3),289);
            QCOMPARE(reopened.findChild<QTableView*>("inventoryMovementJournal")->columnWidth(4),narrow);
        }
        ActivityAuditService::setCurrentActor({"master","Master","master","test"});
        QVERIFY(!WorkshopSpareStockService::addOrder(db,input,nullptr,&error));
    }
    void spareBatchMixedInvoiceIsAtomic() {
        spareStockReceiptIssueAndRollback();
        if(QTest::currentTestFailed()) return;
        ActivityAuditService::setCurrentActor({"owner","Owner","owner","test"});
        QString error;
        auto input=WorkshopSpareStockService::orders(db,&error).first();
        input.name="Batch warehouse part";input.document="BATCH-REPLAY-TEST";
        auto direct=input; direct.name="Batch direct part";
        QSqlQuery q(db);
        const auto count=[&](const QString& table){q.exec("SELECT COUNT(*) FROM "+table);q.next();int n=q.value(0).toInt();q.finish();return n;};
        const int stocks=count("workshop_spare_orders"), parts=count("work_order_parts"), payments=count("purchase_payment_events");
        QVERIFY(!WorkshopSpareStockService::addBatch(db,{{input,0},{direct,999999}},QDate::currentDate(),&error));
        QCOMPARE(count("workshop_spare_orders"),stocks);
        QCOMPARE(count("work_order_parts"),parts);
        QVERIFY(q.exec("CREATE TRIGGER fail_batch_attachment BEFORE INSERT ON work_order_part_attachments BEGIN SELECT RAISE(ABORT,'batch attachment failure'); END"));
        QVERIFY(!WorkshopSpareStockService::addBatch(db,{{input,0},{direct,1}},QDate::currentDate(),&error));
        QCOMPARE(count("workshop_spare_orders"),stocks);
        QCOMPARE(count("work_order_parts"),parts);
        QVERIFY(q.exec("DROP TRIGGER fail_batch_attachment"));
        QVERIFY(!WorkshopSpareStockService::addBatch(db,{{direct,1},{input,999999}},QDate::currentDate(),&error));
        QCOMPARE(count("workshop_spare_orders"),stocks);QCOMPARE(count("work_order_parts"),parts);
        QCOMPARE(count("purchase_payment_events"),payments);
        QVERIFY2(WorkshopSpareStockService::addBatch(db,{{input,0},{direct,1}},QDate::currentDate(),&error),qPrintable(error));
        QCOMPARE(count("workshop_spare_orders"),stocks+1);
        QCOMPARE(count("work_order_parts"),parts+1);
        QCOMPARE(count("purchase_payment_events"),payments+1); // Warehouse receipt records a zero-payment initialization.
        QVERIFY(!WorkshopSpareStockService::addBatch(db,{{input,0},{direct,1}},QDate::currentDate(),&error));
        QCOMPARE(count("workshop_spare_orders"),stocks+1);QCOMPARE(count("work_order_parts"),parts+1);QCOMPARE(count("purchase_payment_events"),payments+1);
        QVERIFY(q.exec("SELECT amount_cents FROM purchase_payment_events ORDER BY id DESC LIMIT 1"));
        QVERIFY(q.next());QCOMPARE(q.value(0).toLongLong(),qint64(0));q.finish();
        QVERIFY(q.exec("SELECT order_number,qty,purchase_price,installed_at FROM work_order_parts WHERE name='Batch direct part'"));
        QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),1);QCOMPARE(q.value(1).toDouble(),input.quantity);
        QCOMPARE(q.value(2).toDouble(),input.unitCost);QVERIFY(q.value(3).toString().isEmpty());q.finish();
        const auto reserve=WorkshopSpareStockService::orders(db,&error).first();
        QCOMPARE(reserve.name,input.name);QCOMPARE(reserve.received,input.quantity);QCOMPARE(reserve.issued,0.0);
        QVERIFY2(WorkshopSpareStockService::addBatch(db,{{input,0}},QDate(),&error),qPrintable(error));
        QCOMPARE(WorkshopSpareStockService::orders(db,&error).first().received,0.0);
    }
    void warehouseHidesFullyIssuedSpares() {
        spareStockReceiptIssueAndRollback();
        if(QTest::currentTestFailed()) return;
        ActivityAuditService::setCurrentActor({"owner","Owner","owner","test"});
        QString error;
        const auto original=WorkshopSpareStockService::orders(db,&error);
        QVERIFY(error.isEmpty());QVERIFY(!original.isEmpty());
        auto item=original.first();QVERIFY(item.received-item.issued>1);
        WorkshopInventoryPage page;page.show();page.refresh();
        auto* table=page.findChild<QTableView*>("workshopSpareTable");QVERIFY(table);
        page.resize(1300,750);
        page.findChild<QTabWidget*>("workshopStockKinds")->setCurrentIndex(0);
        page.refresh();QCoreApplication::processEvents();
        auto* previous=page.findChild<QWidget*>("inventoryPeriodTile");
        for(int i=0;i<4;++i) {
            auto* tile=page.findChild<QWidget*>(QString("inventorySpareTile%1").arg(i));
            QVERIFY(tile && tile->isVisible());
            QVERIFY(tile->geometry().left()>previous->geometry().right());
            QCOMPARE(tile->height(),previous->height());
            QVERIFY(!page.findChild<QLabel*>(QString("inventorySpareValue%1").arg(i))->text().isEmpty());
            previous=tile;
        }
        auto* actions=page.findChild<QWidget*>("inventorySpareActionsTile");
        QVERIFY(actions->geometry().left()>previous->geometry().right());
        QVERIFY(actions->isAncestorOf(page.findChild<QPushButton*>("inventoryAddSpare")));
        QVERIFY(!page.findChild<QWidget*>("inventoryMetricTile0")->isVisible());
        if(!qEnvironmentVariable("CS_TEST_SCREENSHOTS").isEmpty())
            page.grab().save(qEnvironmentVariable("CS_TEST_SCREENSHOTS")+"/spare-tiles.png");
        auto visible=[&]{for(int row=0;row<table->model()->rowCount();++row)
            if(table->model()->index(row,0).data().toLongLong()==item.id)return true;return false;};
        QVERIFY(visible());
        QCOMPARE(table->model()->headerData(6,Qt::Horizontal).toString(),QString::fromUtf8("Замовлено\nПрийнято\nВидано"));
        QVERIFY2(WorkshopSpareStockService::issue(db,item,1,1,&error),qPrintable(error));
        page.markDirty();page.refresh();QVERIFY(visible());
        for(const auto& updated:WorkshopSpareStockService::orders(db,&error))if(updated.id==item.id)item=updated;
        QVERIFY2(WorkshopSpareStockService::issue(db,item,item.received-item.issued,1,&error),qPrintable(error));
        page.markDirty();page.refresh();QVERIFY(!visible());
        QCOMPARE(WorkshopSpareStockService::orders(db,&error).size(),original.size());
        QVERIFY2(WorkshopSpareStockService::cancelDraftIssue(db,item.id,1,&error),qPrintable(error));
        page.markDirty();page.refresh();QVERIFY(visible());
    }
    void spareStockDifferentVehicleConfirmation() {
        spareStockReceiptIssueAndRollback();
        if(QTest::currentTestFailed()) return;
        ActivityAuditService::setCurrentActor({"owner","Owner","owner","test"});
        QSqlQuery q(db);
        QVERIFY(q.exec("UPDATE work_orders SET order_number=474,plate='BB4321BB',car='OPEL VECTRA',vin='',closed_at='',status='open' WHERE number=1"));
        WorkshopSpareOrder input;
        input.name="Gasket";input.article="OEM-OTHER";input.brand=QString::fromUtf8("опель");input.model=QString::fromUtf8("вектра");
        input.plate="AA1234AA";input.masterId="m1";input.supplier="Supplier";input.document="INV-OTHER";
        input.orderedOn=QDate::currentDate();input.quantity=1;input.unitCost=323;
        QString error;
        QVERIFY2(WorkshopSpareStockService::addOrder(db,input,nullptr,&error,QDate::currentDate()),qPrintable(error));
        auto reserve=WorkshopSpareStockService::orders(db,&error).first();
        bool different=false;QString destination;
        QVERIFY(WorkshopSpareStockService::checkIssueTarget(db,reserve,1,&different,&destination,&error));
        QVERIFY(different);QVERIFY(destination.contains("474"));
        QCOMPARE(WorkshopSpareStockService::eligibleOrders(db,reserve,&error).size(),1);
        QVERIFY(!WorkshopSpareStockService::issue(db,reserve,1,1,&error));
        QCOMPARE(WorkshopSpareStockService::orders(db,&error).first().revision,reserve.revision);
        for(const auto& car:{QString("BMW M550i"),QString("OPEL INSIGNIA")}) {
            q.prepare("UPDATE work_orders SET car=? WHERE number=1");q.addBindValue(car);QVERIFY(q.exec());
            QVERIFY(WorkshopSpareStockService::checkIssueTarget(db,reserve,1,&different,&destination,&error));
            QVERIFY(different);
            QVERIFY(!WorkshopSpareStockService::issue(db,reserve,1,1,&error));
            QCOMPARE(WorkshopSpareStockService::eligibleOrders(db,reserve,&error).size(),1);
        }
        QVERIFY(q.exec("UPDATE work_orders SET car='OPEL VECTRA',closed_at='2026-10-01' WHERE number=1"));
        QVERIFY(!WorkshopSpareStockService::issue(db,reserve,1,1,&error,true));
        QVERIFY(q.exec("UPDATE work_orders SET closed_at='' WHERE number=1"));
        WorkshopInventoryPage page;page.resize(1400,800);page.show();page.refresh();
        page.findChild<QTabWidget*>("workshopStockKinds")->setCurrentIndex(0);
        auto* table=page.findChild<QTableView*>("workshopSpareTable");QVERIFY(table);
        QVERIFY(table->model()->rowCount()>0);
        bool cleared=false,declined=false,confirmed=false;
        QTimer::singleShot(50,&page,[&] {
            auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if(!dialog) return;
            auto* target=dialog->findChild<QComboBox*>("spareIssueOrder");
            auto* issue=dialog->findChild<QPushButton*>("reservePrimary");
            if(!target || !issue) {dialog->reject();return;}
            const QString capture=qEnvironmentVariable("CS_RESERVE_QA_DIR");
            if(!capture.isEmpty()) {QDir().mkpath(capture);dialog->grab().save(QDir(capture).filePath("reserve-issue.png"));}
            auto* overview=dialog->findChild<QWidget*>("reserveOrderStrip");
            auto* balance=dialog->findChild<QWidget*>("reserveBalanceStrip");
            const bool overviewShown=overview && overview->isVisible() && overview->findChildren<QFrame*>("reserveMetric").size()==4;
            const bool initiallyEmpty=target->currentText().isEmpty() && overviewShown;
            target->setCurrentIndex(1);
            const bool balanceShown=balance && balance->isVisible() && !overview->isVisible()
                && balance->findChildren<QFrame*>("reserveMetric").size()==4;
            if(!capture.isEmpty()) dialog->grab().save(QDir(capture).filePath("reserve-stock.png"));
            QTest::mouseClick(target->lineEdit(),Qt::LeftButton);
            cleared=initiallyEmpty && balanceShown && target->currentText().isEmpty() && overview->isVisible();
            QTest::keyClicks(target->lineEdit(),"474");
            QTimer::singleShot(0,dialog,[&] {
                auto* message=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                if(message) {declined=message->text().contains("BB4321BB");message->button(QMessageBox::No)->click();}
            });
            issue->click();
            declined=declined && WorkshopSpareStockService::orders(db,&error).first().issued==0;
            QTimer::singleShot(0,dialog,[&] {
                auto* message=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                if(message) {confirmed=true;message->button(QMessageBox::Yes)->click();}
            });
            issue->click();
            if(dialog->isVisible()) dialog->reject();
        });
        table->doubleClicked(table->model()->index(0,1));
        QVERIFY(cleared);QVERIFY(declined);QVERIFY(confirmed);
        QCOMPARE(WorkshopSpareStockService::orders(db,&error).first().issued,1.0);
        QVERIFY(q.exec("SELECT p.qty,p.purchase_price,a.invoice_number FROM work_order_parts p JOIN work_order_part_attachments a ON a.part_id=p.id WHERE p.part_number='OEM-OTHER'"));
        QVERIFY(q.next());QCOMPARE(q.value(0).toDouble(),1.0);QCOMPARE(q.value(1).toDouble(),323.0);QCOMPARE(q.value(2).toString(),QString("INV-OTHER"));
        QVERIFY(!WorkshopSpareStockService::issue(db,reserve,1,1,&error,true));
    }
    void spareMultiplePartsWithInvoicesToOtherVehicle() {
        spareStockReceiptIssueAndRollback();
        if(QTest::currentTestFailed()) return;
        ActivityAuditService::setCurrentActor({"owner","Owner","owner","test"});
        QSqlQuery q(db);QString error;
        QVERIFY(q.exec("UPDATE work_orders SET plate='BB4321BB',car='BMW X5',vin='',closed_at='',status='open' WHERE number=1"));
        QFile source(dir.filePath("multiple-reserve-invoice.pdf"));
        QVERIFY(source.open(QIODevice::WriteOnly));source.write("%PDF-1.4 test invoice");source.close();
        for(int i=0;i<2;++i) {
            WorkshopSpareOrder input;input.name=QString("Multi part %1").arg(i);input.article=QString("MULTI-%1").arg(i);
            input.brand="Audi";input.model="Q5";input.plate="AA1234AA";input.masterId="m1";input.supplier="Test supplier";
            input.quantity=2;input.unitCost=123.45;input.orderedOn=QDate::currentDate();input.document="MULTI-INVOICE";
            input.invoicePath=source.fileName();input.invoiceName="multiple.pdf";
            qint64 id=0;QVERIFY2(WorkshopSpareStockService::addOrder(db,input,&id,&error,QDate::currentDate()),qPrintable(error));
            WorkshopSpareOrder reserve;for(const auto& item:WorkshopSpareStockService::orders(db,&error))if(item.id==id)reserve=item;
            QVERIFY(reserve.id);
            QVERIFY(!WorkshopSpareStockService::issue(db,reserve,2,1,&error));
            QVERIFY2(WorkshopSpareStockService::issue(db,reserve,2,1,&error,true),qPrintable(error));
            QVERIFY(!WorkshopSpareStockService::issue(db,reserve,2,1,&error,true));
            q.prepare("SELECT p.qty,a.file_path,a.invoice_number FROM work_order_parts p JOIN work_order_part_attachments a ON a.part_id=p.id WHERE p.part_number=? AND p.order_number=1");
            q.addBindValue(input.article);QVERIFY(q.exec());QVERIFY(q.next());QCOMPARE(q.value(0).toDouble(),2.0);
            QCOMPARE(q.value(1).toString(),reserve.invoicePath);QVERIFY(QFileInfo::exists(q.value(1).toString()));
            QCOMPARE(q.value(2).toString(),input.document);QVERIFY(!q.next());
            for(const auto& item:WorkshopSpareStockService::orders(db,&error))if(item.id==id){QCOMPARE(item.issued,2.0);QCOMPARE(item.debtCents,reserve.debtCents);}
        }
    }
    void spareStockOptionalPlate() {
        spareStockReceiptIssueAndRollback();
        if(QTest::currentTestFailed()) return;
        ActivityAuditService::setCurrentActor({"owner","Owner","owner","test"});
        WorkshopSpareOrder input;
        input.name="Filter";input.brand="Opel";input.model="Vectra";
        input.plate="";input.masterId="m1";input.supplier="Supplier";
        input.orderedOn=QDate::currentDate();input.quantity=1;input.unitCost=10;
        QString error;qint64 id=0;
        QVERIFY2(WorkshopSpareStockService::addOrder(db,input,&id,&error),qPrintable(error));
        QSqlQuery q(db);q.prepare("SELECT plate FROM workshop_spare_orders WHERE id=?");q.addBindValue(id);
        QVERIFY(q.exec());QVERIFY(q.next());QVERIFY(q.value(0).toString().isEmpty());q.finish();
        QVERIFY(q.exec("UPDATE work_orders SET plate='',car='Toyota Corolla',closed_at='',status='open' WHERE number=1"));
        bool targetFound=false;
        for(const auto& order:WorkshopSpareStockService::eligibleOrders(db,input,&error)) targetFound |= order.first==1;
        QVERIFY(targetFound);
        bool differentVehicle=false;QString targetVehicle;
        QVERIFY2(WorkshopSpareStockService::checkIssueTarget(db,input,1,&differentVehicle,&targetVehicle,&error),qPrintable(error));
        QVERIFY(differentVehicle);QVERIFY(!targetVehicle.isEmpty());
        WorkshopInventoryPage page;page.resize(1200,800);page.show();bool checked=false;
        QTimer::singleShot(100,&page,[&] {
            auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)return;
            auto* header=dialog->findChild<QWidget*>("spareHeader");
            auto* plate=dialog->findChild<QLineEdit*>("spareVehiclePlate");
            checked=header && plate && header->height()>20;
            for(auto* label:dialog->findChildren<QLabel*>())if(label->text().contains(QString::fromUtf8("Держномер")))checked=checked && !label->text().contains('*');
            dialog->grab().save(dir.filePath(QStringLiteral("spare-optional-plate.png")));dialog->reject();
        });
        auto* add=page.findChild<QPushButton*>("inventoryAddSpare");QVERIFY(add);add->click();QVERIFY(checked);
    }
    void spareStockSupplierMarkupOnIssue() {
        spareStockReceiptIssueAndRollback();
        if(QTest::currentTestFailed()) return;
        ActivityAuditService::setCurrentActor({"owner","Owner","owner","test"});
        QSqlQuery q(db);
        QVERIFY(q.exec("UPDATE work_orders SET plate='AA1234AA',closed_at='',status='open' WHERE number=1"));
        QVERIFY(Database::setSetting("settings.fin.store_markups",QStringLiteral("{\"Spare shop\":25,\"База\":0}")));
        WorkshopSpareOrder input;
        input.name="Spark plug";input.article="SPARK-602";input.brand="Audi";input.model="Q7";
        input.plate="AA 1234 AA";input.masterId="m1";input.supplier="Spare shop";
        input.orderedOn=QDate::currentDate();input.quantity=4;input.unitCost=602;input.document="MARKUP-602";
        QString error;qint64 id=0;
        QVERIFY2(WorkshopSpareStockService::addOrder(db,input,&id,&error,QDate::currentDate()),qPrintable(error));
        auto reserve=[&] { for(const auto& item:WorkshopSpareStockService::orders(db,&error)) if(item.id==id)return item;return WorkshopSpareOrder{}; };
        const auto before=reserve();QCOMPARE(before.received,4.0);
        QVERIFY2(WorkshopSpareStockService::issue(db,before,1,1,&error),qPrintable(error));
        QVERIFY(!WorkshopSpareStockService::issue(db,before,1,1,&error));
        QCOMPARE(reserve().issued,1.0);QCOMPARE(reserve().debtCents,before.debtCents);
        QVERIFY2(WorkshopSpareStockService::issue(db,reserve(),2,1,&error),qPrintable(error));
        QCOMPARE(reserve().issued,3.0);QCOMPARE(reserve().debtCents,before.debtCents);
        QVERIFY(q.exec("SELECT p.qty,p.price,p.amount,p.purchase_price,p.purchase_amount,p.markup_percent,p.store,a.invoice_number FROM work_order_parts p JOIN work_order_part_attachments a ON a.part_id=p.id WHERE p.part_number='SPARK-602' ORDER BY p.id"));
        QVERIFY(q.next());QCOMPARE(q.value(0).toDouble(),1.0);QCOMPARE(q.value(1).toDouble(),752.5);
        QCOMPARE(q.value(2).toDouble(),752.5);QCOMPARE(q.value(3).toDouble(),602.0);QCOMPARE(q.value(4).toDouble(),602.0);
        QCOMPARE(q.value(5).toDouble(),25.0);QCOMPARE(q.value(6).toString(),input.supplier);QCOMPARE(q.value(7).toString(),input.document);
        QVERIFY(q.next());QCOMPARE(q.value(0).toDouble(),2.0);QCOMPARE(q.value(1).toDouble(),752.5);
        QCOMPARE(q.value(2).toDouble(),1505.0);QCOMPARE(q.value(4).toDouble(),1204.0);QCOMPARE(q.value(7).toString(),input.document);
        QVERIFY(!q.next());
    }
    void spareStockManualInvoiceNumber() {
        spareStockReceiptIssueAndRollback();
        if(QTest::currentTestFailed()) return;
        ActivityAuditService::setCurrentActor({"owner","Owner","owner","test"});
        QSqlQuery q(db);
        QVERIFY(q.exec("UPDATE work_orders SET plate='AA1234BA',closed_at='',status='open' WHERE number=1"));
        WorkshopSpareOrder input;
        input.name="Filter"; input.article="OEM-MANUAL";
        input.brand=QString::fromUtf8("опель"); input.model=QString::fromUtf8("вектра");
        input.plate=QString::fromUtf8("аа1234ва"); input.masterId="m1"; input.supplier="Supplier";
        input.document="BATCH-123"; input.orderedOn=QDate::currentDate(); input.quantity=1; input.unitCost=100;
        QString error;
        for (int i=0;i<2;++i) {
            QVERIFY2(WorkshopSpareStockService::addOrder(db,input,nullptr,&error,QDate::currentDate()),qPrintable(error));
            const auto stock=WorkshopSpareStockService::orders(db,&error).first();
            QCOMPARE(stock.brand,QStringLiteral("OPEL")); QCOMPARE(stock.model,QStringLiteral("VECTRA"));
            QCOMPARE(stock.plate,QString::fromUtf8("АА 1234 ВА")); QCOMPARE(stock.document,input.document);
            QVERIFY2(WorkshopSpareStockService::issue(db,stock,1,1,&error),qPrintable(error));
        }
        QVERIFY(q.exec("SELECT invoice_number,invoice_date,file_path FROM work_order_part_attachments WHERE invoice_number='BATCH-123'"));
        int count=0;
        while(q.next()) {
            ++count; QCOMPARE(q.value(1).toString(),input.orderedOn.toString(Qt::ISODate));
            QVERIFY(q.value(2).toString().isEmpty());
        }
        QCOMPARE(count,2);
        q.finish();
        auto findInvoice=[&](const QString& number) {
            const auto invoices=InvoiceRegistryService::load(db,&error);
            for(const auto& invoice:invoices) if(invoice.number==number) return invoice;
            return RegisteredInvoice{};
        };
        auto invoice=findInvoice(input.document);
        QVERIFY2(error.isEmpty(),qPrintable(error));
        QCOMPARE(invoice.parts.size(),2);QCOMPARE(invoice.totalCents,qint64(20000));
        QCOMPARE(invoice.paidCents,qint64(0));QCOMPARE(invoice.supplier,input.supplier);
        for(const auto& part:invoice.parts) {
            QString number;
            OrderAttachmentService::partAttachmentPath(db,part.id,nullptr,&number);
            QCOMPARE(number,input.document);
            QVERIFY(!PartsQueryService::loadRowById(db,part.id,&error).paid);
        }
        QVERIFY(q.exec("CREATE TRIGGER fail_stock_payment BEFORE UPDATE ON purchase_payment_balances WHEN NEW.payment_key LIKE 'inventory:%' BEGIN SELECT RAISE(ABORT,'test rollback'); END"));
        QVERIFY(!PurchasePaymentService::payInvoice(db,invoice,&error));
        QVERIFY(q.exec("DROP TRIGGER fail_stock_payment"));error.clear();
        QCOMPARE(findInvoice(input.document).paidCents,qint64(0));
        QVERIFY2(PurchasePaymentService::payInvoice(db,invoice,&error),qPrintable(error));
        QCOMPARE(findInvoice(input.document).paidCents,qint64(20000));
        for(const auto& stock:WorkshopSpareStockService::orders(db,&error)) if(stock.document==input.document) {
            QCOMPARE(stock.debtCents,qint64(0));QCOMPARE(stock.unpaidDays,-1);
        }
        for(const auto& part:invoice.parts) QVERIFY(PartsQueryService::loadRowById(db,part.id,&error).paid);
        QVERIFY(!PurchasePaymentService::payInvoice(db,invoice,&error)); // Stale confirmation cannot pay twice.
        error.clear();

        // Two orders share one receipt; paying the second must not pay the first or unissued stock.
        QVERIFY(q.exec("INSERT INTO work_orders(number,order_number,order_uid,plate,car,status,closed_at) VALUES(2,2,'order-b','AA1234BA','OPEL VECTRA','open','')"));
        QVERIFY(prepareFixtureOrderState(2));
        input.document="SPLIT-123";input.quantity=3;
        QVERIFY2(WorkshopSpareStockService::addOrder(db,input,nullptr,&error,QDate::currentDate()),qPrintable(error));
        auto stock=WorkshopSpareStockService::orders(db,&error).first();const auto stockId=stock.id;
        QVERIFY2(WorkshopSpareStockService::issue(db,stock,1,1,&error),qPrintable(error));
        stock=WorkshopSpareStockService::orders(db,&error).first();
        QVERIFY2(WorkshopSpareStockService::issue(db,stock,1,2,&error),qPrintable(error));
        invoice=findInvoice(input.document);QCOMPARE(invoice.parts.size(),2);
        QCOMPARE(invoice.distribution.size(),3);
        double allocatedQuantity=0,remainingQuantity=0;
        for(const auto& slice:invoice.distribution) {
            allocatedQuantity+=slice.quantity;if(!slice.partId)remainingQuantity+=slice.quantity;
        }
        QCOMPARE(allocatedQuantity,3.0);QCOMPARE(remainingQuantity,1.0);
        QVERIFY2(PurchasePaymentService::payInvoiceOrders(db,invoice,{2},&error),qPrintable(error));
        invoice=findInvoice(input.document);QCOMPARE(invoice.paidCents,qint64(10000));
        for(const auto& part:invoice.parts) QCOMPARE(part.paidCents,part.orderNumber==2?qint64(10000):qint64(0));
        QCOMPARE(WorkshopSpareStockService::orders(db,&error).first().debtCents,qint64(20000));
        const auto receipt=WorkshopSpareStockService::receipts(db,stockId,&error).first();
        auto debt=PurchasePaymentService::detail(db,"inventory",receipt.purchaseId,{},&error);
        QVERIFY2(PurchasePaymentService::pay(db,debt,debt.remainingCents(),&error),qPrintable(error));
        QVERIFY(findInvoice(input.document).paid());
        QCOMPARE(WorkshopSpareStockService::orders(db,&error).first().debtCents,qint64(0));
        const auto linkedParts=findInvoice(input.document).parts;
        QVERIFY2(PurchasePaymentService::resetPart(db,linkedParts.first().id,&error),qPrintable(error));
        QCOMPARE(WorkshopSpareStockService::orders(db,&error).first().debtCents,qint64(10000));
        QCOMPARE(findInvoice(input.document).paidCents,qint64(10000));

        // A legacy issued row with no attachment metadata still exposes its stock invoice number.
        q.prepare("DELETE FROM work_order_part_attachments WHERE part_id=?");q.addBindValue(linkedParts.first().id);QVERIFY(q.exec());
        QString legacyNumber;
        OrderAttachmentService::partAttachmentPath(db,linkedParts.first().id,nullptr,&legacyNumber);
        QCOMPARE(legacyNumber,input.document);QCOMPARE(findInvoice(input.document).parts.size(),2);

        // One issue can consume two receipts; only issued quantities are paid by its invoice.
        input.document="MULTI-RECEIPT";input.quantity=3;
        QVERIFY2(WorkshopSpareStockService::addOrder(db,input,nullptr,&error),qPrintable(error));
        stock=WorkshopSpareStockService::orders(db,&error).first();
        QVERIFY2(WorkshopSpareStockService::receive(db,stock,1,QDate::currentDate(),25,&error),qPrintable(error));
        stock=WorkshopSpareStockService::orders(db,&error).first();
        QVERIFY2(WorkshopSpareStockService::receive(db,stock,2,QDate::currentDate(),0,&error),qPrintable(error));
        stock=WorkshopSpareStockService::orders(db,&error).first();
        const auto multiStockId=stock.id;
        QVERIFY2(WorkshopSpareStockService::issue(db,stock,2,1,&error),qPrintable(error));
        invoice=findInvoice(input.document);QCOMPARE(invoice.totalCents,qint64(20000));QCOMPARE(invoice.paidCents,qint64(2500));
        QVERIFY2(PurchasePaymentService::payInvoice(db,invoice,&error),qPrintable(error));
        QCOMPARE(WorkshopSpareStockService::orders(db,&error).first().debtCents,qint64(10000));
        QVERIFY(findInvoice(input.document).paid());
        const auto multiReceipts=WorkshopSpareStockService::receipts(db,multiStockId,&error);
        QCOMPARE(PurchasePaymentService::detail(db,"inventory",multiReceipts[0].purchaseId,{},&error).paidCents,qint64(10000));
        QCOMPARE(PurchasePaymentService::detail(db,"inventory",multiReceipts[1].purchaseId,{},&error).paidCents,qint64(10000));
        q.finish();

        // Earlier versions recorded an invoice payment on the part alone. Do not charge it again.
        input.document="LEGACY-PAID";input.quantity=1;
        QVERIFY2(WorkshopSpareStockService::addOrder(db,input,nullptr,&error,QDate::currentDate()),qPrintable(error));
        stock=WorkshopSpareStockService::orders(db,&error).first();
        QVERIFY2(WorkshopSpareStockService::issue(db,stock,1,1,&error),qPrintable(error));
        invoice=findInvoice(input.document);QCOMPARE(invoice.parts.size(),1);
        const auto legacyPart=invoice.parts.first();
        q.prepare("DELETE FROM workshop_spare_payment_allocations WHERE issue_id IN (SELECT id FROM workshop_spare_movements WHERE part_id=?)");
        q.addBindValue(legacyPart.id);QVERIFY(q.exec());
        q.prepare("UPDATE purchase_payment_balances SET paid_cents=10000 WHERE payment_key=?");
        q.addBindValue(PurchasePaymentService::key("part",legacyPart.id,legacyPart.orderUid));QVERIFY(q.exec());
        QVERIFY(q.exec("SELECT COUNT(*) FROM purchase_payment_events"));QVERIFY(q.next());const auto events=q.value(0).toInt();q.finish();
        QVERIFY2(PurchasePaymentService::ensureSchema(db,&error),qPrintable(error));
        QVERIFY(findInvoice(input.document).paid());
        QCOMPARE(WorkshopSpareStockService::orders(db,&error).first().debtCents,qint64(0));
        QVERIFY2(PurchasePaymentService::ensureSchema(db,&error),qPrintable(error));
        QVERIFY(q.exec("SELECT COUNT(*) FROM purchase_payment_events"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),events);
    }
    void inventoryJournalPayment() {
        QSqlQuery q(db); QVERIFY(q.exec("ALTER TABLE work_orders ADD COLUMN client TEXT"));
        WorkshopInventoryPurchaseInput input;
        input.purchasedOn = QDate::currentDate(); input.materialName = QString::fromUtf8("Антифриз");
        input.unit = QString::fromUtf8("л"); input.quantity = 1; input.unitCost = 100; input.paidAmount = 0;
        QString error; qint64 first = 0, second = 0;
        QVERIFY2(WorkshopInventoryService::addPurchase(db, input, &first, &error), qPrintable(error));
        input.unitCost = 200;
        QVERIFY2(WorkshopInventoryService::addPurchase(db, input, &second, &error), qPrintable(error));
        WorkshopInventoryPage page; page.resize(1300,750); page.show(); page.refresh();
        auto* journal = page.findChild<QTableView*>("inventoryMovementJournal"); QVERIFY(journal);
        QCOMPARE(journal->model()->rowCount(), 2); QVERIFY(journal->isColumnHidden(10));
        QCOMPARE(journal->model()->index(0,10).data().toLongLong(), second);
        QCOMPARE(journal->model()->index(1,10).data().toLongLong(), first);
        auto* balance = page.findChild<QPushButton*>("inventoryBalance"); QVERIFY(balance);
        auto* add = page.findChild<QPushButton*>("inventoryAddMaterial"); QVERIFY(add);
        QCoreApplication::processEvents(); QVERIFY(balance->mapTo(&page,QPoint()).x() < add->mapTo(&page,QPoint()).x());
        QCOMPARE(page.findChild<QLabel*>("inventoryMetricValue0")->text(), QString::fromUtf8("0 л"));
        QCOMPARE(page.findChild<QLabel*>("inventoryMetricValue1")->text(), QString::fromUtf8("2 л"));
        QCOMPARE(page.findChild<QLabel*>("inventoryMetricValue2")->text(), QString("0"));
        QCOMPARE(page.findChild<QLabel*>("inventoryMetricValue3")->text(), QString::fromUtf8("300 грн"));
        auto* actions = page.findChild<QWidget*>("inventoryActionsTile");
        QVERIFY(actions && actions->isAncestorOf(balance) && actions->isAncestorOf(add));
        QCOMPARE(balance->mapTo(&page, QPoint()).y(), add->mapTo(&page, QPoint()).y());
        QWidget* previous = page.findChild<QWidget*>("inventoryPeriodTile");
        for (int i = 0; i < 4; ++i) {
            auto* tile = page.findChild<QWidget*>(QString("inventoryMetricTile%1").arg(i));
            QVERIFY(previous->geometry().right() < tile->geometry().left());
            QCOMPARE(previous->height(), tile->height());
            previous = tile;
        }
        QVERIFY(previous->geometry().right() < actions->geometry().left());
        if (!qEnvironmentVariable("CS_TEST_SCREENSHOTS").isEmpty())
            page.grab().save(qEnvironmentVariable("CS_TEST_SCREENSHOTS") + "/inventory-tiles.png");
        bool detailFound = false, paymentFound = false;
        QTimer::singleShot(50, &page, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            auto* pay = dialog ? dialog->findChild<QPushButton*>("inventoryDetailPay") : nullptr;
            detailFound = pay && pay->isEnabled();
            if (!detailFound) { if (dialog) dialog->reject(); return; }
            // Preparation is asynchronous: a timer delay is not proof that the
            // payment dialog exists. Keep the progress dialog alive while waiting.
            QTimer paymentReady;
            QElapsedTimer paymentWait;
            paymentWait.start();
            connect(&paymentReady, &QTimer::timeout, &page, [&] {
                auto* payment = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                auto* full = payment && payment->objectName() == "purchasePaymentDialog"
                    ? payment->findChild<QPushButton*>("payInFull") : nullptr;
                if (full && full->isEnabled()) {
                    paymentReady.stop();
                    paymentFound = true;
                    QTimer::singleShot(100, &page, [] {
                        if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                            message->accept();
                    });
                    full->click();
                } else if (paymentWait.elapsed() >= 5000) {
                    paymentReady.stop();
                    if (payment) payment->reject();
                }
            });
            paymentReady.start(10);
            pay->click();
            paymentReady.stop();
            if (dialog->isVisible()) dialog->reject();
        });
        QVERIFY(QMetaObject::invokeMethod(journal, "doubleClicked", Qt::DirectConnection,
            Q_ARG(QModelIndex, journal->model()->index(1,3))));
        QVERIFY(detailFound); QVERIFY(paymentFound);
        QCOMPARE(PurchasePaymentService::detail(db,"inventory",first,{},&error).remainingCents(), qint64(0));
        QCOMPARE(PurchasePaymentService::detail(db,"inventory",second,{},&error).remainingCents(), qint64(20000));
    }
    void inventoryBalanceReferenceSize() {
        WorkshopInventoryPage page;
        page.resize(1300, 750);
        page.show();
        auto* dialog = page.findChild<QDialog*>("workshopInventoryPurchasesTab");
        auto* tabs = page.findChild<QTabWidget*>("inventoryBalanceTabs");
        auto* button = page.findChild<QPushButton*>("inventoryBalance");
        QVERIFY(dialog); QVERIFY(tabs); QVERIFY(button);
        const QRect available = page.screen()->availableGeometry();
        QSize actual;
        bool contentsFit = true;
        QTimer::singleShot(100, dialog, [&] {
            actual = dialog->size();
            for (int index = 0; index < tabs->count(); ++index) {
                tabs->setCurrentIndex(index);
                QCoreApplication::processEvents();
                contentsFit = contentsFit && dialog->rect().contains(tabs->geometry());
            }
            dialog->grab().save(previousWorkingDirectory + "/build-single-instance-audit/balance-reference-size.png");
            dialog->reject();
        });
        button->click();
        QCOMPARE(actual, QSize(qRound(available.width() * 0.64), qRound(available.height() * 0.63)));
        QVERIFY(contentsFit);
    }
    void materialThemePreservesGeometry() {
        WorkshopInventoryPage page;
        auto* form=page.findChild<QWidget*>("workshopInventoryPurchaseForm");QVERIFY(form);
        auto* dialog=qobject_cast<QDialog*>(form->window());QVERIFY(dialog);
        const QString updated=dialog->styleSheet();
        dialog->setStyleSheet("QDialog{background:#344552;} QLabel{color:#f4f7fb;} QLineEdit,QComboBox,QDateEdit,QDoubleSpinBox{background:white;color:#111;border:1px solid #a8b7c4;border-radius:5px;padding:6px;}");
        dialog->show();QTest::qWait(100);
        const QSize original=dialog->size();QMap<QWidget*,QRect> geometry;
        for(auto* widget:dialog->findChildren<QWidget*>())if(widget->isVisible())geometry.insert(widget,widget->geometry());
        dialog->setStyleSheet(updated);QTest::qWait(100);
        QCOMPARE(dialog->size(),original);
        for(auto it=geometry.cbegin();it!=geometry.cend();++it)QCOMPARE(it.key()->geometry(),it.value());
        QVERIFY(dialog->grab().save(dir.filePath(QStringLiteral("material-theme.png"))));
        dialog->reject();
    }
    void inventoryClientPriceUi() {
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE masters(id TEXT PRIMARY KEY,name TEXT,active INTEGER DEFAULT 1,role TEXT,admin_percent REAL DEFAULT 0,active_from TEXT,active_to TEXT)"));
        QVERIFY(q.exec("ALTER TABLE work_orders ADD COLUMN client TEXT"));
        WorkshopInventoryPurchaseInput input;
        input.purchasedOn=QDate::currentDate(); input.materialName=QString::fromUtf8("Керосин");
        input.quantity=10; input.unitCost=300; input.clientPrice=450.25; input.paidAmount=0;
        QString error;
        QVERIFY2(WorkshopInventoryService::addPurchase(db,input,nullptr,&error),qPrintable(error));
        WorkshopInventoryPage page;
        page.resize(1600,850); page.show(); page.refresh();
        QVERIFY(!page.findChild<QTabWidget*>("workshopInventoryTabs"));
        QVERIFY(page.findChild<QWidget*>("workshopMaterialsRegistry"));
        QCoreApplication::processEvents();
        QVERIFY(page.findChild<QDoubleSpinBox*>("inventoryClientPrice"));
        QTableView* purchases=nullptr;
        for(auto* table:page.findChildren<QTableView*>())
            if(table->model()->columnCount()==12) purchases=table;
        QVERIFY(purchases);
        QCOMPARE(purchases->model()->headerData(6,Qt::Horizontal).toString(),QString::fromUtf8("Клієнту за 1 од."));
        QCOMPARE(purchases->model()->index(0,6).data().toString(),QString("450.25"));
        QCOMPARE(purchases->model()->index(0,11).data().toString(),QString("3 000"));
        for(int col=0;col<purchases->model()->columnCount();++col)
            QVERIFY(purchases->model()->headerData(col,Qt::Horizontal).toString()!=QString::fromUtf8("Примітка"));
        auto* form=page.findChild<QWidget*>("workshopInventoryPurchaseForm"); QVERIFY(form);
        auto* materialDialog=qobject_cast<QDialog*>(form->window());QVERIFY(materialDialog);materialDialog->show();QCoreApplication::processEvents();
        auto* grid=qobject_cast<QGridLayout*>(form->layout()); QVERIFY(grid);
        QCOMPARE(grid->columnCount(),2);
        for(int col=0;col<2;++col) {
            auto* item=grid->itemAtPosition(1,col); QVERIFY(item);
            auto* widget=item->widget(); QVERIFY(widget);
            QVERIFY(form->rect().contains(widget->geometry()));
            QVERIFY(qAbs(widget->geometry().center().y()-grid->itemAtPosition(1,0)->geometry().center().y())<=2);
        }
        for(auto* edit:form->findChildren<QLineEdit*>())
            QVERIFY(edit->placeholderText()!=QString::fromUtf8("Примітка"));
        auto* addMaterial=page.findChild<QPushButton*>("inventoryAddMaterial");auto* addSpare=page.findChild<QPushButton*>("inventoryAddSpare");
        QVERIFY(addMaterial);QVERIFY(addSpare);QVERIFY(!addMaterial->isHidden());QVERIFY(addSpare->isHidden());
        QVERIFY(page.grab().save(dir.filePath(QStringLiteral("warehouse-materials.png"))));
        auto* balanceDialog = page.findChild<QDialog*>("workshopInventoryPurchasesTab"); QVERIFY(balanceDialog);
        auto* balanceTabs = page.findChild<QTabWidget*>("inventoryBalanceTabs"); QVERIFY(balanceTabs); QCOMPARE(balanceTabs->count(),2);
        QTimer::singleShot(50, balanceDialog, [&, balanceDialog, balanceTabs] {
            balanceDialog->grab().save(dir.filePath(QStringLiteral("warehouse-acquisitions.png")));
            balanceTabs->setCurrentIndex(1); QCoreApplication::processEvents();
            balanceDialog->grab().save(dir.filePath(QStringLiteral("warehouse-balance.png")));
            balanceDialog->reject();
        });
        page.findChild<QPushButton*>("inventoryBalance")->click();
        QVERIFY(materialDialog->grab().save(dir.filePath(QStringLiteral("warehouse-material-form.png"))));
        auto* stockTabs=page.findChild<QTabWidget*>("workshopStockKinds");QVERIFY(stockTabs);
        stockTabs->setCurrentIndex(0);QCoreApplication::processEvents();
        QVERIFY(!addSpare->isHidden());QVERIFY(addMaterial->isHidden());
        QVERIFY(addSpare->mapTo(&page,QPoint()).x()>page.width()/2);
        QVERIFY(page.grab().save(dir.filePath(QStringLiteral("warehouse-spares.png"))));
        materialDialog->hide();
        QTimer::singleShot(50,[&]{
            auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if(dialog) {
                for(auto* date:dialog->findChildren<QDateEdit*>()) QCOMPARE(date->date(),QDate::currentDate());
                dialog->grab().save(dir.filePath(QStringLiteral("warehouse-spare-form.png")));dialog->reject();}
        });
        addSpare->click();
        page.resize(1300,750); QCoreApplication::processEvents();
        QCOMPARE(page.width(),1300);
        for(int col=0;col<2;++col)
            QVERIFY(form->rect().contains(grid->itemAtPosition(1,col)->widget()->geometry()));
        materialDialog->hide();
    }
    void inventoryNumericFieldsClearOnClick() {
        WorkshopInventoryPage page;
        page.resize(1300,750); page.show();
        auto* form=page.findChild<QWidget*>("workshopInventoryPurchaseForm"); QVERIFY(form);
        form->window()->show();
        QTest::qWait(30);
        for(const auto* name : {"inventoryQuantity", "inventoryUnitCost", "inventoryClientPrice"}) {
            auto* editor=page.findChild<QDoubleSpinBox*>(name); QVERIFY(editor);
            auto* input=editor->findChild<QLineEdit*>(); QVERIFY(input);
            editor->setValue(400);
            QTest::mouseClick(input,Qt::LeftButton);
            QCOMPARE(input->text(),QString());
            QVERIFY(input->hasFocus());
            QTest::keyClicks(input,"12"+editor->locale().decimalPoint()+"25");
            QTest::keyClick(input,Qt::Key_Return);
            QCOMPARE(editor->value(),12.25);

            // A second click clears an already focused field as well.
            QTest::mouseClick(input,Qt::LeftButton);
            QVERIFY(input->text().isEmpty());
            QTest::keyClick(input,Qt::Key_Tab);
            QCOMPARE(editor->value(),12.25);
            QVERIFY(!input->text().isEmpty());
            editor->setFocus();
            QTest::keyClick(editor,Qt::Key_Up);
            QCOMPARE(editor->value(),13.25);
            if(editor->minimum()<=0) {
                QTest::mouseClick(input,Qt::LeftButton);
                QTest::keyClicks(input,"0");
                QTest::keyClick(input,Qt::Key_Return);
                QCOMPARE(editor->value(),0.0);
            }
        }
    }
    void partsReorderAfterPaymentPreservesIdentity_data() {
        QTest::addColumn<QString>("status");
        QTest::addColumn<QString>("closedAt");
        QTest::newRow("open") << QString("open") << QString();
        QTest::newRow("closed") << QString("closed") << QString("2026-09-02");
        QTest::newRow("legacy-closed-marker") << QString("open") << QString("2026-09-02");
    }
    void partsReorderAfterPaymentPreservesIdentity() {
        QFETCH(QString,status); QFETCH(QString,closedAt);
        QSqlQuery query(db);
        query.prepare("UPDATE work_orders SET status=?,closed_at=?");
        query.addBindValue(status); query.addBindValue(closedAt); QVERIFY(query.exec());
        PartsPage page;
        // Keep the fixture visible regardless of today's default date window.
        {
            const QSignalBlocker fromBlocker(page.m_dateFrom), toBlocker(page.m_dateTo);
            page.m_dateFrom->setDate(QDate(2026,9,1));
            page.m_dateTo->setDate(QDate(2026,9,30));
        }
        page.populateRows(PartsQueryService::loadRows(db,{}, {}, {}));
        auto* table=page.m_table;
        QCOMPARE(table->model()->rowCount(),2);
        QCOMPARE(page.visibleRowForPartId(1),0);
        QString error;
        auto debt=PurchasePaymentService::detail(db,"part",1,"order-a");
        QVERIFY(PurchasePaymentService::pay(db,debt,5000,&error));
        QVERIFY(page.applyIncrementalPaidToggle(1,true));
        page.resortRowsAfterPaidToggle(1);
        QCOMPARE(page.visibleRowForPartId(1),0);
        QVERIFY(page.m_partsModel->rowAt(0)->partiallyPaid);
        QCOMPARE(page.selectedPartId(),1);

        bool paymentClicked=false;
        QTimer paymentReady;
        QElapsedTimer paymentWait;
        paymentWait.start();
        connect(&paymentReady,&QTimer::timeout,[&] {
            auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            auto* button=dialog && dialog->objectName()==QStringLiteral("purchasePaymentDialog")
                ? dialog->findChild<QPushButton*>("payInFull") : nullptr;
            if(button && button->isEnabled()) {
                paymentReady.stop();
                QTimer::singleShot(0,[&] {
                    auto* notice=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                    QVERIFY(notice);
                    QCOMPARE(notice->windowTitle(),QString::fromUtf8("Оплату здійснено"));
                    notice->accept();
                });
                paymentClicked=true;
                button->click();
            } else if(paymentWait.elapsed()>=5000) {
                paymentReady.stop();
                if(dialog) dialog->reject();
            }
        });
        paymentReady.start(10);
        table->clicked(table->model()->index(page.visibleRowForPartId(1),12));
        paymentReady.stop();
        QVERIFY(paymentClicked);
        QCOMPARE(page.visibleRowForPartId(1),1);
        QCOMPARE(page.selectedPartId(),1);
        QVERIFY(page.m_partsModel->rowAt(1)->paid);
        QVERIFY(!page.m_partsModel->rowAt(1)->partiallyPaid);
        QVERIFY(PurchasePaymentService::resetPart(db,1));
        QVERIFY(page.applyIncrementalPaidToggle(1,false));
        page.resortRowsAfterPaidToggle(1);
        QCOMPARE(page.visibleRowForPartId(1),0);
        QCOMPARE(page.selectedPartId(),1);
    }
    void partsSearchPreservesPriorityAndIgnoresDateWindow() {
        QSqlQuery query(db);
        QVERIFY(query.exec("UPDATE work_orders SET status='closed',closed_at='2026-09-02'"));
        auto rows=PartsQueryService::loadRows(db,{}, {}, {});
        QCOMPARE(rows.size(),2);
        for(auto& row:rows) row.searchRank=row.paid ? 1 : 7;
        PartsPageTableModelAdapter::ViewPolicy policy;
        policy.today=QDate::currentDate();
        policy.from=policy.today.addDays(1); policy.to=policy.today.addDays(2);
        QVERIFY(PartsPageTableModelAdapter::build(rows,policy).rows.isEmpty());
        policy.textFilter="Part";
        const auto result=PartsPageTableModelAdapter::build(rows,policy);
        QCOMPARE(result.rows.size(),2);
        QCOMPARE(result.rows.first().id,1); // Outstanding first, even for a weaker text match.
        QCOMPARE(result.rows.last().id,2);
        rows.first().searchRank=100;
        QCOMPARE(PartsPageTableModelAdapter::build(rows,policy).rows.size(),1);
    }
    void paymentPersistsAfterReopen() {
        QString error;
        const auto d=PurchasePaymentService::detail(db,"part",1,"order-a");
        QVERIFY2(PurchasePaymentService::pay(db,d,1234,&error),qPrintable(error));
        db.close(); QVERIFY(db.open()); PartsQueryService::invalidateCache();
        QCOMPARE(PurchasePaymentService::detail(db,"part",1,"order-a").paidCents,1234);
        QCOMPARE(PurchasePaymentService::history(db,d).size(),1);
    }
};
QTEST_MAIN(PurchasePaymentTests)
#include "PurchasePaymentTests.moc"
#include <QPainter>
#include <QImage>
#include <QStyleOptionViewItem>
#include <QAbstractItemDelegate>
