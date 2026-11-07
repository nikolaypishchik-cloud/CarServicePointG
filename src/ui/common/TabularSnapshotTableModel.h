#pragma once

#include "core/readmodel/TabularReadModel.h"

#include <QAbstractTableModel>
#include <QHash>
#include <QVariant>

class TabularSnapshotTableModel final : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit TabularSnapshotTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void setSnapshot(const TabularReadModel& snapshot);
    void clearSnapshot();

    [[nodiscard]] int columnByHeader(const QString& header) const;
    [[nodiscard]] int rowByColumnValue(const QString& header, const QVariant& value) const;
    [[nodiscard]] const TabularReadModel& snapshot() const { return m_snapshot; }

private:
    void rebuildHeaderIndex();

    TabularReadModel m_snapshot;
    QHash<QString, int> m_headerIndex;
    mutable QHash<QString, QHash<QString, int>> m_firstRowByColumnValue;
};
