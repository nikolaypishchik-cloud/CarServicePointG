#include "core/services/PartOverduePolicy.h"

#include <QDebug>
#include <QLoggingCategory>
#include <QRegularExpression>

#include <utility>

Q_LOGGING_CATEGORY(csPartOverduePolicyLog, "carservice.parts.overdue.policy")

namespace PartOverduePolicy {

namespace {

QString normalizeStatusForMatch(QString status)
{
    status = status.trimmed().toLower();
    status.replace(QChar(0x2011), QLatin1Char('-'));
    status.replace(QChar(0x2013), QLatin1Char('-'));
    status.replace(QChar(0x2014), QLatin1Char('-'));
    status.replace(QChar(0x00A0), QLatin1Char(' '));
    status = status.simplified();

    // Some rows created by old builds contain Latin look-alikes inside Ukrainian text
    // (for example "в роботi" instead of "в роботі").  Apply this only to strings
    // that already contain Cyrillic characters so English statuses such as
    // "in_progress"/"open" remain intact.
    bool hasCyrillic = false;
    for (const QChar ch : status) {
        const ushort u = ch.unicode();
        if (u >= 0x0400 && u <= 0x04FF) { hasCyrillic = true; break; }
    }
    if (hasCyrillic) {
        status.replace(QLatin1Char('i'), QChar(0x0456));
        status.replace(QLatin1Char('I'), QChar(0x0406));
    }
    return status;
}

QString compactStatusForMatch(QString status)
{
    status = normalizeStatusForMatch(std::move(status));
    status.remove(QRegularExpression(QStringLiteral(R"([\s_\-/]+)")));
    return status;
}

bool hasClosedMarker(const QString& rawClosedAt, const QDate& parsedClosedDate)
{
    if (parsedClosedDate.isValid()) {
        if (parsedClosedDate == QDate(1899, 12, 30)
            || parsedClosedDate == QDate(1900, 1, 1)
            || parsedClosedDate == QDate(1970, 1, 1)) {
            return false;
        }
        return true;
    }

    QString marker = rawClosedAt.trimmed().toLower();
    marker.replace(QChar(0x2011), QLatin1Char('-'));
    marker.replace(QChar(0x2013), QLatin1Char('-'));
    marker.replace(QChar(0x2014), QLatin1Char('-'));
    marker = marker.simplified();
    if (marker.isEmpty()) return false;

    if (marker.startsWith(QStringLiteral("0000-00-00"))
        || marker.startsWith(QStringLiteral("1899-12-30"))
        || marker.startsWith(QStringLiteral("1900-01-01"))
        || marker.startsWith(QStringLiteral("1970-01-01"))) return false;

    if (marker == QStringLiteral("0")
        || marker == QStringLiteral("null")
        || marker == QStringLiteral("none")
        || marker == QStringLiteral("-")
        || marker == QStringLiteral("0000-00-00")
        || marker == QStringLiteral("0000-00-00 00:00:00")
        || marker == QStringLiteral("0000-00-00t00:00:00")) {
        return false;
    }

    return true;
}

} // namespace

QDate businessToday(const QDate& today)
{
    return today.isValid() ? today : QDate::currentDate();
}

QString normalizedOrderStatus(QString status)
{
    status = normalizeStatusForMatch(std::move(status));
    const QString compact = compactStatusForMatch(status);

    if (status == QStringLiteral("open") || status == QStringLiteral("opened")
        || compact == QStringLiteral("open") || compact == QStringLiteral("opened")
        || compact == QString::fromUtf8("відкритий") || compact == QString::fromUtf8("відкрито")
        || compact == QString::fromUtf8("открыт") || compact == QString::fromUtf8("открыто")) {
        return QStringLiteral("open");
    }

    if (status == QStringLiteral("in_progress") || status == QStringLiteral("in-progress")
        || status == QStringLiteral("in progress") || status == QStringLiteral("in_work")
        || compact == QStringLiteral("inprogress") || compact == QStringLiteral("inwork")
        || compact == QString::fromUtf8("вроботі") || compact == QString::fromUtf8("уроботі")
        || compact == QString::fromUtf8("вработе") || compact == QString::fromUtf8("впроцесі")
        || compact == QString::fromUtf8("упроцесі") || compact == QString::fromUtf8("впроцессе")
        || compact == QString::fromUtf8("очікування") || compact == QString::fromUtf8("ожидание")) {
        return QStringLiteral("in_progress");
    }

    if (status == QStringLiteral("closed") || status == QStringLiteral("done")
        || status == QStringLiteral("completed") || status == QStringLiteral("paid")
        || status == QStringLiteral("archived") || status == QStringLiteral("archive")
        || status == QStringLiteral("cancelled") || status == QStringLiteral("canceled")
        || compact == QString::fromUtf8("закритий") || compact == QString::fromUtf8("закрито")
        || compact == QString::fromUtf8("закрыт") || compact == QString::fromUtf8("закрыто")
        || compact == QString::fromUtf8("виконаний") || compact == QString::fromUtf8("виконано")
        || compact == QString::fromUtf8("выполнен") || compact == QString::fromUtf8("выполнено")
        || compact == QString::fromUtf8("оплачено") || compact == QString::fromUtf8("архів")
        || compact == QString::fromUtf8("архив") || compact == QString::fromUtf8("скасований")
        || compact == QString::fromUtf8("скасовано") || compact == QString::fromUtf8("отменен")
        || compact == QString::fromUtf8("отменено")) {
        return QStringLiteral("closed");
    }

    return status;
}

bool isClosedOrderStatus(const QString& normalizedStatus)
{
    return normalizedStatus == QStringLiteral("closed")
        || normalizedStatus == QStringLiteral("done")
        || normalizedStatus == QStringLiteral("completed")
        || normalizedStatus == QStringLiteral("paid")
        || normalizedStatus == QStringLiteral("archived")
        || normalizedStatus == QStringLiteral("archive");
}

bool isCriticalRank(int rank)
{
    return rank == RankCriticalUnpaidNotInstalled
        || rank == RankCriticalInstalledNotPaid;
}

bool isWarningRank(int rank)
{
    return rank == RankWarningUnpaidNotInstalled
        || rank == RankWarningInstalledNotPaid;
}

bool isProblemRank(int rank)
{
    return isCriticalRank(rank) || isWarningRank(rank);
}

int severityForRank(int rank)
{
    if (isCriticalRank(rank)) return SeverityCritical;
    if (isWarningRank(rank)) return SeverityWarning;
    return SeverityNormal;
}

QString highlightColorNameForRank(int rank)
{
    if (isCriticalRank(rank)) return QStringLiteral("red");
    if (isWarningRank(rank)) return QStringLiteral("yellow");
    return QString();
}

bool isActiveOrder(const QString& orderStatus, const QString& orderClosedAtRaw, const QDate& orderClosedDate)
{
    if (hasClosedMarker(orderClosedAtRaw, orderClosedDate)) return false;

    const QString status = normalizedOrderStatus(orderStatus);
    if (status == QStringLiteral("open") || status == QStringLiteral("in_progress")) return true;
    if (status.isEmpty()) return true; // legacy rows without status and without a close marker are open-compatible
    if (isClosedOrderStatus(status)) return false;

    // A non-empty, non-closed business status without a close marker is safer to
    // treat as active than to silently suppress overdue highlighting.  This covers
    // older/localized values that are displayed as open orders but were not mapped
    // by earlier builds.
    return true;
}

Result evaluate(const Input& input, const QDate& todayValue)
{
    Result result;
    const QDate today = businessToday(todayValue);

    result.activeOrder = isActiveOrder(input.orderStatus, input.orderClosedAtRaw, input.orderClosedDate);
    result.eligibleForOverdue = result.activeOrder && !input.paid;

    if (!result.activeOrder) {
        result.rank = RankFinalizedOrClosed;
    } else if (input.paid && input.installedDate.isValid()) {
        result.rank = RankFinalizedOrClosed;
    } else if (input.paid && !input.installedDate.isValid()) {
        result.rank = RankActivePaidNotInstalled;
    } else if (!input.deliveryDate.isValid() || !today.isValid()) {
        result.rank = RankActiveUnpaidNotOverdue;
    } else {
        const int days = input.deliveryDate.daysTo(today);
        if (days < 0) {
            result.rank = RankActiveUnpaidNotOverdue;
        } else {
            result.overdueDays = days;
            if (days >= 5) {
                result.rank = input.installedDate.isValid() ? RankCriticalInstalledNotPaid : RankCriticalUnpaidNotInstalled;
            } else if (days >= 3) {
                result.rank = input.installedDate.isValid() ? RankWarningInstalledNotPaid : RankWarningUnpaidNotInstalled;
            } else {
                result.rank = RankActiveUnpaidNotOverdue;
            }
        }
    }

    result.severity = severityForRank(result.rank);
    result.overdue = isProblemRank(result.rank);

    qCDebug(csPartOverduePolicyLog).noquote()
        << QStringLiteral("PART_OVERDUE_POLICY rowId=") << input.rowId
        << QStringLiteral(" orderUid=") << input.orderUid
        << QStringLiteral(" orderNumber=") << input.orderNumber
        << QStringLiteral(" deliveryDate=") << (input.deliveryDate.isValid() ? input.deliveryDate.toString(Qt::ISODate) : QString())
        << QStringLiteral(" paid=") << input.paid
        << QStringLiteral(" installed=") << input.installedDate.isValid()
        << QStringLiteral(" activeOrder=") << result.activeOrder
        << QStringLiteral(" days=") << result.overdueDays
        << QStringLiteral(" rank=") << result.rank
        << QStringLiteral(" severity=") << result.severity;

    return result;
}

void mergeIntoOrderAggregate(OrderAggregate* aggregate, const Result& policyResult)
{
    if (!aggregate) return;

    // Для вкладки "Поточні наряди" сортування піднімає тільки проблемні групи 0–3.
    // Rank 4/5/9 залишають наряд у звичайному блоці без псевдо-прострочки.
    if (!isProblemRank(policyResult.rank)) return;

    if (policyResult.rank < aggregate->sortRank) aggregate->sortRank = policyResult.rank;
    if (isCriticalRank(policyResult.rank)) {
        aggregate->severity = SeverityCritical;
        aggregate->overdue = true;
        return;
    }
    if (aggregate->severity < SeverityCritical && isWarningRank(policyResult.rank)) {
        aggregate->severity = SeverityWarning;
        aggregate->overdue = true;
    }
}

OrderAggregate aggregateOrderResults(const QVector<Result>& results)
{
    OrderAggregate aggregate;
    for (const Result& result : results) mergeIntoOrderAggregate(&aggregate, result);
    return aggregate;
}

} // namespace PartOverduePolicy
