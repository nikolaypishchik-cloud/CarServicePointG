// Extracted from src/ui/tabs/orders/WorkPartsInlineEditorController.h for P1/P2 monolith reduction.
// Included exactly once by the original translation unit/header; do not compile separately.

        QString seedText;
        bool replaceWithSeed = false;
        if (auto* table = qobject_cast<QTableWidget*>(parent())) {
            openReason = table->property("editorOpenReason").toString();
            takeEditorSeedForIndex(table, index, &seedText, &replaceWithSeed);
        }
        if (!seedText.isEmpty()) {
            edit->setText(seedText);
            QTimer::singleShot(0, edit, [edit]() {
                edit->setCursorPosition(edit->text().size());
                edit->deselect();
            });
        } else if (isHint) {
            edit->clear();
            QTimer::singleShot(0, edit, [edit]() {
                edit->setCursorPosition(0);
                edit->deselect();
            });
        } else {
            edit->setText(raw);
            if (openReason == QStringLiteral("mouse")) {
                QTimer::singleShot(0, edit, [edit]() {
                    edit->deselect();
                });
            } else if (replaceWithSeed) {
                QTimer::singleShot(0, edit, [edit]() {
                    edit->setCursorPosition(edit->text().size());
                    edit->deselect();
                });
            } else {
                QTimer::singleShot(0, edit, [edit]() {
                    edit->selectAll();
                });
            }
        }
    }


    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override
    {
        if (auto* combo = qobject_cast<QComboBox*>(editor)) {
            QString value = combo->currentText();
            if (auto* table = qobject_cast<QTableWidget*>(this->parent()); !table || table->columnCount() != 6 || index.column() != ServiceColMaster) {
                value = normalizeSentence(value);
            }
            model->setData(index, value, Qt::EditRole);
            return;
        }
        if (auto* edit = qobject_cast<QLineEdit*>(editor)) {
            QString value = edit->text();
            if (auto* table = qobject_cast<QTableWidget*>(this->parent())) {
                if (isMainOrderPartsTable(table) && index.column() == PartColOrderedAt) {
                    value = value.trimmed();
                    if (value.isEmpty() && rowHasMeaningfulPartData(table, index.row())) {
                        value = currentOrderDateDisplay();
                    } else if (!value.isEmpty()) {
                        value = displayDateFromStored(storedDateFromDisplay(value));
                    }
                }
            }
            model->setData(index, value, Qt::EditRole);
            return;
        }
        QStyledItemDelegate::setModelData(editor, model, index);
    }

    bool eventFilter(QObject* obj, QEvent* ev) override
    {
        auto comboAncestor = [](QObject* child) -> QComboBox* {
            for (QObject* p = child ? child->parent() : nullptr; p; p = p->parent()) {
                if (auto* combo = qobject_cast<QComboBox*>(p)) return combo;
            }
            return nullptr;
        };
        auto isClosing = [comboAncestor](QObject* editor) -> bool {
            if (!editor) return false;
            if (editor->property("csEditorClosing").toBool()) return true;
            if (auto* combo = qobject_cast<QComboBox*>(editor)) {
                return combo->lineEdit() && combo->lineEdit()->property("csEditorClosing").toBool();
            }
            if (auto* line = qobject_cast<QLineEdit*>(editor)) {
                if (auto* combo = comboAncestor(line)) {
                    return combo->property("csEditorClosing").toBool() || line->property("csEditorClosing").toBool();
                }
            }
            return false;
        };
        auto commitCloseAndNavigate = [this, &isClosing](QWidget* editor, int key, bool navigate) {
            if (!editor || isClosing(editor)) return;
            auto* table = qobject_cast<QTableWidget*>(parent());
            const QPersistentModelIndex editorIndex = editorWidgetPersistentIndex(editor, table);
            const int row = editorIndex.isValid() ? editorIndex.row() : (table ? table->currentRow() : -1);
            const int col = editorIndex.isValid() ? editorIndex.column() : (table ? table->currentColumn() : -1);
            QPointer<QWidget> safeEditor(editor);
            auto commitCurrentCell = [this, table, safeEditor, editorIndex, key]() mutable -> bool {
                if (safeEditor) {
                    return commitInlineEditorCanonical(safeEditor.data(), table, QStringLiteral("event_filter"), key, editorIndex);
                }
                if (table) resetInlineEditorStateForTable(table, "event_filter_orphan_editor");
                return false;
            };

            if (navigate && table && row >= 0 && col >= 0) {
                ExcelNavigationStateMachineRequest request;
                request.table = table;
                request.fromIndex = editorIndex.isValid() ? editorIndex : makePersistentCellIndex(table, row, col);
                request.key = key;
                request.source = QStringLiteral("editor");
                request.activateNextEditor = true;
                request.commitCurrentCell = commitCurrentCell;
                runExcelNavigationStateMachine(request);
                return;
            }

            commitCurrentCell();
        };

        if (auto* combo = qobject_cast<QComboBox*>(obj)) {
            if (ev->type() == QEvent::FocusOut) {
                if (isClosing(combo)) return true;
                if (!combo->view() || !combo->view()->isVisible()) {
                    commitCloseAndNavigate(combo, 0, false);
                    return true;
                }
                return QStyledItemDelegate::eventFilter(obj, ev);
            }
            if (ev->type() == QEvent::KeyPress) {
                auto* ke = static_cast<QKeyEvent*>(ev);
                const int key = ke->key();
                if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Tab || key == Qt::Key_Backtab) {
                    commitCloseAndNavigate(combo, key, true);
                    return true;
                }
            }
            return QStyledItemDelegate::eventFilter(obj, ev);
        }

        if (auto* edit = qobject_cast<QLineEdit*>(obj)) {
            if (ev->type() == QEvent::MouseButtonRelease) {
                edit->setProperty("clearOnFirstMouseClick", false);
            } else if (ev->type() == QEvent::FocusOut) {
                if (isClosing(edit)) return true;
                if (comboAncestor(edit)) {
                    return QStyledItemDelegate::eventFilter(obj, ev);
                }
                commitCloseAndNavigate(edit, 0, false);
                return true;
            } else if (ev->type() == QEvent::KeyPress) {
                auto* ke = static_cast<QKeyEvent*>(ev);
                const int key = ke->key();
                if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Tab || key == Qt::Key_Backtab ||
                    key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down) {
                    QWidget* editorWidget = edit;
                    if (auto* combo = comboAncestor(edit)) {
                        editorWidget = combo;
                    }
                    commitCloseAndNavigate(editorWidget, key, true);
                    return true;
                }
            }
        }
        return QStyledItemDelegate::eventFilter(obj, ev);
    }

    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        if (auto* combo = qobject_cast<QComboBox*>(editor)) {
            const QRect cellRect = option.rect.adjusted(1, 1, -1, -1);
            QRect editorRect = cellRect;
            if (auto* table = qobject_cast<QTableWidget*>(this->parent());
                table && isMainOrderPartsTable(table) && index.column() == PartColStore && table->viewport()) {
                const QRect viewportRect = table->viewport()->rect();
                const int targetWidth = qMin(qMax(cellRect.width(), 260), qMax(1, viewportRect.width()));
                const int maxLeft = viewportRect.right() - targetWidth + 1;
                editorRect.setLeft(qBound(viewportRect.left(), cellRect.left(), maxLeft));
                editorRect.setWidth(targetWidth);
                editorRect.setTop(cellRect.top());
                editorRect.setHeight(cellRect.height());
                editorRect = editorRect.intersected(viewportRect);
            }
            combo->setGeometry(editorRect);
            if (combo->view()) combo->view()->setMinimumWidth(qMax(editorRect.width(), combo->view()->minimumWidth()));
            return;
        }
        editor->setGeometry(option.rect.adjusted(1, 1, -1, -1));
    }
};

