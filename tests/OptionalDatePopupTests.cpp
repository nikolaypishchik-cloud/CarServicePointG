#include <QtTest>
#include "ui/common/OptionalDatePopup.h"

class OptionalDatePopupTests : public QObject {
    Q_OBJECT
private slots:
    void emptyCalendarShowsTodayWithoutSettingTheField() {
        QDateEdit editor;
        editor.setMinimumDate(QDate(1900,1,1));
        editor.setSpecialValueText("Not set");
        editor.setDate(editor.minimumDate());
        new OptionalDatePopup(&editor);
        QSignalSpy changed(&editor,&QDateEdit::dateChanged);
        auto* calendar=editor.calendarWidget();
        editor.resize(240,40);editor.show();
        QTest::mouseClick(&editor,Qt::LeftButton,Qt::NoModifier,QPoint(editor.width()-8,editor.height()/2));
        QTRY_VERIFY(calendar->isVisible());
        QTRY_COMPARE(calendar->selectedDate(),QDate::currentDate());
        QCOMPARE(calendar->monthShown(),QDate::currentDate().month());
        QCOMPARE(calendar->yearShown(),QDate::currentDate().year());
        QCOMPARE(editor.date(),editor.minimumDate());
        QCOMPARE(changed.count(),0);
        QTest::keyClick(calendar,Qt::Key_Escape);
        QCOMPARE(editor.date(),editor.minimumDate());
        const auto saved=QDate(2025,3,12);
        editor.setDate(saved);
        QTest::mouseClick(&editor,Qt::LeftButton,Qt::NoModifier,QPoint(editor.width()-8,editor.height()/2));
        QCoreApplication::processEvents();
        QCOMPARE(calendar->selectedDate(),saved);
        QCOMPARE(editor.date(),saved);
    }
};
QTEST_MAIN(OptionalDatePopupTests)
#include "OptionalDatePopupTests.moc"
