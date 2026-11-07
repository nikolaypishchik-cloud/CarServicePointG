#include "ui/common/TabularSnapshotTableModel.h"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QMetaType>

TabularSnapshotTableModel::TabularSnapshotTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int TabularSnapshotTableModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_snapshot.rows.size();
}

int TabularSnapshotTableModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_snapshot.headers.size();
}

QVariant TabularSnapshotTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) return QVariant();
    if (index.row() < 0 || index.row() >= m_snapshot.rows.size()) return QVariant();
    const auto& row = m_snapshot.rows.at(index.row());
    if (index.column() < 0 || index.column() >= row.size()) return QVariant();

    const QVariant value = row.at(index.column());
    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
        return value;
    case Qt::ToolTipRole:
        return value.toString();
    case Qt::TextAlignmentRole: {
        const int typeId = value.userType();
        const bool numeric = typeId == QMetaType::Int
            || typeId == QMetaType::UInt
            || typeId == QMetaType::LongLong
            || typeId == QMetaType::ULongLong
            || typeId == QMetaType::Double
            || typeId == QMetaType::Float;
        return int((numeric ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter);
    }
    default:
        return QVariant();
    }
}

QVariant TabularSnapshotTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }
    if (section < 0 || section >= m_snapshot.headers.size()) return QVariant();
    return m_snapshot.headers.at(section);
}

void TabularSnapshotTableModel::setSnapshot(const TabularReadModel& snapshot)
{
    beginResetModel();
    m_snapshot = snapshot;
    rebuildHeaderIndex();
    m_firstRowByColumnValue.clear();
    endResetModel();
}

void TabularSnapshotTableModel::clearSnapshot()
{
    setSnapshot(TabularReadModel{});
}

int TabularSnapshotTableModel::columnByHeader(const QString& header) const
{
    return m_headerIndex.value(header.trimmed(), -1);
}

int TabularSnapshotTableModel::rowByColumnValue(const QString& header, const QVariant& value) const
{
    const QString normalizedHeader = header.trimmed();
    const int column = columnByHeader(normalizedHeader);
    if (column < 0) return -1;

    auto cacheIt = m_firstRowByColumnValue.find(normalizedHeader);
    if (cacheIt == m_firstRowByColumnValue.end()) {
        QHash<QString, int> rowByValue;
        rowByValue.reserve(m_snapshot.rows.size());
        for (int row = 0; row < m_snapshot.rows.size(); ++row) {
            const auto& sourceRow = m_snapshot.rows.at(row);
            if (column < 0 || column >= sourceRow.size()) continue;
            const QString key = sourceRow.at(column).toString().trimmed();
            if (!key.isEmpty() && !rowByValue.contains(key)) {
                rowByValue.insert(key, row);
            }
        }
        cacheIt = m_firstRowByColumnValue.insert(normalizedHeader, rowByValue);
    }
    return cacheIt.value().value(value.toString().trimmed(), -1);
}

void TabularSnapshotTableModel::rebuildHeaderIndex()
{
    m_headerIndex.clear();
    for (int col = 0; col < m_snapshot.headers.size(); ++col) {
        m_headerIndex.insert(m_snapshot.headers.at(col).trimmed(), col);
    }
}
