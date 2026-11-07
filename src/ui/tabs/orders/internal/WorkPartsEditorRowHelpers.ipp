// Extracted from src/ui/tabs/orders/WorkPartsInlineEditorController.h for P1/P2 monolith reduction.
// Included exactly once by the original translation unit/header; do not compile separately.

static std::atomic_int g_fontShrinkClampHitCount{0};

QTableWidgetItem* makeTextItem(const QString& text, bool hint)
{
    auto* item = new QTableWidgetItem(text);
    if (hint) {
        QColor c("#dfe6f3");
        c.setAlpha(105);
        item->setForeground(c);
    }
    return item;
}

QTableWidgetItem* makeReadOnlyTextItem(const QString& text,
                                              Qt::Alignment alignment)
{
    auto* item = new QTableWidgetItem(text);
    item->setTextAlignment(alignment);
    item->setFlags((item->flags() | Qt::ItemIsSelectable | Qt::ItemIsEnabled) & ~Qt::ItemIsEditable);
    return item;
}

static QTableWidgetItem* makeSpacerCell()
{
    auto* item = new QTableWidgetItem(QString());
    item->setData(RoleSpacer, true);
    item->setFlags(Qt::NoItemFlags);
    return item;
}

QTableWidgetItem* makeViewportFillCell()
{
    auto* item = makeSpacerCell();
    item->setData(RoleViewportFill, true);
    return item;
}

static bool isViewportFillItem(const QTableWidgetItem* item)
{
    return item && item->data(RoleViewportFill).toBool();
}

bool rowHasHintMarker(QTableWidget* table, int row)
{
    if (!table || row < 0 || row >= table->rowCount()) return false;
    for (int c = 0; c < table->columnCount(); ++c) {
        auto* item = table->item(row, c);
        if (item && item->data(RoleIsHint).toBool()) return true;
    }
    return false;
}


bool rowIsPureViewportFill(QTableWidget* table, int row)
{
    if (!table || row < 0 || row >= table->rowCount()) return false;
    for (int c = 0; c < table->columnCount(); ++c) {
        if (!isViewportFillItem(table->item(row, c))) return false;
    }
    return true;
}

void applyLeftAlignmentToServiceRow(QTableWidget* table, int row)
{
    if (!table || row < 0 || row >= table->rowCount() || table->columnCount() != 6) return;
    for (int col = 0; col < table->columnCount(); ++col) {
        if (auto* item = table->item(row, col)) item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }
}

static void populateServiceInputRow(QTableWidget* table, int row, const QString& masterName)
{
    if (!table || row < 0 || row >= table->rowCount() || table->columnCount() != 6) return;

    table->setItem(row, ServiceColIndex, makePartIndexItem(QString()));

    auto* titleItem = makeTextItem(QString(), true);
    titleItem->setData(RoleIsHint, true);
    titleItem->setData(RoleId, 0);
    table->setItem(row, ServiceColTitle, titleItem);

    auto* priceItem = makeTextItem(QString(), true);
    priceItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    priceItem->setData(RoleIsHint, true);
    table->setItem(row, ServiceColPrice, priceItem);

    auto* qtyItem = makeTextItem(QString(), true);
    qtyItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    qtyItem->setData(RoleIsHint, true);
    table->setItem(row, ServiceColQty, qtyItem);

    auto* masterItem = makeTextItem(masterName.trimmed(), masterName.trimmed().isEmpty());
    masterItem->setData(RoleIsHint, masterName.trimmed().isEmpty());
    masterItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table->setItem(row, ServiceColMaster, masterItem);

    table->setItem(row, ServiceColAttachment, makeAttachmentItem(QString()));
    applyLeftAlignmentToServiceRow(table, row);
}

static bool isMainOrderPartsTable(const QTableWidget* table)
{
    return table && table->objectName() == QStringLiteral("orderPartsTable");
}

static bool isMainOrderServicesTable(const QTableWidget* table)
{
    return table && table->objectName() == QStringLiteral("orderServicesTable");
}

