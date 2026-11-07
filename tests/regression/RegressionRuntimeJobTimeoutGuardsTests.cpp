#include <QtTest/QtTest>

#include "TestSourceAssertions.h"

class RegressionRuntimeJobTimeoutGuardsTests : public QObject
{
    Q_OBJECT
private slots:
    void longRefreshFamiliesHaveTimeoutCancellationGuards()
    {
        const QString current = cs::tests::readSourceFile("src/ui/tabs/orders/CurrentOrdersPageActions.cpp");
        const QString history = cs::tests::readSourceFile("src/ui/tabs/history/HistoryPage.cpp");
        const QString parts = cs::tests::readSourceFile("src/ui/tabs/parts/PartsPageRuntime.cpp");

        for (const QString& source : {current, history, parts}) {
            QVERIFY2(source.contains(QStringLiteral("runtimeTicket.timeoutMs > 0")),
                     "Every long refresh worker must arm a timeout guard from RuntimeJobDispatcher::Ticket::timeoutMs.");
            QVERIFY2(source.contains(QStringLiteral("QTimer::singleShot(qMax(1000, runtimeTicket.timeoutMs)")),
                     "Timeout guards must run asynchronously and must not block the UI thread.");
            QVERIFY2(source.contains(QStringLiteral("workerGuard->requestInterruption()")),
                     "Timeout guards must interrupt the worker thread.");
            QVERIFY2(source.contains(QStringLiteral("RuntimeJob::RuntimeJobDispatcher::instance().cancelFamily(runtimeTicket.family)")),
                     "Timeout guards must cancel the active runtime family so stale results cannot apply.");
        }
    }

    void dispatcherRejectsApplyDuringShutdownEvenBeforeTokenObserved()
    {
        const QString dispatcher = cs::tests::readSourceFile("src/runtime/RuntimeJobDispatcher.cpp");
        QVERIFY2(dispatcher.contains(QStringLiteral("return !m_shutdownRequested")),
                 "RuntimeJobDispatcher::shouldApply() must reject UI application while global shutdown is active.");
    }
};

QTEST_MAIN(RegressionRuntimeJobTimeoutGuardsTests)
#include "RegressionRuntimeJobTimeoutGuardsTests.moc"
