#include "core/services/PartOverduePolicy.h"
#include "core/services/PartsQueryService.h"

#include <QtTest/QtTest>

class PartOverduePolicyTests : public QObject
{
    Q_OBJECT

private slots:
    void rankMatrixAThroughIIsStable();
    void localizedActiveStatusesStayEligibleForOverdue();
    void paidInstalledDeliveryAndCloseTransitionsRecalculateRanks();
    void currentOrderAggregateUsesWorstProblemRankOnly();
    void partsQuerySortUsesPolicyTieBreakersAndVisiblePartDate();
    void highlightColorNamesFollowRankPolicy();
};

namespace {
PartOverduePolicy::Input inputFor(const QDate& today, int deliveryOffsetDays, bool paid, bool installed, const QString& status = QStringLiteral("open"))
{
    PartOverduePolicy::Input input;
    input.deliveryDate = deliveryOffsetDays == 999 ? QDate() : today.addDays(-deliveryOffsetDays);
    input.orderedDate = today.addDays(-30);
    input.installedDate = installed ? today.addDays(-1) : QDate();
    input.orderStatus = status;
    input.paid = paid;
    input.orderNumber = 172;
    input.rowId = 10;
    return input;
}
}

void PartOverduePolicyTests::rankMatrixAThroughIIsStable()
{
    const QDate today(2026, 4, 20);

    auto r = PartOverduePolicy::evaluate(inputFor(today, 5, false, false), today);
    QCOMPARE(r.rank, int(PartOverduePolicy::RankCriticalUnpaidNotInstalled));
    QCOMPARE(r.severity, int(PartOverduePolicy::SeverityCritical));
    QVERIFY(r.overdue);

    r = PartOverduePolicy::evaluate(inputFor(today, 5, false, true), today);
    QCOMPARE(r.rank, int(PartOverduePolicy::RankCriticalInstalledNotPaid));
    QCOMPARE(r.severity, int(PartOverduePolicy::SeverityCritical));

    r = PartOverduePolicy::evaluate(inputFor(today, 3, false, false), today);
    QCOMPARE(r.rank, int(PartOverduePolicy::RankWarningUnpaidNotInstalled));
    QCOMPARE(r.severity, int(PartOverduePolicy::SeverityWarning));

    r = PartOverduePolicy::evaluate(inputFor(today, 3, false, true), today);
    QCOMPARE(r.rank, int(PartOverduePolicy::RankWarningInstalledNotPaid));
    QCOMPARE(r.severity, int(PartOverduePolicy::SeverityWarning));

    r = PartOverduePolicy::evaluate(inputFor(today, 5, true, false), today);
    QCOMPARE(r.rank, int(PartOverduePolicy::RankActivePaidNotInstalled));
    QCOMPARE(r.severity, int(PartOverduePolicy::SeverityNormal));
    QVERIFY(!r.overdue);

    r = PartOverduePolicy::evaluate(inputFor(today, 5, true, true), today);
    QCOMPARE(r.rank, int(PartOverduePolicy::RankFinalizedOrClosed));
    QVERIFY(!r.overdue);

    auto missingDelivery = inputFor(today, 999, false, false);
    missingDelivery.orderedDate = today.addDays(-30);
    r = PartOverduePolicy::evaluate(missingDelivery, today);
    QCOMPARE(r.rank, int(PartOverduePolicy::RankActiveUnpaidNotOverdue));
    QVERIFY(!r.overdue);

    auto futureDelivery = inputFor(today, -2, false, false);
    r = PartOverduePolicy::evaluate(futureDelivery, today);
    QCOMPARE(r.rank, int(PartOverduePolicy::RankActiveUnpaidNotOverdue));

    auto closed = inputFor(today, 10, false, false, QStringLiteral("closed"));
    r = PartOverduePolicy::evaluate(closed, today);
    QCOMPARE(r.rank, int(PartOverduePolicy::RankFinalizedOrClosed));
    QVERIFY(!r.overdue);
}