QString importedPartTraceId(QTableWidget* table, int row)
{
    if (!table || row < 0 || row >= table->rowCount()) return QString();
    for (int col : {PartColName, PartColOrderedAt, PartColOem, PartColQty, PartColPrice, PartColStore, PartColBrand, PartColInvoiceNumber, PartColAttachment}) {
        if (auto* item = table->item(row, col)) {
            const QString trace = item->data(RoleImportedInvoiceTrace).toString().trimmed();
            if (!trace.isEmpty()) return trace;
        }
    }
    return QString();
}


static void populatePartInputRow(QTableWidget* table, int row)
{
    if (!table || row < 0 || row >= table->rowCount()) return;
    if (!isMainOrderPartsTable(table) || table->columnCount() < 12) return;

    table->setItem(row, PartColIndex, makePartIndexItem(QString()));

    auto* deliveredItem = makePartDeliveredItem(false, 0, false);
    deliveredItem->setData(RoleDeliveryDate, QString());
    table->setItem(row, PartColDelivered, deliveredItem);

    auto* dateItem = makeTextItem(QString(), true);
    dateItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    dateItem->setData(RoleIsHint, true);
    table->setItem(row, PartColOrderedAt, dateItem);

    auto* oemItem = makeTextItem(QString(), true);
    oemItem->setData(RoleIsHint, true);
    table->setItem(row, PartColOem, oemItem);

    auto* nameItem = makeTextItem(QString(), true);
    nameItem->setData(RoleIsHint, true);
    nameItem->setData(RoleId, 0);
    table->setItem(row, PartColName, nameItem);

    auto* qtyItem = makeTextItem(QString(), true);
    qtyItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    qtyItem->setData(RoleIsHint, true);
    table->setItem(row, PartColQty, qtyItem);

    auto* priceItem = makeTextItem(QString(), true);
    priceItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    priceItem->setData(RoleIsHint, true);
    table->setItem(row, PartColPrice, priceItem);

    auto* amountItem = makeReadOnlyTextItem(QString(), Qt::AlignLeft | Qt::AlignVCenter);
    amountItem->setData(RoleIsHint, true);
    {
        QColor c("#dfe6f3");
        c.setAlpha(105);
        amountItem->setForeground(c);
    }
    amountItem->setFlags(amountItem->flags() & ~Qt::ItemIsEditable);
    amountItem->setData(RoleIsHint, true);
    table->setItem(row, PartColAmount, amountItem);

    auto* storeItem = makeTextItem(QString(), true);
    storeItem->setData(RoleIsHint, true);
    table->setItem(row, PartColStore, storeItem);

    auto* brandItem = makeTextItem(QString(), true);
    brandItem->setData(RoleIsHint, true);
    table->setItem(row, PartColBrand, brandItem);

    auto* invoiceItem = makeInvoiceNumberItem();
    invoiceItem->setData(RoleIsHint, true);
    table->setItem(row, PartColInvoiceNumber, invoiceItem);

    table->setItem(row, PartColAttachment, makeAttachmentItem(QString()));
    syncPartManufacturerRoleFromRow(table, row);
    syncPartDeliveredCellState(table, row);
    updateSingleMainPartsRowHeight(table, row);
}

static void ensureNextInputRowExists(QTableWidget* table, int currentRow)
{
    if (!table || currentRow < 0 || currentRow >= table->rowCount()) return;

    const int nextRow = currentRow + 1;
    if (table->columnCount() == 6) {
        for (int candidateRow = nextRow; candidateRow < table->rowCount(); ++candidateRow) {
            if (rowHasHintMarker(table, candidateRow) || rowHasMeaningfulServiceData(table, candidateRow)) return;
            if (rowIsPureViewportFill(table, candidateRow)) {
                populateServiceInputRow(table, candidateRow, QString());
                return;
            }
        }

        table->insertRow(table->rowCount());
        populateServiceInputRow(table, table->rowCount() - 1, QString());
        return;
    }

    if (isMainOrderPartsTable(table)) {
        if (nextRow < table->rowCount()) {
            if (rowHasHintMarker(table, nextRow) || rowHasMeaningfulPartData(table, nextRow)) return;
            if (rowIsPureViewportFill(table, nextRow)) {
                populatePartInputRow(table, nextRow);
                return;
            }
        } else {
            table->insertRow(nextRow);
            populatePartInputRow(table, nextRow);
        }
    }
}

