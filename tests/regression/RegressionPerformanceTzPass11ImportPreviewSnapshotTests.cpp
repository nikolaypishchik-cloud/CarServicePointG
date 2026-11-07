#include "TestSourceAssertions.h"
#include "ui/common/dialogs/InvoiceReviewTableModel.h"

class RegressionPerformanceTzPass11ImportPreviewSnapshotTests final : public QObject
{
    Q_OBJECT
private slots:
    void checkboxesAndSelectedEditsWorkForFixedOrder()
    {
        InvoiceReviewTableModel model;
        model.setRows({"Select", "Date", "Order", "Store"},
                      {{"", "30.10.26", "541", "First"}, {"", "30.10.26", "541", "Second"}}, true);
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        QVERIFY(model.flags(model.index(0,0)) & Qt::ItemIsUserCheckable);
        QVERIFY(model.setData(model.index(0,0), Qt::Unchecked, Qt::CheckStateRole));
        QCOMPARE(model.data(model.index(0,0), Qt::CheckStateRole).toInt(), int(Qt::Unchecked));
        model.setColumnForSelected(3, "Selected supplier");
        QCOMPARE(model.data(model.index(0,3)).toString(), QString("First"));
        QCOMPARE(model.data(model.index(1,3)).toString(), QString("Selected supplier"));
        QVERIFY(!model.setData(model.index(1,2), "999"));
        QCOMPARE(model.data(model.index(1,2)).toString(), QString("541"));
        QVERIFY(changed.count() >= 2);
        model.setAllChecked(false);
        QCOMPARE(model.data(model.index(1,0), Qt::CheckStateRole).toInt(), int(Qt::Unchecked));
        model.setAllChecked(true);
        QCOMPARE(model.data(model.index(0,0), Qt::CheckStateRole).toInt(), int(Qt::Checked));
        model.setRows({"Select", "Date", "Order", "Store"}, {{"", "", "", ""}}, false);
        QCOMPARE(model.rowCount(), 1);
        QVERIFY(model.setData(model.index(0,2), "542"));
        QCOMPARE(model.data(model.index(0,2)).toString(), QString("542"));
    }

    void currentOrdersInvoicePreviewUsesSnapshotModel()
    {
        using namespace cs::tests;
        requireContains("src/ui/tabs/orders/CurrentOrdersPageLocalSupport.cpp", "CurrentOrdersExcelImportDialog final : public InvoiceReviewDialog");
        requireContains("src/ui/common/dialogs/InvoiceReviewDialog.h", "m_table=new QTableView(this)");
        requireContains("src/ui/common/dialogs/InvoiceReviewDialog.h", "m_model->setRows(headers,rows,");
        requireContains("src/ui/tabs/orders/CurrentOrdersPageLocalSupport.cpp", "for(int r:selectedRows())");
        requireContains("src/ui/common/dialogs/InvoiceReviewDialog.h", "m_model->setColumnForSelected(");
        requireNotContains("src/ui/tabs/orders/CurrentOrdersPageLocalSupport.cpp", "m_table = new QTableWidget");
        requireNotContains("src/ui/tabs/orders/CurrentOrdersPageLocalSupport.cpp", "m_table->setItem(");
        requireNotContains("src/ui/tabs/orders/CurrentOrdersPageLocalSupport.cpp", "m_table->setRowCount(");
        requireNotContains("src/ui/tabs/orders/CurrentOrdersPageLocalSupport.cpp", "m_table->item(");
    }

    void workPartsSelectivePreviewUsesSnapshotModel()
    {
        using namespace cs::tests;
        requireContains("src/ui/tabs/orders/WorkPartsImportCoordinator.cpp", "InvoiceReviewDialog dlg(parsedDrafts, *data, parent, QString::number(orderNumber))");
        requireContains("src/ui/common/dialogs/InvoiceReviewDialog.h", "m_table->setModel(m_model)");
        requireContains("src/ui/tabs/orders/WorkPartsImportCoordinator.cpp", "dlg.selectedDrafts()");
        requireContains("src/ui/common/dialogs/InvoiceReviewDialog.h", "m_model->setAllChecked(checked)");
        requireNotContains("src/ui/tabs/orders/WorkPartsImportCoordinator.cpp", "m_table = new QTableWidget");
        requireNotContains("src/ui/tabs/orders/WorkPartsImportCoordinator.cpp", "m_table->item(row, 0)");
        requireNotContains("src/ui/tabs/orders/WorkPartsImportCoordinator.cpp", "m_table->setItem(row, 0");
    }

    void performanceGateCoversPass11PreviewClosure()
    {
        using namespace cs::tests;
        requireContains("scripts/check_performance_tz_100_completion.py", "Pass11: both import paths");
        requireContains("scripts/check_performance_tz_100_completion.py", "CurrentOrdersExcelImportDialog final : public InvoiceReviewDialog");
        requireContains("scripts/check_performance_tz_100_completion.py", "InvoiceReviewDialog dlg(parsedDrafts, *data, parent, QString::number(orderNumber))");
        requireContains("cmake/modules/CarServiceTests.cmake", "RegressionPerformanceTzPass11ImportPreviewSnapshotTests");
    }
};

QTEST_MAIN(RegressionPerformanceTzPass11ImportPreviewSnapshotTests)
#include "RegressionPerformanceTzPass11ImportPreviewSnapshotTests.moc"
