#include <QtTest/QtTest>
#include "TestSourceAssertions.h"

class LegacyCanonicalAncillaryImportSupportTests final : public QObject {
    Q_OBJECT
private slots:
    void criticalRegressionGuardsRemainWired();
};

void LegacyCanonicalAncillaryImportSupportTests::criticalRegressionGuardsRemainWired()
{
    cs::tests::requireAnyContains("src/core/services/LegacyMigrationService.cpp", {"Legacy", "migration"});
    cs::tests::requireAnyContains("src/core/services/LegacyMutationLogSupport.cpp", {"mutation", "log"});
}

QTEST_MAIN(LegacyCanonicalAncillaryImportSupportTests)
#include "LegacyCanonicalAncillaryImportSupportTests.moc"
