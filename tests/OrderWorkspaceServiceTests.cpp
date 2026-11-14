#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>

#include "core/crm/CrmClientService.h"
#include "core/services/AccessControlService.h"
#include "infrastructure/db/sqlite/DbBootstrapEnsureSchema.h"
#include "infrastructure/db/sqlite/DbFacadeSupport.h"
#include "infrastructure/db/sqlite/ScopedConnectionManager.h"
#include "TestSourceAssertions.h"

namespace {

QString uniqueConnectionName()
{
    return QStringLiteral("crm_order_link_") + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

void execOk(QSqlDatabase db, const QString& sql)
{
    QSqlQuery q(db);
    QVERIFY2(q.exec(sql), qPrintable(q.lastError().text() + QStringLiteral(" SQL=") + sql));
}

int scalarInt(QSqlDatabase db, const QString& sql)
{
    QSqlQuery q(db);
    if (!q.exec(sql)) qFatal("%s", qPrintable(q.lastError().text() + QStringLiteral(" SQL=") + sql));
    if (!q.next()) qFatal("Expected scalar row");
    return q.value(0).toInt();
}

QString scalarText(QSqlDatabase db, const QString& sql)
{
    QSqlQuery q(db);
    if (!q.exec(sql)) qFatal("%s", qPrintable(q.lastError().text() + QStringLiteral(" SQL=") + sql));
    if (!q.next()) qFatal("Expected scalar row");
    return q.value(0).toString();
}

} // namespace

class OrderWorkspaceServiceTests final : public QObject {
    Q_OBJECT
private slots:
    void criticalRegressionGuardsRemainWired();
    void crmBackfillCreatesLocalClientVehicleOnce();
    void lateRouterDdlFailurePreservesExistingViews();
    void failedPermissionSeedPreservesCallerTransaction();
};

void OrderWorkspaceServiceTests::criticalRegressionGuardsRemainWired()
{
    cs::tests::requireAnyContains("src/core/services/OrderWorkspaceService.cpp", {"workspace", "order_uid"});
    cs::tests::requireAnyContains("src/core/services/OrderStoragePolicyService.cpp", {"OrderStoragePolicyService", "1000"});
}

void OrderWorkspaceServiceTests::lateRouterDdlFailurePreservesExistingViews()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QString error;
    ScopedSqliteConnection connection(root.filePath("router.db"), "router-rollback", &error);
    QVERIFY2(connection.isOpen(), qPrintable(error));
    auto db = connection.db();
    QVERIFY(ensureWorkspaceSchema(db));
    QVERIFY(rebuildRouterViews(db));
    execOk(db, "DROP VIEW router_work_orders");
    execOk(db, "CREATE VIEW router_work_orders AS SELECT * FROM work_orders WHERE 0");
    const auto original = scalarText(db, "SELECT sql FROM sqlite_schema WHERE name='router_work_orders'");
    execOk(db, "DROP VIEW router_open_orders");
    execOk(db, "CREATE TABLE router_open_orders(marker INTEGER)");

    QVERIFY(!rebuildRouterViews(db));
    QCOMPARE(scalarText(db, "SELECT sql FROM sqlite_schema WHERE name='router_work_orders'"), original);
    execOk(db, "DROP TABLE router_open_orders");
    QVERIFY(rebuildRouterViews(db));
    QVERIFY(scalarText(db, "SELECT sql FROM sqlite_schema WHERE name='router_work_orders'") != original);
}

