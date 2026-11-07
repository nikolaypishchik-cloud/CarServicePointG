#include <QtTest/QtTest>
#include <QString>
#include "../TestSourceAssertions.h"

class RegressionP2LegacyTailCiGateTests final : public QObject {
    Q_OBJECT

private slots:
    void transitionalAllowlistHasShrunkBudget()
    {
        const QString allowlist = cs::tests::readSourceFile("scripts/p3_transitional_allowlist.txt");
        QVERIFY(allowlist.contains(QStringLiteral("P2 p5")));
        QVERIFY(allowlist.contains(QStringLiteral("WorkPartsMissingMethods.cpp")));
        QVERIFY(!allowlist.contains(QStringLiteral("OrderDialogFlowSupport.cpp")));
        QVERIFY(!allowlist.contains(QStringLiteral("OrderDialogFlowSupport.h")));
    }

    void ciGateBlocksUndocumentedControlledMarkers()
    {
        const QString gate = cs::tests::readSourceFile("scripts/check_legacy_marker_ci_gate.py");
        QVERIFY(gate.contains(QStringLiteral("MAX_TRANSITIONAL_ALLOWLIST_ENTRIES = 1")));
        QVERIFY(gate.contains(QStringLiteral("new controlled marker without ADR-0002 explanation")));
        QVERIFY(gate.contains(QStringLiteral("controlled marker budget exceeded")));
        QVERIFY(gate.contains(QStringLiteral("legacy")));
        QVERIFY(gate.contains(QStringLiteral("fallback")));
        QVERIFY(gate.contains(QStringLiteral("MissingMethods")));
    }

    void adrAndWorkflowDocumentP5P6Controls()
    {
        const QString adr = cs::tests::readSourceFile("docs/architecture/ADR-0004-transitional-allowlist-ci-gate.md");
        const QString workflow = cs::tests::readSourceFile(".github/workflows/structure-contract.yml");
        const QString runner = cs::tests::readSourceFile("scripts/run_change_gates.py");
        QVERIFY(adr.contains(QStringLiteral("3 entries to 1 entry")));
        QVERIFY(adr.contains(QStringLiteral("check_legacy_marker_ci_gate.py")));
        const auto wired = [](const QString& w, const QString& r) {
            return QRegularExpression("(?m)^\\s+run: python -B scripts/run_change_gates.py --static-only\\s*$").match(w).hasMatch()
                && r.section("REQUIRED_TESTS", 0, 0).contains("'check_legacy_marker_ci_gate.py'")
                && r.contains("for name in STATIC:") && r.contains("if result.returncode:") && r.contains("failures.append(name)");
        };
        QVERIFY(wired(workflow, runner));
        QString disabled = workflow;
        disabled.replace("run: python", "# run: python");
        QVERIFY(!wired(disabled, runner));
        QString missingGate = runner;
        missingGate.replace("'check_legacy_marker_ci_gate.py'", "'removed.py'");
        QVERIFY(!wired(workflow, missingGate));
    }
};

QTEST_MAIN(RegressionP2LegacyTailCiGateTests)
#include "RegressionP2LegacyTailCiGateTests.moc"
