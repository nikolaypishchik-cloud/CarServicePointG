#include "ui/common/OrderTableUiMapper.h"

#include <QAbstractItemModel>
#include <QTableView>

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
