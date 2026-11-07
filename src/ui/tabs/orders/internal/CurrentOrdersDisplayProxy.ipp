// Extracted from CurrentOrdersPageLocalSupport.cpp to keep the P1/P2 module under the monolith gate line budget.
static QDateTime parseCurrentOrderDateTime(const QString& raw)
{
    const QString t = raw.trimmed();
    if (t.isEmpty()) return {};
    for (const QString& fmt : {QString(), QStringLiteral("yyyy-MM-dd HH:mm:ss"), QStringLiteral("yyyy-MM-ddTHH:mm:ss"), QStringLiteral("yyyy-MM-dd HH:mm"), QStringLiteral("yyyy-MM-ddTHH:mm"), QStringLiteral("dd.MM.yy HH:mm"), QStringLiteral("dd.MM.yyyy HH:mm")}) {
        QDateTime dt = fmt.isEmpty() ? DateFormats::parseDateTime(t) : QDateTime::fromString(t, fmt);
        if (dt.isValid()) {
            if (dt.date().year() >= 1900 && dt.date().year() < 2000) dt = dt.addYears(100);
            return dt;
        }
    }
    return {};
}

static QString displayDateTimeStandard(const QVariant& v)
{
    const QString raw = v.toString().trimmed();
    if (raw.isEmpty()) return QString();
    const QDateTime dt = parseCurrentOrderDateTime(raw);
    if (dt.isValid()) return dt.toString(QStringLiteral("dd.MM.yy/HH.mm"));

    QString t = raw;
    const int tpos = t.indexOf('T');
    if (tpos > 0) t = t.left(tpos);
    for (const QString& fmt : {QStringLiteral("yyyy-MM-dd"), QStringLiteral("dd.MM.yy"), QStringLiteral("dd.MM.yyyy")}) {
        QDate d = QDate::fromString(t, fmt);
        if (d.isValid()) {
            if (d.year() >= 1900 && d.year() < 2000) d = d.addYears(100);
            return d.toString(QStringLiteral("dd.MM.yy"));
        }
    }
    return raw;
}

class CurrentOrdersDisplayProxy final : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid()) return QSortFilterProxyModel::data(index, role);
        if (role == Qt::DisplayRole) {
            const QVariant src = QSortFilterProxyModel::data(index, role);
            switch (index.column()) {
            case 2: return displayDateTimeStandard(src);
            case 3: return currentOrdersStatusDisplay(src.toString());
            case 4: return PlateUtils::formatPlateDisplay(src.toString());
            case 8:
            case 9: return FormatUtils::money(src.toDouble(), 0);
            default: break;
            }
        }
        if ((role == Qt::BackgroundRole || role == Qt::ForegroundRole) && sourceModel()) {
            const QModelIndex srcIndex = mapToSource(index);
            if (!srcIndex.isValid()) return QSortFilterProxyModel::data(index, role);
            const int severityCol = modelColumnByHeader(sourceModel(), QStringLiteral("__parts_overdue_severity"));
            const int fallbackOverdueCol = modelColumnByHeader(sourceModel(), QStringLiteral("__parts_overdue"));
            int severity = 0;
            if (severityCol >= 0) {
                const QModelIndex srcSeverity = sourceModel()->index(srcIndex.row(), severityCol);
                severity = sourceModel()->data(srcSeverity).toInt();
            } else if (fallbackOverdueCol >= 0) {
                const QModelIndex srcOverdue = sourceModel()->index(srcIndex.row(), fallbackOverdueCol);
                severity = sourceModel()->data(srcOverdue).toInt() != 0 ? 2 : 0;
            }
            if (severity >= 2) {
                if (role == Qt::BackgroundRole) return QBrush(QColor(255, 160, 160));
                return QBrush(QColor(Qt::black));
            }
            if (severity == 1) {
                if (role == Qt::BackgroundRole) return QBrush(QColor(255, 235, 120));
                return QBrush(QColor(Qt::black));
            }
        }
        return QSortFilterProxyModel::data(index, role);
    }

protected:
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override
    {
        const QAbstractItemModel* sm = sourceModel();
        if (sm) {
            const auto compareIntHeaderAsc = [&](const QString& header) -> std::optional<bool> {
                const int col = modelColumnByHeader(sm, header);
                if (col < 0) return std::nullopt;
                bool okLeft = false;
                bool okRight = false;
                const int leftValue = sm->data(sm->index(left.row(), col)).toInt(&okLeft);
                const int rightValue = sm->data(sm->index(right.row(), col)).toInt(&okRight);
                if (!okLeft || !okRight || leftValue == rightValue) return std::nullopt;
                return leftValue < rightValue;
            };
            const auto compareIntHeaderDesc = [&](const QString& header) -> std::optional<bool> {
                const int col = modelColumnByHeader(sm, header);
                if (col < 0) return std::nullopt;
                bool okLeft = false;
                bool okRight = false;
                const int leftValue = sm->data(sm->index(left.row(), col)).toInt(&okLeft);
                const int rightValue = sm->data(sm->index(right.row(), col)).toInt(&okRight);
                if (!okLeft || !okRight || leftValue == rightValue) return std::nullopt;
                return leftValue > rightValue;
            };

            if (const auto cmp = compareIntHeaderAsc(QStringLiteral("__rank")); cmp.has_value()) {
                return *cmp;
            }
            if (const auto cmp = compareIntHeaderAsc(QStringLiteral("__parts_sort_rank")); cmp.has_value()) {
                return *cmp;
            }

            const int openedCol = modelColumnByHeader(sm, QString::fromUtf8("Відкрито"));
            if (openedCol >= 0) {
                const QDateTime leftOpened = parseCurrentOrderDateTime(sm->data(sm->index(left.row(), openedCol)).toString());
                const QDateTime rightOpened = parseCurrentOrderDateTime(sm->data(sm->index(right.row(), openedCol)).toString());
                if (leftOpened.isValid() && rightOpened.isValid() && leftOpened != rightOpened) {
                    return leftOpened > rightOpened;
                }
            }

            if (const auto cmp = compareIntHeaderDesc(QStringLiteral("__order_year")); cmp.has_value()) {
                return *cmp;
            }
            if (const auto cmp = compareIntHeaderDesc(QStringLiteral("№")); cmp.has_value()) {
                return *cmp;
            }
            if (const auto cmp = compareIntHeaderDesc(QStringLiteral("__internal_number")); cmp.has_value()) {
                return *cmp;
            }
        }
        return QSortFilterProxyModel::lessThan(left, right);
    }
};
