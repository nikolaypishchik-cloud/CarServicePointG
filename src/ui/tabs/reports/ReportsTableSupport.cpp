#include "ui/tabs/reports/ReportsTableSupport.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QFont>
#include <QHeaderView>
#include <QTableView>
#include <QRegularExpression>

namespace {

QString invoiceNumberLabel(QString text)
{
    return text.remove(QRegularExpression(QString::fromUtf8("^\\s*Видаткова\\s+накладна\\s*"), QRegularExpression::CaseInsensitiveOption))
        .remove(QRegularExpression(QString::fromUtf8("^\\s*№\\s*")))
        .remove(QRegularExpression(QString::fromUtf8("\\s+від\\s+\\d{1,2}\\.\\d{1,2}\\.\\d{2,4}\\s*$"), QRegularExpression::CaseInsensitiveOption)).trimmed();
}

QStringList periodHeaders()
{
    return {
        QString::fromUtf8("Дата"), QString::fromUtf8("Наряд"), QString::fromUtf8("Авто"),
        QString::fromUtf8("Майстер"), QString::fromUtf8("Роботи (грн)"),
        QString::fromUtf8("Запчастини (грн)"), QString::fromUtf8("Разом (грн)")
    };
}

QStringList ordersHeaders()
{
    return {
        QString::fromUtf8("Дата"), QString::fromUtf8("Наряд"), QString::fromUtf8("Авто"),
        QString::fromUtf8("Накладні"), QString::fromUtf8("Запчастини, грн"),
        QString::fromUtf8("Дата сплати"), QString::fromUtf8("Сплата")
    };
}

QStringList partsHeaders()
{
    return {
        QString::fromUtf8("Дата"), QString::fromUtf8("Наряд"), QString::fromUtf8("Запчастина"),
        QString::fromUtf8("№ накладної"),
        QString::fromUtf8("Сума закупки"), QString::fromUtf8("Доданий %\n(чистий)"),
        QString::fromUtf8("Кіл-ть"), QString::fromUtf8("Ціна")
    };
}

QStringList outsourceHeaders()
{
    return {
        QString::fromUtf8("Відправлено"), QString::fromUtf8("План повернення"), QString::fromUtf8("Компанія"),
        QString::fromUtf8("Наряд"), QString::fromUtf8("Запчастина"),
        QString::fromUtf8("Сума ремонту"), QString::fromUtf8("Сума клієнта")
    };
}

QStringList payrollHeaders()
{
    return {
        QString::fromUtf8("Співробітник"), QString::fromUtf8("Нарядів"), QString::fromUtf8("База"),
        QString::fromUtf8("%"), QString::fromUtf8("Нараховано")
    };
}

QStringList totalExpensesHeaders()
{
    return {
        QString::fromUtf8("Дата / період"), QString::fromUtf8("Категорія"),
        QString::fromUtf8("Опис витрати"), QString::fromUtf8("Джерело"), QString::fromUtf8("Сума, грн")
    };
}

ReportsTableSupport::ReportsSnapshotTableModel::Cell makeCell(const QString& text,
                                                              int alignment = int(Qt::AlignLeft | Qt::AlignVCenter),
                                                              const QVariant& userValue = QVariant(),
                                                              const QString& tooltip = QString(),
                                                              bool bold = false)
{
    ReportsTableSupport::ReportsSnapshotTableModel::Cell cell;
    cell.text = text;
    cell.alignment = alignment;
    cell.userValue = userValue;
    cell.tooltip = tooltip;
    cell.bold = bold;
    return cell;
}

void finishSnapshotApply(QTableView* table, const ReportsTableSupport::ReportsSnapshotTableModel* model)
{
    if (!table || !model) return;
    table->clearSpans();
    const int colCount = qMax(1, model->columnCount());
    // P1/P2 reports snapshot migration: span application is now index-driven.
    // Large reports no longer scan every report row after each model reset.
    for (int r : model->spanRows()) {
        if (r >= 0 && r < model->rowCount()) table->setSpan(r, 0, 1, colCount);
    }
    if (table->viewport()) table->viewport()->update();
}

} // namespace

namespace ReportsTableSupport {

ReportsSnapshotTableModel::ReportsSnapshotTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int ReportsSnapshotTableModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

int ReportsSnapshotTableModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_headers.size();
}