static bool clipboardProtectedColumn(QTableWidget* table, int column)
{
    if (!table || column < 0 || column >= table->columnCount()) return true;
    if (table->columnCount() == 6) return column == ServiceColIndex || column == ServiceColAttachment;
    if (table->columnCount() >= 12) return column >= 12 || column == PartColIndex || column == PartColDelivered || column == PartColAmount || column == PartColAttachment;
    return false;
}

static bool clipboardCellEditable(QTableWidget* table, int row, int column)
{
    if (!table || row < 0 || column < 0 || row >= table->rowCount() || column >= table->columnCount()) return false;
    if (clipboardProtectedColumn(table, column)) return false;
    auto* item = table->item(row, column);
    if (!item || item->data(RoleViewportFill).toBool()) return false;
    return item->flags().testFlag(Qt::ItemIsEditable);
}

static QString clipboardTextForCellLocal(QTableWidget* table, int row, int col)
{
    if (!table || row < 0 || col < 0 || row >= table->rowCount() || col >= table->columnCount()) return QString();
    if (auto* item = table->item(row, col)) {
        return item->data(RoleIsHint).toBool() ? QString() : item->text();
    }
    return QString();
}

static QList<QModelIndex> selectedIndexesSortedLocal(QTableWidget* table)
{
    QList<QModelIndex> indexes;
    if (!table || !table->selectionModel()) return indexes;
    indexes = table->selectionModel()->selectedIndexes();
    std::sort(indexes.begin(), indexes.end(), [](const QModelIndex& a, const QModelIndex& b) {
        if (a.row() != b.row()) return a.row() < b.row();
        return a.column() < b.column();
    });
    return indexes;
}

