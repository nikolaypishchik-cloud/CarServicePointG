#include <QtTest/QtTest>
#include <QFile>
#include <QString>
#include "../TestSourceAssertions.h"

class RegressionPerformanceTzPass13ProductionLogClosureTests : public QObject
{
    Q_OBJECT
private slots:
    void hotPathProductionInfoLogsRequireDeepDebug();
    void pass13GateCoversProductionLogClosure();
};

static QString readFile(const QString& path)
{
    return cs::tests::readSourceFile(path.toUtf8().constData());
}

static void requireDeepDebugGuard(const QString& path, const QString& marker)
{
    const QString text = readFile(path);
    const int markerIndex = text.indexOf(marker);
    QVERIFY2(markerIndex >= 0, qPrintable(path + QStringLiteral(" missing marker ") + marker));
    const int guardIndex = text.lastIndexOf(QStringLiteral("RuntimeLoggingControl::deepDebugEnabled()"), markerIndex);
    QVERIFY2(guardIndex >= 0, qPrintable(path + QStringLiteral(" missing deep-debug guard before ") + marker));
    QVERIFY2(markerIndex - guardIndex < 512, qPrintable(path + QStringLiteral(" guard too far from ") + marker));
}

void RegressionPerformanceTzPass13ProductionLogClosureTests::hotPathProductionInfoLogsRequireDeepDebug()
{
    requireDeepDebugGuard(QStringLiteral("src/core/services/ActiveArchiveRoutingLayer.cpp"), QStringLiteral("PERF ORDER_ROUTE"));
    requireDeepDebugGuard(QStringLiteral("src/core/services/MutationExecutor.cpp"), QStringLiteral("DB_MUTATION executor_started"));
    requireDeepDebugGuard(QStringLiteral("src/core/services/MutationPostCommitService.cpp"), QStringLiteral("DB_MUTATION post_commit_cache_invalidated"));
    requireDeepDebugGuard(QStringLiteral("src/core/services/PartsQueryService.cpp"), QStringLiteral("PARTS_SORT_REFRESH"));
    requireDeepDebugGuard(QStringLiteral("src/core/sync/SyncPayloadUtils.cpp"), QStringLiteral("PART_PRICING_SYNC_IMPORT"));
    requireDeepDebugGuard(QStringLiteral("src/ui/common/dialogs/OrderDialogLifecycleFlow.cpp"), QStringLiteral("OrderDialog loadOrder elapsed_ms"));
    requireDeepDebugGuard(QStringLiteral("src/ui/main/MainWindowRuntime.cpp"), QStringLiteral("PAGE_PREWARM"));
    requireDeepDebugGuard(QStringLiteral("src/ui/runtime/PageActivationCoordinator.cpp"), QStringLiteral("activation_requested"));
    requireDeepDebugGuard(QStringLiteral("src/core/services/ReportsPdfRenderer.cpp"), QStringLiteral("Report PDF export started"));
    requireDeepDebugGuard(QStringLiteral("src/ui/common/dialogs/OrderDialogPdfFlow.cpp"), QStringLiteral("Order PDF export started"));
}

void RegressionPerformanceTzPass13ProductionLogClosureTests::pass13GateCoversProductionLogClosure()
{
    const QString gate = readFile(QStringLiteral("scripts/check_performance_tz_100_completion.py"));
    QVERIFY(gate.contains(QStringLiteral("RegressionPerformanceTzPass13ProductionLogClosureTests")));
    QVERIFY(gate.contains(QStringLiteral("pass13 production-log hot-path closure")));
}

QTEST_MAIN(RegressionPerformanceTzPass13ProductionLogClosureTests)
#include "RegressionPerformanceTzPass13ProductionLogClosureTests.moc"
