#include <QtTest/QtTest>

#include "runtime/RuntimeJobDispatcher.h"

class RegressionRuntimeJobDispatcherShutdownTests : public QObject
{
    Q_OBJECT
private slots:
    void sameFamilyNewJobCancelsPrevious()
    {
        auto& dispatcher = RuntimeJob::RuntimeJobDispatcher::instance();
        dispatcher.leaveShutdown();
        const auto first = dispatcher.startLatest(RuntimeJob::Family::Reports,
                                                 QStringLiteral("test:first"),
                                                 1000,
                                                 1);
        QVERIFY(first.isValid());
        QVERIFY(!first.isCancelled());

        const auto second = dispatcher.startLatest(RuntimeJob::Family::Reports,
                                                  QStringLiteral("test:second"),
                                                  1000,
                                                  2);
        QVERIFY(second.isValid());
        QVERIFY(first.isCancelled());
        QVERIFY(!second.isCancelled());
        QVERIFY(!dispatcher.shouldApply(first));
        QVERIFY(dispatcher.shouldApply(second));
        dispatcher.finish(second, RuntimeJob::Status::Succeeded);
    }

    void shutdownCancelsAllFamiliesAndRejectsApply()
    {
        auto& dispatcher = RuntimeJob::RuntimeJobDispatcher::instance();
        dispatcher.leaveShutdown();
        const auto importTicket = dispatcher.startLatest(RuntimeJob::Family::Import,
                                                        QStringLiteral("test:import"),
                                                        5000,
                                                        5);
        const auto backupTicket = dispatcher.startLatest(RuntimeJob::Family::Backup,
                                                        QStringLiteral("test:backup"),
                                                        5000,
                                                        5);
        QVERIFY(dispatcher.shouldApply(importTicket));
        QVERIFY(dispatcher.shouldApply(backupTicket));

        dispatcher.enterShutdown(QStringLiteral("RegressionRuntimeJobDispatcherShutdownTests"));
        QVERIFY(dispatcher.isShutdownRequested());
        QVERIFY(importTicket.isCancelled());
        QVERIFY(backupTicket.isCancelled());
        QVERIFY(!dispatcher.shouldApply(importTicket));
        QVERIFY(!dispatcher.shouldApply(backupTicket));

        const auto lateTicket = dispatcher.startLatest(RuntimeJob::Family::Update,
                                                      QStringLiteral("test:late_update"),
                                                      5000,
                                                      1);
        QVERIFY(lateTicket.isValid());
        QVERIFY(lateTicket.isCancelled());
        QVERIFY(!dispatcher.shouldApply(lateTicket));
        dispatcher.leaveShutdown();
        QVERIFY(!dispatcher.isShutdownRequested());
    }
};

QTEST_MAIN(RegressionRuntimeJobDispatcherShutdownTests)
#include "RegressionRuntimeJobDispatcherShutdownTests.moc"
