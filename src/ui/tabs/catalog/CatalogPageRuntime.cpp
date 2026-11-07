// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

#include "ui/tabs/catalog/CatalogPageModulePrelude.h"

void CatalogPage::onToggleFiltersClicked()
{
    if (!m_filtersPanel) return;
    const bool showFilters = !m_filtersPanel->isVisible();
    m_filtersPanel->setVisible(showFilters);
    if (showFilters) {
        m_filtersPanel->updateGeometry();
        updateGeometry();
        if (m_searchEdit) m_searchEdit->setFocus();
    }
}

void CatalogPage::loadAllRows(QString* errorText)
{
    if (m_allRowsLoaded) return;
    m_allRows = CatalogQueryService::loadAllRows(errorText);
    m_schemaReady = errorText == nullptr || errorText->trimmed().isEmpty();
    m_allRowsLoaded = m_schemaReady;
}

void CatalogPage::restoreHeaderState()
{
    if (!m_table || !m_table->horizontalHeader()) return;
    const QString raw = UiStateSettingsService::loadString(QStringLiteral("ui.catalog.header_state"));
    if (raw.trimmed().isEmpty()) return;
    m_restoringHeaderState = true;
    m_table->horizontalHeader()->restoreState(QByteArray::fromBase64(raw.toUtf8()));
    m_restoringHeaderState = false;
}

void CatalogPage::saveHeaderState() const
{
    if (!m_table || !m_table->horizontalHeader()) return;
    UiStateSettingsService::saveString(QStringLiteral("ui.catalog.header_state"), QString::fromLatin1(m_table->horizontalHeader()->saveState().toBase64()));
}

void CatalogPage::loadFilterValues()
{
    QString dataError;
    if (!m_allRowsLoaded) loadAllRows(&dataError);
    if (!dataError.trimmed().isEmpty()) return;

    CatalogFilterState state;
    state.brand = m_brandFilter ? m_brandFilter->currentText().trimmed() : QString();
    state.model = m_modelFilter ? m_modelFilter->currentText().trimmed() : QString();
    state.fuel = m_fuelFilter ? m_fuelFilter->currentText().trimmed() : QString();
    state.group = m_groupFilter ? m_groupFilter->currentText().trimmed() : QString();
    state.search = m_searchEdit ? m_searchEdit->text().trimmed() : QString();
    state.yearFrom = comboYearValue(m_yearFromFilter);
    state.yearTo = comboYearValue(m_yearToFilter);

    const auto options = CatalogQueryService::buildFilterOptions(m_allRows, state);
    const QString allText = QString::fromUtf8("Усі");

    auto findComboIndexByCatalogKey = [](QComboBox* cb, const QString& value, bool brandValue) {
        if (!cb) return -1;
        const QString wantedKey = brandValue
            ? CatalogQueryService::normalizedBrandFilterValue(value)
            : CatalogQueryService::foldedFilterValue(value);
        if (wantedKey.isEmpty()) return -1;
        for (int i = 0; i < cb->count(); ++i) {
            const QString itemKey = brandValue
                ? CatalogQueryService::normalizedBrandFilterValue(cb->itemText(i))
                : CatalogQueryService::foldedFilterValue(cb->itemText(i));
            if (itemKey == wantedKey) return i;
        }
        return -1;
    };

    QSignalBlocker b1(m_brandFilter), b2(m_modelFilter), b3(m_fuelFilter), b4(m_groupFilter), b5(m_yearFromFilter), b6(m_yearToFilter);
    m_brandFilter->clear();
    m_modelFilter->clear();
    m_fuelFilter->clear();
    m_groupFilter->clear();
    m_yearFromFilter->clear();
    m_yearToFilter->clear();

    m_brandFilter->addItem(allText);
    m_modelFilter->addItem(allText);
    m_fuelFilter->addItem(allText);
    m_groupFilter->addItem(allText);
    m_yearFromFilter->addItem(QString::fromUtf8("Будь-який"), 0);
    m_yearToFilter->addItem(QString::fromUtf8("Будь-який"), 0);

    for (const QString& value : options.brands) m_brandFilter->addItem(value);
    for (const QString& value : options.models) m_modelFilter->addItem(value);
    for (const QString& value : options.fuels) m_fuelFilter->addItem(value);
    for (const QString& value : options.groups) m_groupFilter->addItem(value);
    for (int year : options.yearsFrom) m_yearFromFilter->addItem(QString::number(year), year);
    for (int year : options.yearsTo) m_yearToFilter->addItem(QString::number(year), year);

    auto restoreText = [&](QComboBox* cb, const QString& value, bool brandValue) {
        if (!cb) return;
        const int idx = findComboIndexByCatalogKey(cb, value, brandValue);
        cb->setCurrentIndex(idx >= 0 ? idx : 0);
    };
    restoreText(m_brandFilter, state.brand, true);
    restoreText(m_modelFilter, state.model, false);
    restoreText(m_fuelFilter, state.fuel, false);
    restoreText(m_groupFilter, state.group, false);
    setComboYearValue(m_yearFromFilter, state.yearFrom);
    setComboYearValue(m_yearToFilter, state.yearTo);
}