static void activateEditorAt(QTableWidget* table, int row, int col,
                             const QString& openReason,
                             const QPoint& clickPosInViewport);
bool flushInlineEditorValueToModel(QTableWidget* table)
;
static void clearInlineEditorStateIfMatches(QTableWidget* table, int row, int col);
static void clearInlineEditorStateIfMatches(QTableWidget* table, const QPersistentModelIndex& index);
static QWidget* visibleInlineEditorForCell(QTableWidget* table, int row, int col);
static void activateEditorAtPersistentIndex(QTableWidget* table, const QPersistentModelIndex& index,
                                            const QString& openReason,
                                            const QPoint& clickPosInViewport = QPoint());

static constexpr const char* kInlineEditingIndexProperty = "inlineEditingIndex";
static constexpr const char* kEditorSeedIndexProperty = "editorSeedIndex";
static constexpr const char* kEditorWidgetIndexProperty = "csEditorIndex";

static QPersistentModelIndex makePersistentCellIndex(QTableWidget* table, int row, int col)
{
    if (!table || !table->model() || row < 0 || col < 0 || row >= table->rowCount() || col >= table->columnCount()) return {};
    return QPersistentModelIndex(table->model()->index(row, col));
}

static QPersistentModelIndex persistentIndexFromProperty(QObject* owner, const char* propertyName)
{
    if (!owner || !propertyName) return {};
    const QVariant value = owner->property(propertyName);
    if (!value.isValid() || !value.canConvert<QPersistentModelIndex>()) return {};
    return value.value<QPersistentModelIndex>();
}

static bool persistentIndexBelongsToTable(QTableWidget* table, const QPersistentModelIndex& index)
{
    return table && table->model() && index.isValid() && index.model() == table->model()
        && index.row() >= 0 && index.column() >= 0
        && index.row() < table->rowCount() && index.column() < table->columnCount();
}

static constexpr const char* kTableRevisionProperty = "csTableRevision";
static constexpr const char* kTableRevisionTrackerProperty = "csTableRevisionTrackerInstalled";

static quint64 tableRevision(QTableWidget* table)
{
    if (!table) return 0;
    bool ok = false;
    const quint64 revision = table->property(kTableRevisionProperty).toULongLong(&ok);
    return ok ? revision : 0;
}

static quint64 bumpTableRevision(QTableWidget* table, const char* reason = nullptr)
{
    if (!table) return 0;
    const quint64 revision = tableRevision(table) + 1;
    table->setProperty(kTableRevisionProperty, QVariant::fromValue<qulonglong>(revision));
    if (reason) {
        qCDebug(csWorkPartsTraceLog) << "WorkPartsDialog table revision bumped"
                << "table" << (table->objectName().isEmpty() ? QStringLiteral("<unnamed>") : table->objectName())
                << "revision" << revision
                << "reason" << QString::fromLatin1(reason);
    }
    return revision;
}

static QString inlineEditorRowUid(QTableWidget* table, int row)
{
    if (!table || row < 0 || row >= table->rowCount()) return QString();
    const bool isParts = table->columnCount() >= 12 || table->objectName() == QStringLiteral("orderPartsTable");
    const int idColumn = isParts ? PartColName : ServiceColTitle;
    if (auto* item = table->item(row, idColumn)) {
        const int id = item->data(RoleId).toInt();
        if (id > 0) return QStringLiteral("%1:%2").arg(isParts ? QStringLiteral("part") : QStringLiteral("service")).arg(id);
    }
    return QStringLiteral("%1:ui-row:%2").arg(isParts ? QStringLiteral("part") : QStringLiteral("service")).arg(row);
}

