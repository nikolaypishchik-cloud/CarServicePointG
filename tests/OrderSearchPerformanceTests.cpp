#include <QtTest/QtTest>
#include <QSqlQuery>
#include <QSqlError>
#include <QTemporaryDir>
#include <QFile>
#include <QStandardPaths>
#include <QUuid>
#include <QSettings>
#include "infrastructure/db/sqlite/DbFacadeSupport.h"
#include "core/services/HistoryQueryService.h"
#include "core/services/OrderSearchIndex.h"
#include "core/services/ReportsDataLoader.h"
#include "core/services/PartsQueryService.h"
#include "core/cache/PartsQueryCache.h"
#include "core/services/BusinessFormatsMigration.h"
#include "core/services/orders/CurrentOrdersQueryService.h"
#include "core/services/AccessControlService.h"
#include "core/services/ActivityAuditService.h"
#include <QScopeGuard>
#include "PerformanceSamples.h"
#include <QLoggingCategory>

class OrderSearchPerformanceTests : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    QSqlDatabase db;
    void sql(const QString& text) {
        QSqlQuery q(db);
        QVERIFY2(q.exec(text), qPrintable(q.lastError().text() + text.left(120)));
    }
private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("CarServiceTests");
        QCoreApplication::setApplicationName("Safety-OrderSearch-" + QUuid::createUuid().toString(QUuid::Id128));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());
        if (qEnvironmentVariableIntValue("CS_SEARCH_BENCH_REPEATS") > 1)
            QLoggingCategory::setFilterRules(QStringLiteral("*.debug=false"));
        QStandardPaths::setTestModeEnabled(true);
        db = QSqlDatabase::addDatabase("QSQLITE", "carservice_main");
        db.setDatabaseName(dir.filePath("benchmark.db"));
        QVERIFY(db.open());
        sql("CREATE TABLE access_roles(id TEXT PRIMARY KEY,active INTEGER)");
        sql("INSERT INTO access_roles VALUES('owner',1),('master',1)");
        sql("CREATE TABLE staff_accounts(id TEXT PRIMARY KEY,master_id TEXT,role TEXT,active INTEGER)");
        sql("INSERT INTO staff_accounts VALUES('owner','','owner',1)");
        ActivityAuditService::setCurrentActor({"owner","Owner","owner","test"});
        sql("PRAGMA journal_mode=WAL");
        sql("CREATE TABLE masters(id TEXT PRIMARY KEY,name TEXT)");
        sql("INSERT INTO masters VALUES('master','Test Master')");
        sql("CREATE TABLE work_orders(number INTEGER PRIMARY KEY,order_uid TEXT UNIQUE,order_number INTEGER,order_year INTEGER,opened_at TEXT,closed_at TEXT,status TEXT,plate TEXT,plate_norm TEXT,vin TEXT,car TEXT,client TEXT,master_id TEXT,master_name_closed TEXT,works_sum REAL,parts_sum REAL)");
        sql("CREATE TABLE work_order_services(id INTEGER PRIMARY KEY,order_uid TEXT,order_number INTEGER,qty REAL,price REAL,amount REAL)");
        sql("CREATE TABLE work_order_parts(id INTEGER PRIMARY KEY,order_uid TEXT,order_number INTEGER,ordered_at TEXT,delivery_date TEXT,installed_at TEXT,store TEXT,part_number TEXT,name TEXT,manufacturer TEXT,note TEXT,qty REAL,price REAL,amount REAL,service_id INTEGER,ordered_by_admin INTEGER,paid INTEGER,purchase_price REAL,purchase_amount REAL,markup_percent REAL)");
        int count = qEnvironmentVariableIntValue("CS_SEARCH_BENCH_ROWS");
        if (count <= 0) count = 100000;
        sql(QString("WITH RECURSIVE n(i) AS (VALUES(1) UNION ALL SELECT i+1 FROM n WHERE i<__COUNT__) INSERT INTO work_orders SELECT i,'uid-'||i,i,2026,'2026-09-01 09:00',CASE WHEN i%10=0 THEN '' ELSE '2026-09-25 10:00' END,CASE WHEN i%10=0 THEN 'open' ELSE 'completed' END,printf('AA %04d BB',i%10000),printf('AA%04dBB',i%10000),printf('TESTVIN%010d',i),CASE WHEN i=321 THEN 'Rare Model' ELSE 'Common Car' END,CASE WHEN i=321 THEN 'Test Client' ELSE 'Client '||i END,'master','',100,200 FROM n").replace("__COUNT__",QString::number(count)));
        sql("INSERT INTO work_order_services SELECT number,order_uid,number,1,100,100 FROM work_orders");
        sql("INSERT INTO work_order_parts SELECT number,order_uid,number,'2026-09-01','','','Store','','Part','','',1,200,200,0,1,0,150,150,0 FROM work_orders");
        sql(QString("INSERT INTO work_order_services SELECT number+%1,order_uid,number,1,100,100 FROM work_orders").arg(count));
        for(int copy=1;copy<=2;++copy)
            sql(QString("INSERT INTO work_order_parts SELECT number+%1,order_uid,number,'2026-09-01','','','Store','','Part','','',1,200,200,0,1,0,150,150,0 FROM work_orders").arg(count*copy));
        for (const QString& table : {QString("work_order_services"), QString("work_order_parts")}) {
            sql("CREATE INDEX idx_"+table+"_uid ON "+table+"(order_uid)");
            sql("CREATE INDEX idx_"+table+"_num ON "+table+"(order_number)");
            sql("CREATE INDEX idx_"+table+"_search ON "+table+"(TRIM(COALESCE(order_uid,'')),COALESCE(order_number,0))");
        }
        sql("CREATE INDEX idx_order_number ON work_orders(order_number)");
        for(const QString& col : {QString("car_year"),QString("odometer"),QString("engine_type"),QString("engine_volume"),QString("fuel_type"),QString("client_phone"),QString("discount"),QString("car_model"),QString("car_brand"),QString("car_kind"),QString("color"),QString("car_color"),QString("owner_name"),QString("client_uid"),QString("vehicle_uid"),QString("crm_backfilled_at")})
            sql("ALTER TABLE work_orders ADD COLUMN "+col+" TEXT DEFAULT ''");
        for(const QString& col : {QString("manual_markup_override"),QString("markup_source"),QString("pricing_updated_at"),QString("pricing_calc_version")})
            sql("ALTER TABLE work_order_parts ADD COLUMN "+col+" TEXT DEFAULT ''");
        for(const QString& col : {QString("title"),QString("note"),QString("paid"),QString("parent_id")})
            sql("ALTER TABLE work_order_services ADD COLUMN "+col+" TEXT DEFAULT ''");
        sql("CREATE TABLE outsource_repairs(id INTEGER,order_uid TEXT,order_number INTEGER,sent_at TEXT,company TEXT,part_name TEXT,repair_cost REAL,client_price REAL,note TEXT,created_at TEXT,service_id INTEGER)");
        sql("CREATE TABLE work_order_service_masters(service_id INTEGER,master_id TEXT,share REAL)");
        QVERIFY(rebuildRouterViews(db));
        QString indexError;
        QVERIFY2(OrderSearchIndex::ensure(db,&indexError),qPrintable(indexError));
        sql("INSERT INTO order_registry(order_number,order_uid,visible_order_number,order_year,status,opened_at,closed_at,plate,plate_norm,vin,car,client,master_id,master_name_closed,works_sum,parts_sum) SELECT number,order_uid,order_number,order_year,status,opened_at,closed_at,plate,plate_norm,vin,car,client,master_id,master_name_closed,works_sum,parts_sum FROM work_orders WHERE number%2=0");
        qInfo() << "Dataset orders=" << count;
        if (qEnvironmentVariableIsSet("CS_TEST_LOCAL_DATE_STORAGE")) {
            QElapsedTimer migrationTimer; migrationTimer.start();
            QVERIFY2(BusinessFormatsMigration::apply(db, &indexError), qPrintable(indexError));
            qInfo() << "Date storage migration ms=" << migrationTimer.elapsed();
            QSqlQuery check(db);
            QVERIFY(check.exec("SELECT opened_at FROM work_orders WHERE number=321"));
            QVERIFY(check.next());
            QCOMPARE(check.value(0).toString(), QString("01.09.26 09:00"));
        }
    }
    void history_data() {
        QTest::addColumn<QString>("filter");
        QTest::newRow("number") << "321";
        QTest::newRow("plate-prefix") << "AA 032";
        QTest::newRow("vin") << "TESTVIN0000000321";
        QTest::newRow("car") << "Rare Model";
        QTest::newRow("client") << "Test Client";
        QTest::newRow("missing") << "notfoundxyz";
        QTest::newRow("date") << "25.09.26";
        QTest::newRow("date-four-digit-year") << "25.09.2026";
        QTest::newRow("date-iso") << "2026-09-25";
        QTest::newRow("broad-car") << "Common Car";
        QTest::newRow("all") << "";
    }
    void history() {
        QFETCH(QString, filter);
        QElapsedTimer timer; timer.start();
        HistoryQueryParams params{QDate(2026,1,1),QDate(2026,12,31),filter}; params.pageSize=200;
        auto result = PerformanceSamples::measure("history:" + filter, [&] { return HistoryQueryService::loadHistory(params); });
        const auto ms = timer.elapsed();
        qInfo() << "HISTORY" << filter << "ms=" << ms << "rows=" << result.rows.size();
        QVERIFY2(result.errorText.isEmpty(), qPrintable(result.errorText));
        if (filter == "notfoundxyz") QVERIFY(result.rows.isEmpty());
        else QVERIFY(!result.rows.isEmpty());
    }
    void current() {
        for (const QString& filter : {QString("320"),QString("AA 032"),QString("notfoundxyz")}) {
            QElapsedTimer timer; timer.start();
            const auto result=PerformanceSamples::measure("current:" + filter, [&] { return CurrentOrdersQueryService::loadOpenOrders(filter); });
            const auto ms=timer.elapsed();
            qInfo()<<"CURRENT"<<filter<<"ms="<<ms<<"rows="<<result.rows.size();
            QVERIFY2(result.errorText.isEmpty(),qPrintable(result.errorText));
            if(filter=="notfoundxyz") QVERIFY(result.rows.isEmpty()); else QVERIFY(!result.rows.isEmpty());
        }
    }
    void partsIdentityAlternativesPreservePairs() {
        sql("SAVEPOINT parts_identity_check");
        const auto cleanup = qScopeGuard([&] {
            sql("ROLLBACK TO parts_identity_check"); sql("RELEASE parts_identity_check");
            PartsQueryCache::invalidate("identity fixture removed");
        });
        sql("INSERT INTO work_order_parts(id,order_uid,order_number,ordered_at,qty,price,amount,purchase_price,purchase_amount) VALUES"
            "(9000001,'uid-321',321,'2030-01-01',1,1,1,1,1),"
            "(9000002,'',322,'2030-01-01',1,1,1,1,1),"
            "(9000003,'REWRITE_323',0,'2030-01-01',1,1,1,1,1),"
            "(9000004,'uid-321',324,'2030-01-01',1,1,1,1,1),"
            "(9000005,'uid-321',321,'2030-01-01',1,1,1,1,1),"
            "(9000006,'unmatched',0,'2030-01-01',1,1,1,1,1),"
            "(9000007,' uid-321 ',0,'2030-01-01',1,1,1,1,1)");
        PartsQueryCache::invalidate("identity fixture added");
        QString error;
        const auto rows = PartsQueryService::loadRows(db, {}, QDate(2030,1,1), QDate(2030,1,1), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QSet<QString> actual;
        for (const auto& row : rows) actual.insert(QString::number(row.id) + ":" + row.orderDisplay);
        const QSet<QString> expected{"9000001:321", "9000002:322", "9000003:323",
            "9000004:321", "9000004:324", "9000005:321", "9000007:321"};
        QCOMPARE(actual, expected);
        QCOMPARE(rows.size(), expected.size()); // Multiple identity matches must not duplicate a pair.
    }
    void reports() {
        for(const QString& filter : {QString("321"),QString("AA 032"),QString("Rare Model"),QString("notfoundxyz")}) {
            QElapsedTimer timer; timer.start();
            const auto result=PerformanceSamples::measure("reports:" + filter, [&] { return ReportsDataLoader::buildOrdersSnapshot(db.databaseName(),"2026-01-01","2026-12-31",filter); });
            const auto ms=timer.elapsed();
            qInfo()<<"REPORTS"<<filter<<"ms="<<ms<<"rows="<<result.rows.size();
            if(filter=="notfoundxyz") QVERIFY(result.rows.isEmpty()); else QVERIFY2(!result.rows.isEmpty(),qPrintable(result.stats.join(" | ")));
        }
    }
    void changesAndIsolation() {
        sql("SAVEPOINT change_check");
        sql(QString::fromUtf8("UPDATE work_orders SET plate='КЕ 8571 АР',client='ІВАН ЩЕРБАК',car='Renamed Vehicle' WHERE number=321"));
        for(const QString& filter : {QString("KE 8571 AP"),QString::fromUtf8("іван щербак"),QString("Renamed Vehicle")}) {
            const auto result=HistoryQueryService::loadHistory({QDate(2026,1,1),QDate(2026,12,31),filter});
            QVERIFY2(result.errorText.isEmpty(),qPrintable(result.errorText));
            bool found=false;
            for(const auto& row:result.rows) if(row.at(10).toInt()==321) { found=true; QCOMPARE(row.at(8).toDouble(),200.0); QCOMPARE(row.at(9).toDouble(),600.0); }
            QVERIFY(found);
        }
        sql("UPDATE work_order_services SET order_uid='different-order',order_number=321 WHERE id=322");
        sql("INSERT INTO work_order_services(id,order_uid,order_number,qty,price,amount) VALUES(999999,'',321,1,25,25)");
        auto result=HistoryQueryService::loadHistory({QDate(2026,1,1),QDate(2026,12,31),"Renamed Vehicle"});
        QCOMPARE(result.rows.size(),1); QCOMPARE(result.rows[0][8].toDouble(),225.0);
        sql("DELETE FROM work_orders WHERE number=321");
        QVERIFY(HistoryQueryService::loadHistory({QDate(2026,1,1),QDate(2026,12,31),"Renamed Vehicle"}).rows.isEmpty());
        sql("ROLLBACK TO change_check"); sql("RELEASE change_check");
        QCOMPARE(HistoryQueryService::loadHistory({QDate(2026,1,1),QDate(2026,12,31),"Rare Model"}).rows.size(),1);
        sql("SAVEPOINT archive_check");
        sql("DELETE FROM work_orders WHERE number=322");
        sql("UPDATE order_registry SET car='ArchiveOnlyCar',status='completed',closed_at='2026-09-25' WHERE order_number=322");
        QCOMPARE(HistoryQueryService::loadHistory({QDate(2026,1,1),QDate(2026,12,31),"ArchiveOnlyCar"}).rows.size(),1);
        sql("DELETE FROM order_registry WHERE order_number=322");
        QVERIFY(HistoryQueryService::loadHistory({QDate(2026,1,1),QDate(2026,12,31),"ArchiveOnlyCar"}).rows.isEmpty());
        sql("ROLLBACK TO archive_check"); sql("RELEASE archive_check");
        HistoryQueryParams page{QDate(2026,1,1),QDate(2026,12,31),"Common Car"}; page.pageSize=200;
        const auto first=HistoryQueryService::loadHistory(page); QVERIFY(first.hasMore);
        page.offset=200; const auto second=HistoryQueryService::loadHistory(page);
        QCOMPARE(second.rows.size(),200);
        QSet<int> numbers; for(const auto& row:first.rows) numbers.insert(row[10].toInt());
        for(const auto& row:second.rows) QVERIFY(!numbers.contains(row[10].toInt()));
    }
    void queryPlans() {
        HistoryQueryParams params{QDate(2026,1,1),QDate(2026,12,31),"AA 032",false,true}; params.pageSize=200;
        QSqlQuery q(db);
        QVERIFY(q.exec("EXPLAIN QUERY PLAN "+HistoryQueryService::buildHistorySql(params)));
        QStringList plan;
        while(q.next()) plan<<q.value(3).toString();
        qInfo().noquote()<<"HISTORY PLAN\n"<<plan.join('\n');
        QVERIFY(plan.join(' ').contains("idx_search_v1_plate_search"));
        QVERIFY(plan.join(' ').contains("search_uid"));
    }
    void masterIsolationBeforePaginationAndSearch() {
        sql("SAVEPOINT master_scope");
        const auto cleanup = qScopeGuard([&] {
            ActivityAuditService::clearCurrentActor();
            sql("ROLLBACK TO master_scope"); sql("RELEASE master_scope");
        });
        sql("INSERT INTO staff_accounts VALUES('mine','m2','master',1),('unlinked','','master',1),('disabled','m2','master',0)");
        sql("INSERT INTO masters VALUES('m2','Second Master')");
        sql("UPDATE work_orders SET master_id='m2' WHERE number IN (320,321,323)");
        ActivityAuditService::setCurrentActor({"mine","Second Master","master","test"});
        HistoryQueryParams params{QDate(2026,1,1),QDate(2026,12,31),""};
        params.pageSize=1;
        auto first = HistoryQueryService::loadHistory(params);
        QVERIFY2(first.errorText.isEmpty(),qPrintable(first.errorText));
        QCOMPARE(first.rows.size(),1); QCOMPARE(first.rows[0][10].toInt(),323); QVERIFY(first.hasMore);
        params.offset=1;
        auto second=HistoryQueryService::loadHistory(params);
        QCOMPARE(second.rows.size(),1); QCOMPARE(second.rows[0][10].toInt(),321); QVERIFY(!second.hasMore);
        for (const QString& search : {QString("322"),QString("AA 0322"),QString("TESTVIN0000000322"),QString("Client 322")}) {
            params.filter=search; params.offset=0;
            const auto hidden=HistoryQueryService::loadHistory(params);
            QVERIFY2(hidden.errorText.isEmpty(),qPrintable(hidden.errorText)); QVERIFY(hidden.rows.isEmpty());
        }
        auto current=CurrentOrdersQueryService::loadOpenOrders("");
        QVERIFY2(current.errorText.isEmpty(),qPrintable(current.errorText));
        QCOMPARE(current.rows.size(),1); QCOMPARE(current.rows[0][0].toInt(),320);
        QVERIFY(CurrentOrdersQueryService::loadOpenOrders("330").rows.isEmpty());
        QVERIFY(AccessControlService::canAccessOrder(db,321));
        QVERIFY(!AccessControlService::canAccessOrder(db,322));
        if (qEnvironmentVariableIntValue("CS_SEARCH_BENCH_ROWS") == 400) {
            sql("SAVEPOINT without_search_index");
            sql("DROP TABLE order_search_v1");
            current=CurrentOrdersQueryService::loadOpenOrders("");
            QVERIFY2(current.errorText.isEmpty(),qPrintable(current.errorText));
            QCOMPARE(current.rows.size(),1); QCOMPARE(current.rows[0][0].toInt(),320);
            params.filter.clear(); params.offset=0;
            first=HistoryQueryService::loadHistory(params);
            QVERIFY2(first.errorText.isEmpty(),qPrintable(first.errorText));
            QCOMPARE(first.rows.size(),1); QCOMPARE(first.rows[0][10].toInt(),323);
            sql("ROLLBACK TO without_search_index"); sql("RELEASE without_search_index");
        }
        for (const QString& account : {QString("unlinked"),QString("disabled"),QString("missing")}) {
            ActivityAuditService::setCurrentActor({account,account,"master","test"});
            QVERIFY(CurrentOrdersQueryService::loadOpenOrders("").rows.isEmpty());
            params.filter.clear(); QVERIFY(HistoryQueryService::loadHistory(params).rows.isEmpty());
        }
        ActivityAuditService::setCurrentActor({"owner","Owner","owner","test"});
        QVERIFY(!CurrentOrdersQueryService::loadOpenOrders("330").rows.isEmpty());
        params.filter="322"; QVERIFY(!HistoryQueryService::loadHistory(params).rows.isEmpty());
    }
    void cleanupTestCase() { ActivityAuditService::clearCurrentActor(); db.close(); db = {}; QSqlDatabase::removeDatabase("carservice_main"); }
};
QTEST_MAIN(OrderSearchPerformanceTests)
#include "OrderSearchPerformanceTests.moc"