void CatalogPage::loadRows()
{
    QString dataError;
    if (!m_allRowsLoaded) loadAllRows(&dataError);
    if (!dataError.trimmed().isEmpty()) {
        QMessageBox::warning(this, QString::fromUtf8("Каталог"), dataError);
        return;
    }

    const int selectedIdBefore = currentRowId();
    const int vScroll = m_table && m_table->verticalScrollBar() ? m_table->verticalScrollBar()->value() : 0;
    const int hScroll = m_table && m_table->horizontalScrollBar() ? m_table->horizontalScrollBar()->value() : 0;
    CatalogFilterState state;
    state.brand = m_brandFilter ? m_brandFilter->currentText().trimmed() : QString();
    state.model = m_modelFilter ? m_modelFilter->currentText().trimmed() : QString();
    state.fuel = m_fuelFilter ? m_fuelFilter->currentText().trimmed() : QString();
    state.group = m_groupFilter ? m_groupFilter->currentText().trimmed() : QString();
    state.search = m_searchEdit ? m_searchEdit->text().trimmed() : QString();
    state.yearFrom = comboYearValue(m_yearFromFilter);
    state.yearTo = comboYearValue(m_yearToFilter);

    m_rows = CatalogQueryService::filterRows(m_allRows, state);
    if (m_table) {
        m_table->setUpdatesEnabled(false);
        m_table->setSortingEnabled(false);
        m_table->blockSignals(true);
        m_table->setRowCount(0);
    }

    m_table->setRowCount(m_rows.size());
    int restoreRow = -1;
    for (int row = 0; row < m_rows.size(); ++row) {
        const auto& r = m_rows[row];
        const QString price = r.price > 0.0 ? QString::number(r.price, 'f', 2) : QString();
        const QStringList cols = { r.brand, r.model, CatalogQueryService::displayYearsRange(r.yearFrom, r.yearTo), r.fuel, r.group, r.oem, r.name, r.brandName, price };
        for (int c = 0; c < cols.size(); ++c) {
            auto* item = new QTableWidgetItem(cols[c]);
            if (c == 8) item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            if (c == 6 || c == 5) item->setToolTip(r.note);
            m_table->setItem(row, c, item);
        }
        m_table->item(row, 0)->setData(Qt::UserRole, r.id);
        if (r.id == selectedIdBefore) restoreRow = row;
    }
    if (restoreRow >= 0) {
        m_table->selectRow(restoreRow);
    } else if (!m_rows.isEmpty()) {
        m_table->selectRow(0);
    } else {
        m_table->clearSelection();
        onTableSelectionChanged();
    }
    if (m_table && m_table->verticalScrollBar()) m_table->verticalScrollBar()->setValue(vScroll);
    if (m_table && m_table->horizontalScrollBar()) m_table->horizontalScrollBar()->setValue(hScroll);
    if (m_table) {
        m_table->blockSignals(false);
        m_table->setUpdatesEnabled(true);
        m_table->viewport()->update();
    }
}