static void copyCurrentCellToClipboardLocal(QTableWidget* table)
{
    if (!table) return;
    const QList<QModelIndex> indexes = selectedIndexesSortedLocal(table);
    if (!indexes.isEmpty()) {
        QMap<int, QMap<int, QString>> grid;
        QList<int> rows;
        QList<int> cols;
        for (const QModelIndex& idx : indexes) {
            if (!rows.contains(idx.row())) rows.append(idx.row());
            if (!cols.contains(idx.column())) cols.append(idx.column());
            grid[idx.row()][idx.column()] = clipboardTextForCellLocal(table, idx.row(), idx.column());
        }
        std::sort(rows.begin(), rows.end());
        std::sort(cols.begin(), cols.end());
        QStringList outRows;
        for (int row : rows) {
            QStringList outCols;
            for (int col : cols) outCols << grid[row].value(col);
            outRows << outCols.join(QChar('\t'));
        }
        QGuiApplication::clipboard()->setText(outRows.join(QChar('\n')));
        return;
    }
    const int row = table->currentRow();
    const int col = table->currentColumn();
    if (row < 0 || col < 0) return;
    QGuiApplication::clipboard()->setText(clipboardTextForCellLocal(table, row, col));
}

static void clearSelectionOrCurrentCellLocal(QTableWidget* table)
{
    if (!table) return;
    QList<QModelIndex> indexes = selectedIndexesSortedLocal(table);
    if (indexes.isEmpty()) {
        const int row = table->currentRow();
        const int col = table->currentColumn();
        if (row >= 0 && col >= 0) indexes.append(table->model()->index(row, col));
    }
    if (indexes.isEmpty()) return;
    if (QAbstractItemModel* model = table->model()) {
        for (const QModelIndex& idx : indexes) {
            if (!clipboardCellEditable(table, idx.row(), idx.column())) continue;
            model->setData(idx, QString(), Qt::EditRole);
            model->setData(idx, QString(), Qt::DisplayRole);
        }
    }
}

static void cutCurrentCellToClipboardLocal(QTableWidget* table)
{
    copyCurrentCellToClipboardLocal(table);
    clearSelectionOrCurrentCellLocal(table);
}

static void pasteClipboardIntoTableLocal(QTableWidget* table)
{
    if (!table) return;
    const int startRow = table->currentRow();
    const int startCol = table->currentColumn();
    if (startRow < 0 || startCol < 0) return;
    QString raw = QGuiApplication::clipboard()->text();
    raw.replace(QStringLiteral("\n"), QStringLiteral("\n"));
    raw.replace(QChar('\n'), QChar('\n'));
    if (raw.isEmpty()) return;
    const QStringList rows = raw.split(QChar('\n'));
    for (int r = 0; r < rows.size(); ++r) {
        if (rows.at(r).isEmpty() && r == rows.size() - 1) continue;
        const int targetRow = startRow + r;
        if (targetRow < 0 || targetRow >= table->rowCount()) break;
        const QStringList cols = rows.at(r).split(QChar('\t'));
        for (int c = 0; c < cols.size(); ++c) {
            const int targetCol = startCol + c;
            if (!clipboardCellEditable(table, targetRow, targetCol)) continue;
            if (QAbstractItemModel* model = table->model()) {
                const QModelIndex idx = model->index(targetRow, targetCol);
                model->setData(idx, cols.at(c), Qt::EditRole);
            }
        }
    }
    table->viewport()->update();
}

class TableKeyboardNavigator final : public QObject {
public:
    explicit TableKeyboardNavigator(QTableWidget* table)
        : QObject(table), m_table(table) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        Q_UNUSED(watched);
        if (!m_table) return QObject::eventFilter(watched, event);
        if (event->type() != QEvent::KeyPress) return QObject::eventFilter(watched, event);

