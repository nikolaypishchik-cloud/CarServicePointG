#pragma once

#include "core/services/PartsQueryService.h"

#include <QAbstractTableModel>
#include <QDate>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

namespace PartsPageTableModelAdapter {

struct ViewPolicy
{
    QString textFilter;
    QDate from;
    QDate to;
    QDate today;
    bool paymentActionsEnabled = true;

    bool hasTextFilter() const { return !textFilter.trimmed().isEmpty(); }
};

struct TableRow
{
    int id = 0;
    int searchRank = 0;
    int orderNumber = 0;
    int overdueSeverity = 0;
    bool overdue = false;
    bool paid = false;
    bool partiallyPaid = false;
    bool paymentActionsEnabled = true;
    bool installed = false;
    double qty = 0.0;
    double price = 0.0;
    double amount = 0.0;

    QString dateDisplay;
    QString deliveryDateDisplay;
    QString statusDisplay;
    QString storeDisplay;
    QString article;
    QString brand;
    QString name;
    QString qtyDisplay;
    QString priceDisplay;
    QString amountDisplay;
    QString orderDisplay;
    QString orderUid;
    QString autoDisplay;
    QString plate;
    QString carModel;
    QString vin;
    QString deliveryDateRaw;
    QString installedAtRaw;
};

struct AdaptedRows
{
    QVector<PartRowSnapshot> snapshots;
    QVector<TableRow> rows;
};


class PartsArchiveTableModel final : public QAbstractTableModel
{
public:
    explicit PartsArchiveTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    void setRows(const QVector<TableRow>& rows);
    bool replaceRow(int row, const TableRow& rowData);
    [[nodiscard]] const QVector<TableRow>& rows() const { return m_rows; }
    [[nodiscard]] const TableRow* rowAt(int row) const;
    [[nodiscard]] int rowByPartId(int partId) const;
    [[nodiscard]] int firstRowByOrderUid(const QString& orderUid) const;
    [[nodiscard]] int firstRowByOrderNumber(int orderNumber) const;
    [[nodiscard]] static QStringList headers();

private:
    void rebuildIndexes();

    QVector<TableRow> m_rows;
    QHash<int, int> m_rowByPartId;
    QHash<QString, int> m_firstRowByOrderUid;
    QHash<int, int> m_firstRowByOrderNumber;
};

AdaptedRows build(const QVector<PartRowSnapshot>& inputRows,
                  const ViewPolicy& policy);

TableRow adaptRow(const PartRowSnapshot& rowData,
                  const QDate& today = QDate::currentDate(),
                  bool paymentActionsEnabled = true);

AdaptedRows build(const QVector<PartRowSnapshot>& inputRows,
                  bool hasTextFilter,
                  const QDate& today = QDate::currentDate());

QString paidDisplayText(bool paid);

} // namespace PartsPageTableModelAdapter
