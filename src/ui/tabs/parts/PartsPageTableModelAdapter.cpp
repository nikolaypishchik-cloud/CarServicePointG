#include "ui/tabs/parts/PartsPageTableModelAdapter.h"

#include "core/util/FormatUtils.h"
#include "core/services/orders/PartMarkupPolicyService.h"
#include "core/util/StoreNameNormalizer.h"

#include <QtGlobal>
#include <QBrush>
#include <QColor>
#include <QFont>

#include <algorithm>
#include <utility>

namespace {

QString formatMoneyGroupedForPartsTable(double value)
{
    const double rounded = qRound64(value);
    const bool isInteger = qAbs(value - rounded) < 0.000001;
    QString out = FormatUtils::formatNumberSpaces(value, isInteger ? 0 : 2);
    out.replace('.', ',');
    return out;
}

QString displayStoreNameForPartsUiAdapter(const QString& rawStore,
                                           const QString& defaultValue = QString::fromUtf8("Без магазину"))
{
    const QString normalized = StoreNameNormalizer::canonicalDisplayNameOrDefault(rawStore, defaultValue);
    return StoreNameNormalizer::canonKey(normalized) == QStringLiteral("avtonova_d")
        ? QString::fromUtf8("Автонова-D")
        : normalized;
}

QString qtyDisplayForPartsTable(double qty)
{
    return FormatUtils::formatNumberSpaces(qty, qty == qRound64(qty) ? 0 : 2);
}

bool matchesDatePolicy(const PartRowSnapshot& row, const PartsPageTableModelAdapter::ViewPolicy& policy)
{
    if (!policy.from.isValid() && !policy.to.isValid()) return true;

    QDate rowDate = PartsQueryService::parseBusinessDate(row.dateRaw);
    if (!rowDate.isValid()) rowDate = PartsQueryService::parseBusinessDate(PartsQueryService::displayDate(row.dateRaw));
    if (!rowDate.isValid()) return true;

    if (policy.from.isValid() && rowDate < policy.from) return false;
    if (policy.to.isValid() && rowDate > policy.to) return false;
    return true;
}

} // namespace


namespace {
constexpr int ColId = 0;
constexpr int ColDate = 1;
constexpr int ColDeliveryDate = 2;
constexpr int ColStore = 3;
constexpr int ColArticle = 4;
constexpr int ColBrand = 5;
constexpr int ColName = 6;
constexpr int ColQty = 7;
constexpr int ColPrice = 8;
constexpr int ColAmount = 9;
constexpr int ColOrder = 10;
constexpr int ColAuto = 11;
constexpr int ColPaid = 12;
constexpr int kPartsOverdueSeverityRole = Qt::UserRole + 200;

QColor partsOverdueBackgroundForSeverity(int overdueSeverity)
{
    if (overdueSeverity >= 2) return QColor(255, 160, 160);
    if (overdueSeverity == 1) return QColor(255, 235, 120);
    return QColor();
}

QColor partsOverdueForegroundForSeverity(int overdueSeverity)
{
    return overdueSeverity > 0 ? QColor(Qt::black) : QColor();
}
}

namespace PartsPageTableModelAdapter {

PartsArchiveTableModel::PartsArchiveTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

QStringList PartsArchiveTableModel::headers()
{
    return {
        QStringLiteral("ID"),
        QString::fromUtf8("Дата"),
        QString::fromUtf8("Статус\nвстановл."),
        QString::fromUtf8("Магазин"),
        QString::fromUtf8("ОЕМ\nномер"),
        QString::fromUtf8("Бренд"),
        QString::fromUtf8("Назва"),
        QString::fromUtf8("Кіл-ть"),
        QString::fromUtf8("Ціна"),
        QString::fromUtf8("Сума"),
        QString::fromUtf8("Наряд"),
        QString::fromUtf8("Авто"),
        QString::fromUtf8("Сплачено")
    };
}

int PartsArchiveTableModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

int PartsArchiveTableModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : headers().size();
}

const TableRow* PartsArchiveTableModel::rowAt(int row) const
{
    if (row < 0 || row >= m_rows.size()) return nullptr;
    return &m_rows.at(row);
}

int PartsArchiveTableModel::rowByPartId(int partId) const
{
    if (partId <= 0) return -1;
    const auto it = m_rowByPartId.constFind(partId);
    return it == m_rowByPartId.constEnd() ? -1 : it.value();
}

int PartsArchiveTableModel::firstRowByOrderUid(const QString& orderUid) const
{
    const QString key = orderUid.trimmed();
    if (key.isEmpty()) return -1;
    const auto it = m_firstRowByOrderUid.constFind(key);
    return it == m_firstRowByOrderUid.constEnd() ? -1 : it.value();
}

