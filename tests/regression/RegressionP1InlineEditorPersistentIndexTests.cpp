#include <QtTest/QtTest>
#include "../TestSourceAssertions.h"

class RegressionP1InlineEditorPersistentIndexTests final : public QObject {
    Q_OBJECT
private slots:
    void inlineEditorStateUsesPersistentModelIndex();
    void queuedActivationCapturesPersistentIndex();
    void seedAndEditorWidgetIdentityDoNotUseRawRowColumnProperties();
};

void RegressionP1InlineEditorPersistentIndexTests::inlineEditorStateUsesPersistentModelIndex()
{
    const QString controller = cs::tests::inlineEditorImplementation();
    QVERIFY2(controller.contains(QStringLiteral("QPersistentModelIndex")),
             "Inline editor identity must be based on QPersistentModelIndex.");
    QVERIFY2(controller.contains(QStringLiteral("inlineEditingIndex")) &&
             controller.contains(QStringLiteral("setInlineEditingIndex")) &&
             controller.contains(QStringLiteral("inlineEditingPersistentIndex")),
             "Active editor state must store a persistent model index, not row/column integers.");
    QVERIFY2(!controller.contains(QStringLiteral("inlineEditingRow")) &&
             !controller.contains(QStringLiteral("inlineEditingCol")),
             "Raw inlineEditingRow/inlineEditingCol dynamic properties must not be used for active editor identity.");
}

void RegressionP1InlineEditorPersistentIndexTests::queuedActivationCapturesPersistentIndex()
{
    const QString controller = cs::tests::inlineEditorImplementation();
    QVERIFY2(controller.contains(QStringLiteral("activateEditorAtPersistentIndex")),
             "Deferred editor activation must resolve through a persistent index.");
    QVERIFY2(controller.contains(QStringLiteral("targetIndex")) &&
             controller.contains(QStringLiteral("persistentIndexBelongsToTable")),
             "Queued editor actions must validate the persistent index before reopening an editor.");
}

void RegressionP1InlineEditorPersistentIndexTests::seedAndEditorWidgetIdentityDoNotUseRawRowColumnProperties()
{
    const QString controller = cs::tests::inlineEditorImplementation();
    QVERIFY2(controller.contains(QStringLiteral("editorSeedIndex")) &&
             controller.contains(QStringLiteral("takeEditorSeedForIndex")),
             "Typed keyboard seed state must use the target index instead of editorSeedRow/editorSeedCol.");
    QVERIFY2(controller.contains(QStringLiteral("csEditorIndex")) &&
             controller.contains(QStringLiteral("editorWidgetPersistentIndex")),
             "Commit/navigation must recover the edited cell from editor widget persistent index.");
    QVERIFY2(!controller.contains(QStringLiteral("editorSeedRow")) &&
             !controller.contains(QStringLiteral("editorSeedCol")),
             "Raw editorSeedRow/editorSeedCol dynamic properties must not be used.");
}

QTEST_MAIN(RegressionP1InlineEditorPersistentIndexTests)
#include "RegressionP1InlineEditorPersistentIndexTests.moc"