void PartOverduePolicyTests::localizedActiveStatusesStayEligibleForOverdue()
{
    const QDate today(2026, 5, 18);
    for (const QString& status : {QString::fromUtf8("В роботі"), QString::fromUtf8("В роботi"), QString::fromUtf8("Очікування"), QStringLiteral("custom_active")}) {
        auto input = inputFor(today, 5, false, false, status);
        input.orderClosedAtRaw = QStringLiteral("0");
        const auto r = PartOverduePolicy::evaluate(input, today);
        QCOMPARE(r.rank, int(PartOverduePolicy::RankCriticalUnpaidNotInstalled));
        QCOMPARE(r.severity, int(PartOverduePolicy::SeverityCritical));
        QVERIFY(r.overdue);
    }
}

void PartOverduePolicyTests::paidInstalledDeliveryAndCloseTransitionsRecalculateRanks()
{
    const QDate today(2026, 4, 20);
    auto input = inputFor(today, 5, false, false);
    QCOMPARE(PartOverduePolicy::evaluate(input, today).rank, int(PartOverduePolicy::RankCriticalUnpaidNotInstalled));

    input.paid = true;
    QCOMPARE(PartOverduePolicy::evaluate(input, today).rank, int(PartOverduePolicy::RankActivePaidNotInstalled));

    input.paid = false;
    input.installedDate = today;
    QCOMPARE(PartOverduePolicy::evaluate(input, today).rank, int(PartOverduePolicy::RankCriticalInstalledNotPaid));

    input.deliveryDate = QDate();
    QCOMPARE(PartOverduePolicy::evaluate(input, today).rank, int(PartOverduePolicy::RankActiveUnpaidNotOverdue));

    input.deliveryDate = today.addDays(-5);
    input.orderClosedDate = today;
    QCOMPARE(PartOverduePolicy::evaluate(input, today).rank, int(PartOverduePolicy::RankFinalizedOrClosed));
}

void PartOverduePolicyTests::currentOrderAggregateUsesWorstProblemRankOnly()
{
    PartOverduePolicy::OrderAggregate aggregate;

    PartOverduePolicy::Result nonProblem;
    nonProblem.rank = PartOverduePolicy::RankActiveUnpaidNotOverdue;
    nonProblem.severity = PartOverduePolicy::SeverityNormal;
    PartOverduePolicy::mergeIntoOrderAggregate(&aggregate, nonProblem);
    QCOMPARE(aggregate.sortRank, int(PartOverduePolicy::RankFinalizedOrClosed));
    QCOMPARE(aggregate.severity, int(PartOverduePolicy::SeverityNormal));
    QVERIFY(!aggregate.overdue);

    PartOverduePolicy::Result warning;
    warning.rank = PartOverduePolicy::RankWarningInstalledNotPaid;
    warning.severity = PartOverduePolicy::SeverityWarning;
    PartOverduePolicy::mergeIntoOrderAggregate(&aggregate, warning);
    QCOMPARE(aggregate.sortRank, int(PartOverduePolicy::RankWarningInstalledNotPaid));
    QCOMPARE(aggregate.severity, int(PartOverduePolicy::SeverityWarning));
    QVERIFY(aggregate.overdue);

    PartOverduePolicy::Result critical;
    critical.rank = PartOverduePolicy::RankCriticalUnpaidNotInstalled;
    critical.severity = PartOverduePolicy::SeverityCritical;
    PartOverduePolicy::mergeIntoOrderAggregate(&aggregate, critical);
    QCOMPARE(aggregate.sortRank, int(PartOverduePolicy::RankCriticalUnpaidNotInstalled));
    QCOMPARE(aggregate.severity, int(PartOverduePolicy::SeverityCritical));
}