static void logInlineEditorDiagnostic(QTableWidget* table,
                                      const QString& operation,
                                      int row,
                                      int col,
                                      const QString& reason,
                                      bool ok = true)
{
    if (!table) return;
    StructuredDiagnosticsLogger::Event event;
    event.module = QStringLiteral("ui.tabs.orders.WorkPartsDialog");
    event.operation = operation;
    event.orderNumber = table->property("cs_order_number").toInt();
    event.partId = table->columnCount() >= 12 ? (table->item(row, PartColName) ? table->item(row, PartColName)->data(RoleId).toInt() : -1) : -1;
    event.rowUid = inlineEditorRowUid(table, row);
    event.invoiceNumber = (table->columnCount() >= 12 && row >= 0 && row < table->rowCount() && table->item(row, PartColInvoiceNumber))
        ? table->item(row, PartColInvoiceNumber)->data(RoleAttachmentDocumentNumber).toString().trimmed()
        : QString();
    event.transactionId = table->property("csActiveTransactionId").toString().trimmed();
    bool okRev = false;
    const qint64 workPartsRevision = table->property("csWorkPartsTableRevision").toLongLong(&okRev);
    event.tableRevision = okRev ? workPartsRevision : static_cast<qint64>(tableRevision(table));
    event.details = {
        {QStringLiteral("table"), table->objectName()},
        {QStringLiteral("row"), row},
        {QStringLiteral("column"), col},
        {QStringLiteral("reason"), reason},
        {QStringLiteral("ok"), ok},
        {QStringLiteral("inlineEditorActive"), table->property("inlineEditorActive").toBool()},
        {QStringLiteral("tableRevision"), static_cast<qint64>(tableRevision(table))}
    };
    StructuredDiagnosticsLogger::log(ok ? StructuredDiagnosticsLogger::Level::Info : StructuredDiagnosticsLogger::Level::Warning, event);
}


static void ensureTableRevisionTracker(QTableWidget* table)
{
    if (!table || !table->model()) return;
    if (table->property(kTableRevisionTrackerProperty).toBool()) return;
    table->setProperty(kTableRevisionTrackerProperty, true);
    if (!table->property(kTableRevisionProperty).isValid()) {
        table->setProperty(kTableRevisionProperty, QVariant::fromValue<qulonglong>(1));
    }

    QPointer<QTableWidget> safeTable(table);
    auto bumpIfAlive = [safeTable](const char* reason) {
        if (!safeTable) return;
        bumpTableRevision(safeTable.data(), reason);
    };

    QAbstractItemModel* model = table->model();
    QObject::connect(model, &QAbstractItemModel::rowsInserted, table,
                     [bumpIfAlive](const QModelIndex&, int, int) { bumpIfAlive("rows_inserted"); });
    QObject::connect(model, &QAbstractItemModel::rowsRemoved, table,
                     [bumpIfAlive](const QModelIndex&, int, int) { bumpIfAlive("rows_removed"); });
    QObject::connect(model, &QAbstractItemModel::modelReset, table,
                     [bumpIfAlive]() { bumpIfAlive("model_reset"); });
    QObject::connect(model, &QAbstractItemModel::layoutChanged, table,
                     [bumpIfAlive]() { bumpIfAlive("layout_changed"); });
    QObject::connect(model, &QAbstractItemModel::dataChanged, table,
                     [bumpIfAlive](const QModelIndex&, const QModelIndex&) { bumpIfAlive("data_changed"); });
}

static quint64 tableRevisionSnapshot(QTableWidget* table)
{
    ensureTableRevisionTracker(table);
    return tableRevision(table);
}

static bool tableRevisionStillCurrent(QTableWidget* table,
                                      quint64 expectedRevision,
                                      const QString& actionName)
{
    ensureTableRevisionTracker(table);
    const quint64 currentRevision = tableRevision(table);
    if (currentRevision == expectedRevision) return true;
    qCDebug(csWorkPartsTraceLog) << "WorkPartsDialog queued inline editor action cancelled because table revision changed"
            << "table" << (table && !table->objectName().isEmpty() ? table->objectName() : QStringLiteral("<unnamed>"))
            << "action" << actionName
            << "expectedRevision" << expectedRevision
            << "currentRevision" << currentRevision;
    return false;
}

