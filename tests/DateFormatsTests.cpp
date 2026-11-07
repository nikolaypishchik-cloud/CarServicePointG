#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include "core/util/DateFormats.h"
#include "core/util/PlateUtils.h"
#include "core/services/BusinessFormatsMigration.h"

class DateFormatsTests : public QObject {
    Q_OBJECT
private slots:
    void dates_data() {
        QTest::addColumn<QString>("input");
        for (const QString& input : {QString("2026-09-30"),QString("30.09.26"),QString("30.09.2026"),QString("30/09/2026"),QString("2026/09/30")})
            QTest::newRow(qPrintable(input)) << input;
    }
    void dates() {
        QFETCH(QString,input);
        QCOMPARE(DateFormats::parseDate(input),QDate(2026,9,30));
        QCOMPARE(DateFormats::date(input),QString("30.09.26"));
    }
    void timestamps() {
        for (const QString& value : {QString("2026-09-30T09:12:13.456Z"),QString("2026-09-30T12:12:13.456+03:00"),QString("2026-09-30T12:12:13")}) {
            const auto stored=DateFormats::storage(value);
            QVERIFY(stored.startsWith("30.09.26 "));
            QCOMPARE(DateFormats::parseDateTime(stored),DateFormats::parseDateTime(value));
        }
        QCOMPARE(DateFormats::dateTime("30.09.26/12.15"),QString("30.09.26 12:15"));
        QCOMPARE(DateFormats::parseDate("29.02.24"),QDate(2024,2,29));
        QVERIFY(!DateFormats::parseDate("29.02.25").isValid());
        QVERIFY(!DateFormats::parseDateTime("30.09.26 99:00").isValid());
        QCOMPARE(DateFormats::date("not a date"),QString("not a date"));
        QCOMPARE(DateFormats::date(""),QString());
        QCOMPARE(DateFormats::storage("1999-12-31"),QString("1999-12-31"));
        QCOMPARE(DateFormats::storage("2026/09/30"),QString("30.09.26"));
    }
    void plates() {
        const QString expected=QString::fromUtf8("АА 1212 ВА");
        for(const QString& raw : {QString("aa1212ba"),QString("AA-1212-BA"),QString::fromUtf8("аа 1212 ва")}) {
            QCOMPARE(PlateUtils::formatPlateDisplay(raw),expected);
            QCOMPARE(PlateUtils::normalizePlate(raw),QString("AA1212BA"));
        }
    }
    void sqlDates() {
        auto db=QSqlDatabase::addDatabase("QSQLITE","date_formats_test");
        db.setDatabaseName(":memory:");
        QVERIFY(db.open());
        {
            QSqlQuery q(db);
            QVERIFY(q.exec("CREATE TABLE sample(value TEXT)"));
            for(const QString& value : {QString("30.09.26"),QString("30.09.2026"),QString("2026-09-30"),QString("30.09.26 12:30")}) {
                QVERIFY(q.exec("DELETE FROM sample"));
                q.prepare("INSERT INTO sample VALUES(?)"); q.addBindValue(value); QVERIFY(q.exec());
                QVERIFY(q.exec("SELECT date("+DateFormats::isoSql("value")+") FROM sample"));
                QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString("2026-09-30"));
                q.finish();
            }
        }
        db.close(); db=QSqlDatabase(); QSqlDatabase::removeDatabase("date_formats_test");
    }
    void migration() {
        auto db=QSqlDatabase::addDatabase("QSQLITE","business_formats_test");
        db.setDatabaseName(":memory:"); QVERIFY(db.open());
        {
            QSqlQuery q(db);
            QVERIFY(q.exec("CREATE TABLE work_orders(number INTEGER PRIMARY KEY,opened_at TEXT,closed_at TEXT,plate TEXT,plate_norm TEXT)"));
            QVERIFY(q.exec("INSERT INTO work_orders VALUES(1,'2026-09-30T12:30:00','30.09.2026 15:40','AA1212BA','AA1212BA')"));
            QString error;
            QVERIFY2(BusinessFormatsMigration::apply(db,&error),qPrintable(error));
            QVERIFY2(BusinessFormatsMigration::apply(db,&error),qPrintable(error));
            QVERIFY(q.exec("SELECT opened_at,closed_at,plate,plate_norm FROM work_orders"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toString(),QString("30.09.26 12:30:00"));
            QCOMPARE(q.value(1).toString(),QString("30.09.26 15:40"));
            QCOMPARE(q.value(2).toString(),QString::fromUtf8("АА 1212 ВА"));
            QCOMPARE(q.value(3).toString(),QString("AA1212BA"));
            q.finish();
            QVERIFY(db.transaction());
            QVERIFY(q.exec("UPDATE work_orders SET opened_at='2027-01-01',plate='aa-1111-ba' WHERE number=1"));
            QVERIFY(q.exec("SELECT opened_at,plate FROM work_orders")); QVERIFY(q.next());
            QCOMPARE(q.value(0).toString(),QString("01.01.27"));
            QCOMPARE(q.value(1).toString(),QString::fromUtf8("АА 1111 ВА")); q.finish();
            QVERIFY(db.rollback());
            QVERIFY(q.exec("SELECT opened_at FROM work_orders")); QVERIFY(q.next());
            QCOMPARE(q.value(0).toString(),QString("30.09.26 12:30:00")); q.finish();
        }
        db.close(); db=QSqlDatabase(); QSqlDatabase::removeDatabase("business_formats_test");
    }
    void ambiguousCenturyRollsBack() {
        auto db=QSqlDatabase::addDatabase("QSQLITE","date_century_test");
        db.setDatabaseName(":memory:"); QVERIFY(db.open());
        {
            QSqlQuery q(db);
            QVERIFY(q.exec("CREATE TABLE work_orders(number INTEGER PRIMARY KEY,opened_at TEXT)"));
            QVERIFY(q.exec("INSERT INTO work_orders VALUES(1,'2026-04-14'),(2,'1926-04-14')"));
            QString error;
            QVERIFY(!BusinessFormatsMigration::apply(db,&error));
            QVERIFY(error.contains("1926-04-14"));
            QVERIFY(q.exec("SELECT opened_at FROM work_orders WHERE number=1"));
            QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString("2026-04-14"));
        }
        db.close(); db=QSqlDatabase(); QSqlDatabase::removeDatabase("date_century_test");
    }
    void wireDatesPreserveIdentity() {
        QJsonObject payload{{"opened_at","30.09.26 12:30:00"},{"due_date","01.10.26"},
                            {"order_uid","order-30.09.26"},{"updated_at","2026-09-30T12:31:00.123Z"},
                            {"note","30.09.26"},{"day","30.09.26"}};
        const auto result=DateFormats::protocolDates(payload);
        QCOMPARE(result.value("due_date").toString(),QString("2026-10-01"));
        QCOMPARE(DateFormats::parseDateTime(result.value("opened_at").toString()),QDateTime(QDate(2026,9,30),QTime(12,30)));
        for(const QString& key : {QString("order_uid"),QString("updated_at"),QString("note"),QString("day")})
            QCOMPARE(result.value(key),payload.value(key));
    }
};
QTEST_MAIN(DateFormatsTests)
#include "DateFormatsTests.moc"