        auto* ke = static_cast<QKeyEvent*>(event);
        const int key = ke->key();
        const int row = qMax(0, m_table->currentRow());
        const int col = qMax(0, m_table->currentColumn());

        auto moveSelection = [this, row, col, key]() {
            ExcelNavigationStateMachineRequest request;
            request.table = m_table;
            request.fromIndex = makePersistentCellIndex(m_table, row, col);
            request.key = key;
            request.source = QStringLiteral("table");
            request.activateNextEditor = true;
            request.commitCurrentCell = [this]() -> bool {
                if (!m_table || !m_table->property("inlineEditorActive").toBool()) return true;
                return flushInlineEditorValueToModel(m_table);
            };
            runExcelNavigationStateMachine(request);
        };

        if (isExcelNavigationKey(key)) {
            moveSelection();
            return true;
        }

        if (ke->matches(QKeySequence::Copy)) {
            copyCurrentCellToClipboardLocal(m_table);
            return true;
        }
        if (ke->matches(QKeySequence::Cut)) {
            cutCurrentCellToClipboardLocal(m_table);
            return true;
        }
        if (ke->matches(QKeySequence::Paste)) {
            pasteClipboardIntoTableLocal(m_table);
            return true;
        }

        if (key == Qt::Key_Delete || key == Qt::Key_Backspace) {
            if (auto* dlg = qobject_cast<WorkPartsDialog*>(m_table->window())) {
                const bool isServiceTable = m_table->columnCount() == 6;
                if (isServiceTable && row >= 0) {
                    auto* titleItem = m_table->item(row, ServiceColTitle);
                    if (titleItem && !titleItem->data(RoleIsHint).toBool() && !isSpacerItem(titleItem) &&
                        (titleItem->data(RoleId).toInt() > 0 || rowHasMeaningfulServiceData(m_table, row))) {
                        m_table->setCurrentCell(row, col >= 0 ? col : ServiceColTitle);
                        dlg->deleteActiveService();
                        return true;
                    }
                }
            }
            clearSelectionOrCurrentCellLocal(m_table);
            m_table->viewport()->update();
            return true;
        }