static bool queuedEditorActionStillValid(QTableWidget* table,
                                         const QPersistentModelIndex& targetIndex,
                                         quint64 expectedRevision,
                                         const QString& actionName)
{
    return tableRevisionStillCurrent(table, expectedRevision, actionName)
        && persistentIndexBelongsToTable(table, targetIndex);
}

QPersistentModelIndex inlineEditingPersistentIndex(QTableWidget* table)
{
    const QPersistentModelIndex index = persistentIndexFromProperty(table, kInlineEditingIndexProperty);
    return persistentIndexBelongsToTable(table, index) ? index : QPersistentModelIndex();
}

static QPoint inlineEditingCell(QTableWidget* table)
{
    const QPersistentModelIndex index = inlineEditingPersistentIndex(table);
    return index.isValid() ? QPoint(index.row(), index.column()) : QPoint(-1, -1);
}

static void setInlineEditingIndex(QTableWidget* table, const QModelIndex& index)
{
    if (!table) return;
    if (index.isValid() && table->model() && index.model() == table->model()) {
        table->setProperty(kInlineEditingIndexProperty, QVariant::fromValue(QPersistentModelIndex(index)));
    } else {
        table->setProperty(kInlineEditingIndexProperty, QVariant::fromValue(QPersistentModelIndex()));
    }
}

void clearInlineEditingIndex(QTableWidget* table)
{
    if (!table) return;
    table->setProperty(kInlineEditingIndexProperty, QVariant::fromValue(QPersistentModelIndex()));
}

static bool inlineEditingMatches(QTableWidget* table, int row, int col)
{
    const QPersistentModelIndex index = inlineEditingPersistentIndex(table);
    return index.isValid() && index.row() == row && index.column() == col;
}

static void clearEditorSeedState(QTableWidget* table)
{
    if (!table) return;
    table->setProperty(kEditorSeedIndexProperty, QVariant::fromValue(QPersistentModelIndex()));
    table->setProperty("editorSeedText", QString());
    table->setProperty("editorReplaceWithSeed", false);
}

static void setEditorSeedIndex(QTableWidget* table, const QModelIndex& index)
{
    if (!table) return;
    if (index.isValid() && table->model() && index.model() == table->model()) {
        table->setProperty(kEditorSeedIndexProperty, QVariant::fromValue(QPersistentModelIndex(index)));
    } else {
        table->setProperty(kEditorSeedIndexProperty, QVariant::fromValue(QPersistentModelIndex()));
    }
}

static bool takeEditorSeedForIndex(QTableWidget* table, const QModelIndex& index, QString* seedText, bool* replaceWithSeed)
{
    if (!table || !index.isValid()) return false;
    const QPersistentModelIndex seedIndex = persistentIndexFromProperty(table, kEditorSeedIndexProperty);
    if (!persistentIndexBelongsToTable(table, seedIndex) || seedIndex.row() != index.row() || seedIndex.column() != index.column()) return false;
    if (seedText) *seedText = table->property("editorSeedText").toString();
    if (replaceWithSeed) *replaceWithSeed = table->property("editorReplaceWithSeed").toBool();
    clearEditorSeedState(table);
    return true;
}

static void setEditorWidgetIndex(QWidget* editor, const QModelIndex& index)
{
    if (!editor) return;
    if (index.isValid()) {
        editor->setProperty(kEditorWidgetIndexProperty, QVariant::fromValue(QPersistentModelIndex(index)));
    } else {
        editor->setProperty(kEditorWidgetIndexProperty, QVariant::fromValue(QPersistentModelIndex()));
    }
}

static QPersistentModelIndex editorWidgetPersistentIndex(QWidget* editor, QTableWidget* table)
{
    const QPersistentModelIndex index = persistentIndexFromProperty(editor, kEditorWidgetIndexProperty);
    return persistentIndexBelongsToTable(table, index) ? index : QPersistentModelIndex();
}

static bool inlineEditorWidgetBelongsToTable(QTableWidget* table, QWidget* editor)
{
    if (!table || !editor || !table->viewport()) return false;
    for (QWidget* w = editor; w; w = w->parentWidget()) {
        if (w == table->viewport()) return true;
    }
    return false;
}