void PartOverduePolicyTests::partsQuerySortUsesPolicyTieBreakersAndVisiblePartDate()
{
    const QDate today(2026, 4, 20);

    auto makePart = [&](int id, int orderNumber, int deliveryOffsetDays, bool paid, bool installed, const QString& status = QStringLiteral("open"), const QString& closedAt = QString()) {
        PartRowSnapshot row;
        row.id = id;
        row.orderNumber = orderNumber;
        row.orderStatus = status;
        row.orderOpenedAtRaw = QStringLiteral("2026-04-01");
        row.orderClosedAtRaw = closedAt;
        row.dateRaw = QStringLiteral("2026-03-01");
        row.deliveryDateRaw = today.addDays(-deliveryOffsetDays).toString(Qt::ISODate);
        row.installedAtRaw = installed ? QStringLiteral("2026-04-19") : QString();
        row.paid = paid;
        row.hasInvoiceAttachment = true;
        return row;
    };

    QVector<PartRowSnapshot> rows;
    rows << makePart(10, 101, 3, false, false); // rank 2
    rows << makePart(11, 102, 5, false, true);  // rank 1
    rows << makePart(12, 103, 5, false, false); // rank 0
    rows << makePart(13, 104, 3, false, true);  // rank 3
    rows << makePart(14, 105, 5, true, false);  // rank 5, not highlighted

    PartsQueryService::sortRowsForTable(&rows, today);
    QCOMPARE(rows.at(0).id, 12);
    QCOMPARE(rows.at(1).id, 11);
    QCOMPARE(rows.at(2).id, 10);
    QCOMPARE(rows.at(3).id, 13);
    QCOMPARE(rows.at(4).id, 14);

    QVector<PartRowSnapshot> closedRows;
    auto visibleApr06ClosedLater = makePart(20, 150, 10, true, false, QStringLiteral("closed"), QStringLiteral("2026-04-07 12:42:13"));
    visibleApr06ClosedLater.dateRaw = QStringLiteral("2026-04-06");
    auto visibleApr07ClosedEarlier = makePart(21, 101, 10, true, false, QStringLiteral("closed"), QStringLiteral("2026-04-07 09:03:36"));
    visibleApr07ClosedEarlier.dateRaw = QStringLiteral("2026-04-07");
    auto visibleApr01HighOrder = makePart(22, 134, 10, true, false, QStringLiteral("closed"), QStringLiteral("2026-03-31 10:07:35"));
    visibleApr01HighOrder.dateRaw = QStringLiteral("2026-04-01");
    auto visibleApr01LowOrder = makePart(23, 126, 10, true, false, QStringLiteral("closed"), QStringLiteral("2026-04-03 13:34:34"));
    visibleApr01LowOrder.dateRaw = QStringLiteral("2026-04-01");
    closedRows << visibleApr06ClosedLater << visibleApr01LowOrder << visibleApr07ClosedEarlier << visibleApr01HighOrder;
    PartsQueryService::sortRowsForTable(&closedRows, today);
    QCOMPARE(closedRows.at(0).id, 21); // visible 07.04 must stay above hidden later 06.04
    QCOMPARE(closedRows.at(1).id, 20);
    QCOMPARE(closedRows.at(2).id, 22); // same visible date: order 134 before 126
    QCOMPARE(closedRows.at(3).id, 23);
}

void PartOverduePolicyTests::highlightColorNamesFollowRankPolicy()
{
    QCOMPARE(PartOverduePolicy::highlightColorNameForRank(0), QStringLiteral("red"));
    QCOMPARE(PartOverduePolicy::highlightColorNameForRank(1), QStringLiteral("red"));
    QCOMPARE(PartOverduePolicy::highlightColorNameForRank(2), QStringLiteral("yellow"));
    QCOMPARE(PartOverduePolicy::highlightColorNameForRank(3), QStringLiteral("yellow"));
    QVERIFY(PartOverduePolicy::highlightColorNameForRank(5).isEmpty());
    QVERIFY(PartOverduePolicy::highlightColorNameForRank(9).isEmpty());
}

QTEST_MAIN(PartOverduePolicyTests)
#include "PartOverduePolicyTests.moc"