void OrderWorkspaceServiceTests::failedPermissionSeedPreservesCallerTransaction()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QString error;
    ScopedSqliteConnection connection(root.filePath("roles.db"), "roles-rollback", &error);
    QVERIFY2(connection.isOpen(), qPrintable(error));
    auto db = connection.db();
    execOk(db, "CREATE TABLE caller_marker(id INTEGER PRIMARY KEY)");
    QVERIFY(db.transaction());
    execOk(db, "INSERT INTO caller_marker VALUES(1)");
    // The final role normalization requires staff_accounts after grants have been seeded.
    QVERIFY(!AccessControlService::ensureSchema(db, &error));
    QVERIFY(error.contains("staff_accounts"));
    QCOMPARE(scalarInt(db, "SELECT COUNT(*) FROM sqlite_schema WHERE name LIKE 'access_role%'"), 0);
    QCOMPARE(scalarInt(db, "SELECT COUNT(*) FROM caller_marker"), 1);

    execOk(db, "CREATE TABLE staff_accounts(id TEXT PRIMARY KEY,role TEXT)");
    QVERIFY2(AccessControlService::ensureSchema(db, &error), qPrintable(error));
    QVERIFY(scalarInt(db, "SELECT COUNT(*) FROM access_role_permissions") > 0);
    QVERIFY(db.rollback());
    QCOMPARE(scalarInt(db, "SELECT COUNT(*) FROM caller_marker"), 0);
    QCOMPARE(scalarInt(db, "SELECT COUNT(*) FROM sqlite_schema WHERE name LIKE 'access_role%'"), 0);
}

void OrderWorkspaceServiceTests::crmBackfillCreatesLocalClientVehicleOnce()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString connectionName = uniqueConnectionName();
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(dir.filePath(QStringLiteral("crm_backfill.db")));
        QVERIFY2(db.open(), qPrintable(db.lastError().text()));

        execOk(db, QStringLiteral(
            "CREATE TABLE work_orders("
            "number INTEGER PRIMARY KEY,"
            "order_uid TEXT NOT NULL DEFAULT '',"
            "client TEXT NOT NULL DEFAULT '',"
            "client_phone TEXT NOT NULL DEFAULT '',"
            "plate TEXT NOT NULL DEFAULT '',"
            "vin TEXT NOT NULL DEFAULT '',"
            "car TEXT NOT NULL DEFAULT '',"
            "car_year INTEGER NOT NULL DEFAULT 0,"
            "odometer INTEGER NOT NULL DEFAULT 0,"
            "engine_type TEXT NOT NULL DEFAULT '',"
            "fuel_type TEXT NOT NULL DEFAULT '',"
            "color TEXT NOT NULL DEFAULT '',"
            "car_color TEXT NOT NULL DEFAULT '')"));
        execOk(db, QStringLiteral(
            "INSERT INTO work_orders(number, order_uid, client, client_phone, plate, vin, car, car_year, odometer, engine_type, fuel_type, color) "
            "VALUES(1, 'order-test-1', 'Іван Петренко', '+380501112233', 'AA1234BB', 'WVWZZZ1JZXW000001', 'Volkswagen Golf', 2018, 123456, '1.6', 'petrol', 'blue')"));

        QString error;
        QVERIFY2(CrmClientService::backfillWorkOrders(db, &error), qPrintable(error));
        QVERIFY2(CrmClientService::backfillWorkOrders(db, &error), qPrintable(error));

        QCOMPARE(scalarInt(db, QStringLiteral("SELECT COUNT(*) FROM crm_clients")), 1);
        QCOMPARE(scalarInt(db, QStringLiteral("SELECT COUNT(*) FROM crm_vehicles")), 1);
        QCOMPARE(scalarInt(db, QStringLiteral("SELECT COUNT(*) FROM crm_client_events WHERE order_uid='order-test-1' AND event_type='order_linked'")), 1);
        QVERIFY(!scalarText(db, QStringLiteral("SELECT COALESCE(client_uid,'') FROM work_orders WHERE number=1")).trimmed().isEmpty());
        QVERIFY(!scalarText(db, QStringLiteral("SELECT COALESCE(vehicle_uid,'') FROM work_orders WHERE number=1")).trimmed().isEmpty());

        db.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
}

QTEST_MAIN(OrderWorkspaceServiceTests)
#include "OrderWorkspaceServiceTests.moc"
