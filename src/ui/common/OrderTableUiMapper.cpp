#include "ui/common/OrderTableUiMapper.h"

#include <QAbstractItemModel>
#include <QTableView>
#include <QScrollBar>

namespace {

int modelColumnByHeader(const QAbstractItemModel* model, const QString& header)
{
    if (!model) return -1;
    for (int col = 0; col < model->columnCount(); ++col) {
        if (model->headerData(col, Qt::Horizontal).toString().trimmed() == header) return col;
    }
    return -1;
}

void hideInternalColumns(QTableView* table, const QAbstractItemModel* model)
{
    if (!table || !model) return;
    for (int col = 0; col < model->columnCount(); ++col) {
        if (model->headerData(col, Qt::Horizontal).toString().trimmed().startsWith(QStringLiteral("__"))) {
            table->setColumnHidden(col, true);
        }
    }
}

} // namespace

namespace OrderTableUiMapper {

ListPosition capturePosition(QTableView* table)
{
    ListPosition position{{}, table->verticalScrollBar()->value(), table->horizontalScrollBar()->value()};
    const int uidColumn = modelColumnByHeader(table->model(), QStringLiteral("__order_uid"));
    const int numberColumn = modelColumnByHeader(table->model(), QStringLiteral("__internal_number"));
    const auto rows = table->selectionModel()->selectedRows();
    for (const auto& row : rows) {
        const auto uid = row.siblingAtColumn(uidColumn).data().toString().trimmed();
        const int number = row.siblingAtColumn(numberColumn).data().toInt();
        if (!uid.isEmpty() || number > 0)
            position.orders.append({uid, number, row.row() == table->currentIndex().row()});
    }
    return position;
}

void restorePosition(QTableView* table, const ListPosition& position,
                     const std::function<QModelIndex(const SelectedOrder&)>& findRow)
{
    QItemSelection selection;
    QModelIndex current;
    const int lastColumn = table->model()->columnCount() - 1;
    for (const auto& order : position.orders) {
        const auto row = findRow(order);
        if (!row.isValid()) continue;
        selection.select(row, row.siblingAtColumn(lastColumn));
        if (order.current) current = row;
    }
    table->selectionModel()->setCurrentIndex(current, QItemSelectionModel::NoUpdate);
    table->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
    table->verticalScrollBar()->setValue(position.verticalScroll);
    table->horizontalScrollBar()->setValue(position.horizontalScroll);
}

void applyCurrentOrdersModelShape(QTableView* table, const QAbstractItemModel* model)
{
    if (!table || !model) return;
    hideInternalColumns(table, model);

    const int partsCol = modelColumnByHeader(model, QString::fromUtf8("Запчастини"));
    if (partsCol >= 0) {
        table->setColumnHidden(partsCol, false);
        table->viewport()->update();
    }
}

void applyHistoryModelShape(QTableView* table, const QAbstractItemModel* model)
{
    if (!table || !model) return;
    hideInternalColumns(table, model);
}

} // namespace OrderTableUiMapper
