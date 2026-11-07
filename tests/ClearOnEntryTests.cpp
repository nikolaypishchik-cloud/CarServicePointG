#include <QtTest>
#include <QFormLayout>
#include <QDateEdit>
#include <QPushButton>
#include "ui/common/filters/ClearOnEntryFilter.h"

class ClearOnEntryTests:public QObject {
    Q_OBJECT
private slots:
    void textClearsOncePerVisitAndUndoWorks() {
        QWidget host;QFormLayout layout(&host);QLineEdit first("AUDI"),second("Q7");
        layout.addRow("Brand",&first);layout.addRow("Model",&second);ClearOnEntryFilter filter(&host);filter.attach(&first,"Brand");filter.attach(&second,"Model");host.show();host.activateWindow();QApplication::processEvents();
        QTest::mouseClick(&first,Qt::LeftButton);QCOMPARE(first.text(),QString());
        QTest::keyClicks(&first,"BMW");QTest::mouseClick(&first,Qt::LeftButton);QCOMPARE(first.text(),QString("BMW"));
        QTest::mouseClick(&second,Qt::LeftButton);QCOMPARE(second.text(),QString());QTest::mouseClick(&first,Qt::LeftButton);QCOMPARE(first.text(),QString());
        QTest::keyClick(&first,Qt::Key_Z,Qt::ControlModifier);QCOMPARE(first.text(),QString("BMW"));
        first.setReadOnly(true);QTest::mouseClick(&second,Qt::LeftButton);QTest::mouseClick(&first,Qt::LeftButton);QCOMPARE(first.text(),QString("BMW"));
    }
    void numericClearNeverSilentlySavesOldValue() {
        QWidget host;QFormLayout layout(&host);QDoubleSpinBox price;price.setRange(0,1000000);price.setLocale(QLocale(QLocale::Ukrainian));price.setValue(1500);QLineEdit other;
        layout.addRow("Price",&price);layout.addRow("Other",&other);ClearOnEntryFilter filter(&host);filter.attach(&price,"Price");host.show();host.activateWindow();QApplication::processEvents();
        auto* line=price.findChild<QLineEdit*>();QTest::mouseClick(line,Qt::LeftButton);QCOMPARE(line->text(),QString());QCOMPARE(filter.firstIncomplete(),&price);
        other.setFocus();QApplication::processEvents();QCOMPARE(filter.firstIncomplete(),&price);QCOMPARE(line->text(),QString());
        QTest::mouseClick(line,Qt::LeftButton);QTest::keyClicks(line,"500,00");price.interpretText();QCOMPARE(price.value(),500.0);QVERIFY(!filter.firstIncomplete());
        QTest::mouseClick(line,Qt::LeftButton);QCOMPARE(price.value(),500.0);QVERIFY(!line->text().isEmpty());
        line->selectAll();QTest::keyClick(line,Qt::Key_Backspace);QCOMPARE(filter.firstIncomplete(),&price);
    }
    void datesAndListSelection() {
        QWidget host;QFormLayout layout(&host);DraftDateEdit date(QDate(2026,10,28));date.setDisplayFormat("dd.MM.yy");
        DraftDateEdit optional(QDate(2026,10,28));QComboBox combo;combo.addItems({"Ordered","Received"});combo.setCurrentIndex(1);QLineEdit other;
        layout.addRow("Date",&date);layout.addRow("Optional",&optional);layout.addRow("State",&combo);layout.addRow("Other",&other);
        ClearOnEntryFilter filter(&host);filter.attach(&date,"Date");filter.attach(&optional,"Optional",true);filter.attach(&combo,"State");host.show();host.activateWindow();QApplication::processEvents();
        auto* line=date.entryEditor();QTest::mouseClick(date.findChild<QLineEdit*>(),Qt::LeftButton);QCOMPARE(line->text(),QString());QVERIFY(line->isVisible());QCOMPARE(filter.firstIncomplete(),&date);
        QTest::keyClicks(line,"29.10.26");QCOMPARE(date.date(),QDate(2026,10,29));QVERIFY(!filter.firstIncomplete());
        QTest::mouseClick(optional.findChild<QLineEdit*>(),Qt::LeftButton);QVERIFY(filter.empty(&optional));QVERIFY(!filter.firstIncomplete());
        QTest::mouseClick(&combo,Qt::LeftButton);QCOMPARE(combo.currentIndex(),-1);QCOMPARE(filter.firstIncomplete(),&combo);combo.hidePopup();combo.setCurrentIndex(0);QVERIFY(!filter.firstIncomplete());
    }
    void incompleteDatesAndCalendarSelection() {
        QWidget host;QFormLayout layout(&host);DraftDateEdit date(QDate(2026,10,28));date.setDisplayFormat("dd.MM.yy");QLineEdit other;
        layout.addRow("Date",&date);layout.addRow("Other",&other);ClearOnEntryFilter filter(&host);filter.attach(&date,"Date",true);
        host.show();host.activateWindow();QApplication::processEvents();
        QTest::mouseClick(date.findChild<QLineEdit*>(),Qt::LeftButton);auto* line=date.entryEditor();
        QTest::keyClicks(line,"31.02.26");QCOMPARE(filter.firstIncomplete(),&date);
        other.setFocus();QApplication::processEvents();QCOMPARE(line->text(),QString("31.02.26"));QCOMPARE(filter.firstIncomplete(),&date);
        QTest::mouseClick(line,Qt::LeftButton);QCOMPARE(line->text(),QString());
        QTest::keyClicks(line,"29.10.2026");QCOMPARE(date.date(),QDate(2026,10,29));QVERIFY(!filter.firstIncomplete());
        line->selectAll();QTest::keyClicks(line,"29.10.202");QCOMPARE(filter.firstIncomplete(),&date);
        // A calendar click on the existing date does not emit dateChanged.
        QVERIFY(QMetaObject::invokeMethod(date.calendarWidget(),"clicked",Q_ARG(QDate,QDate(2026,10,29))));
        QVERIFY(!filter.firstIncomplete());QVERIFY(!line->isVisible());QCOMPARE(date.date(),QDate(2026,10,29));
        other.setFocus();QTest::mouseClick(date.findChild<QLineEdit*>(),Qt::LeftButton);QVERIFY(filter.empty(&date));
        date.setDate(QDate(2026,10,29));QVERIFY(!filter.empty(&date));QVERIFY(!line->isVisible());
    }
    void keyboardFocusAndOtherFormsPreserveValues() {
        QWidget host;QFormLayout layout(&host);QLineEdit first("Keep"),other("Also keep"),outside("Outside");layout.addRow("First",&first);layout.addRow("Other",&other);layout.addRow("Outside",&outside);
        ClearOnEntryFilter filter(&host);filter.attach(&first,"First");filter.attach(&other,"Other");host.show();host.activateWindow();QApplication::processEvents();
        first.setFocus();QTest::keyClick(&first,Qt::Key_Tab);QCOMPARE(other.text(),QString("Also keep"));QTest::mouseClick(&outside,Qt::LeftButton);QCOMPARE(outside.text(),QString("Outside"));
    }
};
QTEST_MAIN(ClearOnEntryTests)
#include "ClearOnEntryTests.moc"