static void markInlineEditorClosing(QWidget* widget)
{
    if (!widget) return;
    widget->setProperty("csEditorClosing", true);
    if (auto* combo = qobject_cast<QComboBox*>(widget)) {
        combo->hidePopup();
        if (combo->lineEdit()) combo->lineEdit()->setProperty("csEditorClosing", true);
        return;
    }
    if (auto* line = qobject_cast<QLineEdit*>(widget)) {
        for (QObject* p = line->parent(); p; p = p->parent()) {
            if (auto* combo = qobject_cast<QComboBox*>(p)) {
                combo->setProperty("csEditorClosing", true);
                if (combo->lineEdit()) combo->lineEdit()->setProperty("csEditorClosing", true);
                break;
            }
        }
    }
}

void resetInlineEditorStateForTable(QTableWidget* table, const char* reason)
{
    if (!table) return;
    const bool wasActive = table->property("inlineEditorActive").toBool();
    const QPoint oldCell = inlineEditingCell(table);
    const int oldRow = oldCell.x();
    const int oldCol = oldCell.y();
    if (table->viewport()) {
        const auto widgets = table->viewport()->findChildren<QWidget*>(QString(), Qt::FindChildrenRecursively);
        for (QWidget* widget : widgets) {
            if (!widget) continue;
            if (!inlineEditorWidgetBelongsToTable(table, widget)) continue;
            if (!(qobject_cast<QLineEdit*>(widget) || qobject_cast<QComboBox*>(widget))) continue;
            markInlineEditorClosing(widget);
            widget->clearFocus();
        }
        table->viewport()->update();
    }
    table->setProperty("inlineEditorActive", false);
    clearInlineEditingIndex(table);
    table->setProperty("editorOpenReason", QString());
    clearEditorSeedState(table);
    if (wasActive && reason) {
        logInlineEditorDiagnostic(table, QStringLiteral("editor_close"), oldRow, oldCol, QString::fromLatin1(reason), true);
        if (qEnvironmentVariableIntValue("CS_DEEP_DEBUG") > 0) {
            qWarning() << "WorkPartsDialog inline editor state hard reset"
                       << "table" << (table->objectName().isEmpty() ? QStringLiteral("<unnamed>") : table->objectName())
                       << "row" << oldRow
                       << "col" << oldCol
                       << "reason" << QString::fromLatin1(reason);
        }
    }
}

static void logExcelNavigation(QTableWidget* table,
                               const QString& stage,
                               int key,
                               int fromRow,
                               int fromCol,
                               int toRow,
                               int toCol,
                               const QString& extra);

static constexpr const char* kEnsureNextInputRowQueuedProperty = "ensureNextInputRowQueued";
static constexpr const char* kEnsureNextInputRowRetryProperty = "ensureNextInputRowRetryCount";
static constexpr int kEnsureNextInputRowMaxDeferredRetries = 8;

void scheduleDelayedActivateEditor(QTableWidget* table, int row, int col, const QPoint& clickPosInViewport)
{
    if (!table || row < 0 || col < 0) return;
    ensureTableRevisionTracker(table);
    const QPersistentModelIndex targetIndex = makePersistentCellIndex(table, row, col);
    if (!targetIndex.isValid()) return;
    const quint64 queuedRevision = tableRevisionSnapshot(table);
    QPointer<QTableWidget> safeTable(table);
    QTimer::singleShot(0, table, [safeTable, targetIndex, queuedRevision, clickPosInViewport]() {
        if (!safeTable || !safeTable->isVisible()) return;
        if (!queuedEditorActionStillValid(safeTable.data(), targetIndex, queuedRevision, QStringLiteral("deferred_editor_activate"))) return;
        if (safeTable->property("csImportBusy").toBool()) return;
        if (QWidget* popup = QApplication::activePopupWidget()) {
            if (popup != safeTable->viewport()) return;
        }
        const int row = targetIndex.row();
        const int col = targetIndex.column();
        logExcelNavigation(safeTable, QStringLiteral("EDITOR_ACTIVATE"), 0, safeTable->currentRow(), safeTable->currentColumn(), row, col,
                           QStringLiteral(" reason=deferred persistent_index=1 revision=%1").arg(queuedRevision));
        activateEditorAtPersistentIndex(safeTable.data(), targetIndex, QStringLiteral("keyboard_nav"), clickPosInViewport);
    });
}

