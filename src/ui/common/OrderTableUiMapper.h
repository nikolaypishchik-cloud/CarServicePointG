#pragma once

class QAbstractItemModel;
class QTableView;

namespace OrderTableUiMapper {

void applyCurrentOrdersModelShape(QTableView* table, const QAbstractItemModel* model);
void applyHistoryModelShape(QTableView* table, const QAbstractItemModel* model);

} // namespace OrderTableUiMapper