int PartsArchiveTableModel::firstRowByOrderNumber(int orderNumber) const
{
    if (orderNumber <= 0) return -1;
    const auto it = m_firstRowByOrderNumber.constFind(orderNumber);
    return it == m_firstRowByOrderNumber.constEnd() ? -1 : it.value();
}

void PartsArchiveTableModel::rebuildIndexes()
{
    // P1/pass9 parts snapshot migration closure: selection/restore lookups are indexed by the model,
    // not by scanning QTableView rows or widget items after every async refresh.
    m_rowByPartId.clear();
    m_firstRowByOrderUid.clear();
    m_firstRowByOrderNumber.clear();
    m_rowByPartId.reserve(m_rows.size());
    m_firstRowByOrderUid.reserve(m_rows.size());
    m_firstRowByOrderNumber.reserve(m_rows.size());
    for (int row = 0; row < m_rows.size(); ++row) {
        const TableRow& rowData = m_rows.at(row);
        if (rowData.id > 0) m_rowByPartId.insert(rowData.id, row);
        const QString orderUid = rowData.orderUid.trimmed();
        if (!orderUid.isEmpty() && !m_firstRowByOrderUid.contains(orderUid)) {
            m_firstRowByOrderUid.insert(orderUid, row);
        }
        if (rowData.orderNumber > 0 && !m_firstRowByOrderNumber.contains(rowData.orderNumber)) {
            m_firstRowByOrderNumber.insert(rowData.orderNumber, row);
        }
    }
}

QVariant PartsArchiveTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) return QVariant();
    if (index.row() < 0 || index.row() >= m_rows.size()) return QVariant();
    const TableRow& row = m_rows.at(index.row());
    const int col = index.column();

    if (role == kPartsOverdueSeverityRole) {
        return col == ColPaid ? 0 : row.overdueSeverity;
    }

    if (role == Qt::BackgroundRole) {
        if (col == ColPaid) return QVariant();
        const QColor bg = partsOverdueBackgroundForSeverity(row.overdueSeverity);
        return bg.isValid() ? QVariant(QBrush(bg)) : QVariant();
    }

    if (role == Qt::ForegroundRole) {
        if (col == ColPaid) {
            return row.paid ? QVariant(QBrush(QColor(QStringLiteral("#111111"))))
                            : QVariant(QBrush(QColor(QStringLiteral("#8a1010"))));
        }
        const QColor fg = partsOverdueForegroundForSeverity(row.overdueSeverity);
        return fg.isValid() ? QVariant(QBrush(fg)) : QVariant();
    }

    if (role == Qt::FontRole && col == ColPaid) {
        QFont f;
        f.setBold(true);
        return f;
    }

    if (role == Qt::TextAlignmentRole) {
        switch (col) {
        case ColDeliveryDate:
        case ColQty:
        case ColOrder:
        case ColPaid:
            return int(Qt::AlignCenter | Qt::AlignVCenter);
        case ColPrice:
        case ColAmount:
            return int(Qt::AlignRight | Qt::AlignVCenter);
        default:
            return int(Qt::AlignLeft | Qt::AlignVCenter);
        }
    }

    if (role == Qt::ToolTipRole) {
        switch (col) {
        case ColDeliveryDate:
            if (row.installed) return QString::fromUtf8("Встановлено: %1").arg(row.statusDisplay);
            if (row.paid) {
                return row.deliveryDateDisplay.isEmpty()
                    ? QString::fromUtf8("Сплачено")
                    : QString::fromUtf8("Сплачено\nДата доставки: %1").arg(row.deliveryDateDisplay);
            }
            return row.deliveryDateDisplay.isEmpty()
                ? QString::fromUtf8("Очікування")
                : QString::fromUtf8("Очікування\nДата доставки: %1").arg(row.deliveryDateDisplay);
        case ColAuto:
            return row.vin.isEmpty() ? row.autoDisplay : (row.autoDisplay + QString::fromUtf8("\nVIN: ") + row.vin);
        case ColPaid:
            return row.paymentActionsEnabled
                ? QString::fromUtf8("Натисніть, щоб змінити стан оплати")
                : QString::fromUtf8("Майстер не може змінювати стан оплати");
        default:
            return data(index, Qt::DisplayRole);
        }
    }

    if (role == Qt::UserRole) {
        switch (col) {
        case ColDate:
        case ColDeliveryDate:
        case ColPaid:
            return row.id;
        case ColQty:
            return row.qty;
        case ColPrice:
            return row.price;
        case ColAmount:
            return row.amount;
        case ColOrder:
            return row.orderNumber;
        case ColAuto:
            return row.plate;
        default:
            return QVariant();
        }
    }
    if (role == Qt::UserRole + 1) {
        switch (col) {
        case ColDeliveryDate: return row.deliveryDateRaw;
        case ColAuto: return row.carModel;
        case ColPaid: return row.paid;
        default: return QVariant();
        }
    }
    if (role == Qt::UserRole + 2) {
        switch (col) {
        case ColDeliveryDate: return row.installedAtRaw;
        case ColAuto: return row.vin;
        default: return QVariant();
        }
    }
    if (role == Qt::UserRole + 3 && col == ColPaid) return row.paymentActionsEnabled;
    if (role == Qt::UserRole + 4 && col == ColPaid) return row.partiallyPaid;
    if (role == Qt::UserRole + 20 && col == ColId) return row.searchRank;
    if (role == Qt::UserRole + 30 && col == ColOrder) return row.orderUid;

    if (role != Qt::DisplayRole && role != Qt::EditRole) return QVariant();

    switch (col) {
    case ColId: return row.id;
    case ColDate: return row.dateDisplay;
    case ColDeliveryDate: return row.statusDisplay;
    case ColStore: return row.storeDisplay;
    case ColArticle: return row.article;
    case ColBrand: return row.brand;
    case ColName: return row.name;
    case ColQty: return row.qtyDisplay;
    case ColPrice: return row.priceDisplay;
    case ColAmount: return row.amountDisplay;
    case ColOrder: return row.orderDisplay;
    case ColAuto: return row.autoDisplay;
    case ColPaid:
        if (PartMarkupPolicyService::isCustomerSupplied(row.storeDisplay)) return QString::fromUtf8("Не потребує оплати");
        return row.partiallyPaid ? QString::fromUtf8("Частково") : paidDisplayText(row.paid);
    default: return QVariant();
    }
}