void CatalogPage::fillForm(const RowData& row)
{
    if (m_brandEdit) m_brandEdit->setText(row.brand);
    if (m_modelEdit) m_modelEdit->setText(row.model);
    if (m_yearFromEdit) m_yearFromEdit->setValue(row.yearFrom);
    if (m_yearToEdit) m_yearToEdit->setValue(row.yearTo);
    if (m_fuelEdit) m_fuelEdit->setText(row.fuel);
    if (m_groupEdit) m_groupEdit->setText(row.group);
    if (m_oemEdit) m_oemEdit->setText(row.oem);
    if (m_nameEdit) m_nameEdit->setText(row.name);
    if (m_brandNameEdit) m_brandNameEdit->setText(row.brandName);
    if (m_supplierEdit) m_supplierEdit->setText(row.supplier);
    if (m_priceEdit) m_priceEdit->setText(row.price > 0.0 ? QString::number(row.price, 'f', 2) : QString());
    if (m_noteEdit) m_noteEdit->setText(row.note);
}

void CatalogPage::clearForm()
{
    m_table->clearSelection();
}

CatalogPage::RowData CatalogPage::formData() const
{
    RowData r;
    r.id = currentRowId();
    return r;
}

int CatalogPage::currentRowId() const
{
    const int row = m_table->currentRow();
    if (row < 0) return 0;
    auto* item = m_table->item(row, 0);
    return item ? item->data(Qt::UserRole).toInt() : 0;
}

