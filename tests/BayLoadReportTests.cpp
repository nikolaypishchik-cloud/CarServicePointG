#include "core/services/orders/BayLoadReport.h"
#include <QtTest>

using namespace BayLoadReport;
namespace {
constexpr qint64 hour = 3600000;
const QTimeZone kyiv("Europe/Kyiv");
qint64 at(QDate date, int h, int m = 0)
{
    return QDateTime(date, QTime(h, m), kyiv).toMSecsSinceEpoch();
}
Bay fixture(QString id, QDate date)
{
    Bay bay;
    bay.uid = id;
    bay.activeFrom = at(date, 0);
    bay.calendars = {{date, {}, {1, 2, 3, 4, 5}, {{QTime(9, 0), QTime(17, 0)}}}};
    bay.coverage = {{at(date, 0), at(date.addDays(10), 0)}};
    return bay;
}
Result report(QList<Bay> bays, QDate date, int days = 1)
{
    return calculate(bays, date, date.addDays(days - 1), QDateTime(date.addDays(days), QTime(0, 0), kyiv));
}
}

class BayLoadReportTests : public QObject {
    Q_OBJECT
private slots:
    void engineRemovalExample()
    {
        const QDate date(2026, 10, 19);
        auto a = fixture("1", date), b = fixture("2", date);
        a.occupancy = {{"A", at(date, 9), at(date, 11)}, {"B", at(date, 11), at(date, 15)},
                       {"A", at(date, 16), at(date, 17)}};
        b.occupancy = {{"C", at(date, 9), at(date, 15)}};
        const auto r = report({a, b}, date);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.bays["1"].occupiedMs, 7 * hour);
        QCOMPARE(r.bays["1"].freeMs, hour);
        QCOMPARE(*r.bays["1"].loadPercent(), 87.5);
        QCOMPARE(r.total.occupiedMs, 13 * hour);
        QCOMPARE(r.total.availableMs, 16 * hour);
        QCOMPARE(*r.total.loadPercent(), 81.25);
    }
    void beforeReportAndOvernight()
    {
        const QDate date(2026, 10, 19);
        auto bay = fixture("1", date);
        bay.occupancy = {{"archived-order", at(date, 8), at(date, 10)}};
        auto r = report({bay}, date);
        QCOMPARE(r.total.occupiedMs, hour);
        QCOMPARE(r.total.calendarOccupiedMs, 2 * hour);
        bay.occupancy = {{"closed-order", at(date, 16), at(date.addDays(1), 10)}};
        r = report({bay}, date, 2);
        QCOMPARE(r.total.occupiedMs, 2 * hour);
        QCOMPARE(r.total.calendarOccupiedMs, 18 * hour);
        QCOMPARE(*r.total.loadPercent(), 12.5);
    }
    void weekend()
    {
        const QDate date(2026, 10, 23);
        auto bay = fixture("1", date);
        bay.occupancy = {{"A", at(date, 16), at(date.addDays(3), 10)}};
        const auto r = report({bay}, date, 4);
        QCOMPARE(r.total.availableMs, 16 * hour);
        QCOMPARE(r.total.occupiedMs, 2 * hour);
        QCOMPARE(*r.total.loadPercent(), 12.5);
    }
    void nowCapsOpenIntervalAndDenominator()
    {
        const QDate date(2026, 10, 19);
        auto bay = fixture("1", date);
        bay.occupancy = {{"A", at(date, 9), std::nullopt}};
        const auto r = calculate({bay}, date, date, QDateTime(date, QTime(13, 0), kyiv));
        QCOMPARE(r.total.availableMs, 4 * hour);
        QCOMPARE(r.total.occupiedMs, 4 * hour);
        QCOMPARE(*r.total.loadPercent(), 100.0);
        QVERIFY(!bay.occupancy.first().end.has_value());
    }
    void partialCoverageIsNotIdle()
    {
        const QDate date(2026, 10, 19);
        auto bay = fixture("1", date);
        bay.coverage = {{at(date, 12), at(date, 17)}};
        bay.occupancy = {{"A", at(date, 13), at(date, 15)}};
        const auto r = report({bay}, date);
        QCOMPARE(r.total.availableMs, 8 * hour);
        QCOMPARE(r.total.knownMs, 5 * hour);
        QCOMPARE(r.total.occupiedMs, 2 * hour);
        QCOMPARE(r.total.freeMs, 3 * hour);
        QCOMPARE(r.total.unknownMs, 3 * hour);
        QCOMPARE(*r.total.coveragePercent(), 62.5);
        QCOMPARE(*r.total.loadPercent(), 40.0);
    }
    void unavailabilityAndNonworkingDay()
    {
        const QDate date(2026, 10, 19);
        auto bay = fixture("1", date);
        bay.unavailable = {{at(date, 12), at(date, 14)}};
        bay.occupancy = {{"A", at(date, 9), at(date, 12)}, {"B", at(date, 14), at(date, 17)}};
        auto r = report({bay}, date);
        QCOMPARE(r.total.availableMs, 6 * hour);
        QCOMPARE(*r.total.loadPercent(), 100.0);
        r = report({bay}, date.addDays(6));
        QCOMPARE(r.total.availableMs, 0);
        QVERIFY(!r.total.loadPercent());
        QVERIFY(!r.total.coveragePercent());
    }
    void breaksAndVersionedCalendar()
    {
        const QDate date(2026, 10, 19);
        auto bay = fixture("1", date);
        bay.calendars[0].until = date.addDays(1);
        bay.calendars[0].shifts = {{QTime(9, 0), QTime(13, 0)}, {QTime(14, 0), QTime(17, 0)}};
        bay.occupancy = {{"A", at(date, 12), at(date, 15)}};
        auto r = report({bay}, date);
        QCOMPARE(r.total.availableMs, 7 * hour);
        QCOMPARE(r.total.occupiedMs, 2 * hour);
        bay.calendars.append(confirmedDefaultCalendar(date.addDays(1)));
        const auto again = report({bay}, date);
        QCOMPARE(again.total.availableMs, r.total.availableMs);
        QCOMPARE(again.total.occupiedMs, r.total.occupiedMs);
    }
    void daylightSavingAndInvalidBoundaries()
    {
        const QDate date(2024, 10, 27);
        auto bay = fixture("1", date);
        bay.calendars[0].weekdays = {7};
        bay.calendars[0].shifts = {{QTime(2, 0), QTime(5, 0)}};
        auto r = report({bay}, date);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.total.availableMs, 4 * hour);
        bay.calendars[0].shifts = {{QTime(3, 0), QTime(5, 0)}};
        QVERIFY(!report({bay}, date).error.isEmpty());
        const QDate spring(2024, 3, 31);
        bay = fixture("1", spring);
        bay.calendars[0].weekdays = {7};
        bay.calendars[0].shifts = {{QTime(3, 30), QTime(5, 0)}};
        QVERIFY(!report({bay}, spring).error.isEmpty());
    }
    void legacyOverlapsExcluded()
    {
        const QDate date(2026, 10, 19);
        auto bay = fixture("1", date);
        bay.occupancy = {{"A", at(date, 9), at(date, 12)}, {"B", at(date, 11), at(date, 14)}};
        const auto r = report({bay}, date);
        QCOMPARE(r.total.occupiedMs, 4 * hour);
        QCOMPARE(r.total.knownMs, 7 * hour);
        QCOMPARE(r.total.unknownMs, hour);
        QCOMPARE(r.total.conflictMs, hour);
    }
    void orderCannotOccupyTwoBays()
    {
        const QDate date(2026, 10, 19);
        auto a = fixture("1", date), b = fixture("3", date);
        a.occupancy = {{"A", at(date, 9), at(date, 12)}};
        b.occupancy = {{"A", at(date, 11), at(date, 14)}};
        const auto r = report({a, b}, date);
        QCOMPARE(r.bays["1"].conflictMs, hour);
        QCOMPARE(r.bays["3"].conflictMs, hour);
        QCOMPARE(r.total.occupiedMs, 4 * hour);
        QCOMPARE(r.total.unknownMs, 2 * hour);
    }
    void allThreePostsUserSchedule()
    {
        const QDate date(2026, 10, 24); // Saturday is working; Sunday is not.
        QList<Bay> bays;
        for (int i = 1; i <= 3; ++i) {
            auto bay = fixture(QString::number(i), date);
            bay.calendars = {confirmedDefaultCalendar(date)};
            bay.occupancy = {{QString::number(i), at(date, 8), at(date, 18)}};
            bays.append(bay);
        }
        const auto r = report(bays, date, 2);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.bays.size(), 3);
        QCOMPARE(r.total.availableMs, 27 * hour);
        QCOMPARE(r.total.occupiedMs, 27 * hour);
        QCOMPARE(r.total.calendarOccupiedMs, 30 * hour);
        QCOMPARE(r.total.outsideScheduleMs, 3 * hour);
    }
    void missingCoverageAndCalendar()
    {
        const QDate date(2026, 10, 19);
        auto bay = fixture("1", date);
        bay.coverage.clear();
        auto r = report({bay}, date);
        QCOMPARE(r.total.unknownMs, 8 * hour);
        QCOMPARE(r.total.freeMs, 0);
        QVERIFY(!r.total.loadPercent());
        QCOMPARE(*r.total.coveragePercent(), 0.0);
        bay.calendars.clear();
        r = report({bay}, date);
        QVERIFY(!r.total.calendarComplete);
        QVERIFY(!r.total.loadPercent());
        QVERIFY(!r.total.coveragePercent());
    }
    void weightedTotalsAndActivation()
    {
        const QDate date(2026, 10, 19);
        auto a = fixture("1", date), b = fixture("3", date);
        a.occupancy = {{"A", at(date, 9), at(date, 13)}};
        b.activeFrom = at(date, 15);
        b.occupancy = {{"B", at(date, 15), at(date, 17)}};
        const auto r = report({a, b}, date);
        QCOMPARE(r.total.availableMs, 10 * hour);
        QCOMPARE(*r.total.loadPercent(), 60.0); // Not the mean of 50% and 100%.
    }
    void rejectsMalformedInput()
    {
        const QDate date(2026, 10, 19);
        auto bay = fixture("1", date);
        QVERIFY(!report({bay, bay}, date).error.isEmpty());
        bay.calendars.append(bay.calendars.first());
        QVERIFY(!report({bay}, date).error.isEmpty());
        bay = fixture("1", date);
        bay.occupancy = {{"A", at(date, 12), at(date, 11)}};
        QVERIFY(!report({bay}, date).error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(BayLoadReportTests)
#include "BayLoadReportTests.moc"
