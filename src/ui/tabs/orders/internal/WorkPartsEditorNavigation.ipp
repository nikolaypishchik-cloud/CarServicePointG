// Extracted from src/ui/tabs/orders/WorkPartsInlineEditorController.h for P1/P2 monolith reduction.
// Included exactly once by the original translation unit/header; do not compile separately.

    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QComboBox::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRect dropRect = rect().adjusted(width() - 26, 0, -6, 0);
        const int cy = dropRect.center().y();
        const int right = dropRect.center().x() + 4;
        const int left = right - 8;
        const int mid = left + 4;

        QPainterPath path;
        path.moveTo(left, cy - 2);
        path.lineTo(mid, cy + 2);
        path.lineTo(right, cy - 2);

        painter.setPen(QPen(QColor(QStringLiteral("#111111")), 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
    }
};


QFont interfaceHeaderFont(int pointDelta)
{
    const auto fontSettings = UiSettingsService::loadFontSettings();
    const QString family = fontSettings.fontFamily.trimmed();
    const int headingSize = fontSettings.headingFontSize;
    QFont font = qApp ? qApp->font() : QFont();
    if (!family.isEmpty()) font.setFamily(family);
    const int baseSize = headingSize > 0 ? headingSize : qMax(10, font.pointSize());
    font.setPointSize(qMax(6, baseSize + pointDelta));
    font.setBold(true);
    return font;
}

void fillTableViewportWithRows(QTableWidget* table, bool keepHintRowAtBottom)
{
    if (!table) return;
    // Resizing filler rows is presentation, not a user edit or an order save.
    const QSignalBlocker blocker(table);

    removeViewportFillRows(table);

    const int rowHeight = qMax(1, table->verticalHeader() ? table->verticalHeader()->defaultSectionSize() : 40);
    const int viewportHeight = qMax(0, table->viewport() ? table->viewport()->height() : table->height());
    if (viewportHeight <= 0) return;

    const int currentRows = table->rowCount();
    const int rowsNeeded = qMax(currentRows, (viewportHeight + rowHeight - 1) / rowHeight);
    const int rowsToAdd = rowsNeeded - currentRows;
    if (rowsToAdd <= 0) return;

    int insertRow = currentRows;
    if (keepHintRowAtBottom && currentRows > 0) {
        int hintRow = -1;
        for (int row = 0; row < currentRows; ++row) {
            if (rowHasHintMarker(table, row)) {
                hintRow = row;
                break;
            }
        }
        if (hintRow >= 0) insertRow = hintRow + 1;
    }

    // P1 performance: resize once instead of calling insertRow() repeatedly
    // while adding viewport filler rows. This avoids needless model/layout
    // invalidation during dialog resize and open.
    table->setRowCount(currentRows + rowsToAdd);
    for (int row = currentRows + rowsToAdd - 1; row >= insertRow + rowsToAdd; --row) {
        for (int c = 0; c < table->columnCount(); ++c) {
            auto* existing = table->takeItem(row - rowsToAdd, c);
            table->setItem(row, c, existing ? existing : makeViewportFillCell());
        }
        auto* headerItem = table->takeVerticalHeaderItem(row - rowsToAdd);
        table->setVerticalHeaderItem(row, headerItem ? headerItem : new QTableWidgetItem(QString()));
    }
    for (int row = insertRow; row < insertRow + rowsToAdd; ++row) {
        for (int c = 0; c < table->columnCount(); ++c) table->setItem(row, c, makeViewportFillCell());
        setVerticalHeaderLabel(table, row, QString());
    }
}

void setVerticalHeaderLabel(QTableWidget* table, int row, const QString& label)
{
    if (!table || row < 0 || row >= table->rowCount()) return;
    auto* headerItem = new QTableWidgetItem(label);
    headerItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    table->setVerticalHeaderItem(row, headerItem);
}

