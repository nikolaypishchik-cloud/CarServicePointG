#include <QtTest/QtTest>
#include "../TestSourceAssertions.h"

class RegressionP1InlineEditorControllerSplitTests final : public QObject {
    Q_OBJECT
private slots:
    void inlineEditorLivesInDedicatedController();
    void workPartsDialogIncludesControllerNotLegacyInc();
    void cmakeTracksControllerHeader();
    void rejectsDisconnectedOrCommentedInventory();
};

void RegressionP1InlineEditorControllerSplitTests::inlineEditorLivesInDedicatedController()
{
    const QString controller = cs::tests::inlineEditorImplementation();
    QVERIFY2(controller.contains(QStringLiteral("WordStyleInlineEditor")),
             "Inline editor widget must live in the compiled controller implementation.");
    QVERIFY2(controller.contains(QStringLiteral("TableKeyboardNavigator")) &&
             controller.contains(QStringLiteral("NAV_STATE")) &&
             controller.contains(QStringLiteral("flushInlineEditorValueToModel")),
             "Excel-like navigation/commit path must remain in the dedicated controller.");
    QVERIFY2(!QFileInfo::exists(cs::tests::projectRoot() + QStringLiteral("/src/ui/tabs/orders/WorkPartsTableEditorSupport.inc")),
             "Legacy WorkPartsTableEditorSupport.inc must not keep the inline editor implementation.");
}

void RegressionP1InlineEditorControllerSplitTests::workPartsDialogIncludesControllerNotLegacyInc()
{
    const QString dialog = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsDialog.cpp");
    QVERIFY2(dialog.contains(QStringLiteral("WorkPartsInlineEditorController.h")),
             "WorkPartsDialog.cpp must include the dedicated inline editor controller.");
    QVERIFY2(!dialog.contains(QStringLiteral("WorkPartsTableEditorSupport.inc")),
             "WorkPartsDialog.cpp must not include the legacy table editor .inc.");
}

void RegressionP1InlineEditorControllerSplitTests::cmakeTracksControllerHeader()
{
    const QString cmake = cs::tests::compiledSourceInventory();
    QVERIFY2((cs::tests::cmakeListsSource(cmake, QStringLiteral("src/ui/tabs/orders/WorkPartsInlineEditorController.h"))
                 && cs::tests::cmakeListsSource(cmake, QStringLiteral("src/ui/tabs/orders/WorkPartsInlineEditorControllerImpl.cpp"))),
             "CMake header list must track the dedicated inline editor controller header.");
    const QString tests = cs::tests::readSourceFile("cmake/modules/CarServiceTests.cmake");
    QVERIFY(cs::tests::cmakeIncludesModule(cs::tests::readSourceFile("CMakeLists.txt"), "cmake/modules/CarServiceTests.cmake"));
    QVERIFY2(tests.contains(QStringLiteral("RegressionP1InlineEditorControllerSplitTests")),
             "Regression guard for P1 inline editor split must be wired.");
}

void RegressionP1InlineEditorControllerSplitTests::rejectsDisconnectedOrCommentedInventory()
{
    using namespace cs::tests;
    const QString module = "cmake/modules/CarServiceSourcesUi.cmake";
    const QString source = "src/ui/tabs/orders/WorkPartsInlineEditorControllerImpl.cpp";
    QVERIFY(cmakeIncludesModule("include(" + module + ")", module));
    QVERIFY(!cmakeIncludesModule("# include(" + module + ")", module));
    QVERIFY(!cmakeIncludesModule("include(disconnected.cmake)", module));
    QVERIFY(cmakeListsSource("  " + source + "\n", source));
    QVERIFY(!cmakeListsSource("# " + source, source));
    QVERIFY(!cmakeListsSource(source + ".disabled", source));
    const QString builder = readSourceFile("src/ui/tabs/orders/WorkPartsDialogUiBuilder.cpp");
    QVERIFY(builder.contains("createWorkPartsItemDelegate(m_partsTable)"));
    QVERIFY(builder.contains("createWorkPartsKeyboardNavigator(m_partsTable)"));
}

QTEST_MAIN(RegressionP1InlineEditorControllerSplitTests)
#include "RegressionP1InlineEditorControllerSplitTests.moc"
