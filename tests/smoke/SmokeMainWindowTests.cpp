#include <QtTest/QtTest>
#include "../TestSourceAssertions.h"

class SmokeMainWindowTests final : public QObject {
    Q_OBJECT
private slots:
    void criticalRegressionGuardsRemainWired();
};

void SmokeMainWindowTests::criticalRegressionGuardsRemainWired()
{
    cs::tests::requireAnyContains("src/app/StartupPipeline.cpp", {"Startup", "Pipeline"});
    cs::tests::requireAnyContains("src/ui/main/MainWindowRuntime.cpp", {"m_pageOrders", "m_pageParts"});
}

QTEST_MAIN(SmokeMainWindowTests)
#include "SmokeMainWindowTests.moc"