void styleDarkFlatTable(QTableWidget* table)
{
    table->setShowGrid(true);
    table->setCornerButtonEnabled(false);
    table->setFrameShape(QFrame::NoFrame);
    table->setAlternatingRowColors(false);
    table->setSelectionBehavior(QAbstractItemView::SelectItems);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setWordWrap(false);
    table->setTextElideMode(Qt::ElideNone);
    table->setStyleSheet(QString::fromUtf8(
        "QTableWidget {"
        "  border: 1px solid rgba(244,248,255,0.98);"
        "  border-radius: 10px;"
        "  background: rgba(255,255,255,0.90);"
        "  color: #111111;"
        "  outline: none;"
        "  gridline-color: rgba(115,126,140,0.22);"
        "}" 
        "QTableWidget::item {"
        "  background: rgba(255,255,255,0.94);"
        "  color: #111111;"
        "  padding: 6px 5px;"
        "  border-right: 1px solid rgba(115,126,140,0.34);"
        "  border-bottom: 1px solid rgba(115,126,140,0.16);"
        "}" 
        "QTableWidget::item:selected {"
        "  background: #eef4ff;"
        "  color: #111111;"
        "}" 
        "QTableWidget QLineEdit, QTableWidget QAbstractSpinBox, QTableWidget QComboBox {"
        "  background: #ffffff;"
        "  color: #111111;"
        "  border: 0px;"
        "  outline: none;"
        "  padding: 0px;"
        "  margin: 0px;"
        "  selection-background-color: #dce9ff;"
        "  min-height: 30px;"
        "}" 
        "QTableWidget QLineEdit:focus, QTableWidget QAbstractSpinBox:focus, QTableWidget QComboBox:focus {"
        "  border: 1px solid rgba(128,145,170,0.75);"
        "  outline: none;"
        "  background: #ffffff;"
        "}" 
        "QHeaderView::section {"
        "  border: none;"
        "  border-right: 1px solid rgba(255,255,255,0.10);"
        "  border-bottom: 1px solid rgba(255,255,255,0.10);"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #515862, stop:1 #3d434b);"
        "  color: #ffffff;"
        "  padding: 5px 5px;"
        "  font-size: 10pt;"
        "  font-weight: 600;"
        "}" 
        "QHeaderView {"
        "  background: rgba(255,255,255,0.94);"
        "}" 
        "QTableCornerButton::section {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #515862, stop:1 #3d434b);"
        "  border: none;"
        "  border-right: 1px solid rgba(255,255,255,0.10);"
        "  border-bottom: 1px solid rgba(255,255,255,0.10);"
        "}" 
        "QHeaderView::section:vertical {"
        "  background: rgba(255,255,255,0.94);"
        "  color: #111111;"
        "  border: none;"
        "  border-right: 1px solid rgba(115,126,140,0.34);"
        "  border-bottom: 1px solid rgba(115,126,140,0.16);"
        "  font-size: 10pt;"
        "  font-weight: 600;"
        "}" 
    ));

    if (auto* corner = table->findChild<QAbstractButton*>()) {
        corner->setStyleSheet(QString::fromUtf8(
            "background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #515862, stop:1 #3d434b);"
            "border: none;"
            "border-right: 1px solid rgba(255,255,255,0.10);"
            "border-bottom: 1px solid rgba(255,255,255,0.10);"
        ));
        corner->setEnabled(false);
        corner->update();
    }
    if (auto* vh = table->verticalHeader()) {
        vh->setStyleSheet(QString::fromUtf8(
            "QHeaderView { background: rgba(255,255,255,0.94); }"
            "QHeaderView::section {"
            "  background: rgba(255,255,255,0.94);"
            "  color: #111111;"
            "  border: none;"
            "  border-right: 1px solid rgba(115,126,140,0.34);"
            "  border-bottom: 1px solid rgba(115,126,140,0.16);"
            "  font-size: 10pt;"
            "  font-weight: 600;"
            "}"
        ));
    }
}


QString currentPrimaryMasterName(QComboBox* combo)
{
    return combo ? combo->currentText().trimmed() : QString();
}

