#include <QtTest/QtTest>
#include "../TestSourceAssertions.h"

class RegressionP1InlineEditorCanonicalCommitTests final : public QObject {
    Q_OBJECT
private slots:
    void editingFinishedDoesNotCommitDirectly();
    void allEditorCommitCloseGoesThroughCanonicalHelper();
    void commitWritesOnlyEditRole();
};

void RegressionP1InlineEditorCanonicalCommitTests::editingFinishedDoesNotCommitDirectly()
{
    const QString controller = cs::tests::inlineEditorImplementation();
    QVERIFY2(!controller.contains(QStringLiteral("editingFinished")),
             "P1 p6: editingFinished must not own a separate commit/close path.");
    QVERIFY2(controller.contains(QStringLiteral("commitInlineEditorCanonical")),
             "P1 p6: one canonical inline-editor commit helper must own commitData/closeEditor.");
}

void RegressionP1InlineEditorCanonicalCommitTests::allEditorCommitCloseGoesThroughCanonicalHelper()
{
    const QString controller = cs::tests::inlineEditorImplementation();
    const int helperPos = controller.indexOf(QStringLiteral("commitInlineEditorCanonical"));
    QVERIFY2(helperPos >= 0, "Canonical commit helper missing.");
    const int helperCommitData = controller.indexOf(QStringLiteral("emit commitData"), helperPos);
    const int helperCloseEditor = controller.indexOf(QStringLiteral("emit closeEditor"), helperPos);
    QVERIFY2(helperCommitData > helperPos && helperCloseEditor > helperCommitData,
             "Canonical helper must perform commitData and closeEditor in order.");

    int pos = -1;
    int commitCount = 0;
    while ((pos = controller.indexOf(QStringLiteral("emit commitData"), pos + 1)) >= 0) ++commitCount;
    QCOMPARE(commitCount, 1);

    pos = -1;
    int closeCount = 0;
    while ((pos = controller.indexOf(QStringLiteral("emit closeEditor"), pos + 1)) >= 0) ++closeCount;
    QCOMPARE(closeCount, 1);
}

void RegressionP1InlineEditorCanonicalCommitTests::commitWritesOnlyEditRole()
{
    const QString controller = cs::tests::inlineEditorImplementation();
    QVERIFY2(controller.contains(QStringLiteral("model->setData(index, value, Qt::EditRole)")),
             "Canonical commit must still write the edited value to the model.");
    QVERIFY2(!controller.contains(QStringLiteral("model->setData(index, value, Qt::DisplayRole)")),
             "P1/P0 guard: commit must not double-write DisplayRole after EditRole.");
    QVERIFY2(!controller.contains(QStringLiteral("model->setData(idx, cols.at(c), Qt::DisplayRole)")),
             "Clipboard paste must not double-write DisplayRole after EditRole.");
}

QTEST_MAIN(RegressionP1InlineEditorCanonicalCommitTests)
#include "RegressionP1InlineEditorCanonicalCommitTests.moc"
