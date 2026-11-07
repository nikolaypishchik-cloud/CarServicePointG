#include <QtTest/QtTest>
#include <QString>
#include "../TestSourceAssertions.h"

class RegressionP2LegacyFallbackInventoryTests final : public QObject {
    Q_OBJECT

private slots:
    void inventoryAdrExistsAndClassifiesControlledMarkers()
    {
        const QString text = cs::tests::readSourceFile("docs/architecture/ADR-0002-legacy-fallback-inventory.md");
        QVERIFY(text.contains(QStringLiteral("legacy")));
        QVERIFY(text.contains(QStringLiteral("fallback")));
        QVERIFY(text.contains(QStringLiteral("MissingMethods")));
        QVERIFY(text.contains(QStringLiteral("transitional")));
        QVERIFY(text.contains(QStringLiteral("allowlist")));
        QVERIFY(text.contains(QStringLiteral("active")));
        QVERIFY(text.contains(QStringLiteral("deprecated but used")));
        QVERIFY(text.contains(QStringLiteral("test-only")));
        QVERIFY(text.contains(QStringLiteral("delete candidate")));
        QVERIFY(text.contains(QStringLiteral("No runtime code is deleted in this pass")));
    }

    void inventoryGateIsPartOfBuildGraph()
    {
        const QString rootCmake = cs::tests::readSourceFile("CMakeLists.txt");
        QVERIFY(rootCmake.contains(QStringLiteral("CS_LEGACY_TAIL_INVENTORY_SCRIPT")));
        QVERIFY(rootCmake.contains(QStringLiteral("legacy_tail_inventory_gate")));
        QVERIFY(rootCmake.contains(QStringLiteral("add_dependencies(release_archive legacy_tail_inventory_gate)")));

        const QString cmakeTests = cs::tests::readSourceFile("cmake/modules/CarServiceTests.cmake");
        QVERIFY(cmakeTests.contains(QStringLiteral("RegressionP2LegacyFallbackInventoryTests")));
    }
};

QTEST_MAIN(RegressionP2LegacyFallbackInventoryTests)
#include "RegressionP2LegacyFallbackInventoryTests.moc"