static QList<MasterRow> availableOrderMasters()
{
    QList<MasterRow> out;
    const auto allPersonnel = PersonnelSettingsService::loadPersonnel(false);
    for (const auto& m : allPersonnel) {
        const QString role = m.role.trimmed().toLower();
        const bool isMasterRole = role.isEmpty() || role == QStringLiteral("master");
        if (!m.active || !isMasterRole) continue;
        out.push_back(m);
    }
    return out;
}

static void populateMasterEditor(QComboBox* combo, const QString& currentText)
{
    if (!combo) return;
    const QSignalBlocker blocker(combo);
    combo->clear();
    combo->setInsertPolicy(QComboBox::NoInsert);
    combo->setEditable(false);
    combo->setStyleSheet(whiteMasterComboStyle());
    combo->setMaxVisibleItems(18);

    const auto masters = availableOrderMasters();
    for (const auto& m : masters) {
        combo->addItem(m.name, m.id);
    }

    const QString normalizedCurrentText = currentText.trimmed();
    int idx = normalizedCurrentText.isEmpty() ? -1 : combo->findText(normalizedCurrentText, Qt::MatchFixedString);
    if (idx < 0 && !normalizedCurrentText.isEmpty()) {
        combo->addItem(normalizedCurrentText, QString());
        idx = combo->count() - 1;
    }
    if (idx >= 0) {
        combo->setCurrentIndex(idx);
    } else {
        combo->setCurrentIndex(-1);
    }
}

QString moneyText(double v)
{
    return FormatUtils::money(FormatUtils::roundMoneyValue(v), 0);
}

bool isOilServiceTitle(const QString& serviceTitle)
{
    return OilServiceClassifier::isEngineOilServiceTitle(serviceTitle);
}

bool isOilMaintenancePart(const QString& partName)
{
    return OilServiceClassifier::isEngineOilMaintenancePartName(partName);
}


static QString navigationTableName(QTableWidget* table)
{
    if (!table) return QStringLiteral("<null>");
    if (!table->objectName().trimmed().isEmpty()) return table->objectName();
    if (table->columnCount() == 6) return QStringLiteral("services");
    if (isMainOrderPartsTable(table)) return QStringLiteral("parts");
    return QStringLiteral("table_%1_cols").arg(table->columnCount());
}

static QString navigationKeyName(int key)
{
    switch (key) {
    case Qt::Key_Tab: return QStringLiteral("Tab");
    case Qt::Key_Backtab: return QStringLiteral("Backtab");
    case Qt::Key_Return: return QStringLiteral("Return");
    case Qt::Key_Enter: return QStringLiteral("Enter");
    case Qt::Key_Left: return QStringLiteral("Left");
    case Qt::Key_Right: return QStringLiteral("Right");
    case Qt::Key_Up: return QStringLiteral("Up");
    case Qt::Key_Down: return QStringLiteral("Down");
    default: return QString::number(key);
    }
}

static bool isExcelNavigationKey(int key)
{
    return key == Qt::Key_Tab || key == Qt::Key_Backtab ||
           key == Qt::Key_Return || key == Qt::Key_Enter ||
           key == Qt::Key_Left || key == Qt::Key_Right ||
           key == Qt::Key_Up || key == Qt::Key_Down;
}

static void logExcelNavigation(QTableWidget* table,
                               const QString& stage,
                               int key,
                               int fromRow,
                               int fromCol,
                               int toRow = -1,
                               int toCol = -1,
                               const QString& extra = QString())
{
    qCDebug(csWorkPartsTraceLog).noquote() << stage
                      << QStringLiteral(" table=") << navigationTableName(table)
                      << QStringLiteral(" key=") << navigationKeyName(key)
                      << QStringLiteral(" fromRow=") << fromRow
                      << QStringLiteral(" fromCol=") << fromCol
                      << QStringLiteral(" toRow=") << toRow
                      << QStringLiteral(" toCol=") << toCol
                      << extra;
}

