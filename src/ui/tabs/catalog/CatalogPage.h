#pragma once

#include "core/services/catalog/CatalogTypes.h"

#include <QWidget>
#include <QVector>
#include <QTimer>

class QComboBox;
class QLineEdit;
class QSpinBox;
class QTableWidget;
class QPushButton;
class QLabel;

class CatalogPage : public QWidget
{
    Q_OBJECT
public:
    explicit CatalogPage(QWidget* parent = nullptr);
    void refresh();

signals:
    void requestRefreshAll();

private:
    using RowData = CatalogRowData;


    void buildUi();
    void invalidateDataCache();
    void ensureRowDerived(RowData& row) const;
    void loadAllRows(QString* errorText = nullptr);
    void loadFilterValues();
    void loadRows();
    void restoreHeaderState();
    void saveHeaderState() const;
    void fillForm(const RowData& row);
    void clearForm();
    RowData formData() const;
    int currentRowId() const;
    bool editRowData(RowData& row, bool isNew);

private slots:
    void onTableSelectionChanged();
    void onAddClicked();
    void onSaveClicked();
    void onDeleteClicked();
    void onImportFromOrdersClicked();
    void onFilterChanged();
    void onSearchFilterChanged();
    void onToggleFiltersClicked();
    void setPageFrozen(bool frozen);
    void processDeferredRefresh();

private:
    QComboBox* m_brandFilter = nullptr;
    QComboBox* m_modelFilter = nullptr;
    QComboBox* m_fuelFilter = nullptr;
    QComboBox* m_groupFilter = nullptr;
    QComboBox* m_yearFromFilter = nullptr;
    QComboBox* m_yearToFilter = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QTimer* m_searchDebounce = nullptr;
    QTimer* m_refreshDebounce = nullptr;
    QTableWidget* m_table = nullptr;

    QLineEdit* m_brandEdit = nullptr;
    QLineEdit* m_modelEdit = nullptr;
    QSpinBox* m_yearFromEdit = nullptr;
    QSpinBox* m_yearToEdit = nullptr;
    QLineEdit* m_fuelEdit = nullptr;
    QLineEdit* m_groupEdit = nullptr;
    QLineEdit* m_oemEdit = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QLineEdit* m_brandNameEdit = nullptr;
    QLineEdit* m_supplierEdit = nullptr;
    QLineEdit* m_priceEdit = nullptr;
    QLineEdit* m_noteEdit = nullptr;

    QPushButton* m_filtersToggleBtn = nullptr;
    QPushButton* m_addBtn = nullptr;
    QPushButton* m_saveBtn = nullptr;
    QPushButton* m_deleteBtn = nullptr;
    QPushButton* m_importBtn = nullptr;
    QWidget* m_filtersPanel = nullptr;
    QLabel* m_hintLabel = nullptr;

    QVector<RowData> m_rows;
    QVector<RowData> m_allRows;
    bool m_restoringHeaderState = false;
    bool m_loaded = false;
    bool m_schemaReady = false;
    bool m_allRowsLoaded = false;
    bool m_pageFrozen = false;
    bool m_pendingRefreshAll = false;
    bool m_pendingRowsRefresh = false;
};
