#include <QtTest/QtTest>
#include "../TestSourceAssertions.h"

class RegressionP0ImportViewModelBoundaryTests final : public QObject {
    Q_OBJECT
private slots:
    void importedRowsHaveSingleViewModelBoundary();
    void coordinatorUsesOnlyTheViewModelApplyPath();
};

void RegressionP0ImportViewModelBoundaryTests::importedRowsHaveSingleViewModelBoundary()
{
    const QString header = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsImportedRowsViewModel.h");
    const QString source = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsImportedRowsViewModel.cpp");
    const QString rootCmake = cs::tests::readSourceFile("CMakeLists.txt");
    QVERIFY(rootCmake.contains(QStringLiteral("include(cmake/modules/CarServiceSourcesUi.cmake)")));
    const QString cmake = cs::tests::readSourceFile("cmake/modules/CarServiceSourcesUi.cmake");

    QVERIFY2(header.contains(QStringLiteral("struct ImportedPartRow")) &&
             header.contains(QStringLiteral("struct ImportBatch")) &&
             header.contains(QStringLiteral("buildBatch")),
             "P0 import p7 requires a dedicated ViewModel/API batch contract for imported invoice rows.");
    QVERIFY2(source.contains(QStringLiteral("PartImportDraftUiSupport::buildUiRow")) &&
             source.contains(QStringLiteral("traceId")) &&
             source.contains(QStringLiteral("attachmentSource")) &&
             source.contains(QStringLiteral("documentNumber")),
             "The ViewModel must normalize draft rows, trace ids and attachment metadata before UI table application.");
    QVERIFY2(cmake.contains(QStringLiteral("src/ui/tabs/orders/WorkPartsImportedRowsViewModel.cpp")) &&
             cmake.contains(QStringLiteral("src/ui/tabs/orders/WorkPartsImportedRowsViewModel.h")),
             "The import ViewModel must be compiled into the application target.");
}

void RegressionP0ImportViewModelBoundaryTests::coordinatorUsesOnlyTheViewModelApplyPath()
{
    const QString coordinator = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsImportCoordinator.cpp");

    QVERIFY2(coordinator.contains(QStringLiteral("WorkPartsImportedRowsViewModel::buildBatch")),
             "WorkParts import must build a ViewModel batch before touching the parts table.");
    QVERIFY2(coordinator.contains(QStringLiteral("applyImportedRowsThroughViewModel")) &&
             coordinator.contains(QStringLiteral("transfer_to_order_viewmodel")),
             "Imported rows must be applied through one named, logged ViewModel/API path.");
    QVERIFY2(!coordinator.contains(QStringLiteral("PartImportDraftUiSupport::buildUiRow(draft)")),
             "The coordinator must not rebuild and write imported draft rows ad-hoc inside the import loop.");
}

QTEST_MAIN(RegressionP0ImportViewModelBoundaryTests)
#include "RegressionP0ImportViewModelBoundaryTests.moc"