static QPoint nextEditableCell(QTableWidget* table, int row, int col, int key)
{
    if (!table || table->rowCount() <= 0 || table->columnCount() <= 0) return {-1, -1};

    const QVector<int> columns = logicalNavigationColumns(table);
    if (columns.isEmpty()) return {row, col};

    auto logicalIndexForColumn = [&columns](int currentCol) -> int {
        const int exact = columns.indexOf(currentCol);
        if (exact >= 0) return exact;
        const int columnCount = static_cast<int>(columns.size());
        for (int i = 0; i < columnCount; ++i) {
            if (columns[i] >= currentCol) return i;
        }
        return qMax(0, columnCount - 1);
    };

    auto rowAcceptsNavigation = [table, &columns](int r, int logicalIndex) {
        if (r < 0 || r >= table->rowCount()) return false;
        if (rowIsPureViewportFill(table, r)) return false;
        if (logicalIndex < 0 || logicalIndex >= columns.size()) return false;
        auto* item = table->item(r, columns[logicalIndex]);
        if (!item || isSpacerItem(item) || item->data(RoleViewportFill).toBool()) return false;
        return item->flags().testFlag(Qt::ItemIsEditable);
    };

    int targetRow = qBound(0, row, table->rowCount() - 1);
    int logicalIndex = logicalIndexForColumn(col);

    const bool forward = (key == Qt::Key_Tab || key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Right);
    if (forward && logicalIndex == columns.size() - 1) {
        ensureNextInputRowExists(table, targetRow);
    } else if (key == Qt::Key_Down) {
        ensureNextInputRowExists(table, targetRow);
    }

    switch (key) {
    case Qt::Key_Left:
    case Qt::Key_Backtab:
        if (logicalIndex > 0) {
            --logicalIndex;
        } else {
            --targetRow;
            logicalIndex = columns.size() - 1;
        }
        break;
    case Qt::Key_Up:
        --targetRow;
        break;
    case Qt::Key_Down:
        ++targetRow;
        break;
    case Qt::Key_Tab:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        [[fallthrough]];
    case Qt::Key_Right:
    default:
        if (logicalIndex < columns.size() - 1) {
            ++logicalIndex;
        } else {
            ++targetRow;
            logicalIndex = 0;
        }
        break;
    }

    if (targetRow < 0) targetRow = 0;
    if (targetRow >= table->rowCount()) targetRow = table->rowCount() - 1;

    const int rowStep = (key == Qt::Key_Left || key == Qt::Key_Backtab || key == Qt::Key_Up) ? -1 : 1;
    while (targetRow >= 0 && targetRow < table->rowCount() && !rowAcceptsNavigation(targetRow, logicalIndex)) {
        targetRow += rowStep;
    }
    if (targetRow < 0 || targetRow >= table->rowCount()) return {row, col};

    return {targetRow, columns[logicalIndex]};
}


// P1 Inline editor p3: one canonical Excel-like navigation state machine.
// All Enter/Tab/Arrow transitions must pass through the same sequence:
// commit current cell -> validate -> save/model acknowledgement -> move -> activate next editor.
enum class ExcelNavigationStateStep {
    Begin,
    CommitCurrentCell,
    ValidateCurrentCell,
    SaveToModel,
    Move,
    ActivateNextEditor,
    Done,
    Failed
};

static QString excelNavigationStateStepName(ExcelNavigationStateStep step)
{
    switch (step) {
    case ExcelNavigationStateStep::Begin: return QStringLiteral("begin");
    case ExcelNavigationStateStep::CommitCurrentCell: return QStringLiteral("commit_current_cell");
    case ExcelNavigationStateStep::ValidateCurrentCell: return QStringLiteral("validate_current_cell");
    case ExcelNavigationStateStep::SaveToModel: return QStringLiteral("save_to_model");
    case ExcelNavigationStateStep::Move: return QStringLiteral("move");
    case ExcelNavigationStateStep::ActivateNextEditor: return QStringLiteral("activate_next_editor");
    case ExcelNavigationStateStep::Done: return QStringLiteral("done");
    case ExcelNavigationStateStep::Failed: return QStringLiteral("failed");
    }
    return QStringLiteral("unknown");
}

