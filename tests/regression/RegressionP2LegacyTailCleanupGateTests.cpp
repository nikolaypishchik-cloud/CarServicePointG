#include <QtTest/QtTest>
#include <QString>
#include "../TestSourceAssertions.h"

class RegressionP2LegacyTailCleanupGateTests final : public QObject {
    Q_OBJECT

private slots:
    void cleanupAdrDocumentsDeletionPolicyAndCallGraphEvidence()
    {
        const QString adr2 = cs::tests::readSourceFile("docs/architecture/ADR-0002-legacy-fallback-inventory.md");
        const QString adr3 = cs::tests::readSourceFile("docs/architecture/ADR-0003-legacy-tail-cleanup-gate.md");

        QVERIFY(adr2.contains(QStringLiteral("P2 p3–p4 follow-up status")));
        QVERIFY(adr3.contains(QStringLiteral("P2 legacy-tail cleanup gate")));
        QVERIFY(adr3.contains(QStringLiteral("compile-check policy")));
        QVERIFY(adr3.contains(QStringLiteral("grep/call-graph verification")));
        QVERIFY(adr3.contains(QStringLiteral("No runtime deletion was performed")));
        QVERIFY(adr3.contains(QStringLiteral("Static reference probe: PASS")));
    }

    void synchronousFallbacksAreExplicitlyQuarantined()
    {
        const QString cleanupScript = cs::tests::readSourceFile("scripts/check_legacy_tail_cleanup.py");
        QVERIFY(cleanupScript.contains(QStringLiteral("CS_INTERNAL_BLOCKING_SYNC_ALLOWED")));
        QVERIFY(cleanupScript.contains(QStringLiteral("CS_INTERNAL_SYNC_FALLBACK_INTERNAL_ONLY")));
        QVERIFY(cleanupScript.contains(QStringLiteral("waitForFinished")));

        const QString settings = cs::tests::readSourceFile("src/core/services/settings/SyncSettingsService.cpp");
        QVERIFY(settings.contains(QStringLiteral("CS_INTERNAL_BLOCKING_SYNC_ALLOWED")));
        const auto noBlockingProbe = [](const QString& s) {
            return s.contains("external process probe disabled; use async sync backend")
                && !s.contains("QProcess") && !s.contains("waitForFinished(")
                && !s.contains("waitForStarted(");
        };
        QVERIFY(noBlockingProbe(settings));
        QVERIFY(!noBlockingProbe(settings + "QProcess process;"));
        QVERIFY(!noBlockingProbe(settings + "process.waitForFinished(-1);"));

        const QString legacyResolver = cs::tests::readSourceFile("src/core/services/legacyimport/LegacyImportSourceResolver.cpp");
        const auto safeArchiveResolver = [](const QString& s) {
            return s.contains("synchronous archive extraction disabled")
                && !s.contains("QProcess") && !s.contains("waitForFinished(");
        };
        QVERIFY(safeArchiveResolver(legacyResolver));
        QVERIFY(!safeArchiveResolver(legacyResolver + "QProcess p; p.waitForFinished(-1);"));

        const QString invoiceImport = cs::tests::readSourceFile("src/core/import/InvoiceImportEngine.h");
        QVERIFY(invoiceImport.contains(QStringLiteral("CS_INTERNAL_SYNC_FALLBACK_INTERNAL_ONLY")));
        QVERIFY(invoiceImport.contains(QStringLiteral("UI code must use")));
    }

    void cleanupGateIsRegisteredInCMake()
    {
        const QString rootCmake = cs::tests::readSourceFile("CMakeLists.txt");
        QVERIFY(rootCmake.contains(QStringLiteral("CS_LEGACY_TAIL_CLEANUP_SCRIPT")));
        QVERIFY(rootCmake.contains(QStringLiteral("legacy_tail_cleanup_gate")));
        QVERIFY(rootCmake.contains(QStringLiteral("add_dependencies(release_archive legacy_tail_cleanup_gate)")));

        const QString cmakeTests = cs::tests::readSourceFile("cmake/modules/CarServiceTests.cmake");
        QVERIFY(cmakeTests.contains(QStringLiteral("RegressionP2LegacyTailCleanupGateTests")));
    }
};

QTEST_MAIN(RegressionP2LegacyTailCleanupGateTests)
#include "RegressionP2LegacyTailCleanupGateTests.moc"