        const QString text = ke->text();
        if (!text.isEmpty() && text.at(0).isPrint()) {
            setEditorSeedIndex(m_table, m_table->model() ? m_table->model()->index(row, col) : QModelIndex());
            m_table->setProperty("editorSeedText", text);
            m_table->setProperty("editorReplaceWithSeed", true);
            activateEditorAt(m_table, row, col, QStringLiteral("keyboard"));
            return true;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QTableWidget* m_table = nullptr;
};

static constexpr int RoleBatchRowHeightSignature = Qt::UserRole + 181;
static constexpr int RoleBatchRowHeightValue = Qt::UserRole + 182;

static QTableWidgetItem* ensureBatchVerticalHeaderItem(QTableWidget* table, int row)
{
    if (!table || row < 0 || row >= table->rowCount()) return nullptr;
    if (auto* existing = table->verticalHeaderItem(row)) return existing;
    auto* created = new QTableWidgetItem(QString::number(row + 1));
    table->setVerticalHeaderItem(row, created);
    return created;
}

static quint64 batchRowHeightSignature(QTableWidget* table, int row)
{
    if (!table || row < 0 || row >= table->rowCount()) return 0;
    QString signature;
    signature.reserve(256);
    signature += QString::number(table->font().pointSizeF(), 'f', 2);
    for (int c = 0; c < table->columnCount(); ++c) {
        const auto* item = table->item(row, c);
        signature += QLatin1Char('|');
        signature += QString::number(c);
        signature += QLatin1Char(':');
        signature += QString::number(table->columnWidth(c));
        signature += QLatin1Char(':');
        signature += item ? item->text().trimmed().left(160) : QString();
    }
    return static_cast<quint64>(qHash(signature));
}

static QPair<int, int> bufferedVisibleBatchRows(QTableWidget* table, int bufferRows = 24)
{
    if (!table || table->rowCount() <= 0) return qMakePair(0, -1);
    int first = table->rowAt(0);
    int last = table->rowAt(qMax(0, table->viewport() ? table->viewport()->height() - 1 : table->height() - 1));
    if (first < 0) first = table->verticalScrollBar() ? table->verticalScrollBar()->value() : 0;
    if (last < 0) last = qMin(table->rowCount() - 1, first + 60);
    first = qBound(0, first - bufferRows, table->rowCount() - 1);
    last = qBound(0, last + bufferRows, table->rowCount() - 1);
    return qMakePair(first, last);
}

static int calcBatchRowHeight(QTableWidget* table, int row)
{
    if (!table) return 40;
    const int baseHeight = 40;
    const int minEditHeight = 30;
    int maxLines = 1;
    const QFontMetrics fm(table->font());
    for (int c = 0; c < table->columnCount(); ++c) {
        const auto* item = table->item(row, c);
        const QString txt = item ? item->text().trimmed() : QString();
        if (txt.isEmpty()) continue;
        const int colWidth = qMax(40, table->columnWidth(c) - 20);
        QRect br = fm.boundingRect(QRect(0, 0, colWidth, 2000), Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignVCenter, txt);
        const int lines = qMax(1, (br.height() + fm.lineSpacing() - 1) / fm.lineSpacing());
        maxLines = qMax(maxLines, lines);
    }
    return qMax(baseHeight, qMax(minEditHeight + 10, maxLines * fm.lineSpacing() + 14));
}

static void updateSingleBatchRowHeight(QTableWidget* table, int row)
{
    if (!table || row < 0 || row >= table->rowCount()) return;
    auto* headerItem = ensureBatchVerticalHeaderItem(table, row);
    const quint64 signature = batchRowHeightSignature(table, row);
    if (headerItem && headerItem->data(RoleBatchRowHeightSignature).toULongLong() == signature) {
        const int cachedHeight = headerItem->data(RoleBatchRowHeightValue).toInt();
        if (cachedHeight > 0) {
            if (table->rowHeight(row) != cachedHeight) table->setRowHeight(row, cachedHeight);
            return;
        }
    }

    const int height = calcBatchRowHeight(table, row);
    if (headerItem) {
        headerItem->setData(RoleBatchRowHeightSignature, QVariant::fromValue(signature));
        headerItem->setData(RoleBatchRowHeightValue, height);
    }
    if (table->rowHeight(row) != height) table->setRowHeight(row, height);
}

static void updateBatchRowHeights(QTableWidget* table)
{
    if (!table) return;
    // P1 row-height lazy sync/cache: the batch editor no longer recalculates
    // every row on open/resize/edit. Only the viewport plus a bounded buffer is
    // updated, and each row is guarded by a signature/value cache on its header item.
    const auto range = bufferedVisibleBatchRows(table);
    for (int r = range.first; r <= range.second; ++r) {
        updateSingleBatchRowHeight(table, r);
    }
}

static int wrappedNameCellHeight(QTableWidget* table, int row, int column, const QFont& font)
{
    if (!table || row < 0 || row >= table->rowCount() || column < 0 || column >= table->columnCount()) return 0;
    const auto* item = table->item(row, column);
    const QString cellText = item ? item->text().trimmed() : QString();
    if (cellText.isEmpty()) return 0;

    const int availableWidth = qMax(24, table->columnWidth(column) - 12);
    const QFont fitted = fittedWrappedNameCellFont(font, cellText, availableWidth);
    const int textFlags = Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignVCenter;
    const QRectF br = QFontMetricsF(fitted).boundingRect(
        QRectF(0, 0, availableWidth, 1000),
        textFlags,
        cellText);
    return qCeil(br.height()) + 10;
}

int calcMainServicesRowHeight(QTableWidget* table, int row)
{
    if (!isMainOrderServicesTable(table) || row < 0 || row >= table->rowCount()) return kDefaultTableRowHeight;
    return qMax(kDefaultTableRowHeight, wrappedNameCellHeight(table, row, ServiceColTitle, table->font()) + 2);
}

int calcMainPartsRowHeight(QTableWidget* table, int row)
{
    if (!isMainOrderPartsTable(table) || row < 0 || row >= table->rowCount()) return kDefaultTableRowHeight;

    const int baseHeight = kDefaultTableRowHeight;
    const int verticalPadding = 8;

    int targetHeight = baseHeight;
    targetHeight = qMax(targetHeight, wrappedNameCellHeight(table, row, PartColName, table->font()));

    if (const auto* oemItem = table->item(row, PartColOem)) {
        const QString oemText = oemItem->text().trimmed();
        if (!oemText.isEmpty()) {
            const QFont fitted = fittedCellFont(table->font(), oemText, qMax(24, table->columnWidth(PartColOem) - 12), 5.0);
            targetHeight = qMax(targetHeight, qCeil(QFontMetricsF(fitted).height()) + verticalPadding);
        }
    }
    if (const auto* brandItem = table->item(row, PartColBrand)) {
        const QString brandText = brandItem->text().trimmed();
        if (!brandText.isEmpty()) {
            const QFont fitted = fittedCellFont(table->font(), brandText, qMax(24, table->columnWidth(PartColBrand) - 12), 5.0);
            targetHeight = qMax(targetHeight, qCeil(QFontMetricsF(fitted).height()) + verticalPadding);
        }
    }
    if (const auto* storeItem = table->item(row, PartColStore)) {
        const QString storeText = storeItem->text().trimmed();
        if (!storeText.isEmpty()) {
            const QFont fitted = fittedCellFont(table->font(), storeText, qMax(24, table->columnWidth(PartColStore) - 12), 5.0);
            targetHeight = qMax(targetHeight, qCeil(QFontMetricsF(fitted).height()) + verticalPadding);
        }
    }
    if (const auto* invoiceItem = table->item(row, PartColInvoiceNumber)) {
        const QString invoiceText = invoiceItem->text().trimmed();
        if (!invoiceText.isEmpty()) {
            const QFont fitted = fittedCellFont(table->font(), invoiceText, qMax(24, table->columnWidth(PartColInvoiceNumber) - 12), 5.0);
            targetHeight = qMax(targetHeight, qCeil(QFontMetricsF(fitted).height()) + verticalPadding);
        }
    }

    return qMax(baseHeight, targetHeight + 2);
}

void updateSingleMainPartsRowHeight(QTableWidget* table, int row)
{
    if (!isMainOrderPartsTable(table) || row < 0 || row >= table->rowCount()) return;
    table->setRowHeight(row, calcMainPartsRowHeight(table, row));
}

void updateAllMainPartsRowHeights(QTableWidget* table)
{
    if (!isMainOrderPartsTable(table) || table->rowCount() <= 0) return;

    // P0/P1 performance: this helper used to recalculate every row after
    // repositionInputRows(), which could freeze the dialog on large orders.
    // Keep the visual behaviour but limit the expensive font/height work to
    // the viewport plus a small buffer; off-screen rows are corrected lazily by
    // synchronizeAllPairedRowHeights() on scroll/resize/edit.
    int first = table->rowAt(0);
    int last = table->rowAt(qMax(0, table->viewport() ? table->viewport()->height() - 1 : table->height() - 1));
    if (first < 0) first = 0;
    if (last < 0) last = qMin(table->rowCount() - 1, first + 60);
    first = qBound(0, first - 30, table->rowCount() - 1);
    last = qBound(0, last + 30, table->rowCount() - 1);

    for (int row = first; row <= last; ++row) {
        updateSingleMainPartsRowHeight(table, row);
    }
}

QList<QStringList> runBatchEditor(QWidget* parent, const QString& title, const QStringList& headers, const QList<QStringList>& existing, const QString& stateKey)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(title);
    const bool isPartsEditor = (headers.size() == 8);
    const bool isServicesEditor = (headers.size() == 3);
    if (isPartsEditor) dlg.resize(1320, 740);
    else if (isServicesEditor) dlg.resize(1240, 760);
    else dlg.resize(1520, 860);
    auto* root = new QVBoxLayout(&dlg);
    root->setContentsMargins(12,12,12,12);
    root->setSpacing(10);

