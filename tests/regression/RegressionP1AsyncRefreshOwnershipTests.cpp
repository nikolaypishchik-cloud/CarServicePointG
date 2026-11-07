#include <QtTest/QtTest>
#include "../TestSourceAssertions.h"

class RegressionP1AsyncRefreshOwnershipTests final : public QObject {
    Q_OBJECT
private slots:
    void partsPageAsyncRefreshDoesNotUseRawResultPointer();
    void partsPageAsyncRefreshUsesRaiiResultAndQueuedApplyGuard();
};

void RegressionP1AsyncRefreshOwnershipTests::partsPageAsyncRefreshDoesNotUseRawResultPointer()
{
    const QString source = cs::tests::readSourceFile("src/ui/tabs/parts/PartsPageRuntime.cpp");

    QVERIFY2(!source.contains(QStringLiteral("auto* result = new AsyncResult")),
             "P1 async p1 forbids raw pointer result ownership in PartsPage async refresh.");
    QVERIFY2(!source.contains(QStringLiteral("delete result")),
             "P1 async p1/p2 requires RAII result ownership instead of manual delete in async refresh.");
}

void RegressionP1AsyncRefreshOwnershipTests::partsPageAsyncRefreshUsesRaiiResultAndQueuedApplyGuard()
{
    const QString source = cs::tests::readSourceFile("src/ui/tabs/parts/PartsPageRuntime.cpp");
    const QString header = cs::tests::readSourceFile("src/ui/tabs/parts/PartsPage.h");

    QVERIFY2(source.contains(QStringLiteral("std::make_shared<AsyncResult>")),
             "P1 async p2 requires shared/value RAII ownership for async refresh results.");
    QVERIFY2(source.contains(QStringLiteral("AsyncThreadScope::startWorker"))
                 && source.contains(QStringLiteral("const std::shared_ptr<std::atomic_bool>& cancelToken")),
             "P1 async refresh worker must receive the cancellation token through the canonical async lifecycle helper.");
    QVERIFY2(source.contains(QStringLiteral("asyncHandle.shouldApply"))
                 && source.contains(QStringLiteral("m_refreshGeneration")),
             "P1 async refresh result must be applied only through the owner/generation guard.");
    QVERIFY2(header.contains(QStringLiteral("std::shared_ptr<std::atomic_bool> m_refreshCancelToken")),
             "PartsPage must keep RAII cancellation ownership for refresh lifecycle.");
}

QTEST_MAIN(RegressionP1AsyncRefreshOwnershipTests)
#include "RegressionP1AsyncRefreshOwnershipTests.moc"
