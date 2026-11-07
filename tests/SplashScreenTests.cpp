#include <QtTest/QtTest>
#include <QApplication>
#include <QSignalSpy>
#include <QThread>
#include <QDir>
#include "ui/splash/SplashScreen.h"

class SplashScreenTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }
    void blockedStartupDoesNotConsumeAnimation() {
        SplashScreen splash;
        QSignalSpy finished(&splash, &SplashScreen::finished);
        splash.start(6000);
        splash.setStartupReady();
        QTest::qWait(50);
        QThread::msleep(6500); // Model synchronous DB/UI startup on the GUI thread.
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
        QCOMPARE(finished.count(), 0);
        QVERIFY(splash.isVisible());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        QVERIFY(!splash.isVisible());
    }
    void startupReadinessPreventsEarlyFinish() {
        SplashScreen splash;
        QSignalSpy finished(&splash, &SplashScreen::finished);
        splash.start(1000);
        QTest::qWait(3500);
        QCOMPARE(finished.count(), 0);
        QVERIFY(splash.isVisible());
        splash.setStartupReady();
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 400);
        QVERIFY(!splash.isVisible());
        QTest::qWait(100);
        QCOMPARE(finished.count(), 1);
    }
    void visibleFramesAndCompleteAnimation() {
        SplashScreen splash;
        QSignalSpy finished(&splash, &SplashScreen::finished);
        splash.start(6000);
        splash.repaint();
        const auto initial=splash.grab().toImage();
        int visible=0;
        for(int y=0;y<initial.height();++y)for(int x=0;x<initial.width();++x)
            if(qAlpha(initial.pixel(x,y))>0)++visible;
        QVERIFY2(visible>100,"First frame must not be entirely transparent");
        const auto output=qEnvironmentVariable("CS_SPLASH_OUTPUT");
        if(!output.isEmpty()) { QDir().mkpath(output); QVERIFY(initial.save(output+"/01-initial.png")); }
        QTest::qWait(1500);
        const auto typing=splash.grab().toImage();
        QVERIFY(typing!=initial);
        if(!output.isEmpty())QVERIFY(typing.save(output+"/02-typing.png"));
        QTest::qWait(2000);
        const auto title=splash.grab().toImage();
        QVERIFY(title!=typing);
        if(!output.isEmpty())QVERIFY(title.save(output+"/03-title.png"));
        QVERIFY(splash.isVisible());
        splash.setStartupReady();
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(),1,400);
        QVERIFY(!splash.isVisible());
    }
};
QTEST_MAIN(SplashScreenTests)
#include "SplashScreenTests.moc"