QVariant PartsArchiveTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }
    const QStringList h = headers();
    if (section < 0 || section >= h.size()) return QVariant();
    return h.at(section);
}

Qt::ItemFlags PartsArchiveTableModel::flags(const QModelIndex& index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

void PartsArchiveTableModel::setRows(const QVector<TableRow>& rows)
{
    beginResetModel();
    m_rows = rows;
    rebuildIndexes();
    endResetModel();
}

bool PartsArchiveTableModel::replaceRow(int row, const TableRow& rowData)
{
    if (row < 0 || row >= m_rows.size()) return false;
    const int oldId = m_rows.at(row).id;
    const QString oldOrderUid = m_rows.at(row).orderUid.trimmed();
    const int oldOrderNumber = m_rows.at(row).orderNumber;
    m_rows[row] = rowData;
    // P1/pass9 parts snapshot migration closure: keep point updates indexed for O(1) paid-toggle selection restore.
    if (oldId > 0) m_rowByPartId.remove(oldId);
    if (!oldOrderUid.isEmpty() || oldOrderNumber > 0 || rowData.id > 0) {
        rebuildIndexes();
    } else if (rowData.id > 0) {
        m_rowByPartId.insert(rowData.id, row);
    }
    const QModelIndex left = index(row, 0);
    const QModelIndex right = index(row, columnCount() - 1);
    emit dataChanged(left, right, {Qt::DisplayRole, Qt::EditRole, Qt::UserRole, Qt::UserRole + 1,
                                  Qt::UserRole + 2, Qt::UserRole + 3, Qt::UserRole + 4, Qt::UserRole + 20, Qt::UserRole + 30,
                                  Qt::BackgroundRole, Qt::ForegroundRole, Qt::FontRole,
                                  Qt::TextAlignmentRole, Qt::ToolTipRole, kPartsOverdueSeverityRole});
    return true;
}

} // namespace PartsPageTableModelAdapter

