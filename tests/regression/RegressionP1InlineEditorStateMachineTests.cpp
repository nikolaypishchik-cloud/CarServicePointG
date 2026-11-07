#include <QtTest/QtTest>
#include "../TestSourceAssertions.h"

class RegressionP1InlineEditorStateMachineTests final : public QObject {
    Q_OBJECT
private slots:
    void canonicalStateMachineIsPresent();
    void editorNavigationUsesStateMachine();
    void tableNavigationUsesStateMachine();
    void batchEnterFilterDoesNotBypassStateMachine();
};

void RegressionP1InlineEditorStateMachineTests::canonicalStateMachineIsPresent()
{
    const QString controller = cs::tests::inlineEditorImplementation();
    QVERIFY2(controller.contains(QStringLiteral("ExcelNavigationStateMachineRequest")) &&
             controller.contains(QStringLiteral("runExcelNavigationStateMachine")),
             "Enter/Tab/Arrow navigation must have one canonical state-machine entry point.");
    QVERIFY2(controller.contains(QStringLiteral("CommitCurrentCell")) &&
             controller.contains(QStringLiteral("ValidateCurrentCell")) &&
             controller.contains(QStringLiteral("SaveToModel")) &&
             controller.contains(QStringLiteral("ActivateNextEditor")),
             "The navigation state machine must explicitly model commit -> validate -> save/model -> move -> activate.");
}

void RegressionP1InlineEditorStateMachineTests::editorNavigationUsesStateMachine()
{
    const QString controller = cs::tests::inlineEditorImplementation();
    const int lambdaPos = controller.indexOf(QStringLiteral("auto commitCloseAndNavigate"));
    QVERIFY2(lambdaPos >= 0, "Editor commit/navigation handler must exist.");
    const int stateMachinePos = controller.indexOf(QStringLiteral("runExcelNavigationStateMachine(request)"), lambdaPos);
    QVERIFY2(stateMachinePos > lambdaPos, "Editor Enter/Tab/Arrow path must delegate to the canonical state machine.");
    const int oldManualMovePos = controller.indexOf(QStringLiteral("nav_commit_move"), lambdaPos);
    QVERIFY2(oldManualMovePos < 0 || oldManualMovePos > stateMachinePos,
             "Editor navigation must not keep the old manual commit/move path.");
}

void RegressionP1InlineEditorStateMachineTests::tableNavigationUsesStateMachine()
{
    const QString controller = cs::tests::inlineEditorImplementation();
    const int navigatorPos = controller.indexOf(QStringLiteral("class TableKeyboardNavigator"));
    QVERIFY2(navigatorPos >= 0, "Table keyboard navigator must exist.");
    const int requestPos = controller.indexOf(QStringLiteral("ExcelNavigationStateMachineRequest request"), navigatorPos);
    const int runPos = controller.indexOf(QStringLiteral("runExcelNavigationStateMachine(request)"), navigatorPos);
    QVERIFY2(requestPos > navigatorPos && runPos > requestPos,
             "Table Enter/Tab/Arrow path must create a state-machine request and run it.");
}

void RegressionP1InlineEditorStateMachineTests::batchEnterFilterDoesNotBypassStateMachine()
{
    const QString controller = cs::tests::inlineEditorImplementation();
    const int enterFilterPos = controller.indexOf(QStringLiteral("class EnterFilter final"));
    QVERIFY2(enterFilterPos >= 0, "Batch table EnterFilter must exist.");
    const int runPos = controller.indexOf(QStringLiteral("runExcelNavigationStateMachine(request)"), enterFilterPos);
    QVERIFY2(runPos > enterFilterPos,
             "Batch dialog Enter handling must not bypass the canonical Excel navigation state machine.");
}

QTEST_MAIN(RegressionP1InlineEditorStateMachineTests)
#include "RegressionP1InlineEditorStateMachineTests.moc"