void scheduleEnsureNextInputRow(QTableWidget* table, int currentRow)
{
    if (!table || currentRow < 0) return;
    ensureTableRevisionTracker(table);
    if (table->property(kEnsureNextInputRowQueuedProperty).toBool()) return;
    table->setProperty(kEnsureNextInputRowQueuedProperty, true);
    table->setProperty(kEnsureNextInputRowRetryProperty, 0);
    const quint64 queuedRevision = tableRevisionSnapshot(table);
    QPointer<QTableWidget> safeTable(table);
    auto runner = std::make_shared<std::function<void()>>();
    *runner = [safeTable, currentRow, queuedRevision, runner]() {
        if (!safeTable) return;
        if (!safeTable->isVisible()) {
            safeTable->setProperty(kEnsureNextInputRowQueuedProperty, false);
            safeTable->setProperty(kEnsureNextInputRowRetryProperty, 0);
            return;
        }
        if (!tableRevisionStillCurrent(safeTable.data(), queuedRevision, QStringLiteral("ensure_next_input_row"))) {
            safeTable->setProperty(kEnsureNextInputRowQueuedProperty, false);
            safeTable->setProperty(kEnsureNextInputRowRetryProperty, 0);
            return;
        }
        if (safeTable->property("inlineEditorActive").toBool()) {
            const int retryCount = safeTable->property(kEnsureNextInputRowRetryProperty).toInt() + 1;
            safeTable->setProperty(kEnsureNextInputRowRetryProperty, retryCount);
            if (retryCount >= kEnsureNextInputRowMaxDeferredRetries) {
                safeTable->setProperty(kEnsureNextInputRowQueuedProperty, false);
                safeTable->setProperty(kEnsureNextInputRowRetryProperty, 0);
                return;
            }
            QTimer::singleShot(16, safeTable, [runner]() { (*runner)(); });
            return;
        }
        safeTable->setProperty(kEnsureNextInputRowRetryProperty, 0);
        const int vPos = safeTable->verticalScrollBar() ? safeTable->verticalScrollBar()->value() : 0;
        const int hPos = safeTable->horizontalScrollBar() ? safeTable->horizontalScrollBar()->value() : 0;
        const QSignalBlocker blocker(safeTable);
        ensureNextInputRowExists(safeTable, currentRow);
        if (safeTable->verticalScrollBar()) safeTable->verticalScrollBar()->setValue(vPos);
        if (safeTable->horizontalScrollBar()) safeTable->horizontalScrollBar()->setValue(hPos);
        safeTable->viewport()->update();
        safeTable->setProperty(kEnsureNextInputRowQueuedProperty, false);
    };
    QTimer::singleShot(0, table, [runner]() { (*runner)(); });
}

QVector<int> logicalNavigationColumns(QTableWidget* table)
{
    if (!table) return {};
    if (table->columnCount() == 6) {
        return {ServiceColTitle, ServiceColPrice, ServiceColQty, ServiceColMaster};
    }
    if (isMainOrderPartsTable(table)) {
        return {PartColOrderedAt, PartColOem, PartColName, PartColQty, PartColPrice, PartColStore, PartColBrand};
    }

    QVector<int> result;
    for (int c = 0; c < table->columnCount(); ++c) {
        auto* item = table->item(qMax(0, table->currentRow()), c);
        if (item && item->flags().testFlag(Qt::ItemIsEditable)) result.push_back(c);
    }
    return result;
}

void removeViewportFillRows(QTableWidget* table)
{
    if (!table) return;
    for (int row = table->rowCount() - 1; row >= 0; --row) {
        bool removeRow = true;
        for (int c = 0; c < table->columnCount(); ++c) {
            auto* item = table->item(row, c);
            if (!isViewportFillItem(item)) {
                removeRow = false;
                break;
            }
        }
        if (removeRow) table->removeRow(row);
    }
}