bool CatalogPage::editRowData(RowData& row, bool isNew)
{
    QDialog dlg(this);
    dlg.setWindowTitle(isNew ? QString::fromUtf8("Нова запчастина") : QString::fromUtf8("Картка запчастини"));
    dlg.setModal(true);
    dlg.resize(760, 520);
    dlg.setAttribute(Qt::WA_StyledBackground, true);
    dlg.setStyleSheet(QString::fromUtf8(
        "QDialog { background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #6d7b8d, stop:1 #435264); color:#f4efe7; }"
        "QLabel { color:#f4efe7; background:transparent; }"
    ));

    auto* root = new QVBoxLayout(&dlg);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto* form = new QFormLayout();
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(10);

    auto* brandEdit = makeCombo(&dlg, true, catalogBrandOptions());
    auto* modelEdit = makeCombo(&dlg, true);
    auto* yearFromEdit = makeYearCombo(&dlg, QString::fromUtf8("Не вказано"));
    auto* yearToEdit = makeYearCombo(&dlg, QString::fromUtf8("Не вказано"));
    auto* fuelEdit = makeCombo(&dlg, true, standardFuelOptions());
    auto* groupEdit = makeCombo(&dlg, true, standardGroupOptions());
    if (brandEdit->lineEdit()) brandEdit->lineEdit()->setPlaceholderText(QString::fromUtf8("Оберіть марку авто"));
    if (modelEdit->lineEdit()) modelEdit->lineEdit()->setPlaceholderText(QString::fromUtf8("Оберіть модель авто"));
    if (fuelEdit->lineEdit()) fuelEdit->lineEdit()->setPlaceholderText(QString::fromUtf8("Оберіть тип палива"));
    if (groupEdit->lineEdit()) groupEdit->lineEdit()->setPlaceholderText(QString::fromUtf8("Оберіть вузол / агрегат"));
    appendUniqueSorted(groupEdit, CatalogQueryService::activeGroupOptions());
    auto* oemEdit = makeEdit(&dlg, QString::fromUtf8("OEM / артикул"));
    auto* nameEdit = makeEdit(&dlg, QString::fromUtf8("Назва запчастини"));
    auto* brandNameEdit = makeEdit(&dlg, QString::fromUtf8("Bosch / INA / Lemforder"));
    brandNameEdit->setProperty("inputCorrectionRole", "protected");
    auto* supplierEdit = makeEdit(&dlg, QString::fromUtf8("Автонова-D / Еліт / Ямато"));
    auto* priceEdit = makeEdit(&dlg, QString::fromUtf8("0.00"));
    auto* noteEdit = makeEdit(&dlg, QString::fromUtf8("Примітка майстра, сумісність, нюанси"));
    noteEdit->setProperty("inputCorrectionRole", "text");

    auto refreshDialogModels = [&](const QString& brandText, const QString& preferredModel = QString()) {
        const QString keep = preferredModel.isEmpty() ? modelEdit->currentText() : preferredModel;
        const QString selectedBrand = brandText.trimmed();
        const QSignalBlocker blocker(modelEdit);
        modelEdit->clear();
        appendUniqueSorted(modelEdit, CatalogQueryService::modelOptionsForBrand(selectedBrand));
        modelEdit->setCurrentText(keep);
    };

    brandEdit->setCurrentText(row.brand);
    refreshDialogModels(row.brand, row.model);
    setComboYearValue(yearFromEdit, row.yearFrom);
    setComboYearValue(yearToEdit, row.yearTo);
    fuelEdit->setCurrentText(row.fuel);
    groupEdit->setCurrentText(row.group);
    oemEdit->setText(row.oem);
    nameEdit->setText(row.name);
    brandNameEdit->setText(row.brandName);
    supplierEdit->setText(row.supplier);
    if (row.price > 0.0) priceEdit->setText(QString::number(row.price, 'f', 2));
    noteEdit->setText(row.note);

    QObject::connect(brandEdit, &QComboBox::currentTextChanged, &dlg, [&](const QString& text) {
        refreshDialogModels(text);
    });

    form->addRow(QString::fromUtf8("Марка авто"), brandEdit);
    form->addRow(QString::fromUtf8("Модель"), modelEdit);
    form->addRow(QString::fromUtf8("Рік від"), yearFromEdit);
    form->addRow(QString::fromUtf8("Рік до"), yearToEdit);
    form->addRow(QString::fromUtf8("Тип палива"), fuelEdit);
    form->addRow(QString::fromUtf8("Вузол / агрегат"), groupEdit);
    form->addRow(QString::fromUtf8("OEM / артикул"), oemEdit);
    form->addRow(QString::fromUtf8("Назва"), nameEdit);
    form->addRow(QString::fromUtf8("Бренд"), brandNameEdit);
    form->addRow(QString::fromUtf8("Постачальник"), supplierEdit);
    form->addRow(QString::fromUtf8("Остання ціна"), priceEdit);
    form->addRow(QString::fromUtf8("Примітка"), noteEdit);
    root->addLayout(form);

    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch(1);
    auto* cancelBtn = new GlossyButton(QString::fromUtf8("Скасувати"), &dlg);
    auto* saveBtn = new GlossyButton(QString::fromUtf8("Зберегти"), &dlg);
    applyCatalogMetalButtonStyle(cancelBtn);
    applyCatalogMetalButtonStyle(saveBtn);
    cancelBtn->setMinimumWidth(150);
    saveBtn->setMinimumWidth(150);
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(saveBtn);
    root->addLayout(btnRow);

    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    QObject::connect(saveBtn, &QPushButton::clicked, &dlg, [&]() {
        const QString name = norm(nameEdit->text());
        const int yearFrom = comboYearValue(yearFromEdit);
        const int yearTo = comboYearValue(yearToEdit);
        if (name.isEmpty()) {
            QMessageBox::warning(&dlg, QString::fromUtf8("Каталог"), QString::fromUtf8("Вкажіть назву запчастини."));
            return;
        }
        if (yearFrom > 0 && yearTo > 0 && yearFrom > yearTo) {
            QMessageBox::warning(&dlg, QString::fromUtf8("Каталог"), QString::fromUtf8("Рік від не може бути більшим за рік до."));
            return;
        }
        row.brand = norm(brandEdit->currentText());
        row.model = norm(modelEdit->currentText());
        row.yearFrom = yearFrom;
        row.yearTo = yearTo;
        row.fuel = norm(fuelEdit->currentText());
        row.group = norm(groupEdit->currentText());
        row.oem = norm(oemEdit->text());
        row.name = name;
        row.brandName = norm(brandNameEdit->text());
        row.supplier = norm(supplierEdit->text());
        row.price = priceEdit->text().trimmed().replace(',', '.').toDouble();
        row.note = norm(noteEdit->text());
        dlg.accept();
    });

    return dlg.exec() == QDialog::Accepted;
}