    auto* table = new QTableWidget(&dlg);
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->verticalHeader()->setVisible(true);
    table->verticalHeader()->setDefaultAlignment(Qt::AlignCenter);
    table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table->verticalHeader()->setDefaultSectionSize(42);
    table->verticalHeader()->setMinimumSectionSize(42);
    table->horizontalHeader()->setStretchLastSection(false);
    table->horizontalHeader()->setSectionsMovable(true);
    table->setEditTriggers(QAbstractItemView::CurrentChanged | QAbstractItemView::SelectedClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
    table->setSelectionBehavior(QAbstractItemView::SelectItems);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(false);
    table->setWordWrap(true);
    table->setCornerButtonEnabled(false);
    styleDarkFlatTable(table);
    table->setItemDelegate(new FlatTableItemDelegate(table));
    QFont tableFont = table->font();
    tableFont.setPointSize(12);
    table->setFont(tableFont);
    QFont headerFont = table->horizontalHeader()->font();
    headerFont.setPointSize(10);
    headerFont.setBold(true);
    table->horizontalHeader()->setFont(headerFont);
    table->verticalHeader()->setFont(headerFont);
    table->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);

    auto updateNumbers = [table]() {
        // P1 import/batch editor optimization: keep existing header items so
        // row-height cache roles survive renumbering; only create/update when
        // the visible number actually changed.
        for (int r = 0; r < table->rowCount(); ++r) {
            const QString number = QString::number(r + 1);
            if (auto* existing = table->verticalHeaderItem(r)) {
                if (existing->text() != number) existing->setText(number);
                continue;
            }
            table->setVerticalHeaderItem(r, new QTableWidgetItem(number));
        }
    };

