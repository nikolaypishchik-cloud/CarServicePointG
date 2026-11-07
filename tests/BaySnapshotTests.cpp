#include "core/services/orders/BaySnapshot.h"
#include "ui/tabs/orders/BayOccupancyPage.h"
#include <QtTest>
#include <QDateEdit>
#include <QJsonArray>
#include <QTableWidget>
#include <QTabWidget>
#include <QTabBar>
#include <QPushButton>
#include <QFontDatabase>
#include <QDialog>
#include <QComboBox>
#include <QLabel>
#include <QHeaderView>
#include <QSettings>
#include <QTemporaryDir>
#include <QScrollBar>
#include <QStandardPaths>
#include <QUuid>
#include "infrastructure/db/sqlite/Db.h"

namespace {
const QDate day(2026,10,24);
qint64 at(int h) { return QDateTime(day,QTime(h,0),QTimeZone("Europe/Kyiv")).toMSecsSinceEpoch(); }
QJsonObject fixture() {
    QJsonArray bays;
    const QJsonObject calendar{{"from",day.toString(Qt::ISODate)},{"until",QJsonValue::Null},
        {"timezone","Europe/Kyiv"},{"weekdays",QJsonArray{1,2,3,4,5,6}},
        {"shifts",QJsonArray{QJsonArray{"08:00","12:00"},QJsonArray{"13:00","18:00"}}}};
    for(const auto& uid:{"1","2","3"}) {
        bays.append(QJsonObject{{"uid",uid},{"name",QString("Post ")+uid},
            {"active_from",at(0)},{"inactive_from",QJsonValue::Null},{"confirmed_from",at(8)},
            {"calendars",QJsonArray{calendar}}});
    }
    return {{"as_of_ms",at(18)},{"bays",bays},{"states",QJsonArray{QJsonObject{{"order_uid","engine-order"},{"state","on_bay"},{"bay_uid","3"},{"version",1}}}},
        {"intervals",QJsonArray{QJsonObject{{"uid","interval"},{"bay_uid","3"},{"order_uid","engine-order"},{"begin_ms",at(8)},{"end_ms",QJsonValue::Null},{"actor","owner"}}}}};
}
}
class BaySnapshotTests : public QObject {
    Q_OBJECT
    QTemporaryDir settingsDir;
private slots:
    void initTestCase() {
        QVERIFY(settingsDir.isValid());
        QCoreApplication::setOrganizationName("BaySnapshotTests");
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setApplicationName("Safety-BaySnapshot-" + QUuid::createUuid().toString(QUuid::Id128));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settingsDir.path());
        QString error;
        QVERIFY2(Database::prepareRuntime(&error),qPrintable(error));
        QVERIFY2(Database::openPrepared(&error),qPrintable(error));
        const int id=QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR")+"/Fonts/arial.ttf");
        QVERIFY(id>=0);QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(id).first(),10));
    }
    void cleanupTestCase() { Database::close(); }
    void allThreeResourcesAndServerWatermark() {
        const auto data=BaySnapshot::parse(fixture());QVERIFY2(data.error.isEmpty(),qPrintable(data.error));QCOMPARE(data.bays.size(),3);
        const auto report=BayLoadReport::calculate(data.bays,day,day,data.asOf);
        QVERIFY(report.error.isEmpty());QCOMPARE(report.total.availableMs,27LL*3600000);QCOMPARE(report.total.occupiedMs,9LL*3600000);
        QCOMPARE(report.bays.value("3").occupiedMs,9LL*3600000);QCOMPARE(*report.total.coveragePercent(),100.0);
    }
    void unknownIsNotFree() {
        auto json=fixture();auto bays=json["bays"].toArray();auto bay=bays[0].toObject();bay["confirmed_from"]=QJsonValue::Null;bays[0]=bay;json["bays"]=bays;
        const auto data=BaySnapshot::parse(json);QVERIFY(data.error.isEmpty());
        const auto report=BayLoadReport::calculate(data.bays,day,day,data.asOf);
        QCOMPARE(report.bays.value("1").unknownMs,9LL*3600000);QCOMPARE(report.bays.value("1").freeMs,0);QVERIFY(!report.bays.value("1").loadPercent());
    }
    void malformedSnapshotsFailClosed() {
        for(const auto& key:{"as_of_ms","bays","intervals","states"}) {auto json=fixture();json.remove(key);QVERIFY(!BaySnapshot::parse(json).error.isEmpty());}
        auto json=fixture();auto bays=json["bays"].toArray();bays.append(bays[0]);json["bays"]=bays;QVERIFY(!BaySnapshot::parse(json).error.isEmpty());
        json=fixture();json["as_of_ms"]=0;QVERIFY(!BaySnapshot::parse(json).error.isEmpty());
        json=fixture();auto intervals=json["intervals"].toArray();auto i=intervals[0].toObject();i["bay_uid"]="missing";intervals[0]=i;json["intervals"]=intervals;QVERIFY(!BaySnapshot::parse(json).error.isEmpty());
    }
    void unavailableTimeReducesFundWithoutReleasingOrder() {
        auto json=fixture();auto bays=json["bays"].toArray();auto b=bays[2].toObject();
        b["unavailable"]=QJsonArray{QJsonObject{{"begin_ms",at(9)},{"end_ms",at(11)},{"reason","Repair"}}};bays[2]=b;json["bays"]=bays;
        const auto parsed=BaySnapshot::parse(json);QVERIFY(parsed.error.isEmpty());
        const auto report=BayLoadReport::calculate(parsed.bays,day,day,parsed.asOf);
        QCOMPARE(report.total.availableMs,25LL*3600000);QCOMPARE(report.total.occupiedMs,7LL*3600000);
        QCOMPARE(parsed.states.value("engine-order").toObject().value("state").toString(),QString("on_bay"));
    }
    void correctionDialogsCancelWithoutMutation() {
        auto json=fixture();json["can_correct"]=true;
        BayOccupancyPage page;page.resize(1320,760);QVERIFY(page.displaySnapshot(json));page.show();
        auto* tabs=page.findChild<QTabWidget*>();tabs->setCurrentIndex(2);
        auto* history=page.findChild<QTableWidget*>("bayHistoryTable");QVERIFY(history);history->setCurrentCell(0,0);
        const auto output=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
        if(!output.isEmpty())QVERIFY(QDir().mkpath(output));
        for(const auto& name:{"correctBayHistory","editBayCalendar"}) {
            bool seen=false,screenshot=true;
            QTimer::singleShot(50,&page,[&]{
                if(auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
                    seen=true;
                    if(!output.isEmpty())screenshot=dialog->grab().save(output+"/"+name+".png");
                    dialog->reject();
                }
            });
            page.findChild<QPushButton*>(name)->click();QVERIFY(seen);QVERIFY(screenshot);
        }
        QCOMPARE(history->rowCount(),1);QCOMPARE(history->item(0,3)->text(),QString::fromUtf8("Триває"));
        json["can_correct"]=false;QVERIFY(page.displaySnapshot(json));
        QVERIFY(!page.findChild<QPushButton*>("correctBayHistory")->isEnabled());
        QVERIFY(!page.findChild<QPushButton*>("editBayCalendar")->isEnabled());
    }
    void actualPlacementContrast() {
        const auto output=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
        for(bool dark:{true,false}) {
            qApp->setProperty("carservice.graphiteThemeActive",dark);
            QWidget host;host.setStyleSheet("QWidget { background:black; color:black; }");
            BayOccupancyPage page(&host);page.resize(1320,760);QVERIFY(page.displaySnapshot(fixture()));page.show();
            bool seen=false,readable=true;
            QTimer::singleShot(50,&page,[&]{
                if(auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
                    seen=true;
                    for(auto* combo:dialog->findChildren<QComboBox*>())
                        readable=readable && combo->palette().color(QPalette::Text).lightness()<80 && combo->palette().color(QPalette::Base).lightness()>200;
                    for(auto* label:dialog->findChildren<QLabel*>())
                        readable=readable && (dark ? label->palette().color(QPalette::WindowText).lightness()>200 : label->palette().color(QPalette::WindowText).lightness()<80);
                    if(!output.isEmpty())readable=dialog->grab().save(output+(dark?"/placement-dark.png":"/placement-light.png")) && readable;
                    dialog->reject();
                }
            });
            page.findChild<QPushButton*>("changeActualBay")->click();QVERIFY(seen);QVERIFY(readable);
        }
    }
    void occupancyTabsContrast() {
        for(bool dark:{true,false}) {
            qApp->setProperty("carservice.graphiteThemeActive",dark);
            BayOccupancyPage page;page.resize(1320,760);
            QVERIFY(page.displaySnapshot(fixture()));page.show();
            QTest::qWait(50);
            auto* tabs=page.findChild<QTabWidget*>("bayOccupancyTabs");QVERIFY(tabs);
            QVERIFY(tabs->documentMode());
            const QColor text=tabs->tabBar()->palette().color(QPalette::WindowText);
            QVERIFY(dark ? text.lightness()>200 : text.lightness()<80);
            const auto output=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
            if(!output.isEmpty()) {
                QVERIFY(QDir().mkpath(output));
                QVERIFY(page.grab().save(output+(dark?"/occupancy-dark.png":"/occupancy-light.png")));
            }
        }
    }
    void manualColumnsSurviveRefreshAndReopen() {
        const QStringList names={"bayResourcesTable","bayLoadTable","bayHistoryTable","bayAuditTable"};
        {
            BayOccupancyPage page;page.resize(1320,760);page.show();
            QVERIFY(page.displaySnapshot(fixture()));
            for(const auto& name:names) {
                auto* table=page.findChild<QTableWidget*>(name);QVERIFY(table);
                for(int c=0;c<table->columnCount();++c)table->setColumnWidth(c,190+c*19);
            }
            auto changed=fixture();auto bays=changed["bays"].toArray();
            auto bay=bays[0].toObject();bay["confirmed_from"]=QJsonValue::Null;bays[0]=bay;changed["bays"]=bays;
            for(int pass=0;pass<3;++pass) {
                QVERIFY(page.displaySnapshot(pass==1?changed:fixture()));
                auto* tabs=page.findChild<QTabWidget*>();
                for(int i=0;i<tabs->count();++i){tabs->setCurrentIndex(i);QTest::qWait(10);}
                page.resize(1200+pass*50,740);
                for(const auto& name:names) {
                    auto* table=page.findChild<QTableWidget*>(name);
                    for(int c=0;c<table->columnCount();++c)QCOMPARE(table->columnWidth(c),190+c*19);
                }
            }
        }
        BayOccupancyPage reopened;QVERIFY(reopened.displaySnapshot(fixture()));
        for(const auto& name:names) {
            auto* table=reopened.findChild<QTableWidget*>(name);
            for(int c=0;c<table->columnCount();++c)QCOMPARE(table->columnWidth(c),190+c*19);
        }
    }
    void uiSnapshotNoDatabaseOrNetwork() {
        BayOccupancyPage page;page.resize(1320,760);
        for(auto* edit:page.findChildren<QDateEdit*>()) edit->setDate(day);
        QVERIFY(page.displaySnapshot(fixture()));page.show();QTest::qWait(30);
        auto* resources=page.findChild<QTableWidget*>("bayResourcesTable");QVERIFY(resources);QCOMPARE(resources->rowCount(),3);
        auto* report=page.findChild<QTableWidget*>("bayLoadTable");QVERIFY(report);QCOMPARE(report->rowCount(),4);QCOMPARE(report->item(3,1)->text(),QString("27.00"));
        for(auto* button:page.findChildren<QPushButton*>()) if(button->isVisible()) QVERIFY2(button->width()>=button->fontMetrics().horizontalAdvance(button->text()),qPrintable(button->text()));
        const auto output=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
        if(!output.isEmpty()) {QDir().mkpath(output);QVERIFY(page.grab().save(output+"/bay-current.png"));page.findChild<QTabWidget*>()->setCurrentIndex(1);QTest::qWait(30);QVERIFY(page.grab().save(output+"/bay-report.png"));}
        QVERIFY(!page.displaySnapshot({}));
    }
    void readableAuditAndStableRefresh() {
        auto json=fixture();
        json["orders"]=QJsonArray{QJsonObject{{"uid","engine-order"},{"number","477"},{"status","in_progress"}}};
        QJsonArray changes;
        for(int n=0;n<60;++n)changes.append(QJsonObject{{"uid",QString::number(n)},
            {"actor","owner-private-id"},{"recorded_ms",at(10)+n},{"effective_ms",at(10)+n},
            {"before",QJsonObject{{"order_uid","engine-order"},{"state","unknown"},{"bay_uid",QJsonValue::Null}}},
            {"after",QJsonObject{{"order_uid","engine-order"},{"state","on_bay"},{"bay_uid","3"}}}});
        json["audit"]=changes;
        BayOccupancyPage page;page.resize(1100,650);QVERIFY(page.displaySnapshot(json));page.show();
        auto* tabs=page.findChild<QTabWidget*>();tabs->setCurrentIndex(3);QTest::qWait(50);
        auto* audit=page.findChild<QTableWidget*>("bayAuditTable");QVERIFY(audit);
        QCOMPARE(audit->item(0,2)->text(),QString::fromUtf8("Власник"));
        QVERIFY(audit->item(0,5)->text().contains("477"));
        QVERIFY(audit->item(0,5)->text().contains(QString::fromUtf8("На посту")));
        QVERIFY(!audit->item(0,5)->text().contains("order_uid"));
        QVERIFY(!audit->item(0,5)->text().contains("engine-order"));
        audit->setCurrentCell(30,2);audit->selectRow(30);audit->setRowHeight(30,75);
        audit->horizontalScrollBar()->setValue(120);audit->verticalScrollBar()->setValue(24);
        const int x=audit->horizontalScrollBar()->value(),y=audit->verticalScrollBar()->value();
        const auto position=audit->mapTo(&page,QPoint());
        const auto columnWidth=audit->columnWidth(2);
        auto* status=page.findChild<QLabel*>("bayOccupancyStatus");QVERIFY(status);
        status->setText(QString::fromUtf8("Оновлення…"));QTest::qWait(20);
        QCOMPARE(audit->mapTo(&page,QPoint()),position);
        for(int pass=0;pass<3;++pass) {
            QVERIFY(page.displaySnapshot(json));QTest::qWait(20);
            QCOMPARE(audit->currentRow(),30);QVERIFY(audit->selectionModel()->isRowSelected(30,{}));
            QCOMPARE(audit->rowHeight(30),75);QCOMPARE(audit->columnWidth(2),columnWidth);
            QCOMPARE(audit->horizontalScrollBar()->value(),x);QCOMPARE(audit->verticalScrollBar()->value(),y);
            QCOMPARE(audit->mapTo(&page,QPoint()),position);QCOMPARE(tabs->currentIndex(),3);
        }
        const auto output=qEnvironmentVariable("CS_TEST_SCREENSHOTS");
        if(!output.isEmpty())QVERIFY(page.grab().save(output+"/readable-audit.png"));
    }
};
QTEST_MAIN(BaySnapshotTests)
#include "BaySnapshotTests.moc"