QVariant ReportsSnapshotTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) return QVariant();
    if (index.row() < 0 || index.row() >= m_rows.size()) return QVariant();
    const Row& row = m_rows.at(index.row());
    if (index.column() < 0 || index.column() >= row.cells.size()) return QVariant();
    const Cell& cell = row.cells.at(index.column());

    if (cell.extraRoles.contains(role)) return cell.extraRoles.value(role);
    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
        return cell.text;
    case Qt::UserRole:
        return cell.userValue;
    case Qt::ToolTipRole:
        return cell.tooltip.trimmed().isEmpty() ? cell.text : cell.tooltip;
    case Qt::TextAlignmentRole:
        return cell.alignment;
    case Qt::FontRole: {
        if (!cell.bold && !cell.link) return QVariant();
        QFont f;
        f.setBold(cell.bold);
        f.setUnderline(cell.link);
        return f;
    }
    case Qt::ForegroundRole:
        if (cell.foreground.isValid()) return QBrush(cell.foreground);
        if (row.background.isValid()) return QBrush(QColor(Qt::black));
        return cell.link ? QVariant(QBrush(QColor(0, 82, 155))) : QVariant();
    case Qt::BackgroundRole:
        return row.background.isValid() ? QVariant(QBrush(row.background)) : QVariant();
    default:
        return QVariant();
    }
}

QVariant ReportsSnapshotTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }
    if (section < 0 || section >= m_headers.size()) return QVariant();
    return m_headers.at(section);
}

void ReportsSnapshotTableModel::setSnapshot(const QStringList& headers, const QVector<Row>& rows)
{
    beginResetModel();
    m_headers = headers;
    m_rows = rows;
    m_spanRows.clear();
    m_spanRows.reserve(rows.size());
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).spanAllColumns) m_spanRows.push_back(i);
    }
    endResetModel();
}

void ReportsSnapshotTableModel::setMessageRow(const QString& text)
{
    const int cols = qMax(1, m_headers.size());
    Row row;
    row.cells.resize(cols);
    row.cells[0] = makeCell(text);
    row.spanAllColumns = true;
    setSnapshot(m_headers, {row});
}

void ReportsSnapshotTableModel::clearSnapshot()
{
    setSnapshot(m_headers, {});
}

QString ReportsSnapshotTableModel::cellText(int row, int column) const
{
    if (row < 0 || row >= m_rows.size()) return QString();
    const Row& r = m_rows.at(row);
    if (column < 0 || column >= r.cells.size()) return QString();
    return r.cells.at(column).text;
}

QVariant ReportsSnapshotTableModel::cellUserValue(int row, int column) const
{
    if (row < 0 || row >= m_rows.size()) return QVariant();
    const Row& r = m_rows.at(row);
    if (column < 0 || column >= r.cells.size()) return QVariant();
    return r.cells.at(column).userValue;
}

bool ReportsSnapshotTableModel::rowSpansAllColumns(int row) const
{
    return row >= 0 && row < m_rows.size() && m_rows.at(row).spanAllColumns;
}

