#pragma once

#include "core/services/ReportsDataLoader.h"

#include <QAbstractTableModel>
#include <QVariant>
#include <QMap>
#include <QColor>
#include <QVector>

class QTableView;

namespace ReportsTableSupport {

class ReportsSnapshotTableModel final : public QAbstractTableModel
{
public:
    struct Cell {
        QString text;
        QVariant userValue;
        QString tooltip;
        int alignment = int(Qt::AlignLeft | Qt::AlignVCenter);
        bool bold = false;
        bool link = false;
        QColor foreground;
        QMap<int,QVariant> extraRoles;
    };

    struct Row {
        QVector<Cell> cells;
        bool spanAllColumns = false;
        QColor background;
    };

    explicit ReportsSnapshotTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void setSnapshot(const QStringList& headers, const QVector<Row>& rows);
    void setMessageRow(const QString& text);
    void clearSnapshot();

    [[nodiscard]] QString cellText(int row, int column) const;
    [[nodiscard]] QVariant cellUserValue(int row, int column) const;
    [[nodiscard]] bool rowSpansAllColumns(int row) const;
    [[nodiscard]] const QStringList& headers() const { return m_headers; }
    [[nodiscard]] const QVector<int>& spanRows() const { return m_spanRows; }

private:
    QStringList m_headers;
    QVector<Row> m_rows;
    QVector<int> m_spanRows;
};

void setTableBaseStyle(QTableView* table);
void initializePeriodSnapshot(ReportsSnapshotTableModel* model);
void initializeOrdersSnapshot(ReportsSnapshotTableModel* model);
void initializePartsSnapshot(ReportsSnapshotTableModel* model);
void initializeOutsourceSnapshot(ReportsSnapshotTableModel* model);
void initializePayrollSnapshot(ReportsSnapshotTableModel* model);
void initializeTotalExpensesSnapshot(ReportsSnapshotTableModel* model);
void showLoadingRow(QTableView* table, ReportsSnapshotTableModel* model, const QString& text);
void applyPeriodSnapshot(QTableView* table, ReportsSnapshotTableModel* model, const PeriodSnapshot& snapshot);
void applyOrdersSnapshot(QTableView* table, ReportsSnapshotTableModel* model, const OrdersSnapshot& snapshot);
void applyPartsSnapshot(QTableView* table, ReportsSnapshotTableModel* model, const PartsSnapshot& snapshot);
void applyOutsourceSnapshot(QTableView* table, ReportsSnapshotTableModel* model, const OutsourceSnapshot& snapshot);
void applyPayrollSnapshot(QTableView* table, ReportsSnapshotTableModel* model, const PayrollSnapshot& snapshot);
void applyTotalExpensesSnapshot(QTableView* table, ReportsSnapshotTableModel* model, const TotalExpensesSnapshot& snapshot);

} // namespace ReportsTableSupport