struct ExcelNavigationStateMachineRequest {
    QTableWidget* table = nullptr;
    QPersistentModelIndex fromIndex;
    int key = 0;
    QString source;
    bool activateNextEditor = true;
    std::function<bool()> commitCurrentCell;
};

static void logExcelNavigationState(QTableWidget* table,
                                    ExcelNavigationStateStep step,
                                    int key,
                                    int fromRow,
                                    int fromCol,
                                    int toRow,
                                    int toCol,
                                    const QString& source,
                                    const QString& extra = QString())
{
    QString details = QStringLiteral(" state=%1 source=%2")
        .arg(excelNavigationStateStepName(step), source.isEmpty() ? QStringLiteral("unknown") : source);
    if (!extra.isEmpty()) details += QStringLiteral(" ") + extra;
    logExcelNavigation(table,
                       step == ExcelNavigationStateStep::Failed ? QStringLiteral("NAV_STATE_FAIL") : QStringLiteral("NAV_STATE"),
                       key,
                       fromRow,
                       fromCol,
                       toRow,
                       toCol,
                       details);
}

static bool validateExcelNavigationCell(QTableWidget* table, const QPersistentModelIndex& index, QString* reason = nullptr)
{
    if (!persistentIndexBelongsToTable(table, index)) {
        if (reason) *reason = QStringLiteral("invalid_persistent_index");
        return false;
    }
    auto* item = table->item(index.row(), index.column());
    if (!item) {
        if (reason) *reason = QStringLiteral("missing_item");
        return false;
    }
    if (isSpacerItem(item) || item->data(RoleViewportFill).toBool()) {
        if (reason) *reason = QStringLiteral("spacer_or_viewport_fill");
        return false;
    }
    if (!(item->flags() & Qt::ItemIsEditable)) {
        if (reason) *reason = QStringLiteral("not_editable");
        return false;
    }
    if (reason) reason->clear();
    return true;
}

static bool runExcelNavigationStateMachine(ExcelNavigationStateMachineRequest request)
{
    QTableWidget* table = request.table;
    if (!table || !isExcelNavigationKey(request.key)) return false;
    ensureTableRevisionTracker(table);

    QPersistentModelIndex fromIndex = request.fromIndex;
    if (!persistentIndexBelongsToTable(table, fromIndex)) {
        fromIndex = makePersistentCellIndex(table, table->currentRow(), table->currentColumn());
    }
    const int fromRow = fromIndex.isValid() ? fromIndex.row() : table->currentRow();
    const int fromCol = fromIndex.isValid() ? fromIndex.column() : table->currentColumn();
    const QString source = request.source.trimmed().isEmpty() ? QStringLiteral("unknown") : request.source.trimmed();

    logExcelNavigationState(table, ExcelNavigationStateStep::Begin, request.key, fromRow, fromCol, -1, -1, source);

    logExcelNavigationState(table, ExcelNavigationStateStep::CommitCurrentCell, request.key, fromRow, fromCol, fromRow, fromCol, source);
    bool committed = true;
    if (request.commitCurrentCell) {
        committed = request.commitCurrentCell();
    } else if (table->property("inlineEditorActive").toBool()) {
        committed = flushInlineEditorValueToModel(table);
    }
    if (!committed) {
        logExcelNavigationState(table, ExcelNavigationStateStep::Failed, request.key, fromRow, fromCol, fromRow, fromCol, source, QStringLiteral("reason=commit_failed"));
        return false;
    }

    if (table->property("inlineEditorActive").toBool()) {
        resetInlineEditorStateForTable(table, "nav_state_machine_commit_complete");
    }

    if (!persistentIndexBelongsToTable(table, fromIndex)) {
        fromIndex = makePersistentCellIndex(table,
                                            qBound(0, fromRow, qMax(0, table->rowCount() - 1)),
                                            qBound(0, fromCol, qMax(0, table->columnCount() - 1)));
    }

    QString validationReason;
    if (!validateExcelNavigationCell(table, fromIndex, &validationReason)) {
        logExcelNavigationState(table, ExcelNavigationStateStep::Failed, request.key, fromRow, fromCol, -1, -1, source, QStringLiteral("reason=%1").arg(validationReason));
        return false;
    }
    logExcelNavigationState(table, ExcelNavigationStateStep::ValidateCurrentCell, request.key, fromIndex.row(), fromIndex.column(), fromIndex.row(), fromIndex.column(), source);

    // The actual model write is performed by the canonical commit path above.
    // This state exists to make the contract explicit and traceable in logs/tests.
    logExcelNavigationState(table, ExcelNavigationStateStep::SaveToModel, request.key, fromIndex.row(), fromIndex.column(), fromIndex.row(), fromIndex.column(), source, QStringLiteral("result=committed"));

    const QPoint next = nextEditableCell(table, fromIndex.row(), fromIndex.column(), request.key);
    if (next.x() < 0 || next.y() < 0) {
        logExcelNavigationState(table, ExcelNavigationStateStep::Failed, request.key, fromIndex.row(), fromIndex.column(), -1, -1, source, QStringLiteral("reason=no_next_cell"));
        return false;
    }

    table->setCurrentCell(next.x(), next.y());
    if (auto* item = table->item(next.x(), next.y())) {
        table->scrollToItem(item, QAbstractItemView::EnsureVisible);
    }
    if (table->viewport()) table->viewport()->update();
    logExcelNavigationState(table, ExcelNavigationStateStep::Move, request.key, fromIndex.row(), fromIndex.column(), next.x(), next.y(), source);

    if (request.activateNextEditor) {
        logExcelNavigationState(table, ExcelNavigationStateStep::ActivateNextEditor, request.key, fromIndex.row(), fromIndex.column(), next.x(), next.y(), source);
        scheduleDelayedActivateEditor(table, next.x(), next.y());
    }

    logExcelNavigationState(table, ExcelNavigationStateStep::Done, request.key, fromIndex.row(), fromIndex.column(), next.x(), next.y(), source);
    return true;
}

