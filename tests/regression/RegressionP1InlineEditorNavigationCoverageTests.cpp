#include <QtTest/QtTest>
#include "../TestSourceAssertions.h"

class RegressionP1InlineEditorNavigationCoverageTests final : public QObject {
    Q_OBJECT
private slots:
    void boundaryNavigationIsExplicitlyGuarded();
    void rapidKeysAndMouseEntryPointsUseStateMachineOrGuardedActivation();
    void importedEditPathsUsePersistentIdentity();
};

void RegressionP1InlineEditorNavigationCoverageTests::boundaryNavigationIsExplicitlyGuarded()
{
    const QString controller = cs::tests::inlineEditorImplementation();
    QVERIFY2(controller.contains(QStringLiteral("if (targetRow < 0) targetRow = 0")) &&
             controller.contains(QStringLiteral("if (targetRow >= table->rowCount()) targetRow = table->rowCount() - 1")),
             "P1 p7: first/last row navigation must be clamped.");
    QVERIFY2(controller.contains(QStringLiteral("logicalIndex == columns.size() - 1")) &&
             controller.contains(QStringLiteral("logicalIndex = 0")) &&
             controller.contains(QStringLiteral("logicalIndex = columns.size() - 1")),
             "P1 p7: first/last column wrapping must be explicit.");
}

void RegressionP1InlineEditorNavigationCoverageTests::rapidKeysAndMouseEntryPointsUseStateMachineOrGuardedActivation()
{
    const QString controller = cs::tests::inlineEditorImplementation();
    const QString uiBuilder = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsDialogUiBuilder.cpp");
    QVERIFY2(controller.contains(QStringLiteral("Qt::Key_Tab")) &&
             controller.contains(QStringLiteral("Qt::Key_Backtab")) &&
             controller.contains(QStringLiteral("Qt::Key_Left")) &&
             controller.contains(QStringLiteral("Qt::Key_Right")) &&
             controller.contains(QStringLiteral("Qt::Key_Up")) &&
             controller.contains(QStringLiteral("Qt::Key_Down")),
             "P1 p7: rapid Enter/Tab/Arrow coverage must remain in the controller.");
    QVERIFY2(uiBuilder.contains(QStringLiteral("Qt::RightButton")) &&
             uiBuilder.contains(QStringLiteral("setCurrentCell(index.row(), index.column()")),
             "P1 p7: right-click must select the cell before menu/editor actions.");
    QVERIFY2(uiBuilder.contains(QStringLiteral("cellDoubleClicked")) &&
             uiBuilder.contains(QStringLiteral("scheduleDelayedActivateEditor")),
             "P1 p7: double-click edit must go through guarded delayed activation.");
}

void RegressionP1InlineEditorNavigationCoverageTests::importedEditPathsUsePersistentIdentity()
{
    const QString controller = cs::tests::inlineEditorImplementation();
    const QString uiBuilder = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsDialogUiBuilder.cpp");
    QVERIFY2(controller.contains(QStringLiteral("queuedEditorActionStillValid")) &&
             controller.contains(QStringLiteral("targetIndex")) &&
             controller.contains(QStringLiteral("csTableRevision")),
             "P1 p7: edit after import/refresh must cancel stale queued editor actions.");
    QVERIFY2(uiBuilder.contains(QStringLiteral("inlineEditingPersistentIndex(m_partsTable)")),
             "P1 p7: edit-after-import flushing must use persistent editor identity, not raw row/col.");
    QVERIFY2(!uiBuilder.contains(QStringLiteral("inlineEditingRow")) &&
             !uiBuilder.contains(QStringLiteral("inlineEditingCol")),
             "P1 p7: raw inlineEditingRow/inlineEditingCol must not remain in dialog click paths.");
}

QTEST_MAIN(RegressionP1InlineEditorNavigationCoverageTests)
#include "RegressionP1InlineEditorNavigationCoverageTests.moc"
