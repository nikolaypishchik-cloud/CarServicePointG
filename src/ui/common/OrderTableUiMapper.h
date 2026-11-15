#pragma once

#include <QModelIndex>
#include <QString>
#include <QVector>
#include <functional>

class QAbstractItemModel;
class QTableView;

namespace OrderTableUiMapper {

struct SelectedOrder {
    QString uid;
    int number;
    bool current;
};

struct ListPosition {
    QVector<SelectedOrder> orders;
    int verticalScroll;
    int horizontalScroll;
};

ListPosition capturePosition(QTableView* table);
void restorePosition(QTableView* table, const ListPosition& position,
                     const std::function<QModelIndex(const SelectedOrder&)>& findRow);

void applyCurrentOrdersModelShape(QTableView* table, const QAbstractItemModel* model);
void applyHistoryModelShape(QTableView* table, const QAbstractItemModel* model);

} // namespace OrderTableUiMapper