static bool promoteViewportFillRowToInput(QTableWidget* table, int row)
{
    if (!table || row < 0 || row >= table->rowCount()) return false;
    if (!rowIsPureViewportFill(table, row)) return false;

    if (table->columnCount() == 6) {
        populateServiceInputRow(table, row, QString());
        fillTableViewportWithRows(table, true);
        return true;
    }

    if (isMainOrderPartsTable(table)) {
        populatePartInputRow(table, row);
        int visiblePartNumber = 1;
        for (int r = 0; r < table->rowCount(); ++r) {
            auto* nameItem = table->item(r, PartColName);
            auto* indexItem = table->item(r, PartColIndex);
            if (!indexItem) {
                indexItem = makePartIndexItem(QString());
                table->setItem(r, PartColIndex, indexItem);
            }
            if (nameItem && !nameItem->data(RoleIsHint).toBool() && !isSpacerItem(nameItem) && !nameItem->data(RoleViewportFill).toBool()) {
                indexItem->setText(QString::number(visiblePartNumber++));
            } else {
                indexItem->setText(QString());
            }
        }
        fillTableViewportWithRows(table, true);
        updateSingleMainPartsRowHeight(table, row);
        return true;
    }

    return false;
}

static void activateEditorAt(QTableWidget* table, int row, int col,
                             const QString& openReason = QStringLiteral("keyboard"),
                             const QPoint& clickPosInViewport = QPoint())
{
    if (!table) return;
    ensureTableRevisionTracker(table);
    if (table->property("csImportBusy").toBool()) return;
    if (QWidget* popup = QApplication::activePopupWidget()) {
        if (popup != table->viewport()) return;
    }
    if (row < 0 || col < 0 || row >= table->rowCount() || col >= table->columnCount()) return;
    const QPersistentModelIndex targetIndex = makePersistentCellIndex(table, row, col);
    if (!targetIndex.isValid()) return;
    auto* item = table->item(row, col);
    if ((!item || isSpacerItem(item) || item->data(RoleViewportFill).toBool()) && !promoteViewportFillRowToInput(table, row)) return;
    item = table->item(row, col);
    if (!item || isSpacerItem(item) || item->data(RoleViewportFill).toBool()) return;
    if (!(item->flags() & Qt::ItemIsEditable)) return;

    auto isChildOf = [](QWidget* child, QWidget* parent) {
        for (QWidget* w = child; w; w = w->parentWidget()) {
            if (w == parent) return true;
        }
        return false;
    };

    if (table->property("inlineEditorActive").toBool()) {
        const QPersistentModelIndex activeIndex = inlineEditingPersistentIndex(table);
        const int activeRow = activeIndex.isValid() ? activeIndex.row() : -1;
        const int activeCol = activeIndex.isValid() ? activeIndex.column() : -1;
        QWidget* activeEditor = activeIndex.isValid() ? visibleInlineEditorForCell(table, activeRow, activeCol) : nullptr;
        if (!activeEditor) {
            if (qEnvironmentVariableIntValue("CS_DEEP_DEBUG") > 0) {
                qWarning() << "WorkPartsDialog stale inline editor flag cleared before activation"
                           << "table" << (table->objectName().isEmpty() ? QStringLiteral("<unnamed>") : table->objectName())
                           << "row" << activeRow
                           << "col" << activeCol
                           << "targetRow" << row
                           << "targetCol" << col;
            }
            resetInlineEditorStateForTable(table, "activate_stale_editor");
            const quint64 queuedRevision = tableRevisionSnapshot(table);
            QPointer<QTableWidget> retryTable(table);
            QTimer::singleShot(0, table, [retryTable, targetIndex, queuedRevision, openReason, clickPosInViewport]() {
                if (!retryTable || !retryTable->isVisible()) return;
                if (!queuedEditorActionStillValid(retryTable.data(), targetIndex, queuedRevision, QStringLiteral("activate_stale_editor_retry"))) return;
                activateEditorAtPersistentIndex(retryTable.data(), targetIndex, openReason, clickPosInViewport);
            });
            return;
        } else if (activeRow != row || activeCol != col) {
            if (!flushInlineEditorValueToModel(table)) {
                const quint64 queuedRevision = tableRevisionSnapshot(table);
                QPointer<QTableWidget> retryTable(table);
                QTimer::singleShot(0, table, [retryTable, targetIndex, queuedRevision, openReason, clickPosInViewport]() {
                    if (!retryTable || !retryTable->isVisible()) return;
                    if (!queuedEditorActionStillValid(retryTable.data(), targetIndex, queuedRevision, QStringLiteral("flush_failed_activation_retry"))) return;
                    activateEditorAtPersistentIndex(retryTable.data(), targetIndex, openReason, clickPosInViewport);
                });
                return;
            }
        }
    }

    table->setCurrentCell(row, col);
    table->scrollToItem(item, QAbstractItemView::EnsureVisible);
    table->setFocus(Qt::OtherFocusReason);
    table->setProperty("editorOpenReason", openReason);

    QWidget* focused = QApplication::focusWidget();
    if (focused && focused != table && isChildOf(focused, table->viewport()) &&
        table->currentRow() == row && table->currentColumn() == col) {
        focused->setFocus(Qt::OtherFocusReason);
        return;
    }

    table->editItem(item);
    logInlineEditorDiagnostic(table, QStringLiteral("editor_open"), row, col, openReason, true);
    if (QWidget* editor = QApplication::focusWidget(); editor && editor != table && isChildOf(editor, table->viewport())) {
        editor->setFocus(Qt::OtherFocusReason);
    }

    if (table->property("inlineEditorActive").toBool()) {
        const QPersistentModelIndex activeIndex = inlineEditingPersistentIndex(table);
        const int activeRow = activeIndex.isValid() ? activeIndex.row() : -1;
        const int activeCol = activeIndex.isValid() ? activeIndex.column() : -1;
        if (activeIndex.isValid() && activeRow == row && activeCol == col && !visibleInlineEditorForCell(table, row, col)) {
            if (qEnvironmentVariableIntValue("CS_DEEP_DEBUG") > 0) {
                qWarning() << "WorkPartsDialog editor activation produced no visible editor"
                           << "table" << (table->objectName().isEmpty() ? QStringLiteral("<unnamed>") : table->objectName())
                           << "row" << row
                           << "col" << col
                           << "reason" << openReason;
            }
            resetInlineEditorStateForTable(table, "activate_no_visible_editor_after_editItem");
            if (openReason != QStringLiteral("retry_after_no_visible_editor")) {
                const quint64 queuedRevision = tableRevisionSnapshot(table);
                QPointer<QTableWidget> retryTable(table);
                QTimer::singleShot(15, table, [retryTable, targetIndex, queuedRevision, clickPosInViewport]() {
                    if (!retryTable || !retryTable->isVisible()) return;
                    if (!queuedEditorActionStillValid(retryTable.data(), targetIndex, queuedRevision, QStringLiteral("retry_after_no_visible_editor"))) return;
                    activateEditorAtPersistentIndex(retryTable.data(), targetIndex, QStringLiteral("retry_after_no_visible_editor"), clickPosInViewport);
                });
            }
            return;
        }
    }

    if (openReason == QStringLiteral("mouse")) {
        const quint64 queuedRevision = tableRevisionSnapshot(table);
        QPointer<QTableWidget> safeTable(table);
        QTimer::singleShot(0, table, [safeTable, targetIndex, queuedRevision, clickPosInViewport, isChildOf]() {
            auto* table = safeTable.data();
            if (!table || !queuedEditorActionStillValid(table, targetIndex, queuedRevision, QStringLiteral("mouse_cursor_position"))) return;
            if (table->currentRow() != targetIndex.row() || table->currentColumn() != targetIndex.column()) return;
            if (auto* edit = qobject_cast<QLineEdit*>(QApplication::focusWidget())) {
                if (table->viewport() && isChildOf(edit, table->viewport())) {
                    const QPoint localPos = edit->mapFrom(table->viewport(), clickPosInViewport);
                    edit->setCursorPosition(edit->cursorPositionAt(localPos));
                    edit->deselect();
                }
            }
        });
    }
}

