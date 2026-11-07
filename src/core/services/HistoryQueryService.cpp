#include "core/services/HistoryQueryService.h"
#include "core/services/AccessControlService.h"
#include "core/services/OrderSearchIndex.h"
#include "core/services/OrderStoragePolicyService.h"
#include "core/services/OrderSearchAggregator.h"
#include "core/perf/PerfWatchdog.h"
#include "runtime/RuntimeJobDispatcher.h"

#include <QDate>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlRecord>
#include <QSqlQuery>

#include "infrastructure/db/sqlite/Db.h"

namespace {

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

QString compactSqlExpr(const QString& expr)
{
    QString sql = QStringLiteral("LOWER(COALESCE(%1,''))").arg(expr);
    const QString capitals = QString::fromUtf8("АВСЕНІЇКМОРТУХ");
    for (const QChar ch : capitals)
        sql = QStringLiteral("REPLACE(%1,'%2','%3')").arg(sql, QString(ch), foldSearchConfusables(QString(ch)));
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

QString orderChildJoinPredicate(const QString& orderAlias, const QString& childAlias)
{
    return QStringLiteral(
        "((TRIM(COALESCE(%2.order_uid,''))<>'' AND TRIM(COALESCE(%2.order_uid,''))=TRIM(COALESCE(%1.order_uid,''))) "
        "OR (TRIM(COALESCE(%2.order_uid,''))='' AND COALESCE(%2.order_number,0)=COALESCE(%1.number,0)))")
        .arg(orderAlias, childAlias);
}


} // namespace

namespace HistoryQueryService {

QString buildHistorySql(const HistoryQueryParams& params)
{
    return buildHistorySql(Database::db(),params);
}

QString buildHistorySql(QSqlDatabase database, const HistoryQueryParams& params)
{
    if (!database.isValid() || !database.isOpen()) return QString();
    auto escapeLike = [](QString s) {
        s.replace(QLatin1String("\\"), QLatin1String("\\\\"));
        s.replace(QLatin1String("%"), QLatin1String("\\%"));
        s.replace(QLatin1String("_"), QLatin1String("\\_"));
        return s;
    };
    auto sqlQuote = [](QString s) {
        s.replace(QStringLiteral("'"), QStringLiteral("''"));
        return s;
    };

    QString filter = params.filter.trimmed();
    for (const QString& format : {QString("dd.MM.yyyy"), QString("yyyy-MM-dd")}) {
        const QDate date = QDate::fromString(filter, format);
        if (date.isValid()) { filter = date.toString("dd.MM.yy"); break; }
    }
    const QString filterLower = foldSearchConfusables(filter);
    const QString compactFilter = compactSearch(filter);
    const bool digitsOnly = !compactFilter.isEmpty()
        && QRegularExpression(QStringLiteral(R"(^\d+$)")).match(compactFilter).hasMatch();
    const bool dateLike = !filter.isEmpty()
        && QRegularExpression(QStringLiteral(R"(^\d{1,2}([./-]\d{1,2})?([./-]\d{2,4})?([ /]\d{1,2}([.:]\d{1,2})?)?$)")).match(filter).hasMatch();

    auto normalizedSqlDateExpr = [](const QString& col) {
        return QStringLiteral(
            "CASE "
            "WHEN TRIM(COALESCE(%1,''))='' THEN NULL "
            "WHEN substr(TRIM(%1),5,1)='-' AND substr(TRIM(%1),8,1)='-' THEN date(substr(TRIM(%1),1,10)) "
            "WHEN substr(TRIM(%1),3,1)='.' AND substr(TRIM(%1),6,1)='.' AND substr(TRIM(%1),11,1)='.' THEN date(substr(TRIM(%1),7,4) || '-' || substr(TRIM(%1),4,2) || '-' || substr(TRIM(%1),1,2)) "
            "WHEN substr(TRIM(%1),3,1)='.' AND substr(TRIM(%1),6,1)='.' THEN date('20' || substr(TRIM(%1),7,2) || '-' || substr(TRIM(%1),4,2) || '-' || substr(TRIM(%1),1,2)) "
            "ELSE NULL END").arg(col);
    };
    auto normalizedSqlDateTimeExpr = [](const QString& col) {
        return QStringLiteral(
            "CASE "
            "WHEN TRIM(COALESCE(%1,''))='' THEN NULL "
            "WHEN substr(TRIM(%1),5,1)='-' AND substr(TRIM(%1),8,1)='-' THEN datetime(replace(substr(TRIM(%1),1,19),'T',' ')) "
            "WHEN substr(TRIM(%1),3,1)='.' AND substr(TRIM(%1),6,1)='.' AND substr(TRIM(%1),11,1)='.' AND length(TRIM(%1)) >= 16 THEN datetime(substr(TRIM(%1),7,4) || '-' || substr(TRIM(%1),4,2) || '-' || substr(TRIM(%1),1,2) || ' ' || replace(substr(TRIM(%1),12,5),'.',':')) "
            "WHEN substr(TRIM(%1),3,1)='.' AND substr(TRIM(%1),6,1)='.' AND substr(TRIM(%1),11,1)='.' THEN datetime(substr(TRIM(%1),7,4) || '-' || substr(TRIM(%1),4,2) || '-' || substr(TRIM(%1),1,2) || ' 00:00:00') "
            "WHEN substr(TRIM(%1),3,1)='.' AND substr(TRIM(%1),6,1)='.' AND length(TRIM(%1)) >= 14 THEN datetime('20' || substr(TRIM(%1),7,2) || '-' || substr(TRIM(%1),4,2) || '-' || substr(TRIM(%1),1,2) || ' ' || replace(substr(TRIM(%1),10,5),'.',':')) "
            "WHEN substr(TRIM(%1),3,1)='.' AND substr(TRIM(%1),6,1)='.' THEN datetime('20' || substr(TRIM(%1),7,2) || '-' || substr(TRIM(%1),4,2) || '-' || substr(TRIM(%1),1,2) || ' 00:00:00') "
            "ELSE NULL END").arg(col);
    };

    const QString openedDateExpr = params.useSearchIndex ? "o.opened_date" : normalizedSqlDateExpr(QStringLiteral("o.opened_at"));
    const QString closedDateExpr = params.useSearchIndex ? "o.closed_date" : normalizedSqlDateExpr(QStringLiteral("o.closed_at"));
    const QString openedDateTimeExpr = params.useSearchIndex ? "o.opened_datetime" : normalizedSqlDateTimeExpr(QStringLiteral("o.opened_at"));
    const QString closedDateTimeExpr = params.useSearchIndex ? "o.closed_datetime" : normalizedSqlDateTimeExpr(QStringLiteral("o.closed_at"));
    const QString fromIso = (params.from.isValid() ? params.from : QDate(1900, 1, 1)).toString(QStringLiteral("yyyy-MM-dd"));
    const QString toIso = (params.to.isValid() ? params.to : QDate(9999, 12, 31)).toString(QStringLiteral("yyyy-MM-dd"));
    const QString openedDisplayExpr = params.useSearchIndex ? "o.opened_display" : displayDateSqlExpr(QStringLiteral("o.opened_at"));
    const QString closedDisplayExpr = params.useSearchIndex ? "o.closed_display" : displayDateSqlExpr(QStringLiteral("o.closed_at"));
    const QString openedCompactExpr = params.useSearchIndex ? "o.opened_search" : compactSqlExpr(openedDisplayExpr);
    const QString closedCompactExpr = params.useSearchIndex ? "o.closed_search" : compactSqlExpr(closedDisplayExpr);
    const QString plateCompactExpr = params.useSearchIndex ? "o.plate_search" : compactSqlExpr(QStringLiteral("o.plate"));
    const QString vinCompactExpr = params.useSearchIndex ? "o.vin_search" : compactSqlExpr(QStringLiteral("o.vin"));
    const QString carCompactExpr = params.useSearchIndex ? "o.car_search" : compactSqlExpr(QStringLiteral("o.car"));
    const QString clientCompactExpr = params.useSearchIndex ? "o.client_search" : compactSqlExpr(QStringLiteral("o.client"));
    const QString compactPrefixLiteral = sqlQuote(escapeLike(compactFilter) + QStringLiteral("%"));
    const bool platePrefixCandidate = !compactFilter.isEmpty()
        && !digitsOnly
        && !dateLike
        && QRegularExpression(QStringLiteral(R"(^[a-z]{2}\d{1,5}[a-z]{0,2}$)")).match(compactFilter).hasMatch();

    QString rankExpr = QStringLiteral("50");

    if (!filter.isEmpty()) {
        const QString lowerLiteral = sqlQuote(QStringLiteral("%%") + escapeLike(filterLower) + QStringLiteral("%%"));
        const QString compactLiteral = sqlQuote(QStringLiteral("%%") + escapeLike(compactFilter) + QStringLiteral("%%"));
        const QString numLiteral = sqlQuote(compactFilter.isEmpty() ? filterLower : compactFilter);
        const QString compactExactLiteral = sqlQuote(compactFilter);

        if (digitsOnly) {
            rankExpr = QStringLiteral(
                "CASE "
                "WHEN CAST(COALESCE(o.order_number,o.number) AS TEXT) = '%1' THEN 1 "
                "WHEN %2 LIKE '%3' ESCAPE '\\' OR LOWER(COALESCE(o.plate,'')) LIKE '%4' ESCAPE '\\' THEN 2 "
                "WHEN %5 LIKE '%6' ESCAPE '\\' OR %7 LIKE '%6' ESCAPE '\\' OR %8 LIKE '%3' ESCAPE '\\' OR %9 LIKE '%3' ESCAPE '\\' THEN 3 "
                "WHEN %10 LIKE '%3' ESCAPE '\\' OR LOWER(COALESCE(o.vin,'')) LIKE '%4' ESCAPE '\\' THEN 4 "
                "ELSE 100 END")
                .arg(numLiteral,
                     plateCompactExpr,
                     compactLiteral,
                     lowerLiteral,
                     openedCompactExpr,
                     compactPrefixLiteral,
                     closedCompactExpr,
                     openedDisplayExpr,
                     closedDisplayExpr,
                     vinCompactExpr);
        } else if (dateLike) {
            rankExpr = QStringLiteral(
                "CASE "
                "WHEN %1 LIKE '%2' ESCAPE '\\' OR %3 LIKE '%2' ESCAPE '\\' THEN 1 "
                "WHEN %4 LIKE '%5' ESCAPE '\\' OR %6 LIKE '%5' ESCAPE '\\' THEN 2 "
                "ELSE 100 END")
                .arg(openedDisplayExpr,
                     lowerLiteral,
                     closedDisplayExpr,
                     openedCompactExpr,
                     compactLiteral,
                     closedCompactExpr);
        } else {
            rankExpr = QStringLiteral(
                "CASE "
                "WHEN %4 = '%5' THEN 1 "
                "WHEN %4 LIKE '%6' ESCAPE '\\' THEN 2 "
                "WHEN LOWER(COALESCE(o.car,'')) LIKE '%1' ESCAPE '\\' OR %2 LIKE '%3' ESCAPE '\\' THEN 3 "
                "WHEN %4 LIKE '%3' ESCAPE '\\' OR LOWER(COALESCE(o.plate,'')) LIKE '%1' ESCAPE '\\' THEN 4 "
                "WHEN %7 LIKE '%1' ESCAPE '\\' OR %8 LIKE '%3' ESCAPE '\\' THEN 5 "
                "WHEN LOWER(COALESCE(o.client,'')) LIKE '%1' ESCAPE '\\' OR %9 LIKE '%3' ESCAPE '\\' THEN 6 "
                "WHEN %10 LIKE '%1' ESCAPE '\\' OR %11 LIKE '%1' ESCAPE '\\' OR %12 LIKE '%3' ESCAPE '\\' OR %13 LIKE '%3' ESCAPE '\\' THEN 7 "
                "ELSE 100 END")
                .arg(lowerLiteral,
                     carCompactExpr,
                     compactLiteral,
                     plateCompactExpr,
                     compactExactLiteral,
                     compactPrefixLiteral,
                     QStringLiteral("LOWER(COALESCE(o.vin,''))"),
                     vinCompactExpr,
                     clientCompactExpr,
                     openedDisplayExpr,
                     closedDisplayExpr,
                     openedCompactExpr,
                     closedCompactExpr);
        }
    }

    // Calculate child totals only for matching orders, using the normalized UID
    // expression index. The old window aggregates joined every child to every order.
    QString sql = QStringLiteral(
        "WITH history_source AS ("
        "  SELECT * FROM __CS_SEARCH_SOURCE__ o "
        "  WHERE __CS_STATUS_PREDICATE__ AND (((__CS_CLOSED_DATE_EXPR__) >= date('__CS_FROM_ISO__') AND (__CS_CLOSED_DATE_EXPR__) <= date('__CS_TO_ISO__'))"
        "     OR ((__CS_OPENED_DATE_EXPR__) >= date('__CS_FROM_ISO__') AND (__CS_OPENED_DATE_EXPR__) <= date('__CS_TO_ISO__')))"
        " __CS_SOURCE_PAGE__ "
        ") "
        "SELECT * FROM ("
        "SELECT CAST(COALESCE(o.order_number,o.number) AS TEXT) AS '№', "
        "       o.opened_at AS 'Відкрито', "
        "       o.closed_at AS 'Закрито', "
        "       o.status AS 'Статус', "
        "       o.plate AS 'Номер', "
        "       o.car AS 'Авто', "
        "       o.client AS 'Клієнт', "
        "       COALESCE(NULLIF(o.master_name_closed,''), (SELECT name FROM masters m WHERE m.id=o.master_id), '') AS 'Майстер', "
        "       COALESCE((SELECT SUM(COALESCE(s2.amount,COALESCE(s2.qty,1)*COALESCE(s2.price,0))) FROM work_order_services s2 WHERE %1),o.works_sum,0) AS 'Роботи', "
        "       COALESCE((SELECT SUM(COALESCE(p2.amount,COALESCE(p2.qty,1)*COALESCE(p2.price,0))) FROM work_order_parts p2 WHERE %2),o.parts_sum,0) AS 'Запчастини', "
        "       o.number AS '__internal_number', "
        "       COALESCE(o.order_uid,'') AS '__order_uid', "
        "       COALESCE(o.order_year, CAST(strftime('%Y', (CASE WHEN substr(trim(COALESCE(o.opened_at,'')),3,1)='.' AND substr(trim(COALESCE(o.opened_at,'')),6,1)='.' THEN CASE WHEN substr(trim(COALESCE(o.opened_at,'')),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(COALESCE(o.opened_at,'')),7,4) ELSE '20'||substr(trim(COALESCE(o.opened_at,'')),7,2) END||'-'||substr(trim(COALESCE(o.opened_at,'')),4,2)||'-'||substr(trim(COALESCE(o.opened_at,'')),1,2) ELSE COALESCE(o.opened_at,'') END)) AS INTEGER), 0) AS '__order_year', "
        "       __CS_RANK_EXPR__ AS '__rank', "
        "       CASE "
        "       WHEN ((__CS_CLOSED_DATE_EXPR__) >= date('__CS_FROM_ISO__') AND (__CS_CLOSED_DATE_EXPR__) <= date('__CS_TO_ISO__')) THEN 0 "
        "       WHEN ((__CS_OPENED_DATE_EXPR__) >= date('__CS_FROM_ISO__') AND (__CS_OPENED_DATE_EXPR__) <= date('__CS_TO_ISO__')) THEN 1 "
        "       ELSE 2 END AS '__date_rank', "
        "       CAST(COALESCE(o.order_number, o.number, 0) AS INTEGER) AS '__order_number_sort', "
        "       COALESCE(__CS_CLOSED_DATETIME_EXPR__, __CS_OPENED_DATETIME_EXPR__) AS '__effective_datetime_sort', "
        "       __CS_OPENED_DATETIME_EXPR__ AS '__opened_datetime_sort' "
        "       __CS_PLATE_SEARCH_SELECT__ "
        "       __CS_MOBILE_STATE_SELECT__ "
        "FROM history_source o "
        ") q ").arg(orderChildJoinPredicate("o", "s2"), orderChildJoinPredicate("o", "p2"));
    sql.replace("__CS_SEARCH_SOURCE__", params.useSearchIndex
        ? "(SELECT r.*,s.plate_search,s.vin_search,s.car_search,s.client_search,s.opened_display,s.closed_display,s.opened_search,s.closed_search,s.opened_date,s.closed_date,s.opened_datetime,s.closed_datetime FROM router_order_locator r JOIN order_search_v1 s ON s.number=r.number)"
        : "router_order_locator");
    sql.replace("__CS_PLATE_SEARCH_SELECT__", params.useSearchIndex ? ",o.plate_search AS __plate_search" : "");
    sql.replace("__CS_MOBILE_STATE_SELECT__", params.includeMobileState
        ? ",COALESCE((SELECT w.mobile_state_json FROM work_orders w WHERE w.order_uid=o.order_uid LIMIT 1),'{}') AS __mobile_state"
        : "");
    QString sourcePage;
    if (params.useSearchIndex && params.pageSize>0) {
        const QString sort=digitsOnly
            ? rankExpr+" ASC,o.order_year DESC,COALESCE(o.closed_datetime,o.opened_datetime) DESC,o.opened_datetime DESC,o.number DESC"
            : "CAST(COALESCE(o.order_number,o.number,0) AS INTEGER) DESC,COALESCE(o.closed_datetime,o.opened_datetime) DESC,o.opened_datetime DESC,o.number DESC";
        sourcePage=QString(" ORDER BY %1 LIMIT %2 OFFSET %3 ").arg(sort).arg(params.pageSize+1).arg(qMax(0,params.offset));
    }
    sql.replace("__CS_SOURCE_PAGE__",sourcePage);
    if (params.useSearchIndex && !filter.isEmpty()) {
        QString candidateRank=rankExpr;
        for (const QString& field : {QString("plate"),QString("car"),QString("vin"),QString("client")})
            candidateRank.replace("LOWER(COALESCE(o."+field+",''))", "s."+field+"_search");
        candidateRank.replace("o.order_number","s.visible_number").replace("o.","s.");
        const QString candidate=platePrefixCandidate
            ? QString("s.plate_search LIKE '%1' ESCAPE '\\'").arg(compactPrefixLiteral)
            : "("+candidateRank+") < 100";
        sql.replace("WHERE __CS_STATUS_PREDICATE__", "WHERE o.number IN (SELECT s.number FROM order_search_v1 s WHERE "+candidate+") AND __CS_STATUS_PREDICATE__");
    }
    sql.replace(QStringLiteral("__CS_RANK_EXPR__"), rankExpr);
    sql.replace(QStringLiteral("__CS_STATUS_PREDICATE__"),
        QStringLiteral("(%1) AND (%2)")
            .arg(params.includeActive ? QStringLiteral("1=1") : OrderStoragePolicyService::closedOrderPredicate(QStringLiteral("o")),
                 AccessControlService::currentOrderPredicate(database, QStringLiteral("o"))));
    sql.replace(QStringLiteral("__CS_CLOSED_DATE_EXPR__"), closedDateExpr);
    sql.replace(QStringLiteral("__CS_OPENED_DATE_EXPR__"), openedDateExpr);
    sql.replace(QStringLiteral("__CS_FROM_ISO__"), fromIso);
    sql.replace(QStringLiteral("__CS_TO_ISO__"), toIso);
    sql.replace(QStringLiteral("__CS_CLOSED_DATETIME_EXPR__"), closedDateTimeExpr);
    sql.replace(QStringLiteral("__CS_OPENED_DATETIME_EXPR__"), openedDateTimeExpr);
    if (!filter.isEmpty()) {
        if (digitsOnly && !compactFilter.isEmpty()) {
            bool visibleNumberOk = false;
            const int visibleNumber = compactFilter.toInt(&visibleNumberOk);
            QStringList uidLiterals;
            if (visibleNumberOk && visibleNumber > 0 && !params.includeActive) {
                const auto hits = OrderSearchAggregator::findByVisibleNumber(visibleNumber, 500, nullptr);
                for (const auto& hit : hits) {
                    const QString uid = hit.orderUid.trimmed();
                    if (uid.isEmpty()) continue;
                    const QString quoted = QStringLiteral("'%1'").arg(sqlQuote(uid));
                    if (!uidLiterals.contains(quoted)) uidLiterals << quoted;
                }
            }
            QStringList digitPredicates;
            digitPredicates << QStringLiteral("q.__rank < 100");
            if (!uidLiterals.isEmpty()) {
                digitPredicates << QStringLiteral("q.__order_uid IN (%1)").arg(uidLiterals.join(QStringLiteral(", ")));
            }
            sql += QStringLiteral("WHERE (%1) ").arg(digitPredicates.join(QStringLiteral(" OR ")));
        } else if (platePrefixCandidate) {
            sql += QStringLiteral("WHERE %1 LIKE '%2' ESCAPE '\\' ")
                .arg(params.useSearchIndex ? "q.__plate_search" : compactSqlExpr(QStringLiteral("q.\"Номер\"")), compactPrefixLiteral);
        } else {
            sql += QStringLiteral("WHERE q.__rank < 100 ");
        }
    }

    if (digitsOnly) {
        sql += QStringLiteral(
            "ORDER BY q.__rank ASC, q.__order_year DESC, q.__effective_datetime_sort DESC, q.__opened_datetime_sort DESC, q.__internal_number DESC");
    } else {
        sql += QStringLiteral("ORDER BY q.__order_number_sort DESC, q.__effective_datetime_sort DESC, q.__opened_datetime_sort DESC, q.__internal_number DESC");
    }
    if (params.pageSize > 0 && !params.useSearchIndex)
        sql += QString(" LIMIT %1 OFFSET %2").arg(params.pageSize+1).arg(qMax(0,params.offset));
    return sql;
}

HistoryQueryResult loadHistory(const HistoryQueryParams& params)
{
    RuntimeJob::RuntimeJobDispatcher::warnIfUiThreadBlockingCall(QStringLiteral("HistoryQueryService::loadHistory"));
    PerfWatchdog::ScopedTimer perfTimer(QStringLiteral("HistoryQueryService::loadHistory"), 80);
    HistoryQueryResult result;
    // Do not refresh persisted financial sums for the entire database from this
    // read path. The history query uses bounded CTE aggregates for the selected
    // date window, while full repair remains an explicit maintenance operation.
    const auto database=Database::db();
    auto indexedParams = params;
    indexedParams.useSearchIndex = OrderSearchIndex::available(database);
    indexedParams.includeMobileState = database.record("work_orders").contains("mobile_state_json");
    const QString sql = buildHistorySql(database,indexedParams);
    QSqlQuery q(database);
    if (!q.exec(sql)) {
        result.errorText = q.lastError().text();
        return result;
    }
    const QSqlRecord rec = q.record();
    for (int i = 0; i < rec.count(); ++i) result.headers.push_back(rec.fieldName(i));
    while (q.next()) {
        if (params.pageSize > 0 && result.rows.size() >= params.pageSize) { result.hasMore=true; break; }
        QVector<QVariant> row;
        row.reserve(rec.count());
        for (int i = 0; i < rec.count(); ++i) row.push_back(q.value(i));
        result.rows.push_back(std::move(row));
    }
    return result;
}

} // namespace HistoryQueryService
