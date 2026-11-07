#pragma once

#include <QWidget>

class QComboBox;
class QDateEdit;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTabWidget;
class QTableView;
class QTimer;
class QDialog;
class TabularSnapshotTableModel;

class WorkshopInventoryPage : public QWidget
{
    Q_OBJECT
public:
    explicit WorkshopInventoryPage(QWidget* parent = nullptr);

    void refresh();
    void markDirty();
    void openPurchase(qint64 purchaseId);
    void focusPurchase(qint64 purchaseId);

signals:
    void requestRefreshAll();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildUi();
    void refreshModels();
    void refreshMaterialChoices();
    void scheduleRefresh(int delayMs = 0);
    qint64 selectedPurchaseId() const;
    void addPurchase();
    void addSpareOrder();
    void manageSpare(int row, bool deleteOnly = false);
    void deleteSelectedPurchase();
    void deletePurchaseById(qint64 purchaseId);
    void showPeriodDetails(int row);
    void showPurchaseDetails(int row);
    void showExpenseDetails(int row);
    void openExpenseOrder(int row);
    void showBalanceDetails(int row);
    bool payMaterial(QWidget* parent, const QString& material, qint64 purchaseId = 0);
    void setSummary(const QString& text, bool error = false);

    QDateEdit* m_from = nullptr;
    QDateEdit* m_to = nullptr;
    QLabel* m_summary = nullptr;
    QLabel* m_materialMetrics[4] = {};
    QLabel* m_spareMetrics[4] = {};
    QTabWidget* m_stockTabs = nullptr;
    QDialog* m_materialDialog = nullptr;
    QTableView* m_spareTable = nullptr;
    TabularSnapshotTableModel* m_spareModel = nullptr;
    QPushButton* m_addSpareButton = nullptr;
    QLineEdit *m_category = nullptr, *m_article = nullptr, *m_batch = nullptr, *m_document = nullptr;
    QDoubleSpinBox* m_minimum = nullptr;
    QDateEdit *m_orderedDate = nullptr, *m_dueDate = nullptr, *m_expiryDate = nullptr;
    QDateEdit* m_purchaseDate = nullptr;
    QComboBox* m_materialName = nullptr;
    QComboBox* m_balanceMaterial = nullptr;
    QComboBox* m_unit = nullptr;
    QDoubleSpinBox* m_quantity = nullptr;
    QDoubleSpinBox* m_unitCost = nullptr;
    QComboBox* m_priceBasis = nullptr;
    QDoubleSpinBox* m_clientPrice = nullptr;
    QLineEdit* m_supplier = nullptr;
    QPushButton* m_addPurchaseButton = nullptr;
    QPushButton* m_deletePurchaseButton = nullptr;
    QTableView* m_periodTable = nullptr;
    QTableView* m_purchasesTable = nullptr;
    QTableView* m_expensesTable = nullptr;
    QTableView* m_balanceTable = nullptr;
    TabularSnapshotTableModel* m_periodModel = nullptr;
    TabularSnapshotTableModel* m_purchasesModel = nullptr;
    TabularSnapshotTableModel* m_expensesModel = nullptr;
    TabularSnapshotTableModel* m_balanceModel = nullptr;
    QTimer* m_refreshDebounce = nullptr;
    bool m_dirty = true;
};
