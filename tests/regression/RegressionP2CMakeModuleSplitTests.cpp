\
    #include <QtTest/QtTest>
    #include <QFile>
    #include <QFileInfo>
    #include <QStringList>
    #include "../TestSourceAssertions.h"

    class RegressionP2CMakeModuleSplitTests : public QObject {
        Q_OBJECT
    private slots:
        void rootCMakeIncludesLayerModules();
        void sourceListsAreOwnedByLayerModules();
        void testsAreRegisteredFromDedicatedModule();
    };

    static QString readProjectText(const QString& rel)
    {
        return cs::tests::readSourceFile(rel.toUtf8().constData());
    }

    void RegressionP2CMakeModuleSplitTests::rootCMakeIncludesLayerModules()
    {
        const QString cmake = readProjectText(QStringLiteral("CMakeLists.txt"));
        const QStringList modules = {
            QStringLiteral("CarServiceSourcesCore.cmake"),
            QStringLiteral("CarServiceSourcesInfrastructure.cmake"),
            QStringLiteral("CarServiceSourcesIntegrations.cmake"),
            QStringLiteral("CarServiceSourcesUi.cmake"),
            QStringLiteral("CarServiceTests.cmake"),
        };
        for (const QString& module : modules) {
            QVERIFY2(cmake.contains(QStringLiteral("include(cmake/modules/%1)").arg(module)),
                     qPrintable(QStringLiteral("Root CMakeLists.txt does not include %1").arg(module)));
            QVERIFY2(QFileInfo::exists(cs::tests::projectRoot() + QStringLiteral("/cmake/modules/") + module),
                     qPrintable(QStringLiteral("Missing CMake module %1").arg(module)));
        }
    }

    void RegressionP2CMakeModuleSplitTests::sourceListsAreOwnedByLayerModules()
    {
        const QString cmake = readProjectText(QStringLiteral("CMakeLists.txt"));
        QVERIFY2(cmake.contains(QStringLiteral("${CS_CORE_SOURCES}")), "Root CMake must consume core layer list");
        QVERIFY2(cmake.contains(QStringLiteral("${CS_INFRASTRUCTURE_SOURCES}")), "Root CMake must consume infrastructure layer list");
        QVERIFY2(cmake.contains(QStringLiteral("${CS_INTEGRATIONS_SOURCES}")), "Root CMake must consume integrations layer list");
        QVERIFY2(cmake.contains(QStringLiteral("${CS_UI_SOURCES}")), "Root CMake must consume UI layer list");
        QVERIFY2(!cmake.contains(QStringLiteral("src/ui/tabs/orders/WorkPartsDialog.cpp")),
                 "Root CMake must not re-own raw UI source entries after the P2 module split");

        const QString core = readProjectText(QStringLiteral("cmake/modules/CarServiceSourcesCore.cmake"));
        const QString infra = readProjectText(QStringLiteral("cmake/modules/CarServiceSourcesInfrastructure.cmake"));
        const QString integrations = readProjectText(QStringLiteral("cmake/modules/CarServiceSourcesIntegrations.cmake"));
        const QString ui = readProjectText(QStringLiteral("cmake/modules/CarServiceSourcesUi.cmake"));
        QVERIFY2(core.contains(QStringLiteral("src/core/services/orders/WorkPartsPersistenceRepositoryFacade.cpp")),
                 "Core module must own the WorkParts repository facade application-service boundary");
        QVERIFY2(infra.contains(QStringLiteral("src/infrastructure/db/sqlite/Db.cpp")),
                 "Infrastructure module must own sqlite implementation files");
        QVERIFY2(core.contains(QStringLiteral("src/app/update/UpdateService.cpp")),
                 "Application source module must own the interactive update coordinator");
        QVERIFY2(ui.contains(QStringLiteral("src/ui/tabs/orders/WorkPartsDialog.cpp")),
                 "UI module must own UI implementation files");
    }

    void RegressionP2CMakeModuleSplitTests::testsAreRegisteredFromDedicatedModule()
    {
        const QString tests = readProjectText(QStringLiteral("cmake/modules/CarServiceTests.cmake"));
        QVERIFY2(tests.contains(QStringLiteral("cs_add_internal_test")), "Test module must own internal test registration");
        QVERIFY2(tests.contains(QStringLiteral("RegressionP2ArchitectureLayerRulesTests")),
                 "P2 architecture layer regression test must be registered");
        QVERIFY2(tests.contains(QStringLiteral("RegressionP2CMakeModuleSplitTests")),
                 "P2 CMake split regression test must be registered");
    }

    QTEST_MAIN(RegressionP2CMakeModuleSplitTests)
    #include "RegressionP2CMakeModuleSplitTests.moc"