void CatalogPage::onTableSelectionChanged()
{
    const bool hasSelection = currentRowId() > 0;
    m_saveBtn->setEnabled(hasSelection);
    m_deleteBtn->setEnabled(hasSelection);
}

void CatalogPage::onAddClicked()
{
    RowData r;
    if (!editRowData(r, true)) return;
    if (!CatalogController::saveRow(this, r)) return;
    invalidateDataCache();
    loadFilterValues();
    loadRows();
    m_loaded = true;
    emit requestRefreshAll();
}

void CatalogPage::onSaveClicked()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_rows.size()) return;
    RowData r = m_rows[row];
    if (!editRowData(r, false)) return;
    if (!CatalogController::saveRow(this, r)) return;
    invalidateDataCache();
    loadFilterValues();
    loadRows();
    m_loaded = true;
    emit requestRefreshAll();
}

void CatalogPage::onDeleteClicked()
{
    const int id = currentRowId();
    if (id <= 0) return;
    if (!CatalogController::deleteRowWithConfirmation(this, id)) return;
    invalidateDataCache();
    loadFilterValues();
    loadRows();
    m_loaded = true;
    emit requestRefreshAll();
}

void CatalogPage::onImportFromOrdersClicked()
{
    if (!CatalogController::importFromOrders(this)) return;
    invalidateDataCache();
    loadFilterValues();
    loadRows();
    m_loaded = true;
}

void CatalogPage::onFilterChanged()
{
    if (m_searchDebounce) m_searchDebounce->stop();
    m_pendingRefreshAll = true;
    m_pendingRowsRefresh = false;
    if (m_pageFrozen) {
        m_loaded = false;
        return;
    }
    if (m_refreshDebounce) m_refreshDebounce->start(140);
    else processDeferredRefresh();
}

void CatalogPage::onSearchFilterChanged()
{
    if (m_searchDebounce) m_searchDebounce->stop();
    m_pendingRowsRefresh = true;
    if (m_pageFrozen) {
        m_loaded = false;
        return;
    }
    if (m_refreshDebounce) m_refreshDebounce->start(90);
    else processDeferredRefresh();
}

void CatalogPage::setPageFrozen(bool frozen)
{
    m_pageFrozen = frozen;
    if (!m_pageFrozen && (m_pendingRefreshAll || m_pendingRowsRefresh || !m_loaded)) {
        if (m_refreshDebounce) m_refreshDebounce->start(0);
        else processDeferredRefresh();
    }
}

void CatalogPage::processDeferredRefresh()
{
    if (m_pageFrozen) return;

    const bool refreshAll = m_pendingRefreshAll || !m_loaded;
    const bool refreshRows = refreshAll || m_pendingRowsRefresh || !m_loaded;
    m_pendingRefreshAll = false;
    m_pendingRowsRefresh = false;

    QElapsedTimer timer;
    timer.start();
    if (refreshAll) loadFilterValues();
    if (refreshRows) loadRows();
    m_loaded = true;

    const QString perfLabel = refreshAll
        ? QStringLiteral("PERF Catalog filters(full) elapsed_ms=%1 rows=%2")
        : QStringLiteral("PERF Catalog filters(search) elapsed_ms=%1 rows=%2");
    qInfo().noquote() << perfLabel.arg(timer.elapsed()).arg(m_rows.size());
}

void CatalogPage::refresh()
{
    if (m_loaded && !m_pendingRefreshAll && !m_pendingRowsRefresh) return;
    if (m_pageFrozen) {
        m_pendingRefreshAll = true;
        return;
    }
    if (m_refreshDebounce) m_refreshDebounce->start(0);
    else processDeferredRefresh();
}