static void activateEditorAtPersistentIndex(QTableWidget* table, const QPersistentModelIndex& index,
                                            const QString& openReason,
                                            const QPoint& clickPosInViewport)
{
    ensureTableRevisionTracker(table);
    if (!persistentIndexBelongsToTable(table, index)) return;
    activateEditorAt(table, index.row(), index.column(), openReason, clickPosInViewport);
}

static void clearInlineEditorStateIfMatches(QTableWidget* table, int row, int col)
{
    if (!table) return;
    if (!table->property("inlineEditorActive").toBool()) return;
    if (!inlineEditingMatches(table, row, col)) return;
    table->setProperty("inlineEditorActive", false);
    clearInlineEditingIndex(table);
    if (table->viewport()) table->viewport()->update();
}

static void clearInlineEditorStateIfMatches(QTableWidget* table, const QPersistentModelIndex& index)
{
    if (!persistentIndexBelongsToTable(table, index)) return;
    clearInlineEditorStateIfMatches(table, index.row(), index.column());
}

static QWidget* visibleInlineEditorForCell(QTableWidget* table, int row, int col)
{
    if (!table || row < 0 || col < 0 || row >= table->rowCount() || col >= table->columnCount()) return nullptr;
    const QRect cellRect = table->visualRect(table->model()->index(row, col)).adjusted(-3, -3, 3, 3);
    auto isChildOf = [](QWidget* child, QWidget* parent) {
        for (QWidget* w = child; w; w = w->parentWidget()) {
            if (w == parent) return true;
        }
        return false;
    };
    auto comboAncestor = [](QWidget* child) -> QWidget* {
        for (QWidget* w = child; w; w = w->parentWidget()) {
            if (qobject_cast<QComboBox*>(w)) return w;
        }
        return child;
    };

    if (QWidget* focused = QApplication::focusWidget()) {
