#include <QtTest/QtTest>
#include "../TestSourceAssertions.h"

class RegressionP1InlineEditorRevisionGuardTests final : public QObject {
    Q_OBJECT
private slots:
    void tableRevisionTrackerIsPresent();
    void queuedEditorActivationCapturesRevision();
    void delayedRetryActionsCancelOnRevisionChange();
};

void RegressionP1InlineEditorRevisionGuardTests::tableRevisionTrackerIsPresent()
{
    const QString controller = cs::tests::inlineEditorImplementation();
    QVERIFY2(controller.contains(QStringLiteral("kTableRevisionProperty")) &&
             controller.contains(QStringLiteral("csTableRevision")) &&
             controller.contains(QStringLiteral("ensureTableRevisionTracker")),
             "Inline editor controller must maintain a typed table/model revision for queued editor actions.");
    QVERIFY2(controller.contains(QStringLiteral("rowsInserted")) &&
             controller.contains(QStringLiteral("rowsRemoved")) &&
             controller.contains(QStringLiteral("modelReset")) &&
             controller.contains(QStringLiteral("layoutChanged")) &&
             controller.contains(QStringLiteral("dataChanged")),
             "The revision tracker must observe model/table changes that invalidate queued row/column actions.");
}

static bool activationChecksCapturedRevision(const QString& controller)
{
    const QString signature = QStringLiteral("void scheduleDelayedActivateEditor(QTableWidget* table, int row, int col, const QPoint& clickPosInViewport)\n{");
    const int start = controller.indexOf(signature);
    const int end = controller.indexOf(QStringLiteral("void scheduleEnsureNextInputRow("), start);
    if (start < 0 || end <= start) return false;
    const auto body = controller.mid(start, end-start);
    const int capture = body.indexOf(QStringLiteral("tableRevisionSnapshot(table)"));
    const int timer = body.indexOf(QStringLiteral("QTimer::singleShot"));
    const int guard = body.indexOf(QStringLiteral("if (!queuedEditorActionStillValid(safeTable.data(), targetIndex, queuedRevision,"));
    const int activate = body.indexOf(QStringLiteral("activateEditorAtPersistentIndex("));
    return capture >= 0 && timer > capture && guard > timer && activate > guard;
}

void RegressionP1InlineEditorRevisionGuardTests::queuedEditorActivationCapturesRevision()
{
    QString controller = cs::tests::inlineEditorImplementation();
    controller.replace("\r\n", "\n");
    QVERIFY2(activationChecksCapturedRevision(controller), "Capture and validate revision in the actual scheduled function before activation.");
    auto broken = controller;
    broken.replace("if (!queuedEditorActionStillValid(safeTable.data(), targetIndex, queuedRevision,", "if (!unrelatedGuard(");
    QVERIFY(!activationChecksCapturedRevision(broken));
    broken=controller;broken.replace("tableRevisionSnapshot(table)","0");
    QVERIFY(!activationChecksCapturedRevision(broken));
}

void RegressionP1InlineEditorRevisionGuardTests::delayedRetryActionsCancelOnRevisionChange()
{
    const QString controller = cs::tests::inlineEditorImplementation();
    QVERIFY2(controller.contains(QStringLiteral("activate_stale_editor_retry")) &&
             controller.contains(QStringLiteral("flush_failed_activation_retry")) &&
             controller.contains(QStringLiteral("retry_after_no_visible_editor")),
             "Activation retry timers must be named and guarded.");
    QVERIFY2(controller.contains(QStringLiteral("tableRevisionStillCurrent")) &&
             controller.contains(QStringLiteral("ensure_next_input_row")) &&
             controller.contains(QStringLiteral("queued inline editor action cancelled because table revision changed")),
             "Queued editor/input-row actions must cancel if the model revision changed.");
}

QTEST_MAIN(RegressionP1InlineEditorRevisionGuardTests)
#include "RegressionP1InlineEditorRevisionGuardTests.moc"
