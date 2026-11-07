#include "core/util/DateFormats.h"
#include "core/services/orders/CurrentOrdersQueryService.h"
#include "core/services/orders/OrderMutationFacade.h"
#include "core/services/orders/BaySnapshot.h"
#include "core/services/AccessControlService.h"
#include "core/diagnostics/RuntimeLoggingControl.h"

#include "core/services/OrderSearchAggregator.h"
#include "core/services/OrderSearchIndex.h"
#include "core/services/HistoryQueryService.h"
#include "core/services/OrderStoragePolicyService.h"
#include "core/services/PartsQueryService.h"
#include "core/services/PartOverduePolicy.h"
#include "infrastructure/db/sqlite/Db.h"
#include "core/perf/PerfWatchdog.h"
#include "runtime/RuntimeJobDispatcher.h"

#include <QDebug>
#include <QHash>
#include <QDate>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QStringList>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QtGlobal>
#include <algorithm>

namespace {

void appendWorkshopPosts(CurrentOrdersQueryResult& result)
{
    const auto posts=BaySnapshot::currentPosts();
    const int uid=result.headers.indexOf("__order_uid");
    const int position=qMin(1,result.headers.size());
    result.headers.insert(position,QString::fromUtf8("Пост"));
    for(auto& row:result.rows) {
        const int post=uid>=0?posts.value(row.value(uid).toString(),-1):-1;
        row.insert(position,post>0?QVariant(post):QVariant(post==0?QString::fromUtf8("Поза постом"):QString::fromUtf8("Невідомо")));
    }
}

QStringList cachedTableAndViewNames()
{
    // CurrentOrdersPage::refresh used to call db.tables() repeatedly from many
    // helper branches. On large SQLite files this becomes visible UI latency,
    // so cache the schema name list for a short window during one refresh cycle.
    static QStringList cachedNames;
    static QElapsedTimer cacheTimer;
    if (!cacheTimer.isValid() || cacheTimer.elapsed() > 3000) {
        const QSqlDatabase db = Database::db();
        cachedNames = (db.isValid() && db.isOpen())
            ? db.tables(static_cast<QSql::TableType>(QSql::Tables | QSql::Views))
            : QStringList{};
        cacheTimer.restart();
    }
    return cachedNames;
}

bool hasTableOrView(const QString& name)
{
    const QStringList names = cachedTableAndViewNames();
    for (const QString& existing : names) {
        if (existing.compare(name, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

int scalarIntOrMinusOne(const QString& sql, QString* errorText = nullptr)
{
    QSqlQuery q(Database::db());
    if (!q.exec(sql)) {
        if (errorText) *errorText = q.lastError().text();
        return -1;
    }
    return q.next() ? q.value(0).toInt() : -1;
}

bool tableOrViewUsableForSelect(const QString& name, const QString& context)
{
    if (!hasTableOrView(name)) return false;

    QString errorText;
    const int count = scalarIntOrMinusOne(QStringLiteral("SELECT COUNT(*) FROM ") + name, &errorText);
    if (count < 0) {
        qWarning().noquote() << QStringLiteral("CURRENT_ORDERS_SOURCE_RECOVERY reason=queryable_check_failed context=%1 source=%2 error=%3")
            .arg(context, name, errorText);
        return false;
    }
    return true;
}

QString openOrdersNativePredicateSql(const QString& alias)
{
    return QStringLiteral(
        "(NOT %2 "
        "AND LOWER(TRIM(COALESCE(%1.status,''))) NOT IN ('planned','заплановано','Заплановано','deleted','closed','Закритий','Закрито','Виконаний','Виконано','закритий','закрито','виконаний','виконано','done','completed','paid','оплачено','Оплачено','archived','archive','архів','Архів'))")
        .arg(alias, OrderStoragePolicyService::closedOrderPredicate(alias));
}

bool routerOpenOrderLocatorUsable()
{
    if (!hasTableOrView(QStringLiteral("router_open_order_locator"))) return false;

    QString routerError;
    const int routerOpenCount = scalarIntOrMinusOne(QStringLiteral("SELECT COUNT(*) FROM router_open_order_locator"), &routerError);
    if (routerOpenCount < 0) {
        qWarning().noquote() << QStringLiteral("CURRENT_ORDERS_SOURCE_RECOVERY reason=router_open_query_failed error=") << routerError;
        return false;
    }
    if (routerOpenCount > 0) return true;

    if (!hasTableOrView(QStringLiteral("work_orders"))) return true;

    QString nativeError;
    const QString nativeSql = QStringLiteral("SELECT COUNT(*) FROM work_orders o WHERE %1").arg(openOrdersNativePredicateSql(QStringLiteral("o")));
    const int nativeOpenCount = scalarIntOrMinusOne(nativeSql, &nativeError);
    if (nativeOpenCount > 0) {
        qWarning().noquote() << QStringLiteral("CURRENT_ORDERS_SOURCE_RECOVERY reason=router_open_empty nativeOpenCount=") << nativeOpenCount;
        return false;
    }
    if (nativeOpenCount < 0) {
        qWarning().noquote() << QStringLiteral("CURRENT_ORDERS_SOURCE_RECOVERY native_count_failed error=") << nativeError;
    }
    return true;
}

QString foldSearchConfusables(QString value)
{
    value = value.toLower();
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

QString compactSqlExpr(const QString& expr)
{
    QString sql = QStringLiteral("LOWER(COALESCE(%1,''))").arg(expr);
    sql = QStringLiteral("REPLACE(%1,'а','a')").arg(sql);
    sql = QStringLiteral("REPLACE(%1,'в','b')").arg(sql);
    sql = QStringLiteral("REPLACE(%1,'с','c')").arg(sql);
    sql = QStringLiteral("REPLACE(%1,'е','e')").arg(sql);
    sql = QStringLiteral("REPLACE(%1,'н','h')").arg(sql);
    sql = QStringLiteral("REPLACE(%1,'і','i')").arg(sql);
    sql = QStringLiteral("REPLACE(%1,'ї','i')").arg(sql);
    sql = QStringLiteral("REPLACE(%1,'к','k')").arg(sql);
    sql = QStringLiteral("REPLACE(%1,'м','m')").arg(sql);
    sql = QStringLiteral("REPLACE(%1,'о','o')").arg(sql);
    sql = QStringLiteral("REPLACE(%1,'р','p')").arg(sql);
    sql = QStringLiteral("REPLACE(%1,'т','t')").arg(sql);
    sql = QStringLiteral("REPLACE(%1,'у','y')").arg(sql);
    sql = QStringLiteral("REPLACE(%1,'х','x')").arg(sql);
    sql = QStringLiteral("REPLACE(%1,' ','')").arg(sql);
    sql = QStringLiteral("REPLACE(%1,'-','')").arg(sql);
    sql = QStringLiteral("REPLACE(%1,'_','')").arg(sql);
    return sql;
}

QString displayDateSqlExpr(const QString& expr)
{
    return QStringLiteral(
        "CASE "
        "WHEN TRIM(COALESCE(%1,''))='' THEN '' "
        "WHEN substr(TRIM(%1),3,1)='.' AND substr(TRIM(%1),6,1)='.' AND substr(TRIM(%1),9,1)='/' THEN substr(TRIM(%1),1,8) "
        "WHEN substr(TRIM(%1),3,1)='.' AND substr(TRIM(%1),6,1)='.' THEN substr(TRIM(%1),1,8) "
        "WHEN substr(TRIM(%1),5,1)='-' AND substr(TRIM(%1),8,1)='-' THEN substr(TRIM(%1),9,2) || '.' || substr(TRIM(%1),6,2) || '.' || substr(TRIM(%1),3,2) "
        "ELSE '' END").arg(expr);
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


QString sqlQuote(QString value)
{
    value.replace(QLatin1Char('\''), QLatin1String("''"));
    return value;
}

QString escapeLike(const QString& s)
{
    QString out = s;
    out.replace(QLatin1String("\\"), QLatin1String("\\\\"));
    out.replace(QLatin1String("%"), QLatin1String("\\%"));
    out.replace(QLatin1String("_"), QLatin1String("\\_"));
    return out;
}


QString currentOrderPartSourceSelectSql(const QString& tableName, const QString& alias)
{
    return QStringLiteral(
        "SELECT %1.id, %1.order_uid, %1.order_number, %1.ordered_at, %1.delivery_date, %1.installed_at, "
        "       %1.store, %1.part_number, %1.name, %1.manufacturer, %1.note, %1.qty, %1.price, %1.amount, "
        "       %1.service_id, %1.ordered_by_admin, %1.paid, %1.purchase_price, %1.purchase_amount, %1.markup_percent "
        "  FROM %2 %1")
        .arg(alias, tableName);
}

QString currentOrderPartDuplicatePredicate(const QString& existingAlias, const QString& candidateAlias)
{
    return QStringLiteral(
        "((TRIM(COALESCE(%2.order_uid,''))<>'' AND TRIM(COALESCE(%1.order_uid,''))=TRIM(COALESCE(%2.order_uid,''))) "
        " OR (TRIM(COALESCE(%2.order_uid,''))='' AND COALESCE(%1.order_number,0)=COALESCE(%2.order_number,0))) "
        "AND COALESCE(%1.ordered_at,'')=COALESCE(%2.ordered_at,'') "
        "AND COALESCE(%1.delivery_date,'')=COALESCE(%2.delivery_date,'') "
        "AND COALESCE(%1.installed_at,'')=COALESCE(%2.installed_at,'') "
        "AND COALESCE(%1.store,'')=COALESCE(%2.store,'') "
        "AND COALESCE(%1.part_number,'')=COALESCE(%2.part_number,'') "
        "AND COALESCE(%1.manufacturer,'')=COALESCE(%2.manufacturer,'') "
        "AND COALESCE(%1.name,'')=COALESCE(%2.name,'') "
        "AND COALESCE(%1.note,'')=COALESCE(%2.note,'') "
        "AND ABS(COALESCE(%1.qty,0)-COALESCE(%2.qty,0))<0.0001 "
        "AND ABS(COALESCE(%1.price,0)-COALESCE(%2.price,0))<0.0001 "
        "AND ABS(COALESCE(%1.amount,0)-COALESCE(%2.amount,0))<0.0001 "
        "AND COALESCE(%1.paid,0)=COALESCE(%2.paid,0)")
        .arg(existingAlias, candidateAlias);
}

QString unifiedPartsSourceSql()
{
    const bool hasWorkParts = hasTableOrView(QStringLiteral("work_order_parts"));
    const bool hasCacheParts = hasTableOrView(QStringLiteral("router_cache_work_order_parts"));
    const bool hasRouterParts = tableOrViewUsableForSelect(QStringLiteral("router_work_order_parts"), QStringLiteral("current_orders_parts_source"));

    QStringList selects;
    if (hasWorkParts) {
        selects << currentOrderPartSourceSelectSql(QStringLiteral("work_order_parts"), QStringLiteral("p"));
    }
    if (hasCacheParts) {
        QString select = currentOrderPartSourceSelectSql(QStringLiteral("router_cache_work_order_parts"), QStringLiteral("c"));
        if (hasWorkParts) {
            select += QStringLiteral(" WHERE NOT EXISTS (SELECT 1 FROM work_order_parts w WHERE ")
                + currentOrderPartDuplicatePredicate(QStringLiteral("w"), QStringLiteral("c"))
                + QStringLiteral(")");
        }
        selects << select;
    }
    if (hasRouterParts) {
        QString select = currentOrderPartSourceSelectSql(QStringLiteral("router_work_order_parts"), QStringLiteral("r"));
        QStringList antiDuplicates;
        if (hasWorkParts) {
            antiDuplicates << (QStringLiteral("NOT EXISTS (SELECT 1 FROM work_order_parts w WHERE ")
                + currentOrderPartDuplicatePredicate(QStringLiteral("w"), QStringLiteral("r"))
                + QStringLiteral(")"));
        }
        if (hasCacheParts) {
            antiDuplicates << (QStringLiteral("NOT EXISTS (SELECT 1 FROM router_cache_work_order_parts c WHERE ")
                + currentOrderPartDuplicatePredicate(QStringLiteral("c"), QStringLiteral("r"))
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

QString orderPartJoinPredicate(const QString& orderAlias, const QString& partAlias)
{
    return QStringLiteral(
        "((TRIM(COALESCE(%2.order_uid,''))<>'' AND LOWER(TRIM(COALESCE(%2.order_uid,''))) NOT LIKE 'rewrite\_%' ESCAPE '\' "
        "  AND TRIM(COALESCE(%2.order_uid,''))=TRIM(COALESCE(%1.order_uid,''))) "
        "OR (COALESCE(%2.order_number,0)>0 AND COALESCE(%2.order_number,0)=COALESCE(%1.number,0)))")
        .arg(orderAlias, partAlias);
}

bool isSyntheticRewriteUid(const QString& uid)
{
    return uid.trimmed().startsWith(QStringLiteral("rewrite_"), Qt::CaseInsensitive);
}

int rewriteOrderNumberFromUid(const QString& uid)
{
    const QString normalized = uid.trimmed().toLower();
    if (!normalized.startsWith(QStringLiteral("rewrite_"))) return 0;
    bool ok = false;
    const int number = normalized.mid(QStringLiteral("rewrite_").size()).toInt(&ok);
    return ok && number > 0 ? number : 0;
}

QString orderAggregateKey(const QString& uid, int number)
{
    const QString trimmedUid = uid.trimmed();
    if (!trimmedUid.isEmpty() && !isSyntheticRewriteUid(trimmedUid)) {
        return QStringLiteral("uid:%1").arg(trimmedUid);
    }
    const int effectiveNumber = number > 0 ? number : rewriteOrderNumberFromUid(trimmedUid);
    return QStringLiteral("num:%1").arg(effectiveNumber);
}

int headerIndex(const CurrentOrdersQueryResult& result, const QString& header)
{
    for (int i = 0; i < result.headers.size(); ++i) {
        if (result.headers.at(i) == header) return i;
    }
    return -1;
}

QDate parseDateForSort(const QVariant& value)
{
    const QString raw = value.toString().trimmed();
    if (raw.isEmpty()) return {};
    QDate d = DateFormats::parseDate(raw.left(10));
    if (d.isValid()) return d;
    d = QDate::fromString(raw.left(10), QStringLiteral("dd.MM.yyyy"));
    if (d.isValid()) return d;
    d = QDate::fromString(raw.left(8), QStringLiteral("dd.MM.yy"));
    if (d.isValid()) return d;
    return {};
}

QString currentOrdersPartWhereSql(const CurrentOrdersQueryResult& result, int uidCol, int numberCol)
{
    QStringList uidLiterals;
    QStringList numberLiterals;
    for (const QVector<QVariant>& row : result.rows) {
        const QString uid = uidCol >= 0 ? row.value(uidCol).toString().trimmed() : QString();
        const int number = numberCol >= 0 ? row.value(numberCol).toInt() : 0;
        if (!uid.isEmpty()) {
            const QString quoted = QStringLiteral("'%1'").arg(sqlQuote(uid));
            if (!uidLiterals.contains(quoted)) uidLiterals << quoted;
        }
        if (number > 0) {
            const QString value = QString::number(number);
            if (!numberLiterals.contains(value)) numberLiterals << value;
            const QString rewriteUid = QStringLiteral("'rewrite_%1'").arg(number);
            if (!uidLiterals.contains(rewriteUid)) uidLiterals << rewriteUid;
        }
    }

    QStringList predicates;
    if (!uidLiterals.isEmpty()) {
        predicates << (QStringLiteral("TRIM(COALESCE(p.order_uid,'')) IN (")
            + uidLiterals.join(QStringLiteral(",")) + QStringLiteral(")"));
    }
    if (!numberLiterals.isEmpty()) {
        predicates << (QStringLiteral("COALESCE(p.order_number,0) IN (")
            + numberLiterals.join(QStringLiteral(",")) + QStringLiteral(")"));
    }
    if (predicates.isEmpty()) return QStringLiteral("WHERE 1=0");
    return QStringLiteral("WHERE ") + predicates.join(QStringLiteral(" OR "));
}

bool loadCurrentOrdersOverdueAggregates(const QString& partsSourceSql,
                                        const QString& currentOrdersPartWhereSql,
                                        QHash<QString, PartOverduePolicy::OrderAggregate>* out,
                                        QString* errorText)
{
    PerfWatchdog::ScopedTimer perfTimer(QStringLiteral("CurrentOrdersQueryService::loadOverdueAggregates"), 50);
    if (!out) return false;
    out->clear();
    if (partsSourceSql.trimmed().isEmpty()) return true;

    const QString attachmentExpr = hasTableOrView(QStringLiteral("work_order_part_attachments"))
        ? QStringLiteral("EXISTS(SELECT 1 FROM work_order_part_attachments a WHERE a.part_id=p.id)")
        : QStringLiteral("0");

    const QString sql = QStringLiteral(
        "SELECT COALESCE(p.id,0), "
        "       COALESCE(p.order_uid,''), "
        "       COALESCE(p.order_number,0), "
        "       COALESCE(p.ordered_at,''), "
        "       COALESCE(p.delivery_date,''), "
        "       COALESCE(p.installed_at,''), "
        "       COALESCE(p.paid,0), "
        "       %1 "
        "FROM %2 p "
        "%3")
        .arg(attachmentExpr, partsSourceSql, currentOrdersPartWhereSql);

    QSqlQuery q(Database::db());
    if (!q.exec(sql)) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }

    const QDate today = PartOverduePolicy::businessToday();
    while (q.next()) {
        PartRowSnapshot row;
        row.id = q.value(0).toInt();
        row.orderUid = q.value(1).toString().trimmed();
        row.orderNumber = q.value(2).toInt();
        row.dateRaw = q.value(3).toString();
        row.deliveryDateRaw = q.value(4).toString();
        row.installedAtRaw = q.value(5).toString();
        row.paid = q.value(6).toInt() != 0;
        row.hasInvoiceAttachment = q.value(7).toInt() != 0;

        // The caller already limits this query to orders displayed in "Поточні наряди".
        // Use the shared parts-page policy instead of a parallel SQL rank formula so
        // manual rows, Excel-converted invoices, cleared delivery dates and localized
        // active statuses are highlighted identically in both tables.
        row.orderStatus = QStringLiteral("in_progress");
        PartsQueryService::refreshDerivedFields(&row, today);

        PartOverduePolicy::Result policyResult;
        policyResult.rank = row.overdueRank;
        policyResult.severity = row.overdueSeverity;
        policyResult.overdue = row.overdue;
        policyResult.overdueDays = row.overdueDays;
        policyResult.activeOrder = row.highlightEligibleOrder;
        policyResult.eligibleForOverdue = row.highlightEligibleOrder && !row.paid;

        if (!PartOverduePolicy::isProblemRank(policyResult.rank)) continue;

        const QString uidKey = orderAggregateKey(row.orderUid, row.orderNumber);
        PartOverduePolicy::OrderAggregate uidAggregate = out->value(uidKey);
        PartOverduePolicy::mergeIntoOrderAggregate(&uidAggregate, policyResult);
        if (uidAggregate.overdue) out->insert(uidKey, uidAggregate);

        if (row.orderNumber > 0) {
            const QString numberKey = orderAggregateKey(QString(), row.orderNumber);
            PartOverduePolicy::OrderAggregate numberAggregate = out->value(numberKey);
            PartOverduePolicy::mergeIntoOrderAggregate(&numberAggregate, policyResult);
            if (numberAggregate.overdue) out->insert(numberKey, numberAggregate);
        }
    }
    return true;
}

void sortCurrentOrdersRows(CurrentOrdersQueryResult* result, int sortRankCol, int numberCol)
{
    if (!result || sortRankCol < 0 || numberCol < 0) return;
    const int rankCol = headerIndex(*result, QStringLiteral("__rank"));
    const int openedCol = headerIndex(*result, QStringLiteral("Відкрито"));
    const int yearCol = headerIndex(*result, QStringLiteral("__order_year"));
    const int internalNumberCol = headerIndex(*result, QStringLiteral("__internal_number"));
    std::stable_sort(result->rows.begin(), result->rows.end(), [&](const QVector<QVariant>& a, const QVector<QVariant>& b) {
        const int aRank = rankCol >= 0 ? a.value(rankCol).toInt() : 0;
        const int bRank = rankCol >= 0 ? b.value(rankCol).toInt() : 0;
        if (aRank != bRank) return aRank < bRank;
        const int aPartsRank = a.value(sortRankCol).toInt();
        const int bPartsRank = b.value(sortRankCol).toInt();
        if (aPartsRank != bPartsRank) return aPartsRank < bPartsRank;
        const QDate aOpened = openedCol >= 0 ? parseDateForSort(a.value(openedCol)) : QDate();
        const QDate bOpened = openedCol >= 0 ? parseDateForSort(b.value(openedCol)) : QDate();
        if (aOpened != bOpened) return aOpened > bOpened;
        const int aYear = yearCol >= 0 ? a.value(yearCol).toInt() : 0;
        const int bYear = yearCol >= 0 ? b.value(yearCol).toInt() : 0;
        if (aYear != bYear) return aYear > bYear;
        const int aNum = a.value(numberCol).toInt();
        const int bNum = b.value(numberCol).toInt();
        if (aNum != bNum) return aNum > bNum;
        const int aInternal = internalNumberCol >= 0 ? a.value(internalNumberCol).toInt() : 0;
        const int bInternal = internalNumberCol >= 0 ? b.value(internalNumberCol).toInt() : 0;
        return aInternal > bInternal;
    });
}

void applyCurrentOrdersOverduePolicy(CurrentOrdersQueryResult* result, const QString& partsSourceSql)
{
    if (!result) return;
    const int uidCol = headerIndex(*result, QStringLiteral("__order_uid"));
    const int numberCol = headerIndex(*result, QStringLiteral("№"));
    const int severityCol = headerIndex(*result, QStringLiteral("__parts_overdue_severity"));
    const int overdueCol = headerIndex(*result, QStringLiteral("__parts_overdue"));
    const int sortRankCol = headerIndex(*result, QStringLiteral("__parts_sort_rank"));
    if (numberCol < 0 || severityCol < 0 || overdueCol < 0 || sortRankCol < 0) return;

    QHash<QString, PartOverduePolicy::OrderAggregate> byKey;
    QString aggregateError;
    const QString currentOnlyWhere = currentOrdersPartWhereSql(*result, uidCol, numberCol);
    if (!loadCurrentOrdersOverdueAggregates(partsSourceSql, currentOnlyWhere, &byKey, &aggregateError)) {
        qWarning().noquote() << QStringLiteral("CURRENT_ORDERS_OVERDUE_REFRESH aggregateLoadError=") << aggregateError;
    }

    int criticalOrders = 0;
    int warningOrders = 0;
    for (QVector<QVariant>& row : result->rows) {
        const QString uid = uidCol >= 0 ? row.value(uidCol).toString() : QString();
        const int number = row.value(numberCol).toInt();
        PartOverduePolicy::OrderAggregate aggregate;
        const QString uidKey = orderAggregateKey(uid, number);
        if (byKey.contains(uidKey)) {
            aggregate = byKey.value(uidKey);
        } else {
            // Some imported/manual part rows in old databases have only order_number even
            // when the parent order already has order_uid. Keep the lookup compatible
            // without widening the SQL aggregate to the whole parts table.
            aggregate = byKey.value(orderAggregateKey(QString(), number));
        }
        row[severityCol] = aggregate.severity;
        row[overdueCol] = aggregate.overdue ? 1 : 0;
        row[sortRankCol] = aggregate.sortRank;
        if (aggregate.severity == PartOverduePolicy::SeverityCritical) ++criticalOrders;
        else if (aggregate.severity == PartOverduePolicy::SeverityWarning) ++warningOrders;
    }

    sortCurrentOrdersRows(result, sortRankCol, numberCol);

    if (qEnvironmentVariableIsSet("CS_DEEP_DEBUG")) {
        if (RuntimeLoggingControl::deepDebugEnabled()) qInfo().noquote() << QStringLiteral("CURRENT_ORDERS_OVERDUE_REFRESH totalOrders=") << result->rows.size()
                          << QStringLiteral(" criticalOrders=") << criticalOrders
                          << QStringLiteral(" warningOrders=") << warningOrders;
    }
}

} // namespace

CurrentOrdersQueryResult CurrentOrdersQueryService::loadOpenOrders(const QString& filter)
{
    RuntimeJob::RuntimeJobDispatcher::warnIfUiThreadBlockingCall(QStringLiteral("CurrentOrdersQueryService::loadOpenOrders"));
    PerfWatchdog::ScopedTimer perfTimer(QStringLiteral("CurrentOrdersQueryService::loadOpenOrders"), 80);
    if (OrderSearchIndex::available(Database::db())) {
        CurrentOrdersQueryResult indexed;
        indexed.hasSource = true;
        indexed.sourceTable = "router_open_order_locator";
        HistoryQueryParams params{QDate(1900,1,1),QDate(9999,12,31),filter,true,true};
        const QString sql = QStringLiteral(
            "SELECT h.* FROM (%1) h WHERE EXISTS "
            "(SELECT 1 FROM router_open_order_locator o WHERE o.number=h.__internal_number AND %2)")
            .arg(HistoryQueryService::buildHistorySql(params),openOrdersNativePredicateSql("o"));
        QSqlQuery q(Database::db());
        if (!q.exec(sql)) { indexed.errorText=q.lastError().text(); return indexed; }
        const auto rec=q.record();
        QList<int> columns;
        for (int i=0;i<rec.count();++i) {
            if (rec.fieldName(i)==QStringLiteral("Закрито")) continue;
            columns.push_back(i); indexed.headers.push_back(rec.fieldName(i));
        }
        indexed.headers << "__parts_overdue_severity" << "__parts_overdue" << "__parts_sort_rank";
        while(q.next()) {
            QVector<QVariant> row;
            for(int col:columns) row.push_back(q.value(col));
            row << 0 << 0 << 9;
            indexed.rows.push_back(std::move(row));
        }
        applyCurrentOrdersOverduePolicy(&indexed,QStringLiteral("router_work_order_parts"));
        appendWorkshopPosts(indexed);
        return indexed;
    }
    CurrentOrdersQueryResult result;
    // Do not run the full persisted financial-sums refresh here.
    // The previous first-open path updated every work_orders row with correlated
    // subqueries from the UI refresh path, which was visible as a freeze on large
    // databases. This read model calculates visible totals through bounded CTEs
    // over the currently displayed active orders and leaves persisted totals to
    // mutation/repair flows.

    const QString filterLower = foldSearchConfusables(filter);
    const QString compactFilter = compactSearch(filter);
    const bool digitsOnly = !compactFilter.isEmpty()
        && QRegularExpression(QStringLiteral(R"(^\d+$)")).match(compactFilter).hasMatch();

    const QString dateDisplayExpr = displayDateSqlExpr(QStringLiteral("o.opened_at"));
    const QString dateCompactExpr = compactSqlExpr(dateDisplayExpr);
    const QString plateCompactExpr = compactSqlExpr(QStringLiteral("o.plate"));
    const QString vinCompactExpr = compactSqlExpr(QStringLiteral("o.vin"));
    const QString carCompactExpr = compactSqlExpr(QStringLiteral("o.car"));
    const QString clientCompactExpr = compactSqlExpr(QStringLiteral("o.client"));

    QString rankExpr = QStringLiteral("50");
    if (!filter.isEmpty()) {
        const QString lowerLiteral = sqlQuote(QStringLiteral("%%") + escapeLike(filterLower) + QStringLiteral("%%"));
        const QString compactLiteral = sqlQuote(QStringLiteral("%%") + escapeLike(compactFilter) + QStringLiteral("%%"));
        const QString compactPrefixLiteral = sqlQuote(escapeLike(compactFilter) + QStringLiteral("%"));
        const QString numLiteral = sqlQuote(compactFilter.isEmpty() ? filterLower : compactFilter);
        const QString compactExactLiteral = sqlQuote(compactFilter);

        if (digitsOnly) {
            rankExpr = QStringLiteral(
                "CASE "
                "WHEN CAST(COALESCE(o.order_number,o.number) AS TEXT) = '%1' THEN 1 "
                "WHEN %2 LIKE '%3' ESCAPE '\\' OR LOWER(COALESCE(o.plate,'')) LIKE '%4' ESCAPE '\\' THEN 2 "
                "WHEN %5 LIKE '%6' ESCAPE '\\' OR %7 LIKE '%4' ESCAPE '\\' THEN 3 "
                "WHEN %8 LIKE '%3' ESCAPE '\\' OR LOWER(COALESCE(o.vin,'')) LIKE '%4' ESCAPE '\\' THEN 4 "
                "ELSE 100 END")
                .arg(numLiteral,
                     plateCompactExpr,
                     compactLiteral,
                     lowerLiteral,
                     dateCompactExpr,
                     compactPrefixLiteral,
                     dateDisplayExpr,
                     vinCompactExpr);
        } else {
            rankExpr = QStringLiteral(
                "CASE "
                "WHEN %1 = '%2' THEN 1 "
                "WHEN %1 LIKE '%3' ESCAPE '\\' OR LOWER(COALESCE(o.plate,'')) LIKE '%4' ESCAPE '\\' THEN 2 "
                "WHEN %5 LIKE '%4' ESCAPE '\\' OR %6 LIKE '%3' ESCAPE '\\' THEN 3 "
                "WHEN %7 LIKE '%4' ESCAPE '\\' OR %8 LIKE '%3' ESCAPE '\\' THEN 4 "
                "WHEN LOWER(COALESCE(o.client,'')) LIKE '%4' ESCAPE '\\' OR %9 LIKE '%3' ESCAPE '\\' THEN 5 "
                "ELSE 100 END")
                .arg(plateCompactExpr,
                     compactExactLiteral,
                     compactLiteral,
                     lowerLiteral,
                     QStringLiteral("LOWER(COALESCE(o.car,''))"),
                     carCompactExpr,
                     QStringLiteral("LOWER(COALESCE(o.vin,''))"),
                     vinCompactExpr,
                     clientCompactExpr);
        }
    }

    const bool hasRouterServices = hasTableOrView(QStringLiteral("router_work_order_services"));
    const QString resolvedPartsTable = unifiedPartsSourceSql();
    const QString resolvedServicesTable = hasRouterServices ? QStringLiteral("router_work_order_services") : QStringLiteral("work_order_services");
    const bool hasRouterOpenLocator = routerOpenOrderLocatorUsable();
    const bool hasWorkOrders = hasTableOrView(QStringLiteral("work_orders"));
    result.hasSource = hasRouterOpenLocator || hasWorkOrders;
    if (!hasRouterOpenLocator && !hasWorkOrders) {
        result.errorText = QStringLiteral("neither usable router_open_order_locator nor work_orders exists");
        return result;
    }

    const QString openNativePredicate = openOrdersNativePredicateSql(QStringLiteral("o"));
    const QString resolvedSourceTable = hasRouterOpenLocator
        ? QStringLiteral("router_open_order_locator")
        : QStringLiteral("work_orders");
    result.sourceTable = resolvedSourceTable;
    const QString sourceWhereClause = QStringLiteral(" WHERE ") + openNativePredicate
        + QStringLiteral(" AND (") + AccessControlService::currentOrderPredicate(Database::db(), QStringLiteral("o")) + QLatin1Char(')');

    const QString servicesKeyExpr = QStringLiteral("COALESCE(NULLIF(TRIM(COALESCE(s2.order_uid,'')),''), '')");
    const QString partsKeyExpr = QStringLiteral("COALESCE(NULLIF(TRIM(COALESCE(p2.order_uid,'')),''), '')");
    const QString servicesSumJoinPredicate = QStringLiteral(
        "((TRIM(COALESCE(ss.order_uid_key,''))<>'' AND TRIM(COALESCE(ss.order_uid_key,''))=TRIM(COALESCE(o.order_uid,''))) "
        "OR (TRIM(COALESCE(ss.order_uid_key,''))='' AND COALESCE(ss.order_number_key,0)=COALESCE(o.number,0)))");
    const QString partsSumJoinPredicate = QStringLiteral(
        "((TRIM(COALESCE(ps.order_uid_key,''))<>'' AND TRIM(COALESCE(ps.order_uid_key,''))=TRIM(COALESCE(o.order_uid,''))) "
        "OR (TRIM(COALESCE(ps.order_uid_key,''))='' AND COALESCE(ps.order_number_key,0)=COALESCE(o.number,0)))");

    const QString servicesOpenJoinPredicate = QStringLiteral(
        "((TRIM(COALESCE(s2.order_uid,''))<>'' AND TRIM(COALESCE(s2.order_uid,''))=TRIM(COALESCE(oo.order_uid,''))) "
        "OR (COALESCE(s2.order_number,0)=COALESCE(oo.number,0)))");
    const QString partsOpenJoinPredicate = QStringLiteral(
        "((TRIM(COALESCE(p2.order_uid,''))<>'' AND TRIM(COALESCE(p2.order_uid,''))=TRIM(COALESCE(oo.order_uid,''))) "
        "OR (COALESCE(p2.order_number,0)=COALESCE(oo.number,0)) "
        "OR (LOWER(TRIM(COALESCE(p2.order_uid,''))) = 'rewrite_' || CAST(COALESCE(oo.order_number,oo.number,0) AS TEXT)))");

    QString sql = QStringLiteral(
        "WITH open_source AS ("
        "  SELECT * FROM __CS_SOURCE_TABLE__ o __CS_SOURCE_WHERE__"
        "), services_sum AS ("
        "  SELECT %1 AS order_uid_key, COALESCE(s2.order_number,0) AS order_number_key, "
        "         SUM(COALESCE(s2.amount, COALESCE(s2.qty,1) * COALESCE(s2.price,0))) AS total "
        "  FROM __CS_SERVICES_TABLE__ s2 "
        "  WHERE EXISTS (SELECT 1 FROM open_source oo WHERE __CS_SERVICES_OPEN_JOIN_PREDICATE__) "
        "  GROUP BY order_uid_key, order_number_key"
        "), parts_sum AS ("
        "  SELECT %2 AS order_uid_key, COALESCE(p2.order_number,0) AS order_number_key, "
        "         SUM(COALESCE(p2.amount, COALESCE(p2.qty,1) * COALESCE(p2.price,0))) AS total "
        "  FROM __CS_PARTS_TABLE__ p2 "
        "  WHERE EXISTS (SELECT 1 FROM open_source oo WHERE __CS_PARTS_OPEN_JOIN_PREDICATE__) "
        "  GROUP BY order_uid_key, order_number_key"
        ") "
        "SELECT * FROM ("
        "SELECT CAST(COALESCE(o.order_number,o.number) AS TEXT) AS '№', "
        "       o.opened_at AS 'Відкрито', "
        "       o.status AS 'Статус', "
        "       o.plate AS 'Номер', "
        "       o.car AS 'Авто', "
        "       o.client AS 'Клієнт', "
        "       COALESCE(NULLIF(o.master_name_closed,''), (SELECT name FROM masters m WHERE m.id=o.master_id), '') AS 'Майстер', "
        "       COALESCE(ss.total, o.works_sum, 0) AS 'Роботи', "
        "       COALESCE(ps.total, o.parts_sum, 0) AS 'Запчастини', "
        "       o.number AS '__internal_number', "
        "       COALESCE(o.order_uid,'') AS '__order_uid', "
        "       COALESCE(o.order_year, CAST(strftime('%Y', (CASE WHEN substr(trim(COALESCE(o.opened_at,'')),3,1)='.' AND substr(trim(COALESCE(o.opened_at,'')),6,1)='.' THEN CASE WHEN substr(trim(COALESCE(o.opened_at,'')),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(COALESCE(o.opened_at,'')),7,4) ELSE '20'||substr(trim(COALESCE(o.opened_at,'')),7,2) END||'-'||substr(trim(COALESCE(o.opened_at,'')),4,2)||'-'||substr(trim(COALESCE(o.opened_at,'')),1,2) ELSE COALESCE(o.opened_at,'') END)) AS INTEGER), 0) AS '__order_year', "
        "       0 AS '__parts_overdue_severity', "
        "       0 AS '__parts_overdue', "
        "       9 AS '__parts_sort_rank', "
        "       __CS_RANK_EXPR__ AS '__rank' "
        "FROM open_source o "
        "LEFT JOIN services_sum ss ON __CS_SERVICES_SUM_JOIN_PREDICATE__ "
        "LEFT JOIN parts_sum ps ON __CS_PARTS_SUM_JOIN_PREDICATE__"
        ") q ").arg(servicesKeyExpr, partsKeyExpr);

    QStringList uidLiterals;
    if (digitsOnly && !compactFilter.isEmpty()) {
        bool visibleNumberOk = false;
        const int visibleNumber = compactFilter.toInt(&visibleNumberOk);
        if (visibleNumberOk && visibleNumber > 0) {
            const auto hits = OrderSearchAggregator::findByVisibleNumber(visibleNumber, 500, nullptr);
            for (const auto& hit : hits) {
                const QString uid = hit.orderUid.trimmed();
                if (uid.isEmpty()) continue;
                const QString quoted = QStringLiteral("'%1'").arg(sqlQuote(uid));
                if (!uidLiterals.contains(quoted)) uidLiterals << quoted;
            }
        }
    }

    if (digitsOnly && !compactFilter.isEmpty()) {
        if (uidLiterals.isEmpty()) {
            sql += QStringLiteral("WHERE 1=0 ");
        } else {
            sql += QStringLiteral("WHERE q.__order_uid IN (") + uidLiterals.join(QStringLiteral(", ")) + QStringLiteral(") ");
        }
    } else {
        sql += QStringLiteral("WHERE q.__rank < 100 ");
    }

    const QString orderBySql = digitsOnly
        ? QStringLiteral("q.__rank ASC, q.__parts_sort_rank ASC, q.\"Відкрито\" DESC, q.__order_year DESC, CAST(q.\"№\" AS INTEGER) DESC, q.__internal_number DESC")
        : QStringLiteral("q.__rank ASC, q.__parts_sort_rank ASC, q.\"Відкрито\" DESC, q.__order_year DESC, CAST(q.\"№\" AS INTEGER) DESC, q.__internal_number DESC");
    sql += QStringLiteral("ORDER BY ") + orderBySql;
    sql.replace(QStringLiteral("__CS_RANK_EXPR__"), rankExpr);
    sql.replace(QStringLiteral("__CS_SERVICES_SUM_JOIN_PREDICATE__"), servicesSumJoinPredicate);
    sql.replace(QStringLiteral("__CS_PARTS_SUM_JOIN_PREDICATE__"), partsSumJoinPredicate);
    sql.replace(QStringLiteral("__CS_SERVICES_OPEN_JOIN_PREDICATE__"), servicesOpenJoinPredicate);
    sql.replace(QStringLiteral("__CS_PARTS_OPEN_JOIN_PREDICATE__"), partsOpenJoinPredicate);
    sql.replace(QStringLiteral("__CS_SERVICES_TABLE__"), resolvedServicesTable);
    sql.replace(QStringLiteral("__CS_PARTS_TABLE__"), resolvedPartsTable);
    sql.replace(QStringLiteral("__CS_SOURCE_TABLE__"), resolvedSourceTable);
    sql.replace(QStringLiteral("__CS_SOURCE_WHERE__"), sourceWhereClause);

    QSqlQuery q(Database::db());
    if (!q.exec(sql)) {
        result.errorText = q.lastError().text();
        return result;
    }

    const QSqlRecord rec = q.record();
    for (int i = 0; i < rec.count(); ++i) result.headers.push_back(rec.fieldName(i));
    while (q.next()) {
        QVector<QVariant> row;
        row.reserve(rec.count());
        for (int i = 0; i < rec.count(); ++i) row.push_back(q.value(i));
        result.rows.push_back(std::move(row));
    }
    applyCurrentOrdersOverduePolicy(&result, resolvedPartsTable);
    appendWorkshopPosts(result);
    return result;
}