    const int baseRows = qMax(8, existing.size() + 2);
    table->setRowCount(baseRows);
    for (int r = 0; r < baseRows; ++r) {
        for (int c = 0; c < headers.size(); ++c) {
            if (!table->item(r, c)) table->setItem(r, c, new QTableWidgetItem());
        }
    }
    updateNumbers();
    updateBatchRowHeights(table);

    for (int r = 0; r < existing.size(); ++r) {
        const auto row = existing[r];
        for (int c = 0; c < headers.size() && c < row.size(); ++c) {
            table->item(r, c)->setText(row[c]);
        }
    }
    if (headers.size() == 8) {
        for (int r = 0; r < table->rowCount(); ++r) {
            if (auto* dateItem = table->item(r, 0); dateItem && dateItem->text().trimmed().isEmpty()) dateItem->setText(currentOrderDateDisplay());
            if (auto* amountItem = table->item(r, 5)) amountItem->setFlags(amountItem->flags() & ~Qt::ItemIsEditable);
        }
    }

    for (int i = 0; i < headers.size(); ++i) {
        table->horizontalHeader()->setSectionResizeMode(i, QHeaderView::Interactive);
    }
    if (headers.size() == 3) {
        table->setColumnWidth(0, 760);
        table->setColumnWidth(1, 260);
        table->setColumnWidth(2, 260);
    } else if (headers.size() == 8) {
        table->setColumnWidth(0, 140);
        table->setColumnWidth(1, 180);
        table->setColumnWidth(2, 480);
        table->setColumnWidth(3, 140);
        table->setColumnWidth(4, 170);
        table->setColumnWidth(5, 170);
        table->setColumnWidth(6, 220);
        table->setColumnWidth(7, 180);
    }
    restoreHeaderState(table->horizontalHeader(), stateKey);

    if (isPartsEditor) {
        if (QWidget* pw = parent ? parent->window() : nullptr) {
            const QRect pg = pw->frameGeometry();
            const int dlgW = qMin(1260, qMax(980, pg.width() - 520));
            const int dlgH = qMin(700, qMax(560, pg.height() - 140));
            dlg.setFixedSize(dlgW, dlgH);
            const int x = pg.x() + qMin(320, qMax(180, pg.width() / 6));
            const int y = pg.y() + 26;
            dlg.move(x, y);
        } else {
            dlg.setFixedSize(1180, 660);
        }
    }
    bindHeaderStatePersistence(table->horizontalHeader(), stateKey);
    updateBatchRowHeights(table);
    QObject::connect(table->horizontalHeader(), &QHeaderView::sectionResized, &dlg, [table](int, int, int) {
        QTimer::singleShot(0, table, [table]() { updateBatchRowHeights(table); });
    });