void setVerticalHeaderLabel(QTableWidget* table, int row, const QString& label)
;

QString glossyActionButtonStyle(bool primary, bool compact)
{
    const QString border = primary ? QStringLiteral("rgba(255,255,255,0.72)") : QStringLiteral("rgba(255,255,255,0.58)");
    const QString text = QStringLiteral("#f8fbff");
    const QString normal = primary
        ? QStringLiteral("qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #5f6d7d, stop:0.12 #8795a5, stop:0.46 #364150, stop:0.52 #121926, stop:1 #0a1019)")
        : QStringLiteral("qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #536170, stop:0.12 #7b8998, stop:0.46 #313b48, stop:0.52 #111825, stop:1 #091018)");
    const QString hover = primary
        ? QStringLiteral("qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #6b7989, stop:0.14 #97a4b2, stop:0.48 #3f4a59, stop:0.54 #182030, stop:1 #0c1320)")
        : QStringLiteral("qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #5f6d7c, stop:0.14 #8b98a5, stop:0.48 #394452, stop:0.54 #151d2b, stop:1 #0b1220)");
    const QString pressed = primary
        ? QStringLiteral("qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #121925, stop:0.45 #1d2838, stop:1 #4a5969)")
        : QStringLiteral("qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #101722, stop:0.45 #1a2533, stop:1 #445262)");

    const int minHeight = compact ? 24 : 40;
    const int radius = compact ? 12 : 16;
    const int fontPx = compact ? 14 : 15;
    const QString padding = compact ? QStringLiteral("4px 20px 5px 20px")
                                    : QStringLiteral("8px 22px 9px 22px");
    const QString pressedPadding = compact ? QStringLiteral("6px 18px 3px 22px")
                                           : QStringLiteral("11px 20px 6px 24px");

    return QString::fromUtf8(
        "QPushButton {"
        " color:%1;"
        " font-family:'Times New Roman'; font-weight:normal;"
        " font-size:%2px;"
        " min-height:%3px;"
        " padding:%4;"
        " border:1px solid %5;"
        " border-radius:%6px;"
        " background:%7;"
        "}"
        "QPushButton:hover { background:%8; border-color: rgba(255,255,255,0.82); }"
        "QPushButton:pressed { background:%9; padding:%10; }"
        "QPushButton:disabled { color: rgba(245,248,252,0.52); border-color: rgba(255,255,255,0.18); background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(90,100,112,0.50), stop:1 rgba(35,41,49,0.44)); }"
    )
    .arg(text)
    .arg(fontPx)
    .arg(minHeight)
    .arg(padding)
    .arg(border)
    .arg(radius)
    .arg(normal)
    .arg(hover)
    .arg(pressed)
    .arg(pressedPadding);
}

QString whiteMasterComboStyle()
{
    return QString::fromUtf8(
        "QComboBox {"
        " background:#ffffff;"
        " color:#080b0f;"
        " border:1px solid rgba(52,63,77,0.92);"
        " border-radius:12px;"
        " padding:4px 34px 4px 12px;"
        " min-height:28px;"
        " selection-background-color: rgba(88,120,166,0.22);"
        " selection-color:#080b0f;"
        "}"
        "QComboBox:hover { border-color: rgba(90,106,126,0.98); }"
        "QComboBox:focus { border:2px solid rgba(93,126,176,0.95); padding:3px 33px 3px 11px; }"
        "QComboBox::drop-down {"
        " subcontrol-origin: padding;"
        " subcontrol-position: top right;"
        " width:28px;"
        " border:none;"
        " border-left:1px solid rgba(52,63,77,0.16);"
        " background:rgba(8,11,15,0.02);"
        " border-top-right-radius:11px;"
        " border-bottom-right-radius:11px;"
        "}"
        "QComboBox QAbstractItemView { background:#ffffff; color:#080b0f; border:1px solid rgba(52,63,77,0.92); selection-background-color: rgba(88,120,166,0.24); selection-color:#080b0f; }"
    );
}

class ArrowComboBox final : public QComboBox
{
public:
    explicit ArrowComboBox(QWidget* parent = nullptr)
        : QComboBox(parent)
    {
