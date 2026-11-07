#include "core/services/AccessControlService.h"
#include "core/services/PartsQueryService.h"
#include "core/util/StoreNameNormalizer.h"
#include "core/services/PurchasePaymentService.h"
#include "core/services/orders/PartMarkupPolicyService.h"

#include "core/cache/PartsQueryCache.h"
#include "core/cache/SnapshotCacheInvalidator.h"
#include "core/backup/BackupCommitPublisher.h"
#include "core/services/OrderSearchAggregator.h"
#include "core/services/PartOverduePolicy.h"
#include "core/perf/PerfWatchdog.h"
#include "core/util/FormatUtils.h"
#include "core/util/PlateUtils.h"
#include "infrastructure/db/sqlite/Db.h"
#include "runtime/RuntimeJobDispatcher.h"
#include "core/diagnostics/RuntimeLoggingControl.h"

#include <QDebug>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QSet>
#include <QStringList>
#include <algorithm>

namespace {

QDate normalizeBusinessDate(QDate d)
{
    if (d.isValid() && d.year() >= 1900 && d.year() < 2000) d = d.addYears(100);
    return d;
}


bool hasTableOrView(QSqlDatabase db, const QString& name)
{
    if (!db.isValid() || !db.isOpen()) return false;
    const QStringList names = db.tables(static_cast<QSql::TableType>(QSql::Tables | QSql::Views));
    for (const QString& existing : names) {
        if (existing.compare(name, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

bool tableOrViewUsableForSelect(QSqlDatabase db, const QString& name, const QString& context)
{
    if (!hasTableOrView(db, name)) return false;

    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM ") + name)) {
        qWarning().noquote() << QStringLiteral("PARTS_SOURCE_RECOVERY reason=queryable_check_failed context=%1 source=%2 error=%3")
            .arg(context, name, q.lastError().text());
        return false;
    }
    return true;
}

QString partSourceSelectSql(const QString& tableName, const QString& alias)
{
    return QStringLiteral(
        "SELECT %1.id, %1.order_uid, %1.order_number, %1.ordered_at, %1.delivery_date, %1.installed_at, "
        "       %1.store, %1.part_number, %1.manufacturer, %1.name, %1.qty, %1.price, %1.amount, "
        "       %1.purchase_price, %1.purchase_amount, %1.paid "
        "  FROM %2 %1")
        .arg(alias, tableName);
}

QString partDuplicatePredicate(const QString& existingAlias, const QString& candidateAlias)
{
    return QStringLiteral(
        "((TRIM(COALESCE(%2.order_uid,''))<>'' AND TRIM(COALESCE(%1.order_uid,''))=TRIM(COALESCE(%2.order_uid,''))) "
        " OR (TRIM(COALESCE(%2.order_uid,''))='' AND COALESCE(%1.order_number,0)=COALESCE(%2.order_number,0))) "
        "AND (%1.id=%2.id OR (COALESCE(%1.ordered_at,'')=COALESCE(%2.ordered_at,'') "
        "AND COALESCE(%1.delivery_date,'')=COALESCE(%2.delivery_date,'') "
        "AND COALESCE(%1.installed_at,'')=COALESCE(%2.installed_at,'') "
        "AND COALESCE(%1.store,'')=COALESCE(%2.store,'') "
        "AND COALESCE(%1.part_number,'')=COALESCE(%2.part_number,'') "
        "AND COALESCE(%1.manufacturer,'')=COALESCE(%2.manufacturer,'') "
        "AND COALESCE(%1.name,'')=COALESCE(%2.name,'') "
        "AND ABS(COALESCE(%1.qty,0)-COALESCE(%2.qty,0))<0.0001 "
        "AND ABS(COALESCE(%1.price,0)-COALESCE(%2.price,0))<0.0001 "
        "AND ABS(COALESCE(%1.amount,0)-COALESCE(%2.amount,0))<0.0001 "
        "AND COALESCE(%1.paid,0)=COALESCE(%2.paid,0)))")
        .arg(existingAlias, candidateAlias);
}

QString unifiedPartsSourceSql(QSqlDatabase db)
{
    const bool hasWorkParts = hasTableOrView(db, QStringLiteral("work_order_parts"));
    const bool hasCacheParts = hasTableOrView(db, QStringLiteral("router_cache_work_order_parts"));
    const bool hasRouterParts = tableOrViewUsableForSelect(db, QStringLiteral("router_work_order_parts"), QStringLiteral("parts_page_source"));

    QStringList selects;
    if (hasWorkParts) {
        selects << partSourceSelectSql(QStringLiteral("work_order_parts"), QStringLiteral("p"));
    }
    if (hasCacheParts) {
        QString select = partSourceSelectSql(QStringLiteral("router_cache_work_order_parts"), QStringLiteral("c"));
        if (hasWorkParts) {
            select += QStringLiteral(" WHERE NOT EXISTS (SELECT 1 FROM work_order_parts w WHERE ")
                + partDuplicatePredicate(QStringLiteral("w"), QStringLiteral("c"))
                + QStringLiteral(")");
        }
        selects << select;
    }
    if (hasRouterParts) {
        QString select = partSourceSelectSql(QStringLiteral("router_work_order_parts"), QStringLiteral("r"));
        QStringList antiDuplicates;
        if (hasWorkParts) {
            antiDuplicates << (QStringLiteral("NOT EXISTS (SELECT 1 FROM work_order_parts w WHERE ")
                + partDuplicatePredicate(QStringLiteral("w"), QStringLiteral("r"))
                + QStringLiteral(")"));
        }
        if (hasCacheParts) {
            antiDuplicates << (QStringLiteral("NOT EXISTS (SELECT 1 FROM router_cache_work_order_parts c WHERE ")
                + partDuplicatePredicate(QStringLiteral("c"), QStringLiteral("r"))
                + QStringLiteral(")"));
        }
        if (!antiDuplicates.isEmpty()) select += QStringLiteral(" WHERE ") + antiDuplicates.join(QStringLiteral(" AND "));
        selects << select;
    }

    if (selects.isEmpty()) return QStringLiteral("work_order_parts");
    if (selects.size() == 1) {
        if (hasWorkParts) return QStringLiteral("work_order_parts");
        if (hasCacheParts && !hasRouterParts) return QStringLiteral("router_cache_work_order_parts");
        if (hasRouterParts) return QStringLiteral("router_work_order_parts");
    }
    return QStringLiteral("(") + selects.join(QStringLiteral(" UNION ALL ")) + QStringLiteral(")");
}

QString canonicalOrderUidExpr(const QString& uidExpr, const QString& numberExpr)
{
    return QStringLiteral(
        "COALESCE(NULLIF(TRIM(COALESCE(%1,'')),''), CASE WHEN COALESCE(%2,0)>0 THEN 'rewrite_' || CAST(%2 AS TEXT) ELSE '' END)")
        .arg(uidExpr, numberExpr);
}

QString partOrderJoinPredicate(const QString& partAlias, const QString& orderAlias)
{
    const QString canonicalPartOrderUid = canonicalOrderUidExpr(QStringLiteral("%1.order_uid").arg(partAlias), QStringLiteral("%1.order_number").arg(partAlias));
    const QString canonicalJoinedOrderUid = canonicalOrderUidExpr(QStringLiteral("%1.order_uid").arg(orderAlias), QStringLiteral("%1.number").arg(orderAlias));
    return QStringLiteral(
        "((%1 = %2) "
        "OR (COALESCE(%3.order_number,0)>0 AND COALESCE(%3.order_number,0)=COALESCE(%4.order_number,%4.number,0)) "
        "OR (LOWER(TRIM(COALESCE(%3.order_uid,''))) = 'rewrite_' || CAST(COALESCE(%4.order_number,%4.number,0) AS TEXT)))")
        .arg(canonicalJoinedOrderUid, canonicalPartOrderUid, partAlias, orderAlias);
}

QString indexedPartOrderQuery(QString query, const QString& partsSource,
                              const QString& ordersSource, const QString& partAlias)
{
    // Split the three identity alternatives into equality joins. An OR over
    // normalized expressions otherwise scans every order for every part.
    // Snapshot-local row tokens preserve distinct source rows, even with reused
    // business IDs. UNION deduplicates only the same part/order pair.
    const QString prefix = QStringLiteral(
        "WITH cs_parts AS MATERIALIZED (SELECT source.*, ROW_NUMBER() OVER () AS cs_token, "
        "%1 AS cs_uid, COALESCE(source.order_number,0) AS cs_number, "
        "LOWER(TRIM(COALESCE(source.order_uid,''))) AS cs_rewrite FROM %2 source), "
        "cs_orders AS MATERIALIZED (SELECT source.*, ROW_NUMBER() OVER () AS cs_token, "
        "%3 AS cs_uid, COALESCE(source.order_number,source.number,0) AS cs_number, "
        "'rewrite_' || CAST(COALESCE(source.order_number,source.number,0) AS TEXT) AS cs_rewrite FROM %4 source), "
        "cs_pairs AS (SELECT p.cs_token AS part_token,o.cs_token AS order_token "
        "FROM cs_parts p JOIN cs_orders o ON p.cs_uid=o.cs_uid "
        "UNION SELECT p.cs_token,o.cs_token FROM cs_parts p JOIN cs_orders o ON p.cs_number=o.cs_number WHERE p.cs_number>0 "
        "UNION SELECT p.cs_token,o.cs_token FROM cs_parts p JOIN cs_orders o ON p.cs_rewrite=o.cs_rewrite) ")
        .arg(canonicalOrderUidExpr(QStringLiteral("source.order_uid"), QStringLiteral("source.order_number")),
             partsSource,
             canonicalOrderUidExpr(QStringLiteral("source.order_uid"), QStringLiteral("source.number")),
             ordersSource);
    const QString oldJoin = QStringLiteral("FROM %1 %2 JOIN %3 o ON %4 ")
        .arg(partsSource, partAlias, ordersSource, partOrderJoinPredicate(partAlias, QStringLiteral("o")));
    const QString newJoin = QStringLiteral(
        "FROM cs_pairs matches JOIN cs_parts %1 ON %1.cs_token=matches.part_token "
        "JOIN cs_orders o ON o.cs_token=matches.order_token ").arg(partAlias);
    query.replace(oldJoin, newJoin);
    return prefix + query;
}

int rewriteOrderNumberFromUid(const QString& uid)
{
    const QString normalized = uid.trimmed().toLower();
    if (!normalized.startsWith(QStringLiteral("rewrite_"))) return 0;
    bool ok = false;
    const int number = normalized.mid(QStringLiteral("rewrite_").size()).toInt(&ok);
    return ok && number > 0 ? number : 0;
}

QString openOrderStatusPredicateSql(const QString& alias)
{
    // Same business intent as CurrentOrdersQueryService::openOrdersNativePredicateSql,
    // but with sentinel close-date protection.  Some historical databases contain
    // duplicate/stale order links; parts highlighting must follow the active order
    // that is visible on "Поточні наряди", not a stale closed snapshot matched by
    // old order_uid / rewrite_* identity.
    return QStringLiteral(
        "(TRIM(COALESCE(%1.closed_at,''))='' "
        " OR TRIM(COALESCE(%1.closed_at,'')) IN ('0','0000-00-00','0000-00-00 00:00:00','1899-12-30','1900-01-01','1970-01-01')) "
        "AND LOWER(TRIM(COALESCE(%1.status,''))) NOT IN "
        "('closed','закритий','закрито','закрыт','закрыто','виконаний','виконано','выполнен','выполнено','done','completed','paid','оплачено','archived','archive','архів','архив','cancelled','canceled','скасовано','скасований','отменено','отменен')")
        .arg(alias);
}

struct OpenOrderIdentityContext
{
    QSet<int> numbers;
    QSet<QString> uids;
};

OpenOrderIdentityContext loadOpenOrderIdentityContext(QSqlDatabase db)
{
    OpenOrderIdentityContext ctx;
    if (!db.isValid() || !db.isOpen()) return ctx;

    QString sql;
    if (tableOrViewUsableForSelect(db, QStringLiteral("router_open_order_locator"), QStringLiteral("parts_overdue_open_context"))) {
        sql = QStringLiteral("SELECT COALESCE(number,0), COALESCE(order_number,0), COALESCE(order_uid,'') FROM router_open_order_locator");
    } else if (hasTableOrView(db, QStringLiteral("work_orders"))) {
        sql = QStringLiteral("SELECT COALESCE(number,0), COALESCE(order_number,0), COALESCE(order_uid,'') FROM work_orders o WHERE %1")
            .arg(openOrderStatusPredicateSql(QStringLiteral("o")));
    }
    if (sql.isEmpty()) return ctx;

    QSqlQuery q(db);
    if (!q.exec(sql)) {
        qWarning().noquote() << QStringLiteral("PARTS_OVERDUE_OPEN_CONTEXT_FAILED error=") << q.lastError().text();
        return ctx;
    }
    while (q.next()) {
        const int internalNumber = q.value(0).toInt();
        const int visibleNumber = q.value(1).toInt();
        const QString uid = q.value(2).toString().trimmed();
        if (internalNumber > 0) ctx.numbers.insert(internalNumber);
        if (visibleNumber > 0) ctx.numbers.insert(visibleNumber);
        if (!uid.isEmpty()) ctx.uids.insert(uid);
    }
    return ctx;
}

void applyOpenOrderContextToPartRow(PartRowSnapshot* row, const OpenOrderIdentityContext& ctx)
{
    if (!row) return;
    const QString uid = row->orderUid.trimmed();
    const int rewriteNumber = rewriteOrderNumberFromUid(uid);
    const bool matchesOpenOrder = (row->orderNumber > 0 && ctx.numbers.contains(row->orderNumber))
        || (rewriteNumber > 0 && ctx.numbers.contains(rewriteNumber))
        || (!uid.isEmpty() && ctx.uids.contains(uid));
    if (!matchesOpenOrder) return;

    row->orderStatus = QStringLiteral("in_progress");
    row->orderClosedAtRaw.clear();
    row->orderClosedDate = QDate();
}


QString attachmentExistsSqlExpr(QSqlDatabase db, const QString& partIdExpr)
{
    if (!hasTableOrView(db, QStringLiteral("work_order_part_attachments"))) return QStringLiteral("0");
    return QStringLiteral("EXISTS(SELECT 1 FROM work_order_part_attachments a WHERE a.part_id=%1)").arg(partIdExpr);
}

QDate effectiveDeliveryDateForPolicy(const PartRowSnapshot& row)
{
    if (row.hasInvoiceAttachment) {
        // Imported invoice rows normally have a PDF attachment. Some historical and
        // Excel-converted invoice rows do not persist a separate delivery_date; in
        // that case the visible invoice/order date is the only reliable business
        // delivery anchor for overdue highlighting.
        if (row.deliveryDate.isValid()) return row.deliveryDate;
        return row.orderedDate;
    }
    if (!row.orderedDate.isValid()) return row.deliveryDate;
    if (!row.deliveryDate.isValid()) return row.orderedDate;

    // Manual rows have no separate delivery-date UI. Older builds saved an empty manual
    // delivery date as the save date, so the row looked fresh and skipped overdue sorting.
    // For manual/no-invoice rows, the visible date column is the business delivery anchor.
    if (row.orderedDate < row.deliveryDate) return row.orderedDate;
    return row.deliveryDate;
}

QDate primarySortDate(const PartRowSnapshot& row)
{
    // Для звичайного/закритого архівного блоку головна дата сортування —
    // видима дата з колонки "Дата" (ordered_at). Приховані opened_at/closed_at
    // не повинні переставляти рядки так, щоб у таблиці дата 06.04 була вище
    // за 07.04 або щоб на одній даті менший номер наряду був вище більшого.
    if (row.orderedDate.isValid()) return row.orderedDate;
    if (row.deliveryDate.isValid()) return row.deliveryDate;
    if (row.orderClosedDate.isValid()) return row.orderClosedDate;
    if (row.orderOpenedDate.isValid()) return row.orderOpenedDate;
    return {};
}

QDate unpaidNotInstalledAnchorDate(const PartRowSnapshot& row)
{
    if (row.deliveryDate.isValid()) return row.deliveryDate;
    if (row.orderedDate.isValid()) return row.orderedDate;
    return {};
}

QDate paidNotInstalledAnchorDate(const PartRowSnapshot& row)
{
    // Для оплачених, але не встановлених позицій старшинство також іде від дати доставки.
    if (row.deliveryDate.isValid()) return row.deliveryDate;
    if (row.orderedDate.isValid()) return row.orderedDate;
    return {};
}

PartOverduePolicy::Input policyInputForRow(const PartRowSnapshot& row)
{
    PartOverduePolicy::Input input;
    input.deliveryDate = effectiveDeliveryDateForPolicy(row);
    input.orderedDate = row.orderedDate;
    input.installedDate = row.installedDate;
    input.orderClosedDate = row.orderClosedDate;
    input.orderStatus = row.orderStatus;
    input.orderClosedAtRaw = row.orderClosedAtRaw;
    input.orderUid = row.orderUid;
    input.orderNumber = row.orderNumber;
    input.rowId = row.id;
    input.paid = row.paid || PartMarkupPolicyService::isCustomerSupplied(row.store);
    return input;
}

void logPartsSortRefresh(const QVector<PartRowSnapshot>& rows)
{
    int criticalRows = 0;
    int warningRows = 0;
    int normalRows = 0;
    int closedRows = 0;
    for (const PartRowSnapshot& row : rows) {
        if (PartOverduePolicy::isCriticalRank(row.overdueRank)) ++criticalRows;
        else if (PartOverduePolicy::isWarningRank(row.overdueRank)) ++warningRows;
        else if (row.overdueRank == PartOverduePolicy::RankFinalizedOrClosed) ++closedRows;
        else ++normalRows;
    }
    if (RuntimeLoggingControl::deepDebugEnabled()) {
        qInfo().noquote() << QStringLiteral("PARTS_SORT_REFRESH totalRows=") << rows.size()
                          << QStringLiteral(" criticalRows=") << criticalRows
                          << QStringLiteral(" warningRows=") << warningRows
                          << QStringLiteral(" normalRows=") << normalRows
                          << QStringLiteral(" closedRows=") << closedRows;
    }
}

enum PartArchiveSortBucket {
    BucketCriticalUnpaidNotInstalled = PartOverduePolicy::RankCriticalUnpaidNotInstalled,
    BucketCriticalInstalledNotPaid = PartOverduePolicy::RankCriticalInstalledNotPaid,
    BucketWarningUnpaidNotInstalled = PartOverduePolicy::RankWarningUnpaidNotInstalled,
    BucketWarningInstalledNotPaid = PartOverduePolicy::RankWarningInstalledNotPaid,
    BucketActiveUnpaidNotOverdue = PartOverduePolicy::RankActiveUnpaidNotOverdue,
    BucketActivePaidNotInstalled = PartOverduePolicy::RankActivePaidNotInstalled,
    BucketFinalizedOrClosed = PartOverduePolicy::RankFinalizedOrClosed,
};

int stateSortBucket(const PartRowSnapshot& row)
{
    return row.overdueRank;
}

bool lessDateAsc(const QDate& a, const QDate& b)
{
    if (a.isValid() != b.isValid()) return a.isValid();
    if (!a.isValid()) return false;
    return a < b;
}

bool lessDateDesc(const QDate& a, const QDate& b)
{
    if (a.isValid() != b.isValid()) return a.isValid();
    if (!a.isValid()) return false;
    return a > b;
}

bool compareUnpaidNotInstalledBucket(const PartRowSnapshot& a, const PartRowSnapshot& b)
{
    const QDate aAnchor = unpaidNotInstalledAnchorDate(a);
    const QDate bAnchor = unpaidNotInstalledAnchorDate(b);
    if (aAnchor != bAnchor) return lessDateAsc(aAnchor, bAnchor);

    if (a.deliveryDate != b.deliveryDate) return lessDateAsc(a.deliveryDate, b.deliveryDate);
    if (a.orderedDate != b.orderedDate) return lessDateAsc(a.orderedDate, b.orderedDate);
    if (a.orderNumber != b.orderNumber) return a.orderNumber > b.orderNumber;
    return a.id > b.id;
}

bool compareInstalledNotPaidBucket(const PartRowSnapshot& a, const PartRowSnapshot& b)
{
    // Rank 1/3 мають ті самі business tie-breakers, що й rank 0/2:
    // delivery_date ASC, ordered_at ASC, order number DESC, part.id DESC.
    // installed_at не має піднімати/опускати рядок усередині проблемного рангу.
    if (a.deliveryDate != b.deliveryDate) return lessDateAsc(a.deliveryDate, b.deliveryDate);
    if (a.orderedDate != b.orderedDate) return lessDateAsc(a.orderedDate, b.orderedDate);
    if (a.orderNumber != b.orderNumber) return a.orderNumber > b.orderNumber;
    return a.id > b.id;
}

bool comparePaidNotInstalledBucket(const PartRowSnapshot& a, const PartRowSnapshot& b)
{
    const QDate aAnchor = paidNotInstalledAnchorDate(a);
    const QDate bAnchor = paidNotInstalledAnchorDate(b);
    if (aAnchor != bAnchor) return lessDateAsc(aAnchor, bAnchor);

    if (a.deliveryDate != b.deliveryDate) return lessDateAsc(a.deliveryDate, b.deliveryDate);
    if (a.orderedDate != b.orderedDate) return lessDateAsc(a.orderedDate, b.orderedDate);
    if (a.orderNumber != b.orderNumber) return a.orderNumber > b.orderNumber;
    return a.id > b.id;
}

bool comparePaidInstalledBucketNewestFirst(const PartRowSnapshot& a, const PartRowSnapshot& b)
{
    const QDate aPrimary = primarySortDate(a);
    const QDate bPrimary = primarySortDate(b);
    if (aPrimary != bPrimary) return lessDateDesc(aPrimary, bPrimary);

    if (a.orderNumber != b.orderNumber) return a.orderNumber > b.orderNumber;
    if (a.installedDate != b.installedDate) return lessDateAsc(a.installedDate, b.installedDate);
    if (a.deliveryDate != b.deliveryDate) return lessDateDesc(a.deliveryDate, b.deliveryDate);
    return a.id > b.id;
}

bool lessPartRowsForTable(const PartRowSnapshot& a, const PartRowSnapshot& b)
{
    // Closing an order does not settle its supplier purchases.
    if (a.paid != b.paid) return !a.paid;
    const int aBucket = stateSortBucket(a);
    const int bBucket = stateSortBucket(b);
    if (aBucket != bBucket) return aBucket < bBucket;

    switch (aBucket) {
    case BucketCriticalUnpaidNotInstalled:
    case BucketWarningUnpaidNotInstalled:
    case BucketActiveUnpaidNotOverdue:
        return compareUnpaidNotInstalledBucket(a, b);
    case BucketCriticalInstalledNotPaid:
    case BucketWarningInstalledNotPaid:
        return compareInstalledNotPaidBucket(a, b);
    case BucketActivePaidNotInstalled:
        return comparePaidNotInstalledBucket(a, b);
    case BucketFinalizedOrClosed:
    default:
        return comparePaidInstalledBucketNewestFirst(a, b);
    }
}

QString foldSearchConfusables(QString value)
{
    value = value.simplified().trimmed().toLower();
    value.replace(QChar(0x0430), QLatin1Char('a'));
    value.replace(QChar(0x0432), QLatin1Char('b'));
    value.replace(QChar(0x0441), QLatin1Char('c'));
    value.replace(QChar(0x0435), QLatin1Char('e'));
    value.replace(QChar(0x043d), QLatin1Char('h'));
    value.replace(QChar(0x0456), QLatin1Char('i'));
    value.replace(QChar(0x0457), QLatin1Char('i'));
    value.replace(QChar(0x043a), QLatin1Char('k'));
    value.replace(QChar(0x043c), QLatin1Char('m'));
    value.replace(QChar(0x043e), QLatin1Char('o'));
    value.replace(QChar(0x0440), QLatin1Char('p'));
    value.replace(QChar(0x0442), QLatin1Char('t'));
    value.replace(QChar(0x0443), QLatin1Char('y'));
    value.replace(QChar(0x0445), QLatin1Char('x'));
    return value;
}

QString compactSearch(QString value)
{
    value = foldSearchConfusables(value);
    value.remove(QRegularExpression(QStringLiteral(R"([^\p{L}\p{N}]+)")));
    return value;
}

bool isDateLikeSearch(const QString& value)
{
    const QString s = value.trimmed();
    if (s.isEmpty()) return false;
    return QRegularExpression(QStringLiteral(R"(^\d{1,2}([./-]\d{1,2})?([./-]\d{2,4})?([ /]\d{1,2}([.:]\d{1,2})?)?$)"))
        .match(s)
        .hasMatch();
}

int partSearchRank(const PartRowSnapshot& row, const QString& filter)
{
    const QString needle = foldSearchConfusables(filter);
    const QString compactNeedle = compactSearch(filter);
    if (needle.isEmpty()) return 0;

    const bool digitsOnly = !compactNeedle.isEmpty()
        && QRegularExpression(QStringLiteral(R"(^\d+$)")).match(compactNeedle).hasMatch();
    const bool dateLike = isDateLikeSearch(filter);

    auto lowered = [](const QString& value) { return foldSearchConfusables(value); };
    auto compact = [](const QString& value) { return compactSearch(value); };
    auto normalizedPlate = [](const QString& value) { return PlateUtils::normalizePlate(value).trimmed().toLower(); };

    const QString orderText = row.orderDisplay.trimmed().isEmpty() ? QString::number(row.orderNumber) : row.orderDisplay.trimmed();
    const QString articleText = row.article.trimmed();
    const QString brandText = row.brand.trimmed();
    const QString nameText = row.name.trimmed();
    const QString plateText = row.plate.trimmed();
    const QString carText = row.carModel.trimmed();
    const QString vinText = row.vin.trimmed();
    const QString orderedDisplay = PartsQueryService::displayDate(row.dateRaw);
    const QString deliveryDisplay = PartsQueryService::displayDate(row.deliveryDateRaw);

    const QString orderCompact = compact(orderText);
    const QString articleCompact = compact(articleText);
    const QString brandCompact = compact(brandText);
    const QString nameCompact = compact(nameText);
    const QString plateCompact = compact(plateText);
    const QString carCompact = compact(carText);
    const QString vinCompact = compact(vinText);
    const QString orderedCompact = compact(orderedDisplay);
    const QString deliveryCompact = compact(deliveryDisplay);

    const QString needlePlate = normalizedPlate(filter);
    const QString plateNormalized = normalizedPlate(plateText);

    const bool orderExact = !compactNeedle.isEmpty() && orderCompact == compactNeedle;
    const bool oemExact = !compactNeedle.isEmpty() && articleCompact == compactNeedle;
    const bool plateMatch = (!needlePlate.isEmpty() && plateNormalized.contains(needlePlate))
        || (!compactNeedle.isEmpty() && plateCompact.contains(compactNeedle))
        || lowered(plateText).contains(needle);
    const bool dateMatch = (!compactNeedle.isEmpty() && (orderedCompact.startsWith(compactNeedle) || deliveryCompact.startsWith(compactNeedle)))
        || lowered(orderedDisplay).contains(needle)
        || lowered(deliveryDisplay).contains(needle);
    const bool carMatch = lowered(carText).contains(needle)
        || (!compactNeedle.isEmpty() && carCompact.contains(compactNeedle));
    const bool brandMatch = lowered(brandText).contains(needle)
        || (!compactNeedle.isEmpty() && brandCompact.contains(compactNeedle));
    const bool vinMatch = lowered(vinText).contains(needle)
        || (!compactNeedle.isEmpty() && vinCompact.contains(compactNeedle));
    const bool nameMatch = lowered(nameText).contains(needle)
        || (!compactNeedle.isEmpty() && nameCompact.contains(compactNeedle));

    if (digitsOnly) {
        if (orderExact) return 1;
        if (plateMatch) return 2;
        if (dateMatch) return 3;
        if (oemExact) return 4;
        if (vinMatch) return 5;
        if (carMatch) return 6;
        if (brandMatch) return 7;
        if (nameMatch) return 8;
        return 100;
    }

    if (dateLike) {
        if (dateMatch) return 1;
        return 100;
    }

    if (oemExact) return 1;
    if (carMatch) return 2;
    if (plateMatch) return 3;
    if (dateMatch) return 4;
    if (vinMatch) return 5;
    if (brandMatch) return 6;
    if (nameMatch) return 7;
    if (orderExact) return 8;
    return 100;
}

bool lessPartRowsForSearch(const PartRowSnapshot& a, const PartRowSnapshot& b)
{
    if (a.paid != b.paid) return !a.paid;
    const int aBucket = stateSortBucket(a);
    const int bBucket = stateSortBucket(b);
    if (aBucket != bBucket) return aBucket < bBucket;
    if (a.searchRank != b.searchRank) return a.searchRank < b.searchRank;
    return lessPartRowsForTable(a, b);
}


QString normalizedSqlDateExpr(const QString& expr)
{
    return QStringLiteral(
        "CASE "
        "WHEN TRIM(COALESCE(%1,''))='' THEN NULL "
        "WHEN substr(TRIM(%1),5,1)='-' AND substr(TRIM(%1),8,1)='-' THEN "
        "     CASE "
        "         WHEN CAST(substr(TRIM(%1),1,4) AS INTEGER) BETWEEN 1900 AND 1999 THEN date(printf('%04d', CAST(substr(TRIM(%1),1,4) AS INTEGER) + 100) || substr(TRIM(%1),5,6)) "
        "         ELSE date(substr(TRIM(%1),1,10)) "
        "     END "
        "WHEN substr(TRIM(%1),3,1)='.' AND substr(TRIM(%1),6,1)='.' AND length(TRIM(%1))>=10 THEN "
        "     date(substr(TRIM(%1),7,4) || '-' || substr(TRIM(%1),4,2) || '-' || substr(TRIM(%1),1,2)) "
        "WHEN substr(TRIM(%1),3,1)='.' AND substr(TRIM(%1),6,1)='.' AND length(TRIM(%1))>=8 THEN "
        "     date('20' || substr(TRIM(%1),7,2) || '-' || substr(TRIM(%1),4,2) || '-' || substr(TRIM(%1),1,2)) "
        "ELSE NULL END").arg(expr);
}

} // namespace

namespace PartsQueryService {

QDate parseBusinessDate(const QString& raw)
{
    const QString s = raw.trimmed();
    if (s.isEmpty()) return {};

    static const QRegularExpression isoDate(QStringLiteral(R"((\d{4})-(\d{2})-(\d{2}))"));
    static const QRegularExpression dotDateLong(QStringLiteral(R"((\d{2})\.(\d{2})\.(\d{4}))"));
    static const QRegularExpression dotDateShort(QStringLiteral(R"((\d{2})\.(\d{2})\.(\d{2}))"));

    const auto isoMatch = isoDate.match(s);
    if (isoMatch.hasMatch()) {
        int y = isoMatch.captured(1).toInt();
        const int mon = isoMatch.captured(2).toInt();
        const int day = isoMatch.captured(3).toInt();
        if (y >= 1900 && y < 2000) y += 100;
        const QDate d(y, mon, day);
        if (d.isValid()) return d;
    }

    const auto longMatch = dotDateLong.match(s);
    if (longMatch.hasMatch()) {
        QDate d(longMatch.captured(3).toInt(), longMatch.captured(2).toInt(), longMatch.captured(1).toInt());
        if (d.isValid()) return normalizeBusinessDate(d);
    }

    const auto shortMatch = dotDateShort.match(s);
    if (shortMatch.hasMatch()) {
        QDate d(2000 + shortMatch.captured(3).toInt(), shortMatch.captured(2).toInt(), shortMatch.captured(1).toInt());
        if (d.isValid()) return normalizeBusinessDate(d);
    }

    for (const QString& fmt : {QStringLiteral("yyyy-MM-dd"), QStringLiteral("dd.MM.yyyy"), QStringLiteral("dd.MM.yy")}) {
        QDate d = QDate::fromString(s, fmt);
        if (d.isValid()) return normalizeBusinessDate(d);
    }
    return {};
}

QString displayDate(const QString& raw)
{
    const QDate d = parseBusinessDate(raw);
    return d.isValid() ? d.toString(QStringLiteral("dd.MM.yy")) : raw.trimmed();
}


struct PartBackupContext
{
    QString orderUid;
    int orderNumber = 0;
};

PartBackupContext loadPartBackupContext(QSqlDatabase db, int partId)
{
    PartBackupContext ctx;
    if (partId <= 0) return ctx;
    if ((!db.isValid() || !db.isOpen()) && !Database::ensureConnectionAlive(nullptr)) return ctx;
    if (!db.isValid() || !db.isOpen()) db = Database::db();
    if (!db.isValid() || (!db.isOpen() && !db.open())) return ctx;

    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT COALESCE(order_uid,''), COALESCE(order_number,0) FROM work_order_parts WHERE id=? LIMIT 1"));
    q.addBindValue(partId);
    if (!q.exec() || !q.next()) return ctx;
    ctx.orderUid = q.value(0).toString().trimmed();
    ctx.orderNumber = q.value(1).toInt();
    return ctx;
}

PartRowSnapshot loadRowById(int partId, QString* errorText)
{
    return loadRowById(Database::db(), partId, errorText);
}

PartRowSnapshot loadRowById(QSqlDatabase db, int partId, QString* errorText)
{
    if(errorText)errorText->clear();
    PartRowSnapshot row;
    if (partId <= 0) return row;
    if ((!db.isValid() || !db.isOpen()) && !Database::ensureConnectionAlive(errorText)) return row;
    if (!db.isValid() || !db.isOpen()) db = Database::db();
    if (!db.isValid() || (!db.isOpen() && !db.open())) {
        if (errorText) *errorText = db.lastError().text();
        return row;
    }

    const OpenOrderIdentityContext openOrderContext = loadOpenOrderIdentityContext(db);

    auto readFromOrdersTable = [&](const QString& ordersTable) -> bool {
        QSqlQuery q(db);
        const QString attachmentExpr = attachmentExistsSqlExpr(db, QStringLiteral("p.id"));
        const QString sql = QStringLiteral(
            "SELECT p.id, COALESCE(p.ordered_at,''), COALESCE(p.delivery_date,''), COALESCE(p.installed_at,''), "
            "       COALESCE(p.store,''), COALESCE(p.part_number,''), COALESCE(p.manufacturer,''), COALESCE(p.name,''), "
            "       COALESCE(p.qty,0), COALESCE(p.price,0), COALESCE(p.amount,0), "
            "       COALESCE(p.purchase_price,0), "
            "       COALESCE(p.purchase_amount, CASE WHEN COALESCE(p.purchase_price,0)>0 THEN COALESCE(p.purchase_price,0)*COALESCE(p.qty,1) ELSE 0 END), "
            "       COALESCE(p.order_number,0), "
            "       COALESCE(NULLIF(TRIM(COALESCE(p.order_uid,'')),''), NULLIF(TRIM(COALESCE(o.order_uid,'')),''), ''), "
            "       CAST(COALESCE(o.order_number,o.number,p.order_number) AS TEXT), "
            "       COALESCE(o.plate,''), COALESCE(o.car,''), COALESCE(o.vin,''), COALESCE(o.status,''), COALESCE(o.opened_at,''), COALESCE(o.closed_at,''), COALESCE(p.paid,0), "
            "       %2 "
            "FROM work_order_parts p LEFT JOIN %1 o ON "
            "     (NULLIF(TRIM(COALESCE(p.order_uid,'')),'') IS NOT NULL AND NULLIF(TRIM(COALESCE(o.order_uid,'')),'') = NULLIF(TRIM(COALESCE(p.order_uid,'')),'')) "
            "  OR (NULLIF(TRIM(COALESCE(p.order_uid,'')),'') IS NULL AND COALESCE(o.order_number,o.number,0)=COALESCE(p.order_number,0)) "
            "WHERE p.id=? LIMIT 1").arg(ordersTable, attachmentExpr);
        q.prepare(sql);
        q.addBindValue(partId);
        if (!q.exec() || !q.next()) {
            if(q.lastError().isValid() && errorText)*errorText=q.lastError().text();
            return false;
        }
        row.id = q.value(0).toInt();
        row.dateRaw = q.value(1).toString();
        row.deliveryDateRaw = q.value(2).toString();
        row.installedAtRaw = q.value(3).toString();
        row.store = StoreNameNormalizer::canonicalDisplayName(q.value(4).toString());
        row.article = q.value(5).toString();
        row.brand = q.value(6).toString().trimmed();
        row.name = q.value(7).toString();
        row.qty = q.value(8).toDouble();
        row.price = q.value(9).toDouble();
        row.amount = q.value(10).toDouble();
        row.purchasePrice = q.value(11).toDouble();
        row.purchaseAmount = q.value(12).toDouble();
        row.orderNumber = q.value(13).toInt();
        row.orderUid = q.value(14).toString().trimmed();
        row.orderDisplay = q.value(15).toString().trimmed();
        row.plate = PlateUtils::formatPlateDisplay(q.value(16).toString().trimmed());
        row.carModel = q.value(17).toString().trimmed();
        row.vin = q.value(18).toString().trimmed();
        row.orderStatus = q.value(19).toString().trimmed();
        row.orderOpenedAtRaw = q.value(20).toString().trimmed();
        row.orderClosedAtRaw = q.value(21).toString().trimmed();
        row.paid = q.value(22).toInt() != 0;
        row.hasInvoiceAttachment = q.value(23).toInt() != 0;
        QString paymentError;
        const auto paymentBalances=PurchasePaymentService::balances(db,&paymentError);
        if(!paymentError.isEmpty()) {
            if(errorText)*errorText=paymentError;
            row={};
            return false;
        }
        PurchasePaymentService::apply(row, paymentBalances);
        applyOpenOrderContextToPartRow(&row, openOrderContext);
        refreshDerivedFields(&row);
        return row.id > 0;
    };

    // Fall back only when the router is absent, never after a failed routed read.
    readFromOrdersTable(hasTableOrView(db,QStringLiteral("router_work_orders"))
        ? QStringLiteral("router_work_orders") : QStringLiteral("work_orders"));
    return row;
}
bool updatePartDeliveryDate(QSqlDatabase db, int partId, const QString& deliveryDateIso, QString* errorText)
{
    if (partId <= 0) return false;
    if ((!db.isValid() || !db.isOpen()) && !Database::ensureConnectionAlive(errorText)) return false;
    if (!db.isValid() || !db.isOpen()) db = Database::db();
    if (!db.isValid() || (!db.isOpen() && !db.open())) { if (errorText) *errorText = db.lastError().text(); return false; }
    const PartBackupContext backupCtx = loadPartBackupContext(db, partId);
    const PartRowSnapshot beforeRow = loadRowById(db, partId, nullptr);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE work_order_parts SET delivery_date=? WHERE id=?"));
    q.addBindValue(deliveryDateIso.trimmed());
    q.addBindValue(partId);
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    SnapshotCacheInvalidator::invalidateOrdersAndParts(QStringLiteral("PartsQueryService/updatePartDeliveryDate"));
    const PartRowSnapshot afterRow = loadRowById(db, partId, nullptr);
    if (RuntimeLoggingControl::deepDebugEnabled()) {
        qInfo().noquote() << QStringLiteral("PART_DELIVERY_CHANGED partId=") << partId
                          << QStringLiteral(" oldDeliveryDate=") << beforeRow.deliveryDateRaw
                          << QStringLiteral(" newDeliveryDate=") << deliveryDateIso.trimmed()
                          << QStringLiteral(" oldRank=") << beforeRow.overdueRank
                          << QStringLiteral(" newRank=") << afterRow.overdueRank;
    }
    BackupCommitPublisher::publish(
        BackupCommitPublisher::makeEvent(QStringLiteral("PartsQueryService"),
                                         QStringLiteral("parts_archive"),
                                         QStringLiteral("part"),
                                         QString::number(partId),
                                         backupCtx.orderUid,
                                         backupCtx.orderNumber,
                                         QStringLiteral("update_delivery_date"),
                                         QString::fromUtf8("Оновлено дату отримання запчастини"),
                                         {QStringLiteral("work_order_parts")}),
        nullptr);
    return true;
}

PartArchiveDateBounds loadDateBounds(QSqlDatabase db, QString* errorText)
{
    PartArchiveDateBounds bounds;
    if ((!db.isValid() || !db.isOpen()) && !Database::ensureConnectionAlive(errorText)) return bounds;
    if (!db.isValid() || !db.isOpen()) db = Database::db();
    if (!db.isValid() || (!db.isOpen() && !db.open())) { if (errorText) *errorText = db.lastError().text(); return bounds; }

    const QString cacheKey = PartsQueryCache::makeDateBoundsKey(db.databaseName());
    if (PartsQueryCache::tryGetDateBounds(cacheKey, &bounds)) {
        return bounds;
    }

    PerfWatchdog::ScopedTimer timer(QStringLiteral("PartsQueryService::loadDateBounds"), 80);
    QSqlQuery q(db);
    const QString partsSourceSql = unifiedPartsSourceSql(db);
    const QString orderedExpr = normalizedSqlDateExpr(QStringLiteral("src.ordered_at"));
    const QString sql = QStringLiteral(
        "SELECT MIN(%1), MAX(%1) "
        "FROM %2 src "
        "JOIN router_work_orders o ON %3 "
        "WHERE %1 IS NOT NULL").arg(orderedExpr, partsSourceSql, partOrderJoinPredicate(QStringLiteral("src"), QStringLiteral("o")));
    if (!q.exec(indexedPartOrderQuery(sql, partsSourceSql, QStringLiteral("router_work_orders"), QStringLiteral("src")))) {
        if (errorText) *errorText = q.lastError().text();
        return bounds;
    }
    if (q.next()) {
        bounds.minDate = parseBusinessDate(q.value(0).toString());
        bounds.maxDate = parseBusinessDate(q.value(1).toString());
    }
    PartsQueryCache::storeDateBounds(cacheKey, bounds);
    return bounds;
}

PartArchiveDateBounds loadDateBounds(QString* errorText)
{
    return loadDateBounds(Database::db(), errorText);
}

QVector<PartRowSnapshot> loadRows(const QString& activeFilter, const QDate& from, const QDate& to, QString* errorText)
{
    return loadRows(Database::db(), activeFilter, from, to, errorText);
}

QVector<PartRowSnapshot> loadRows(QSqlDatabase db, const QString& activeFilter, const QDate& from, const QDate& to, QString* errorText)
{
    if(errorText)errorText->clear();
    RuntimeJob::RuntimeJobDispatcher::warnIfUiThreadBlockingCall(QStringLiteral("PartsQueryService::loadRows"));
    QVector<PartRowSnapshot> rows;
    if ((!db.isValid() || !db.isOpen()) && !Database::ensureConnectionAlive(errorText)) return rows;
    if (!db.isValid() || !db.isOpen()) db = Database::db();
    if (!db.isValid() || (!db.isOpen() && !db.open())) { if (errorText) *errorText = db.lastError().text(); return rows; }
    const bool hasTextFilter = !activeFilter.trimmed().isEmpty();
    const bool applyDateWindow = !hasTextFilter && from.isValid() && to.isValid();
    const QString cacheKey = PartsQueryCache::makeRowsKey(db.databaseName(), activeFilter, from, to);
    if (PartsQueryCache::tryGetRows(cacheKey, &rows)) {
        const QDate today = QDate::currentDate();
        for (PartRowSnapshot& cachedRow : rows) {
            refreshDerivedFields(&cachedRow, today);
        }
        if (activeFilter.trimmed().isEmpty()) sortRowsForTable(&rows, today);
        else std::stable_sort(rows.begin(), rows.end(), lessPartRowsForSearch);
        logPartsSortRefresh(rows);
        return rows;
    }
    PerfWatchdog::ScopedTimer timer(QStringLiteral("PartsQueryService::loadRows"), 120);

    const QString partsSourceSql = unifiedPartsSourceSql(db);
    const QString orderedExpr = normalizedSqlDateExpr(QStringLiteral("p.ordered_at"));
    const QString attachmentExpr = attachmentExistsSqlExpr(db, QStringLiteral("p.id"));
    QString sql =
        "SELECT p.id, "
        "       COALESCE(p.ordered_at,''), "
        "       COALESCE(p.delivery_date,''), "
        "       COALESCE(p.installed_at,''), "
        "       COALESCE(p.store,''), "
        "       COALESCE(p.part_number,''), "
        "       COALESCE(p.manufacturer,''), "
        "       COALESCE(p.name,''), "
        "       COALESCE(p.qty,0), "
        "       COALESCE(p.price,0), "
        "       COALESCE(p.amount,0), "
        "       COALESCE(p.purchase_price,0), "
        "       COALESCE(p.purchase_amount, CASE WHEN COALESCE(p.purchase_price,0)>0 THEN COALESCE(p.purchase_price,0)*COALESCE(p.qty,1) ELSE 0 END), "
        "       COALESCE(p.order_number,0), "
        "       COALESCE(NULLIF(TRIM(COALESCE(p.order_uid,'')),''), NULLIF(TRIM(COALESCE(o.order_uid,'')),''), ''), "
        "       CAST(COALESCE(o.order_number,o.number,p.order_number) AS TEXT), "
        "       COALESCE(o.plate,''), "
        "       COALESCE(o.car,''), "
        "       COALESCE(o.vin,''), "
        "       COALESCE(o.status,''), "
        "       COALESCE(o.opened_at,''), "
        "       COALESCE(o.closed_at,''), "
        "       COALESCE(p.paid,0), "
        "       %2 "
        "FROM %1 p ";
    sql = sql.arg(partsSourceSql, attachmentExpr);
    sql += QStringLiteral("JOIN router_work_orders o ON %1 ").arg(partOrderJoinPredicate(QStringLiteral("p"), QStringLiteral("o")));

    if (applyDateWindow) {
        sql += QStringLiteral("WHERE (%1) >= date(?) AND (%1) <= date(?) ").arg(orderedExpr);
    }

    sql += QStringLiteral(
        "ORDER BY CASE "
        "           WHEN LENGTH(TRIM(COALESCE(p.ordered_at,'')))=8 THEN '20' || SUBSTR(TRIM(COALESCE(p.ordered_at,'')),7,2) || '-' || SUBSTR(TRIM(COALESCE(p.ordered_at,'')),4,2) || '-' || SUBSTR(TRIM(COALESCE(p.ordered_at,'')),1,2) "
        "           WHEN LENGTH(TRIM(COALESCE(p.ordered_at,'')))=10 AND SUBSTR(TRIM(COALESCE(p.ordered_at,'')),5,1)='-' THEN "
        "                CASE "
        "                    WHEN CAST(SUBSTR(TRIM(COALESCE(p.ordered_at,'')),1,4) AS INTEGER) BETWEEN 1900 AND 1999 "
        "                        THEN printf('%04d', CAST(SUBSTR(TRIM(COALESCE(p.ordered_at,'')),1,4) AS INTEGER) + 100) || SUBSTR(TRIM(COALESCE(p.ordered_at,'')),5) "
        "                    ELSE TRIM(COALESCE(p.ordered_at,'')) "
        "                END "
        "           ELSE '' "
        "         END DESC, CAST(COALESCE(p.order_number,0) AS INTEGER) DESC, p.id DESC");

    QSqlQuery q(db);
    q.setForwardOnly(true);
    q.prepare(indexedPartOrderQuery(sql, partsSourceSql, QStringLiteral("router_work_orders"), QStringLiteral("p")));
    if (applyDateWindow) {
        q.addBindValue(from.toString(Qt::ISODate));
        q.addBindValue(to.toString(Qt::ISODate));
    }
    if (!q.exec()) {
        const QString firstError = q.lastError().text();
        QString legacySql =
            "SELECT p.id, "
            "       COALESCE(p.ordered_at,''), "
            "       COALESCE(p.delivery_date,''), "
            "       COALESCE(p.installed_at,''), "
            "       COALESCE(p.store,''), "
            "       COALESCE(p.part_number,''), "
            "       COALESCE(p.manufacturer,''), "
            "       COALESCE(p.name,''), "
            "       COALESCE(p.qty,0), "
            "       COALESCE(p.price,0), "
            "       COALESCE(p.amount,0), "
            "       COALESCE(p.purchase_price,0), "
            "       COALESCE(p.purchase_amount, CASE WHEN COALESCE(p.purchase_price,0)>0 THEN COALESCE(p.purchase_price,0)*COALESCE(p.qty,1) ELSE 0 END), "
            "       COALESCE(p.order_number,0), "
            "       COALESCE(NULLIF(TRIM(COALESCE(p.order_uid,'')),''), NULLIF(TRIM(COALESCE(o.order_uid,'')),''), ''), "
            "       CAST(COALESCE(o.order_number,o.number,p.order_number) AS TEXT), "
            "       COALESCE(o.plate,''), "
            "       COALESCE(o.car,''), "
            "       COALESCE(o.vin,''), "
            "       COALESCE(o.status,''), "
            "       COALESCE(o.opened_at,''), "
            "       COALESCE(o.closed_at,''), "
            "       COALESCE(p.paid,0), "
            "       %1 "
            "FROM work_order_parts p ";
        legacySql = legacySql.arg(attachmentExpr);
        legacySql += QStringLiteral("JOIN work_orders o ON %1 ").arg(partOrderJoinPredicate(QStringLiteral("p"), QStringLiteral("o")));
        if (applyDateWindow) {
            legacySql += QStringLiteral("WHERE (%1) >= date(?) AND (%1) <= date(?) ").arg(orderedExpr);
        }
        legacySql += QStringLiteral(
            "ORDER BY CASE "
            "           WHEN LENGTH(TRIM(COALESCE(p.ordered_at,'')))=8 THEN '20' || SUBSTR(TRIM(COALESCE(p.ordered_at,'')),7,2) || '-' || SUBSTR(TRIM(COALESCE(p.ordered_at,'')),4,2) || '-' || SUBSTR(TRIM(COALESCE(p.ordered_at,'')),1,2) "
            "           WHEN LENGTH(TRIM(COALESCE(p.ordered_at,'')))=10 AND SUBSTR(TRIM(COALESCE(p.ordered_at,'')),5,1)='-' THEN "
            "                CASE "
            "                    WHEN CAST(SUBSTR(TRIM(COALESCE(p.ordered_at,'')),1,4) AS INTEGER) BETWEEN 1900 AND 1999 "
            "                        THEN printf('%04d', CAST(SUBSTR(TRIM(COALESCE(p.ordered_at,'')),1,4) AS INTEGER) + 100) || SUBSTR(TRIM(COALESCE(p.ordered_at,'')),5) "
            "                    ELSE TRIM(COALESCE(p.ordered_at,'')) "
            "                END "
            "           ELSE '' "
            "         END DESC, CAST(COALESCE(p.order_number,0) AS INTEGER) DESC, p.id DESC");
        q.clear();
        q.prepare(indexedPartOrderQuery(legacySql, QStringLiteral("work_order_parts"), QStringLiteral("work_orders"), QStringLiteral("p")));
        if (applyDateWindow) {
            q.addBindValue(from.toString(Qt::ISODate));
            q.addBindValue(to.toString(Qt::ISODate));
        }
        if (!q.exec()) {
            if (errorText) *errorText = firstError + QStringLiteral("\n") + q.lastError().text();
            return rows;
        }
    }

    rows.reserve(256);
    const QString compactFilter = compactSearch(activeFilter);
    const bool digitsOnly = !compactFilter.isEmpty()
        && QRegularExpression(QStringLiteral(R"(^\d+$)")).match(compactFilter).hasMatch();
    QSet<QString> allowedOrderUids;
    if (digitsOnly) {
        bool visibleNumberOk = false;
        const int visibleNumber = compactFilter.toInt(&visibleNumberOk);
        if (visibleNumberOk && visibleNumber > 0) {
            const auto hits = OrderSearchAggregator::findByVisibleNumber(visibleNumber, 500, nullptr);
            for (const auto& hit : hits) {
                const QString uid = hit.orderUid.trimmed();
                if (!uid.isEmpty()) allowedOrderUids.insert(uid);
            }
        }
    }

    const OpenOrderIdentityContext openOrderContext = loadOpenOrderIdentityContext(db);

    QString paymentError;
    const auto paymentBalances = PurchasePaymentService::balances(db, &paymentError);
    if(!paymentError.isEmpty()) {
        if(errorText)*errorText=paymentError;
        return {};
    }
    while (q.next()) {
        PartRowSnapshot rowData;
        rowData.id = q.value(0).toInt();
        rowData.dateRaw = q.value(1).toString();
        rowData.deliveryDateRaw = q.value(2).toString();
        rowData.installedAtRaw = q.value(3).toString();
        rowData.store = StoreNameNormalizer::canonicalDisplayName(q.value(4).toString());
        rowData.article = q.value(5).toString();
        rowData.brand = q.value(6).toString().trimmed();
        rowData.name = q.value(7).toString();
        rowData.qty = q.value(8).toDouble();
        rowData.price = q.value(9).toDouble();
        rowData.amount = q.value(10).toDouble();
        rowData.purchasePrice = q.value(11).toDouble();
        rowData.purchaseAmount = q.value(12).toDouble();
        rowData.orderNumber = q.value(13).toInt();
        rowData.orderUid = q.value(14).toString().trimmed();
        rowData.orderDisplay = q.value(15).toString().trimmed();
        rowData.plate = PlateUtils::formatPlateDisplay(q.value(16).toString().trimmed());
        rowData.carModel = q.value(17).toString().trimmed();
        rowData.vin = q.value(18).toString().trimmed();
        rowData.orderStatus = q.value(19).toString().trimmed();
        rowData.orderOpenedAtRaw = q.value(20).toString().trimmed();
        rowData.orderClosedAtRaw = q.value(21).toString().trimmed();
        rowData.paid = q.value(22).toInt() != 0;
        rowData.hasInvoiceAttachment = q.value(23).toInt() != 0;
        PurchasePaymentService::apply(rowData, paymentBalances);
        applyOpenOrderContextToPartRow(&rowData, openOrderContext);
        rowData.orderedDate = parseBusinessDate(rowData.dateRaw);
        rowData.deliveryDate = parseBusinessDate(rowData.deliveryDateRaw);
        rowData.installedDate = parseBusinessDate(rowData.installedAtRaw);
        refreshDerivedFields(&rowData);
        rowData.searchRank = partSearchRank(rowData, activeFilter);
        if (digitsOnly && !allowedOrderUids.isEmpty() && !allowedOrderUids.contains(rowData.orderUid.trimmed())) {
            continue;
        }
        if (digitsOnly && allowedOrderUids.isEmpty()) {
            continue;
        }
        rows.push_back(rowData);
    }

    if(q.lastError().isValid()) {
        if(errorText)*errorText=q.lastError().text();
        return {};
    }

    if (activeFilter.trimmed().isEmpty()) sortRowsForTable(&rows);
    else std::stable_sort(rows.begin(), rows.end(), lessPartRowsForSearch);
    logPartsSortRefresh(rows);
    PartsQueryCache::storeRows(cacheKey, rows);
    return rows;
}


void refreshDerivedFields(PartRowSnapshot* row, const QDate& today)
{
    if (!row) return;

    // Keep derived dates self-healing. Business overdue is calculated only from delivery_date.
    row->orderedDate = parseBusinessDate(row->dateRaw);
    row->deliveryDate = parseBusinessDate(row->deliveryDateRaw);
    row->installedDate = parseBusinessDate(row->installedAtRaw);
    row->orderOpenedDate = parseBusinessDate(row->orderOpenedAtRaw);
    row->orderClosedDate = parseBusinessDate(row->orderClosedAtRaw);

    const QDate effectiveDelivery = effectiveDeliveryDateForPolicy(*row);
    if (effectiveDelivery.isValid() && effectiveDelivery != row->deliveryDate) {
        qDebug().noquote()
            << QStringLiteral("PART_MANUAL_EFFECTIVE_DELIVERY partId=") << row->id
            << QStringLiteral(" orderNumber=") << row->orderNumber
            << QStringLiteral(" orderedAt=") << row->dateRaw
            << QStringLiteral(" storedDelivery=") << row->deliveryDateRaw
            << QStringLiteral(" effectiveDelivery=") << effectiveDelivery.toString(Qt::ISODate);
        row->deliveryDate = effectiveDelivery;
    }

    row->installed = row->installedDate.isValid();
    const PartOverduePolicy::Result policy = PartOverduePolicy::evaluate(policyInputForRow(*row), today);
    row->highlightEligibleOrder = policy.activeOrder;
    row->overdueRank = policy.rank;
    row->overdueDays = policy.overdueDays;
    row->overdueSeverity = policy.severity;
    row->overdue = policy.overdue;
}

void sortRowsForTable(QVector<PartRowSnapshot>* rows, const QDate& today)
{
    if (!rows) return;
    const QDate effectiveToday = PartOverduePolicy::businessToday(today);
    for (PartRowSnapshot& row : *rows) refreshDerivedFields(&row, effectiveToday);
    if (rows->size() < 2) return;
    std::stable_sort(rows->begin(), rows->end(), lessPartRowsForTable);
}

void invalidateCache()
{
    PartsQueryCache::invalidate();
}

} // namespace PartsQueryService

namespace PartsQueryService {
QVector<PartRowSnapshot> loadScopedRows(const QString& filter,const QDate& from,const QDate& to,
    bool masterOnly,const QString& account,QString* error) {
    auto db=Database::db();QString failure;
    auto rows=loadRows(db,filter,from,to,&failure);
    if(masterOnly) {
        const auto scope=AccessControlService::masterOrderScope(db,account);
        rows.erase(std::remove_if(rows.begin(),rows.end(),[&](const auto& row){return !scope.contains(row.orderUid,row.orderNumber);}),rows.end());
    }
    if(error)*error=failure;
    if(!failure.isEmpty())rows.clear();return rows;
}
}
