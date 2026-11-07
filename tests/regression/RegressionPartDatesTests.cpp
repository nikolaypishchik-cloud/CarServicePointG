#include <QtTest/QtTest>
#include "../TestSourceAssertions.h"

class RegressionPartDatesTests final : public QObject {
    Q_OBJECT
private slots:
    void criticalRegressionGuardsRemainWired();
    void disconnectedPolicyIsRejected();
};

static bool datePolicyIsConnected(const QString& implementation, const QString& prelude,
                                  const QString& policy)
{
    return cs::tests::sourceIncludesDirective(implementation,
               QStringLiteral("ui/tabs/orders/WorkPartsDialogModulePrelude.h"))
        && cs::tests::sourceIncludesDirective(prelude,
               QStringLiteral("ui/tabs/orders/WorkPartsPartEditServices.h"))
        && policy.contains(QStringLiteral("class PartDatePolicyService final"))
        && policy.contains(QStringLiteral("PartDatePolicyService::resolveForSave(table,"))
        && policy.contains(QStringLiteral("state.storedOrderedAt"));
}

void RegressionPartDatesTests::criticalRegressionGuardsRemainWired()
{
    QVERIFY(datePolicyIsConnected(
        cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsPartEditServices.cpp"),
        cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsDialogModulePrelude.h"),
        cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsPartEditServices.h")));
    cs::tests::requireAnyContains("src/ui/tabs/orders/WorkPartsPartEditServices.h", {"ordered_at", "delivery_date", "invoice_date"});
}

void RegressionPartDatesTests::disconnectedPolicyIsRejected()
{
    const auto implementation = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsPartEditServices.cpp");
    const auto prelude = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsDialogModulePrelude.h");
    auto policy = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsPartEditServices.h");
    QVERIFY(!datePolicyIsConnected(QString(), prelude, policy));
    QVERIFY(!datePolicyIsConnected(implementation, QString(), policy));
    policy.replace(QStringLiteral("PartDatePolicyService::resolveForSave(table,"), QStringLiteral("bypassDatePolicy(table,"));
    QVERIFY(!datePolicyIsConnected(implementation, prelude, policy));
}

QTEST_MAIN(RegressionPartDatesTests)
#include "RegressionPartDatesTests.moc"