namespace PartsPageTableModelAdapter {

QString paidDisplayText(bool paid)
{
    return paid ? QString::fromUtf8("Сплачено") : QString::fromUtf8("Не сплачено");
}

TableRow adaptRow(const PartRowSnapshot& sourceRow,
                  const QDate& today,
                  bool paymentActionsEnabled)
{
    PartRowSnapshot rowData = sourceRow;
    PartsQueryService::refreshDerivedFields(&rowData, today.isValid() ? today : QDate::currentDate());

    if (!paymentActionsEnabled) {
        // Masters track installation deadlines, independently of supplier payment.
        auto installation = rowData;
        installation.paid = installation.installed;
        PartsQueryService::refreshDerivedFields(&installation, today.isValid() ? today : QDate::currentDate());
        rowData.overdueSeverity = installation.overdueSeverity;
        rowData.overdue = installation.overdue;
    }

    TableRow row;
    row.id = rowData.id;
    row.searchRank = rowData.searchRank;
    row.orderNumber = rowData.orderNumber;
    row.overdueSeverity = rowData.overdueSeverity;
    row.overdue = rowData.overdue;
    row.paid = rowData.paid;
    row.partiallyPaid = !rowData.paid && rowData.paidAmount > 0;
    row.paymentActionsEnabled = paymentActionsEnabled && !PartMarkupPolicyService::isCustomerSupplied(rowData.store);
    row.installed = rowData.installed;
    row.qty = rowData.qty;
    row.price = rowData.price;
    row.amount = rowData.amount;

    row.dateDisplay = PartsQueryService::displayDate(rowData.dateRaw);
    row.deliveryDateDisplay = PartsQueryService::displayDate(rowData.deliveryDateRaw);
    row.statusDisplay = rowData.installed
        ? PartsQueryService::displayDate(rowData.installedAtRaw)
        : (rowData.paid ? QString() : QString::fromUtf8("Очікування"));
    row.storeDisplay = displayStoreNameForPartsUiAdapter(rowData.store);
    row.article = rowData.article;
    row.brand = rowData.brand;
    row.name = rowData.name;
    row.qtyDisplay = qtyDisplayForPartsTable(rowData.qty);
    row.priceDisplay = formatMoneyGroupedForPartsTable(rowData.price);
    row.amountDisplay = formatMoneyGroupedForPartsTable(rowData.amount);
    if (PartMarkupPolicyService::isCustomerSupplied(rowData.store)) {
        row.priceDisplay.clear();
        row.amountDisplay.clear();
    }
    row.orderDisplay = rowData.orderDisplay.isEmpty()
        ? QString::number(rowData.orderNumber)
        : rowData.orderDisplay;
    row.orderUid = rowData.orderUid;
    row.plate = rowData.plate;
    row.carModel = rowData.carModel;
    row.vin = rowData.vin;
    row.deliveryDateRaw = rowData.deliveryDateRaw;
    row.installedAtRaw = rowData.installedAtRaw;
    row.autoDisplay = rowData.plate.isEmpty()
        ? rowData.carModel
        : (rowData.carModel.isEmpty()
            ? rowData.plate
            : (rowData.plate + QString::fromUtf8(" | ") + rowData.carModel));

    return row;
}

AdaptedRows build(const QVector<PartRowSnapshot>& inputRows,
                  const ViewPolicy& policy)
{
    AdaptedRows out;

    const QDate effectiveToday = policy.today.isValid() ? policy.today : QDate::currentDate();
    const bool hasTextFilter = policy.hasTextFilter();

    out.snapshots.reserve(inputRows.size());
    for (PartRowSnapshot rowData : inputRows) {
        PartsQueryService::refreshDerivedFields(&rowData, effectiveToday);
        if (hasTextFilter && rowData.searchRank >= 100) continue;
        // When the user searches by order number/OEM/plate/text, the search result
        // must not be narrowed by the saved date range. PartsQueryService already
        // disables the SQL date window for text searches; keep the adapter policy
        // identical so manual rows dated outside the currently selected period
        // still appear on "Замовлення запчастин" when the order is searched.
        if (!hasTextFilter && !matchesDatePolicy(rowData, policy)) continue;
        out.snapshots.push_back(std::move(rowData));
    }

    // P1 PartsPage p4: keep sorting in the model-adapter/proxy boundary.
    // The widget receives a ready ordered snapshot and does not manually move UI rows.
    PartsQueryService::sortRowsForTable(&out.snapshots, effectiveToday);
    if (hasTextFilter) {
        std::stable_sort(out.snapshots.begin(), out.snapshots.end(), [](const PartRowSnapshot& a, const PartRowSnapshot& b) {
            if (a.paid != b.paid) return !a.paid;
            if (a.overdueRank != b.overdueRank) return a.overdueRank < b.overdueRank;
            return a.searchRank < b.searchRank;
        });
    }

    out.rows.reserve(out.snapshots.size());
    for (const PartRowSnapshot& rowData : out.snapshots) {
        out.rows.push_back(adaptRow(rowData, effectiveToday, policy.paymentActionsEnabled));
    }

    return out;
}

AdaptedRows build(const QVector<PartRowSnapshot>& inputRows,
                  bool hasTextFilter,
                  const QDate& today)
{
    ViewPolicy policy;
    policy.textFilter = hasTextFilter ? QStringLiteral("*") : QString();
    policy.today = today;
    return build(inputRows, policy);
}

} // namespace PartsPageTableModelAdapter