void setTableBaseStyle(QTableView* table)
{
    if (!table) return;
    table->setAlternatingRowColors(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setWordWrap(true);
    table->setSortingEnabled(false);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table->verticalHeader()->setDefaultSectionSize(qMax(28, table->fontMetrics().height() + 12));
    table->horizontalHeader()->setStretchLastSection(false);
}

void initializePeriodSnapshot(ReportsSnapshotTableModel* model)
{
    if (model) model->setSnapshot(periodHeaders(), {});
}

void initializeOrdersSnapshot(ReportsSnapshotTableModel* model)
{
    if (model) model->setSnapshot(ordersHeaders(), {});
}

void initializePartsSnapshot(ReportsSnapshotTableModel* model)
{
    if (model) model->setSnapshot(partsHeaders(), {});
}

void initializeOutsourceSnapshot(ReportsSnapshotTableModel* model)
{
    if (model) model->setSnapshot(outsourceHeaders(), {});
}

void initializePayrollSnapshot(ReportsSnapshotTableModel* model)
{
    if (model) model->setSnapshot(payrollHeaders(), {});
}

void initializeTotalExpensesSnapshot(ReportsSnapshotTableModel* model)
{
    if (model) model->setSnapshot(totalExpensesHeaders(), {});
}

void showLoadingRow(QTableView* table, ReportsSnapshotTableModel* model, const QString& text)
{
    if (!table || !model) return;
    table->setUpdatesEnabled(false);
    table->blockSignals(true);
    model->setMessageRow(text);
    table->clearSpans();
    table->setSpan(0, 0, 1, qMax(1, model->columnCount()));
    table->blockSignals(false);
    table->setUpdatesEnabled(true);
    if (table->viewport()) table->viewport()->update();
}

void applyPeriodSnapshot(QTableView* table, ReportsSnapshotTableModel* model, const PeriodSnapshot& snapshot)
{
    if (!table || !model) return;
    table->setUpdatesEnabled(false);
    table->blockSignals(true);

    QVector<ReportsSnapshotTableModel::Row> rows;
    rows.reserve(snapshot.rows.size());
    for (const auto& row : snapshot.rows) {
        ReportsSnapshotTableModel::Row out;
        out.cells = {
            makeCell(row.date),
            makeCell(row.orderNumber, int(Qt::AlignLeft | Qt::AlignVCenter), row.internalOrderNumber),
            makeCell(row.car),
            makeCell(row.master.isEmpty() ? QString::fromUtf8("—") : row.master),
            makeCell(row.works, int(Qt::AlignRight | Qt::AlignVCenter)),
            makeCell(row.parts, int(Qt::AlignRight | Qt::AlignVCenter)),
            makeCell(row.total, int(Qt::AlignRight | Qt::AlignVCenter))
        };
        rows.push_back(out);
    }
    model->setSnapshot(periodHeaders(), rows);
    if (rows.isEmpty()) model->setMessageRow(QString::fromUtf8("За вибраний період немає закритих нарядів"));

    table->blockSignals(false);
    table->setUpdatesEnabled(true);
    finishSnapshotApply(table, model);
}

void applyOrdersSnapshot(QTableView* table, ReportsSnapshotTableModel* model, const OrdersSnapshot& snapshot)
{
    if (!table || !model) return;
    table->setUpdatesEnabled(false);
    table->blockSignals(true);

    QVector<ReportsSnapshotTableModel::Row> rows;
    rows.reserve(snapshot.rows.size());
    for (const auto& row : snapshot.rows) {
        ReportsSnapshotTableModel::Row out;
        QStringList invoiceLabels;
        for (const auto& invoice : row.invoiceNumbers) invoiceLabels.append(invoiceNumberLabel(invoice));
        out.cells = {
            makeCell(row.date, int(Qt::AlignLeft | Qt::AlignVCenter), row.internalOrderNumber),
            makeCell(row.orderNumber),
            makeCell(row.car),
            makeCell(invoiceLabels.join("\n"), int(Qt::AlignLeft | Qt::AlignVCenter), row.invoiceKeys),
            makeCell(row.invoiceTotal, int(Qt::AlignRight | Qt::AlignVCenter)),
            makeCell(row.paymentDue),
            makeCell(row.paymentStatus)
        };
        out.cells[3].link=!row.invoiceKeys.isEmpty();out.cells[6].link=true;
        out.cells[6].bold=row.paymentStatus.startsWith(QString::fromUtf8("Не сплачено"), Qt::CaseInsensitive);
        rows.push_back(out);
    }
    model->setSnapshot(ordersHeaders(), rows);
    if (rows.isEmpty()) model->setMessageRow(QString::fromUtf8("За вибраний період немає нарядів"));

    table->blockSignals(false);
    table->setUpdatesEnabled(true);
    finishSnapshotApply(table, model);
}

void applyPartsSnapshot(QTableView* table, ReportsSnapshotTableModel* model, const PartsSnapshot& snapshot)
{
    if (!table || !model) return;
    table->setUpdatesEnabled(false);
    table->blockSignals(true);

    QVector<ReportsSnapshotTableModel::Row> rows;
    rows.reserve(snapshot.rows.size());
    for (const auto& row : snapshot.rows) {
        ReportsSnapshotTableModel::Row out;
        if (row.isStoreHeader) {
            out.cells.resize(partsHeaders().size());
            out.cells[0] = makeCell(QString::fromUtf8("Магазин: %1").arg(row.store),
                                    int(Qt::AlignLeft | Qt::AlignVCenter), QVariant(), QString(), true);
            out.spanAllColumns = true;
            rows.push_back(out);
            continue;
        }

        out.cells = {
            makeCell(row.date),
            makeCell(row.orderNumber, int(Qt::AlignLeft | Qt::AlignVCenter), row.internalOrderNumber),
            makeCell(row.partName, int(Qt::AlignLeft | Qt::AlignVCenter), QVariant(), row.tooltip),
            makeCell(invoiceNumberLabel(row.invoiceNumber), int(Qt::AlignLeft | Qt::AlignVCenter),
                     QVariantMap{{QStringLiteral("path"), row.invoicePath},
                                 {QStringLiteral("date"), row.invoiceDate},
                                 {QStringLiteral("store"), row.store}},
                     QString::fromUtf8("Відкрити PDF накладної")),
            makeCell(row.purchase, int(Qt::AlignRight | Qt::AlignVCenter)),
            makeCell(row.net, int(Qt::AlignRight | Qt::AlignVCenter)),
            makeCell(row.qty, int(Qt::AlignCenter | Qt::AlignVCenter)),
            makeCell(row.client, int(Qt::AlignRight | Qt::AlignVCenter))
        };
        out.cells[3].link = !row.invoiceNumber.trimmed().isEmpty();
        rows.push_back(out);
    }
    model->setSnapshot(partsHeaders(), rows);
    if (rows.isEmpty()) model->setMessageRow(QString::fromUtf8("За вибраний період немає придбаних запчастин"));

    table->blockSignals(false);
    table->setUpdatesEnabled(true);
    finishSnapshotApply(table, model);
}

void applyOutsourceSnapshot(QTableView* table, ReportsSnapshotTableModel* model, const OutsourceSnapshot& snapshot)
{
    if (!table || !model) return;
    table->setUpdatesEnabled(false);
    table->blockSignals(true);

    QVector<ReportsSnapshotTableModel::Row> rows;
    rows.reserve(snapshot.rows.size());
    for (const auto& row : snapshot.rows) {
        ReportsSnapshotTableModel::Row out;
        out.cells = {
            makeCell(row.sentDate, int(Qt::AlignLeft | Qt::AlignVCenter), row.serviceId),
            makeCell(row.returnDate),
            makeCell(row.company),
            makeCell(row.orderNumber, int(Qt::AlignLeft | Qt::AlignVCenter), row.internalOrderNumber),
            makeCell(row.partName),
            makeCell(row.repairCost, int(Qt::AlignRight | Qt::AlignVCenter)),
            makeCell(row.clientPrice, int(Qt::AlignRight | Qt::AlignVCenter))
        };
        rows.push_back(out);
    }
    model->setSnapshot(outsourceHeaders(), rows);
    if (rows.isEmpty()) model->setMessageRow(QString::fromUtf8("За вибраний період немає записів аутсорсу"));

    table->blockSignals(false);
    table->setUpdatesEnabled(true);
    finishSnapshotApply(table, model);
}

void applyPayrollSnapshot(QTableView* table, ReportsSnapshotTableModel* model, const PayrollSnapshot& snapshot)
{
    if (!table || !model) return;
    table->setUpdatesEnabled(false);
    table->blockSignals(true);

    QVector<ReportsSnapshotTableModel::Row> rows;
    rows.reserve(snapshot.rows.size());
    for (const auto& row : snapshot.rows) {
        ReportsSnapshotTableModel::Row out;
        out.cells = {
            makeCell(row.masterName, int(Qt::AlignLeft | Qt::AlignVCenter), row.masterId),
            makeCell(row.ordersCount, int(Qt::AlignCenter | Qt::AlignVCenter)),
            makeCell(row.baseSum, int(Qt::AlignRight | Qt::AlignVCenter)),
            makeCell(row.percent, int(Qt::AlignRight | Qt::AlignVCenter)),
            makeCell(row.accrued, int(Qt::AlignRight | Qt::AlignVCenter))
        };
        rows.push_back(out);
    }
    model->setSnapshot(payrollHeaders(), rows);
    if (rows.isEmpty()) model->setMessageRow(QString::fromUtf8("За вибраний період немає нарахувань"));

    table->blockSignals(false);
    table->setUpdatesEnabled(true);
    finishSnapshotApply(table, model);
}

void applyTotalExpensesSnapshot(QTableView* table, ReportsSnapshotTableModel* model, const TotalExpensesSnapshot& snapshot)
{
    if (!table || !model) return;
    table->setUpdatesEnabled(false);
    table->blockSignals(true);

    QVector<ReportsSnapshotTableModel::Row> rows;
    rows.reserve(snapshot.rows.size());
    for (const auto& row : snapshot.rows) {
        const bool bold = row.isTotal;
        ReportsSnapshotTableModel::Row out;
        out.cells = {
            makeCell(row.period, int(Qt::AlignLeft | Qt::AlignVCenter), row.expenseId, QString(), bold),
            makeCell(row.category),
            makeCell(row.description),
            makeCell(row.source),
            makeCell(row.total, int(Qt::AlignRight | Qt::AlignVCenter), QVariant(), QString(), bold)
        };
        rows.push_back(out);
    }
    model->setSnapshot(totalExpensesHeaders(), rows);
    if (rows.isEmpty()) model->setMessageRow(snapshot.error.isEmpty()
        ? QString::fromUtf8("За вибраний період немає збережених витрат") : snapshot.error);

    table->blockSignals(false);
    table->setUpdatesEnabled(true);
    finishSnapshotApply(table, model);
}

} // namespace ReportsTableSupport
