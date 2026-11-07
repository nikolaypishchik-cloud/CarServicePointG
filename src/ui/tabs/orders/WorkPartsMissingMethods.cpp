#include "ui/common/ContextMenuAppearance.h"
#include "ui/tabs/orders/WorkPartsDialog.h"
#include <QMenu>
#include <QTableWidget>
#include <QHeaderView>
#include <QSignalBlocker>
#include <QAction>
#include <QColor>
#include <QBrush>
#include <QApplication>
#include <QClipboard>
#include <QStringList>
#include <QMap>
#include <QPointer>
#include <QTimer>

namespace {
constexpr int ServiceColIndex = 0;
constexpr int ServiceColTitle = 1;
constexpr int ServiceColPrice = 2;
constexpr int ServiceColQty = 3;
constexpr int ServiceColMaster = 4;
constexpr int ServiceColAttachment = 5;
constexpr int PartColIndex = 0;
constexpr int PartColDelivered = 1;
constexpr int PartColOrderedAt = 2;
constexpr int PartColOem = 3;
constexpr int PartColName = 4;
constexpr int PartColQty = 5;
constexpr int PartColPrice = 6;
constexpr int PartColAmount = 7;
constexpr int PartColStore = 8;
constexpr int PartColBrand = 9;
constexpr int PartColInvoiceNumber = 10;
constexpr int PartColAttachment = 11;
constexpr int RoleIsHint = Qt::UserRole + 2;
constexpr int RoleViewportFill = Qt::UserRole + 7;

static bool itemHasRealText(QTableWidgetItem* item)
{
    if (!item) return false;
    if (item->data(RoleIsHint).toBool() || item->data(RoleViewportFill).toBool()) return false;
    return !item->text().simplified().trimmed().isEmpty();
}

static bool serviceRowHasData(QTableWidget* table, int row)
{
    return table && row >= 0 && row < table->rowCount() && itemHasRealText(table->item(row, ServiceColTitle));
}

static bool partRowHasData(QTableWidget* table, int row)
{
    return table && row >= 0 && row < table->rowCount() && itemHasRealText(table->item(row, PartColName));
}

static QTableWidgetItem* makeCenteredEditableItem()
{
    auto* item = new QTableWidgetItem();
    item->setTextAlignment(Qt::AlignCenter);
    return item;
}

static QTableWidgetItem* makeTextItem(const QString& text = QString())
{
    auto* item = new QTableWidgetItem(text);
    return item;
}

static void stylePopupMenu(QMenu& menu)
{
    ContextMenuAppearance::apply(menu);
}

static bool isProtectedColumn(QTableWidget* table, int column)
{
    if (!table || column < 0 || column >= table->columnCount()) return true;
    if (table->columnCount() == 6) {
        return column == ServiceColIndex || column == ServiceColAttachment;
    }
    if (table->columnCount() >= 12) {
        return column >= 12 || column == PartColIndex || column == PartColDelivered || column == PartColAmount || column == PartColAttachment;
    }
    return false;
}

static bool isCellEditableForClipboard(QTableWidget* table, int row, int column)
{
    if (!table || row < 0 || column < 0 || row >= table->rowCount() || column >= table->columnCount()) return false;
    if (isProtectedColumn(table, column)) return false;
    auto* item = table->item(row, column);
    if (!item) return false;
    if (item->data(RoleViewportFill).toBool()) return false;
    return (item->flags() & Qt::ItemIsEditable);
}

static QString normalizedClipboardText(const QString& text)
{
    QString out = text;
    out.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    out.replace(QChar('\r'), QChar('\n'));
    return out;
}

static QString clipboardTextForCell(QTableWidget* table, int row, int col)
{
    if (!table || row < 0 || col < 0 || row >= table->rowCount() || col >= table->columnCount()) return QString();
    if (auto* item = table->item(row, col)) {
        return item->data(RoleIsHint).toBool() ? QString() : item->text();
    }
    return QString();
}

static QList<QModelIndex> selectedIndexesSorted(QTableWidget* table)
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

static void copyCurrentCellToClipboard(QTableWidget* table)
{
    if (!table) return;
    const QList<QModelIndex> indexes = selectedIndexesSorted(table);
    if (!indexes.isEmpty()) {
        QMap<int, QMap<int, QString>> grid;
        QList<int> rows;
        QList<int> cols;
        for (const QModelIndex& idx : indexes) {
            if (!rows.contains(idx.row())) rows.append(idx.row());
            if (!cols.contains(idx.column())) cols.append(idx.column());
            grid[idx.row()][idx.column()] = clipboardTextForCell(table, idx.row(), idx.column());
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
    QGuiApplication::clipboard()->setText(clipboardTextForCell(table, row, col));
}

static void clearCurrentCellValue(QTableWidget* table)
{
    if (!table) return;
    QList<QModelIndex> indexes = selectedIndexesSorted(table);
    if (indexes.isEmpty()) {
        const int row = table->currentRow();
        const int col = table->currentColumn();
        if (row >= 0 && col >= 0) indexes.append(table->model()->index(row, col));
    }
    if (indexes.isEmpty()) return;
    if (QAbstractItemModel* model = table->model()) {
        for (const QModelIndex& idx : indexes) {
            if (!isCellEditableForClipboard(table, idx.row(), idx.column())) continue;
            model->setData(idx, QString(), Qt::EditRole);
            model->setData(idx, QString(), Qt::DisplayRole);
            if (table->columnCount() >= 12 && idx.column() == PartColInvoiceNumber) {
                if (auto* invoiceItem = table->item(idx.row(), idx.column())) {
                    invoiceItem->setData(Qt::UserRole + 13, QString());
                }
                if (auto* attachmentItem = table->item(idx.row(), PartColAttachment)) {
                    attachmentItem->setData(Qt::UserRole + 13, QString());
                }
            }
        }
    }
}

static void cutCurrentCellToClipboard(QTableWidget* table)
{
    if (!table) return;
    copyCurrentCellToClipboard(table);
    clearCurrentCellValue(table);
}

static void pasteClipboardIntoTable(QTableWidget* table)
{
    if (!table) return;
    const int startRow = table->currentRow();
    const int startCol = table->currentColumn();
    if (startRow < 0 || startCol < 0) return;

    const QString raw = normalizedClipboardText(QGuiApplication::clipboard()->text());
    if (raw.isEmpty()) return;
    const QStringList rows = raw.split(QChar('\n'));
    for (int r = 0; r < rows.size(); ++r) {
        if (rows.at(r).isEmpty() && r == rows.size() - 1) continue;
        const int targetRow = startRow + r;
        if (targetRow < 0 || targetRow >= table->rowCount()) break;
        const QStringList cols = rows.at(r).split('	');
        for (int c = 0; c < cols.size(); ++c) {
            const int targetCol = startCol + c;
            if (!isCellEditableForClipboard(table, targetRow, targetCol)) continue;
            if (QAbstractItemModel* model = table->model()) {
                const QModelIndex idx = model->index(targetRow, targetCol);
                const QString value = cols.at(c);
                model->setData(idx, value, Qt::EditRole);
                model->setData(idx, value, Qt::DisplayRole);
                if (table->columnCount() >= 12 && targetCol == PartColInvoiceNumber) {
                    if (auto* invoiceItem = table->item(targetRow, targetCol)) {
                        invoiceItem->setData(Qt::UserRole + 13, value.trimmed());
                    }
                    if (auto* attachmentItem = table->item(targetRow, PartColAttachment)) {
                        attachmentItem->setData(Qt::UserRole + 13, value.trimmed());
                    }
                }
            }
        }
    }
    table->viewport()->update();
}
}

void WorkPartsDialog::insertEmptyServiceRow(int row)
{
    if (!m_servicesTable) return;
    logWorkPartsDiagnostic(QStringLiteral("row_insert_begin"), QStringLiteral("service"), row);
    if (row < 0) row = m_servicesTable->currentRow() >= 0 ? m_servicesTable->currentRow() + 1 : m_servicesTable->rowCount();
    if (row > m_servicesTable->rowCount()) row = m_servicesTable->rowCount();
    m_servicesTable->insertRow(row);
    for (int c = 0; c < m_servicesTable->columnCount(); ++c) {
        QTableWidgetItem* item = (c == ServiceColIndex) ? makeCenteredEditableItem() : makeTextItem();
        if (c == ServiceColAttachment) item->setText(QString());
        m_servicesTable->setItem(row, c, item);
    }
    if (auto* titleItem = m_servicesTable->item(row, ServiceColTitle)) titleItem->setData(RoleIsHint, false);
    renumberServiceItems();
    bumpWorkPartsTableRevision(QStringLiteral("insert_service_row"));
    logWorkPartsDiagnostic(QStringLiteral("row_insert_end"), QStringLiteral("service"), row);
    m_servicesTable->setCurrentCell(row, ServiceColTitle);
    logWorkPartsDiagnostic(QStringLiteral("editor_open"), QStringLiteral("service"), row, -1, QString(), {{QStringLiteral("column"), ServiceColTitle}, {QStringLiteral("source"), QStringLiteral("insertEmptyServiceRow")}});
    m_servicesTable->editItem(m_servicesTable->item(row, ServiceColTitle));
}

void WorkPartsDialog::insertEmptyPartRow(int row)
{
    if (!m_partsTable) return;
    const bool bulkApply = m_loadingParts || property("csImportApplyingRows").toBool() || m_partsTable->property("csImportApplyingRows").toBool();
    if (!bulkApply) {
        logWorkPartsDiagnostic(QStringLiteral("row_insert_begin"), QStringLiteral("part"), row);
    }
    if (row < 0) row = m_partsTable->currentRow() >= 0 ? m_partsTable->currentRow() + 1 : m_partsTable->rowCount();
    if (row > m_partsTable->rowCount()) row = m_partsTable->rowCount();
    m_partsTable->insertRow(row);
    for (int c = 0; c < m_partsTable->columnCount(); ++c) {
        QTableWidgetItem* item = (c == PartColIndex) ? makeCenteredEditableItem() : makeTextItem();
        if (c == PartColAttachment) item->setText(QString());
        m_partsTable->setItem(row, c, item);
    }
    if (auto* nameItem = m_partsTable->item(row, PartColName)) nameItem->setData(RoleIsHint, false);
    if (!bulkApply) {
        renumberPartItems();
    }
    bumpWorkPartsTableRevision(bulkApply ? QStringLiteral("insert_part_row_bulk") : QStringLiteral("insert_part_row"));
    if (!bulkApply) {
        logWorkPartsDiagnostic(QStringLiteral("row_insert_end"), QStringLiteral("part"), row);
        m_partsTable->setCurrentCell(row, PartColName);
        logWorkPartsDiagnostic(QStringLiteral("editor_open"), QStringLiteral("part"), row, -1, QString(), {{QStringLiteral("column"), PartColName}, {QStringLiteral("source"), QStringLiteral("insertEmptyPartRow")}});
        m_partsTable->editItem(m_partsTable->item(row, PartColName));
    }
}

void WorkPartsDialog::renumberServiceItems()
{
    if (!m_servicesTable) return;
    int visibleNumber = 1;
    for (int r = 0; r < m_servicesTable->rowCount(); ++r) {
        if (!m_servicesTable->item(r, ServiceColIndex)) m_servicesTable->setItem(r, ServiceColIndex, makeCenteredEditableItem());
        auto* indexItem = m_servicesTable->item(r, ServiceColIndex);
        indexItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        indexItem->setFlags(indexItem->flags() & ~Qt::ItemIsEditable);
        indexItem->setText(serviceRowHasData(m_servicesTable, r) ? QString::number(visibleNumber++) : QString());
        if (auto* attachmentItem = m_servicesTable->item(r, ServiceColAttachment)) {
            const bool hasAttachment = !attachmentItem->data(Qt::UserRole + 9).toString().trimmed().isEmpty()
                || !attachmentItem->data(Qt::UserRole + 11).toString().trimmed().isEmpty();
            if (serviceRowHasData(m_servicesTable, r) || hasAttachment) {
                attachmentItem->setText(QString::fromUtf8("📎"));
                attachmentItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
                if (!hasAttachment) attachmentItem->setToolTip(QString::fromUtf8("Натисніть, щоб додати фото"));
            } else {
                attachmentItem->setText(QString());
                attachmentItem->setToolTip(QString::fromUtf8("Натисніть, щоб додати фото"));
                attachmentItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            }
        }
    }
}

void WorkPartsDialog::renumberPartItems()
{
    if (!m_partsTable) return;
    int visibleNumber = 1;
    for (int r = 0; r < m_partsTable->rowCount(); ++r) {
        if (!m_partsTable->item(r, PartColIndex)) m_partsTable->setItem(r, PartColIndex, makeCenteredEditableItem());
        auto* indexItem = m_partsTable->item(r, PartColIndex);
        indexItem->setTextAlignment(Qt::AlignCenter);
        indexItem->setFlags(indexItem->flags() & ~Qt::ItemIsEditable);
        indexItem->setText(partRowHasData(m_partsTable, r) ? QString::number(visibleNumber++) : QString());
        if (auto* attachmentItem = m_partsTable->item(r, PartColAttachment)) {
            const bool hasAttachment = !attachmentItem->data(Qt::UserRole + 9).toString().trimmed().isEmpty()
                || !attachmentItem->data(Qt::UserRole + 11).toString().trimmed().isEmpty()
                || !attachmentItem->data(Qt::UserRole + 12).toString().trimmed().isEmpty()
                || !attachmentItem->data(Qt::UserRole + 13).toString().trimmed().isEmpty();
            if (!partRowHasData(m_partsTable, r) && !hasAttachment) {
                attachmentItem->setText(QString());
                attachmentItem->setToolTip(QString::fromUtf8("Натисніть, щоб додати файл"));
            }
            attachmentItem->setTextAlignment(Qt::AlignCenter);
        }
    }
}

void WorkPartsDialog::showServicesMenu(const QPoint& pos)
{
    if (!m_servicesTable) return;
    QPoint viewportPos = pos;
    if (m_servicesTable->viewport()) {
        viewportPos = m_servicesTable->viewport()->mapFromGlobal(QCursor::pos());
    }
    if (!m_servicesTable->viewport() || !m_servicesTable->viewport()->rect().contains(viewportPos)) {
        if (QObject* sourceObj = sender()) {
            if (auto* sourceWidget = qobject_cast<QWidget*>(sourceObj)) {
                if (m_servicesTable->viewport() && sourceWidget != m_servicesTable->viewport()) {
                    viewportPos = m_servicesTable->viewport()->mapFrom(sourceWidget, pos);
                }
            }
        }
    }
    const QModelIndex index = m_servicesTable->indexAt(viewportPos);
    if (index.isValid()) {
        m_servicesTable->setCurrentCell(index.row(), index.column(),
                                        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Current);
    }

    QMenu menu(m_servicesTable);
    stylePopupMenu(menu);
    QAction* copyAct = menu.addAction(QString::fromUtf8("Копіювати"));
    QAction* cutAct = menu.addAction(QString::fromUtf8("Вирізати"));
    QAction* pasteAct = menu.addAction(QString::fromUtf8("Вставити"));
    menu.addSeparator();
    QAction* addBelow = menu.addAction(QString::fromUtf8("Додати рядок нижче"));
    QAction* del = menu.addAction(QString::fromUtf8("Видалити рядок"));
    const int currentRow = m_servicesTable->currentRow();
    const int currentCol = m_servicesTable->currentColumn();
    cutAct->setEnabled(isCellEditableForClipboard(m_servicesTable, currentRow, currentCol));
    pasteAct->setEnabled(currentRow >= 0 && currentCol >= 0);
    del->setEnabled(currentRow >= 0 && serviceRowHasData(m_servicesTable, currentRow));
    QAction* picked = menu.exec((m_servicesTable->viewport() ? m_servicesTable->viewport() : m_servicesTable)->mapToGlobal(viewportPos));
    if (picked == copyAct) copyCurrentCellToClipboard(m_servicesTable);
    else if (picked == cutAct) cutCurrentCellToClipboard(m_servicesTable);
    else if (picked == pasteAct) pasteClipboardIntoTable(m_servicesTable);
    else if (picked == addBelow) insertEmptyServiceRow(currentRow >= 0 ? currentRow + 1 : -1);
    else if (picked == del) deleteActiveService();
    renumberServiceItems();
}

void WorkPartsDialog::showPartsMenu(const QPoint& pos)
{
    if (!m_partsTable) return;
    QPoint viewportPos = pos;
    if (m_partsTable->viewport()) {
        viewportPos = m_partsTable->viewport()->mapFromGlobal(QCursor::pos());
    }
    if (!m_partsTable->viewport() || !m_partsTable->viewport()->rect().contains(viewportPos)) {
        if (QObject* sourceObj = sender()) {
            if (auto* sourceWidget = qobject_cast<QWidget*>(sourceObj)) {
                if (m_partsTable->viewport() && sourceWidget != m_partsTable->viewport()) {
                    viewportPos = m_partsTable->viewport()->mapFrom(sourceWidget, pos);
                }
            }
        }
    }
    const QModelIndex index = m_partsTable->indexAt(viewportPos);
    if (index.isValid()) {
        m_partsTable->setCurrentCell(index.row(), index.column(),
                                     QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Current);
    }

    QMenu menu(m_partsTable);
    stylePopupMenu(menu);
    QAction* copyAct = menu.addAction(QString::fromUtf8("Копіювати"));
    QAction* cutAct = menu.addAction(QString::fromUtf8("Вирізати"));
    QAction* pasteAct = menu.addAction(QString::fromUtf8("Вставити"));
    menu.addSeparator();
    QAction* addBelow = menu.addAction(QString::fromUtf8("Додати рядок нижче"));
    QAction* del = menu.addAction(QString::fromUtf8("Видалити рядок"));
    const int currentRow = m_partsTable->currentRow();
    const int currentCol = m_partsTable->currentColumn();
    cutAct->setEnabled(isCellEditableForClipboard(m_partsTable, currentRow, currentCol));
    pasteAct->setEnabled(currentRow >= 0 && currentCol >= 0);
    del->setEnabled(currentRow >= 0 && partRowHasData(m_partsTable, currentRow));
    QAction* picked = menu.exec((m_partsTable->viewport() ? m_partsTable->viewport() : m_partsTable)->mapToGlobal(viewportPos));
    if (picked == copyAct) copyCurrentCellToClipboard(m_partsTable);
    else if (picked == cutAct) cutCurrentCellToClipboard(m_partsTable);
    else if (picked == pasteAct) pasteClipboardIntoTable(m_partsTable);
    else if (picked == addBelow) insertEmptyPartRow(currentRow >= 0 ? currentRow + 1 : -1);
    else if (picked == del) {
        QPointer<WorkPartsDialog> guard(this);
        QTimer::singleShot(0, this, [guard]() {
            if (!guard) return;
            guard->deleteActivePart();
        });
        return;
    }
    renumberPartItems();
}
