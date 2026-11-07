#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>

#include "core/crm/CrmClientService.h"
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
};

void OrderWorkspaceServiceTests::criticalRegressionGuardsRemainWired()
{
    cs::tests::requireAnyContains("src/core/services/OrderWorkspaceService.cpp", {"workspace", "order_uid"});
    cs::tests::requireAnyContains("src/core/services/OrderStoragePolicyService.cpp", {"OrderStoragePolicyService", "1000"});
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
