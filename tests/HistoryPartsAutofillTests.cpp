#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include "core/services/orders/HistoryPartsAutofillService.h"
#include "core/services/ActivityAuditService.h"
#include "core/services/AccessControlService.h"
#include "core/services/StaffAccountService.h"
#include "core/repository/ServiceRepository.h"
#include "TestSourceAssertions.h"
#include "ui/tabs/orders/WorkPartsDialog.h"
#include "ui/tabs/orders/HistoryPartsDraftRoles.h"
#include "infrastructure/db/sqlite/Db.h"
#include <QTableWidget>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QSettings>
#include <QLineEdit>
#include <QUuid>

class HistoryPartsAutofillTests : public QObject {
    Q_OBJECT
    QSqlDatabase db;
    void sql(const QString& statement) {
        QSqlQuery q(db);
        QVERIFY2(q.exec(statement), qPrintable(q.lastError().text() + "\n" + statement));
    }
    int count() {
        QSqlQuery q(db);
        if (!q.exec("SELECT COUNT(*) FROM work_order_parts WHERE order_uid='current'") || !q.next()) return -1;
        return q.value(0).toInt();
    }
    void apply() {
        QString error;
        QVERIFY2(HistoryPartsAutofillService::apply(db, 2, 20, &error), qPrintable(error));
    }
private slots:
    void liveEditorFillsBeforeSave() {
        ActivityAuditService::setCurrentActor({"qa-owner", "Owner", "owner", "qa-session"});
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("CarServiceTests");
        QCoreApplication::setApplicationName("Safety-HistoryPartsLiveEditor-" + QUuid::createUuid().toString(QUuid::Id128));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
        {
            QString runtimeError;
            QVERIFY2(Database::prepareRuntime(&runtimeError), qPrintable(runtimeError));
            QVERIFY2(Database::openPrepared(&runtimeError), qPrintable(runtimeError));
            auto liveDb = Database::db();
            QVERIFY(Database::ensureSchema());
            QString accessError;
            QVERIFY2(StaffAccountService::ensureSchema(liveDb, &accessError), qPrintable(accessError));
            QVERIFY2(AccessControlService::ensureSchema(liveDb, &accessError), qPrintable(accessError));
            QSqlQuery q(liveDb);
            QVERIFY(q.exec("INSERT INTO staff_accounts(id,display_name,role,active) VALUES('qa-owner','Owner','owner',1)"));
            const QStringList setup = {
                "INSERT INTO masters(id,name,role) VALUES('live-master','Test Master','master')",
                "INSERT INTO work_orders(number,order_number,order_uid,order_year,car,car_brand,car_model,car_year,engine_volume,fuel_type,status,opened_at,closed_at,plate,plate_norm,client,master_id) VALUES(1,1,'history',2026,'Audi Q7','Audi','Q7',2015,'3.0','Diesel','completed','2026-08-01','2026-09-01','AA 1234 AA','AA1234AA','Test','live-master'),(2,2,'current',2026,'Audi Q7','Audi','Q7',2015,'3.0','Diesel','open','2026-10-01','','AA 1234 AA','AA1234AA','Test','live-master')",
                "INSERT INTO work_order_services(id,order_number,order_uid,title,qty,price,amount) VALUES(10,1,'history','Oil change',1,100,100),(11,1,'history','Brake change',1,200,200)",
                "INSERT INTO work_order_parts(order_number,order_uid,service_id,part_number,name,qty,price,amount) VALUES(1,'history',10,'OEM-123','Oil filter',1,200,200),(1,'history',10,'','Oil 5W40',8,400,3200),(1,'history',11,'BRAKE-1','Brake pads',2,500,1000)"
            };
            for (const auto& statement : setup) QVERIFY2(q.exec(statement), qPrintable(q.lastError().text()));
            QWidget parent;
            parent.resize(1400, 800);
            parent.show();
            QJsonObject savedDraft;
            {
                WorkPartsDialog dialog(&parent, 2);
                QVERIFY(dialog.m_servicesTable);
                QVERIFY(dialog.m_partsTable);
                dialog.setEmbedded(true);
                dialog.show();
                dialog.activateWindow();
                QTest::qWait(500);
                // Start a snapshot before typing; its queued result must not erase newer draft edits.
                dialog.scheduleWorkPartsRefresh(true, true, false, false, QStringLiteral("test_edit_during_refresh"));
                // A normal table edit must trigger the queued preview, without OK/close.
                dialog.insertEmptyServiceRow(0);
                auto* editor = dialog.m_servicesTable->findChild<QLineEdit*>();
                QVERIFY(editor);
                editor->selectAll();
                QTest::keyClicks(editor, "Oil change");
                QTest::keyClick(editor, Qt::Key_Return);
                auto partCount = [&]() {
                    return dialog.serializeDraftState().value("parts").toArray().size();
                };
                QTRY_COMPARE(partCount(), 2);
                QVERIFY(q.exec("SELECT COUNT(*) FROM work_order_parts WHERE order_uid='current'"));
                QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 0);
                QVERIFY(q.exec("SELECT COUNT(*) FROM work_order_services WHERE order_uid='current'"));
                QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 0);
                q.finish();
                dialog.m_servicesTable->item(0, 1)->setText("  Oil change  ");
                QTest::qWait(50);
                QCOMPARE(partCount(), 2);
                dialog.m_servicesTable->item(0, 1)->setText("Different work");
                QTRY_COMPARE(partCount(), 0);
                dialog.m_servicesTable->item(0, 1)->setText("Oil change");
                QTRY_COMPARE(partCount(), 2);
                dialog.insertEmptyServiceRow(1);
                dialog.m_servicesTable->item(1, 1)->setText("Brake change");
                QTRY_COMPARE(partCount(), 3);
                const auto draft = dialog.serializeDraftState();
                const auto services = draft.value("services").toArray();
                const auto parts = draft.value("parts").toArray();
                QCOMPARE(services.size(), 2);
                const QString oilKey = services[0].toObject().value("historyWorkKey").toString();
                const QString brakeKey = services[1].toObject().value("historyWorkKey").toString();
                QVERIFY(!oilKey.isEmpty() && !brakeKey.isEmpty() && oilKey != brakeKey);
                QCOMPARE(parts[0].toObject().value("historyWorkKey").toString(), oilKey);
                QCOMPARE(parts[2].toObject().value("historyWorkKey").toString(), brakeKey);
                savedDraft = draft;
                // Discard the editor: generated rows must not leak into persistent data.
            }
            QVERIFY(q.exec("SELECT COUNT(*) FROM work_order_parts WHERE order_uid='current'"));
            QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 0);
            q.finish();
            {
                WorkPartsDialog restored(&parent, 2);
                restored.setEmbedded(true);
                QTest::qWait(500);
                QString error;
                QVERIFY2(restored.restoreDraftState(savedDraft, &error), qPrintable(error));
                QTest::qWait(50);
                QCOMPARE(restored.serializeDraftState().value("services").toArray().size(), 2);
                QCOMPARE(restored.serializeDraftState().value("parts").toArray().size(), 3);
                for (int r = 0; r < restored.m_partsTable->rowCount(); ++r) {
                    auto* name = restored.m_partsTable->item(r, 4);
                    if (name && name->text().compare("Oil filter", Qt::CaseInsensitive) == 0) { restored.m_partsTable->removeRow(r); break; }
                }
                QVERIFY2(restored.commitPendingEdits(&error, false, true), qPrintable(error));
                QVERIFY2(q.exec("SELECT p.name,s.title,p.qty FROM work_order_parts p JOIN work_order_services s ON s.id=p.service_id WHERE p.order_uid='current' ORDER BY p.name"), qPrintable(q.lastError().text()));
                QVERIFY(q.next()); QCOMPARE(q.value(0).toString().toLower(), QString("brake pads")); QCOMPARE(q.value(1).toString().toLower(), QString("brake change")); QCOMPARE(q.value(2).toDouble(), 2.0);
                QVERIFY(q.next()); QCOMPARE(q.value(0).toString().toLower(), QString("oil 5w40")); QCOMPARE(q.value(1).toString().toLower(), QString("oil change")); QCOMPARE(q.value(2).toDouble(), 8.0);
                QVERIFY(!q.next());
            }
            liveDb.close();
        }
        QSqlDatabase::removeDatabase("carservice_main");
    }
    void init() {
        db = QSqlDatabase::addDatabase("QSQLITE", "history_autofill_test");
        db.setDatabaseName(":memory:");
        QVERIFY(db.open());
        sql("CREATE TABLE work_orders(number INTEGER PRIMARY KEY,order_uid TEXT UNIQUE,order_number INTEGER,order_year INTEGER DEFAULT 2026,car_brand TEXT DEFAULT 'Audi',car_model TEXT DEFAULT 'Q7',car TEXT DEFAULT 'Audi Q7',car_year INTEGER DEFAULT 2015,engine_volume TEXT DEFAULT '3.0',fuel_type TEXT DEFAULT 'Diesel',status TEXT DEFAULT 'completed',opened_at TEXT DEFAULT '2026-08-01',closed_at TEXT DEFAULT '2026-09-01',works_sum REAL DEFAULT 0,parts_sum REAL DEFAULT 0)");
        sql("CREATE TABLE work_order_services(id INTEGER PRIMARY KEY,order_uid TEXT,order_number INTEGER,title TEXT DEFAULT 'Oil change',qty REAL DEFAULT 1,price REAL DEFAULT 0,amount REAL DEFAULT 0)");
        sql("CREATE TABLE service_masters(service_id INTEGER,master_id TEXT,share_percent REAL,role TEXT,master_name_closed TEXT)");
        sql("CREATE TABLE work_order_parts(id INTEGER PRIMARY KEY,order_uid TEXT,order_number INTEGER,ordered_at TEXT DEFAULT '',delivery_date TEXT DEFAULT '',installed_at TEXT DEFAULT '',store TEXT DEFAULT '',part_number TEXT DEFAULT '',name TEXT, note TEXT DEFAULT '',manufacturer TEXT DEFAULT '',qty REAL,price REAL DEFAULT 0,amount REAL DEFAULT 0,service_id INTEGER DEFAULT 0,ordered_by_admin INTEGER DEFAULT 1,paid INTEGER DEFAULT 0,purchase_price REAL DEFAULT 0,purchase_amount REAL DEFAULT 0,markup_percent REAL DEFAULT 0,manual_markup_override INTEGER DEFAULT 0,markup_source TEXT DEFAULT 'policy',pricing_updated_at TEXT DEFAULT '',pricing_calc_version INTEGER DEFAULT 1)");
        sql("CREATE INDEX orders_vehicle ON work_orders(car_year,order_uid)");
        sql("CREATE INDEX services_uid ON work_order_services(order_uid,id)");
        sql("CREATE INDEX parts_uid ON work_order_parts(order_uid,id)");
        sql("INSERT INTO work_orders(number,order_uid,order_number) VALUES(1,'history',1)");
        sql("INSERT INTO work_orders(number,order_uid,order_number,status,closed_at) VALUES(2,'current',2,'open','')");
        sql("INSERT INTO work_order_services(id,order_uid,order_number) VALUES(10,'history',1),(20,'current',2)");
        sql("INSERT INTO work_order_parts(id,order_uid,order_number,part_number,name,qty,price,amount,service_id,paid,store,ordered_at) VALUES(1,'history',1,'OEM-123','Oil filter',1,200,200,10,1,'Old store','2026-08-01'),(2,'history',1,'','Oil 5W40',8,400,3200,10,1,'Old store','2026-08-01')");
    }
    void cleanup() {
        db.close(); db = QSqlDatabase();
        QSqlDatabase::removeDatabase("history_autofill_test");
    }
    void exactMatchAndIdempotence() {
        apply(); QCOMPARE(count(), 2);
        apply(); QCOMPARE(count(), 2);
        QSqlQuery q(db);
        QVERIFY(q.exec("SELECT part_number,name,qty,service_id,price,amount,purchase_price,paid,store,ordered_at,installed_at FROM work_order_parts WHERE order_uid='current' ORDER BY id"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toString(), QString("OEM-123"));
        QCOMPARE(q.value(1).toString(), QString("Oil filter")); QCOMPARE(q.value(2).toDouble(), 1.0);
        do {
            QCOMPARE(q.value(3).toInt(), 20);
            for (int i : {4,5,6,7}) QCOMPARE(q.value(i).toDouble(), 0.0);
            for (int i : {8,9,10}) QVERIFY(q.value(i).toString().isEmpty());
        } while (q.next());
        sql("UPDATE work_order_parts SET qty=7.5 WHERE order_uid='current' AND name='Oil 5W40'");
        apply(); QCOMPARE(count(),2);
        QVERIFY(q.exec("SELECT qty FROM work_order_parts WHERE order_uid='current' AND name='Oil 5W40'"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toDouble(),7.5);
    }
    void mismatch_data() {
        QTest::addColumn<QString>("change");
        QTest::newRow("brand") << "UPDATE work_orders SET car_brand='BMW' WHERE number=2";
        QTest::newRow("model") << "UPDATE work_orders SET car_model='Q5' WHERE number=2";
        QTest::newRow("year") << "UPDATE work_orders SET car_year=2016 WHERE number=2";
        QTest::newRow("volume") << "UPDATE work_orders SET engine_volume='2.0' WHERE number=2";
        QTest::newRow("fuel") << "UPDATE work_orders SET fuel_type='Petrol' WHERE number=2";
        QTest::newRow("missing-fuel") << "UPDATE work_orders SET fuel_type='' WHERE number=2";
        QTest::newRow("missing-volume") << "UPDATE work_orders SET engine_volume='' WHERE number=2";
        QTest::newRow("missing-year") << "UPDATE work_orders SET car_year=0 WHERE number=2";
        QTest::newRow("missing-car") << "UPDATE work_orders SET car_brand='',car_model='',car='' WHERE number=2";
        QTest::newRow("different-service") << "UPDATE work_order_services SET title='Oil change and inspection' WHERE id=20";
        QTest::newRow("open-source-with-old-closed-date") << "UPDATE work_orders SET status='open' WHERE number=1";
        QTest::newRow("closed-target") << "UPDATE work_orders SET status='completed' WHERE number=2";
    }
    void mismatch() { QFETCH(QString,change); sql(change); apply(); QCOMPARE(count(),0); }
    void normalizationAndFractionalQuantity() {
        sql("UPDATE work_orders SET car_brand='',car_model='',car='audiQ7',engine_volume='3,0 л',fuel_type=' DIESEL ' WHERE number=2");
        sql("UPDATE work_order_services SET title='  OIL   CHANGE ' WHERE id=20");
        sql("UPDATE work_order_parts SET qty=7.5 WHERE id=2");
        apply(); QCOMPARE(count(),2);
        QSqlQuery q(db); QVERIFY(q.exec("SELECT qty FROM work_order_parts WHERE order_uid='current' AND name='Oil 5W40'"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toDouble(),7.5);
    }
    void legacySingleServiceAndAmbiguousMultiple() {
        sql("UPDATE work_order_parts SET service_id=0 WHERE order_uid='history'");
        sql("INSERT INTO work_order_services(id,order_uid,order_number,title) VALUES(11,'history',1,'Brake change')");
        apply(); QCOMPARE(count(),0);
        sql("UPDATE work_order_parts SET service_id=10 WHERE id=1");
        apply(); QCOMPARE(count(),1);
        sql("DELETE FROM work_order_services WHERE id=11");
        apply(); QCOMPARE(count(),2);
    }
    void latestCompletedOrderOnly() {
        sql("INSERT INTO work_orders(number,order_uid,order_number,closed_at) VALUES(3,'newer',3,'01.10.26')");
        sql("INSERT INTO work_order_services(id,order_uid,order_number) VALUES(30,'newer',3)");
        sql("INSERT INTO work_order_parts(order_uid,order_number,name,qty,service_id) VALUES('newer',3,'Oil 0W30',6.5,30)");
        apply(); QCOMPARE(count(),1);
        QSqlQuery q(db); QVERIFY(q.exec("SELECT name,qty FROM work_order_parts WHERE order_uid='current'"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString("Oil 0W30")); QCOMPARE(q.value(1).toDouble(),6.5);
    }
    void existingManualPartAndDuplicateSourceRows() {
        sql("INSERT INTO work_order_parts(order_uid,order_number,name,part_number,qty,price) VALUES('current',2,'Filter custom name','oem 123',2,999)");
        sql("INSERT INTO work_order_parts(order_uid,order_number,name,qty,service_id) VALUES('history',1,'Oil 5W40',0.5,10)");
        apply(); QCOMPARE(count(),2);
        QSqlQuery q(db); QVERIFY(q.exec("SELECT qty,price FROM work_order_parts WHERE order_uid='current' ORDER BY id"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toDouble(),2.0); QCOMPARE(q.value(1).toDouble(),999.0);
        QVERIFY(q.next()); QCOMPARE(q.value(0).toDouble(),8.5);
    }
    void archivedUidIsolation() {
        sql("CREATE TABLE archived_orders AS SELECT * FROM work_orders WHERE number=1");
        sql("CREATE TABLE archived_services AS SELECT * FROM work_order_services WHERE order_uid='history'");
        sql("CREATE TABLE archived_parts AS SELECT * FROM work_order_parts WHERE order_uid='history'");
        sql("DELETE FROM work_order_parts WHERE order_uid='history'");
        sql("DELETE FROM work_order_services WHERE order_uid='history'");
        sql("DELETE FROM work_orders WHERE number=1");
        sql("CREATE VIEW router_work_orders AS SELECT * FROM work_orders UNION ALL SELECT * FROM archived_orders");
        sql("CREATE VIEW router_work_order_services AS SELECT * FROM work_order_services UNION ALL SELECT * FROM archived_services");
        sql("CREATE VIEW router_work_order_parts AS SELECT * FROM work_order_parts UNION ALL SELECT * FROM archived_parts");
        // Archive service IDs may overlap with IDs in a different order.
        sql("INSERT INTO work_order_parts(order_uid,order_number,name,qty,service_id) VALUES('unrelated',1,'Wrong part',99,10)");
        apply(); QCOMPARE(count(),2);
    }
    void transactionalRollback() {
        sql("DELETE FROM work_order_services WHERE id=20");
        sql("CREATE TRIGGER reject_second BEFORE INSERT ON work_order_parts WHEN NEW.order_uid='current' AND NEW.name='Oil 5W40' BEGIN SELECT RAISE(ABORT,'test failure'); END");
        QVERIFY(db.transaction());
        WorkOrderServiceRow service; service.title="Oil change";
        int id=0; QString error;
        QVERIFY2(ServiceRepository::insert(db,2,service,&id,&error),qPrintable(error));
        QTest::ignoreMessage(QtCriticalMsg,
            "DB_MUTATION part_insert_failed order= 2  orderUid= current  error= test failure Unable to fetch row");
        QVERIFY(!HistoryPartsAutofillService::apply(db,2,id,&error)); QVERIFY(!error.isEmpty());
        QVERIFY2(error.contains(QStringLiteral("test failure")), qPrintable(error));
        QVERIFY(db.rollback()); QCOMPARE(count(),0);
        QSqlQuery q(db); QVERIFY(q.exec("SELECT COUNT(*) FROM work_order_services WHERE order_uid='current'"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),0);
    }
    void heterogeneousHistoryKeepsGeneralSearch() {
        sql("WITH RECURSIVE n(x) AS (VALUES(100) UNION ALL SELECT x+1 FROM n WHERE x<399) INSERT INTO work_orders(number,order_uid,order_number,car_model) SELECT x,'diverse-'||x,x,'Different model '||x FROM n");
        sql("INSERT INTO work_order_services(order_uid,order_number,title) SELECT order_uid,number,'Oil change' FROM work_orders WHERE number>=100");
        sql("INSERT INTO work_order_parts(order_uid,order_number,name,qty,service_id) SELECT order_uid,order_number,'Other car part',1,id FROM work_order_services WHERE order_number>=100");
        apply();QCOMPARE(count(),2);
        QSqlQuery q(db);QVERIFY(q.exec("SELECT name FROM work_order_parts WHERE order_uid='current' ORDER BY name"));
        QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("Oil 5W40"));
        QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("Oil filter"));QVERIFY(!q.next());
    }
    void nullableVehicleDescriptionKeepsMatching() {
        sql("UPDATE work_orders SET car=NULL WHERE number=1");
        apply();QCOMPARE(count(),2);
    }
    void hundredThousandOrders() {
        sql("WITH RECURSIVE n(x) AS (VALUES(100) UNION ALL SELECT x+1 FROM n WHERE x<100097) INSERT INTO work_orders(number,order_uid,order_number,car_model) SELECT x,'perf-'||x,x,'Q5' FROM n");
        sql("INSERT INTO work_order_services(order_uid,order_number,title) SELECT order_uid,number,'Oil change' FROM work_orders WHERE number>=100");
        sql("INSERT INTO work_order_parts(order_uid,order_number,name,part_number,qty,service_id) SELECT order_uid,order_number,'Performance oil','PERF',6.5,id FROM work_order_services WHERE order_number>=100");
        QElapsedTimer timer; timer.start(); apply(); const qint64 elapsed=timer.elapsed();
        QCOMPARE(count(),2);
        qInfo() << "100000 orders history autofill ms=" << elapsed;
        QVERIFY2(elapsed<1000,qPrintable(QString::number(elapsed)));
        sql("DELETE FROM work_order_parts WHERE order_uid='current'");
        sql("UPDATE work_orders SET car_model='Q7' WHERE number>=100");
        timer.restart(); apply(); const qint64 sameCarElapsed=timer.elapsed();
        QCOMPARE(count(),1);
        qInfo() << "100000 same-vehicle orders with parts, ms=" << sameCarElapsed;
        QVERIFY2(sameCarElapsed<1000,qPrintable(QString::number(sameCarElapsed)));
    }
    void entryPointsRemainWired() {
        for (const char* file : {"src/core/services/orders/OrderMutationFacade.cpp",
             "src/ui/tabs/orders/WorkPartsServicePersistence.cpp", "src/ui/tabs/orders/WorkPartsPersistenceCoordinator.cpp"})
            cs::tests::requireContains(file,"HistoryPartsAutofillService::apply");
    }
};

QTEST_MAIN(HistoryPartsAutofillTests)
#include "HistoryPartsAutofillTests.moc"