    QObject::connect(table, &QTableWidget::itemChanged, &dlg, [table, updateNumbers](QTableWidgetItem* changed) {
        if (!changed) return;
        const int row = changed->row();
        const int col = changed->column();
        const bool isServices = table->columnCount() == 3;
        const bool isParts = table->columnCount() == 8;
        const bool normalizeCol = (isServices && (col == 0 || col == 2)) || (isParts && (col == 2 || col == 6 || col == 7));
        if (normalizeCol) {
            const QString norm = normalizeSentence(changed->text());
            if (norm != changed->text()) {
                QSignalBlocker blocker(table);
                changed->setText(norm);
            }
        }
        if (isParts) {
            if (col == 0 && changed->text().trimmed().isEmpty()) {
                QSignalBlocker blocker(table);
                changed->setText(currentOrderDateDisplay());
            }
            if (col == 3 || col == 4) {
                bool okQty = false, okPrice = false;
                const double qty = table->item(row, 3) ? parseLooseNumber(table->item(row, 3)->text().trimmed(), &okQty) : 1.0;
                const double price = table->item(row, 4) ? parseLooseNumber(table->item(row, 4)->text().trimmed(), &okPrice) : 0.0;
                const double amount = FormatUtils::roundMoneyValue((okQty && qty > 0.0 ? qty : 1.0) * (okPrice && price >= 0.0 ? price : 0.0));
                if (auto* amountItem = table->item(row, 5)) {
                    QSignalBlocker blocker(table);
                    amountItem->setText(moneyText(amount));
                }
            }
        }

        bool hasAny = false;
        for (int c = 0; c < table->columnCount(); ++c) {
            auto* it = table->item(row, c);
            if (it && !it->text().trimmed().isEmpty()) { hasAny = true; break; }
        }
        updateSingleBatchRowHeight(table, row);
        if (hasAny && row == table->rowCount() - 1) {
            const int newRow = table->rowCount();
            {
                QSignalBlocker blocker(table);
                table->insertRow(newRow);
                for (int c = 0; c < table->columnCount(); ++c) {
                    table->setItem(newRow, c, new QTableWidgetItem());
                }
                if (isParts) {
                    if (auto* dateItem = table->item(newRow, 0)) dateItem->setText(currentOrderDateDisplay());
                    if (auto* amountItem = table->item(newRow, 5)) amountItem->setFlags(amountItem->flags() & ~Qt::ItemIsEditable);
                }
            }
            updateNumbers();
            updateSingleBatchRowHeight(table, newRow);
        }
    });

    class EnterFilter final : public QObject {
    public:
        explicit EnterFilter(QTableWidget* t) : QObject(t), table(t) {}
    protected:
        bool eventFilter(QObject* obj, QEvent* ev) override {
            if ((ev->type() == QEvent::KeyPress) && table) {
                auto* ke = static_cast<QKeyEvent*>(ev);
                const int key = ke->key();
                if (key == Qt::Key_Return || key == Qt::Key_Enter) {
                    ExcelNavigationStateMachineRequest request;
                    request.table = table;
                    request.fromIndex = makePersistentCellIndex(table, table->currentRow(), table->currentColumn());
                    request.key = key;
                    request.source = QStringLiteral("batch_table");
                    request.activateNextEditor = true;
                    request.commitCurrentCell = [this]() -> bool {
                        if (!table || !table->property("inlineEditorActive").toBool()) return true;
                        return flushInlineEditorValueToModel(table);
                    };
                    runExcelNavigationStateMachine(request);
                    return true;
                }
            }
            return QObject::eventFilter(obj, ev);
        }
        QTableWidget* table;
    };
    table->installEventFilter(new EnterFilter(table));
    QObject::connect(table, &QTableWidget::cellClicked, &dlg, [table](int row, int col) {
        const QPoint clickPos = table->viewport()->mapFromGlobal(QCursor::pos());
        scheduleDelayedActivateEditor(table, row, col, clickPos);
    });

    root->addWidget(table, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, [&dlg, table]() {
        flushInlineEditorValueToModel(table);
        if (QWidget* fw = dlg.focusWidget()) fw->clearFocus();
        table->setFocus();
        flushInlineEditorValueToModel(table);
        dlg.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    root->addWidget(buttons);
    if (dlg.exec() != QDialog::Accepted) return {};

    QList<QStringList> out;
    for (int r = 0; r < table->rowCount(); ++r) {
        QStringList row; row.reserve(headers.size());
        bool any = false;
        for (int c = 0; c < table->columnCount(); ++c) {
            QString txt = table->item(r, c) ? table->item(r, c)->text().trimmed() : QString();
            if (!txt.isEmpty()) any = true;
            row << txt;
        }
        if (!any) continue;
        if (row.size() == 3) {
            row[0] = normalizeSentence(row[0]);
            row[2] = normalizePartSentence(row[2]);
        } else if (row.size() == 8) {
            row[0] = row[0].trimmed().isEmpty() ? currentOrderDateDisplay() : row[0].trimmed();
            row[2] = normalizePartSentence(row[2]);
            row[6] = normalizeSentence(row[6]);
            row[7] = normalizeSentence(row[7]);
            bool okQty = false, okPrice = false;
            const double qty = parseLooseNumber(row[3].trimmed(), &okQty);
            const double price = parseLooseNumber(row[4].trimmed(), &okPrice);
            row[5] = moneyText(FormatUtils::roundMoneyValue((okQty && qty > 0.0 ? qty : 1.0) * (okPrice && price >= 0.0 ? price : 0.0)));
        }
        out << row;
    }
    return out;
}
