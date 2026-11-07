#include <QtTest/QtTest>
#include "../TestSourceAssertions.h"

class SmokeSettingsPageTests final : public QObject {
    Q_OBJECT
private slots:
    void criticalRegressionGuardsRemainWired();
};

void SmokeSettingsPageTests::criticalRegressionGuardsRemainWired()
{
    cs::tests::requireContains("src/ui/tabs/settings/SettingsPage.cpp", "SettingsPage");
    cs::tests::requireAnyContains("src/ui/tabs/settings/SettingsPageDataSupport.cpp", {"directory", "backup", "settings"});
}

QTEST_MAIN(SmokeSettingsPageTests)
#include "SmokeSettingsPageTests.moc"
