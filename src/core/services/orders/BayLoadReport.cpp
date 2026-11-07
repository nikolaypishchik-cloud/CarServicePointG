#include "BayLoadReport.h"

#include <algorithm>

namespace BayLoadReport {
namespace {
using Intervals = QList<Interval>;

Intervals unite(Intervals rows)
{
    std::sort(rows.begin(), rows.end(), [](auto a, auto b) { return a.begin < b.begin; });
    Intervals result;
    for (auto row : rows) {
        if (row.end <= row.begin) continue;
        if (result.isEmpty() || result.last().end < row.begin) result.append(row);
        else result.last().end = std::max(result.last().end, row.end);
    }
    return result;
}

Intervals intersect(const Intervals& a, const Intervals& b)
{
    Intervals result;
    qsizetype i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        Interval row{std::max(a[i].begin, b[j].begin), std::min(a[i].end, b[j].end)};
        if (row.end > row.begin) result.append(row);
        if (a[i].end < b[j].end) ++i; else ++j;
    }
    return result;
}

Intervals subtract(const Intervals& a, const Intervals& b)
{
    Intervals result;
    for (auto row : a) {
        auto cursor = row.begin;
        for (auto cut : b) {
            if (cut.end <= cursor) continue;
            if (cut.begin >= row.end) break;
            if (cut.begin > cursor) result.append({cursor, cut.begin});
            cursor = std::max(cursor, cut.end);
            if (cursor >= row.end) break;
        }
        if (cursor < row.end) result.append({cursor, row.end});
    }
    return result;
}

qint64 duration(const Intervals& rows)
{
    qint64 result = 0;
    for (auto row : rows) result += row.end - row.begin;
    return result;
}

Intervals overlaps(const Intervals& rows)
{
    QMap<qint64, int> events;
    for (auto row : rows) { ++events[row.begin]; --events[row.end]; }
    Intervals result;
    int count = 0;
    qint64 previous = 0;
    for (auto event = events.cbegin(); event != events.cend(); ++event) {
        if (count > 1 && event.key() > previous) result.append({previous, event.key()});
        count += event.value();
        previous = event.key();
    }
    return unite(result);
}

QDateTime boundary(QDate date, QTime time, const QTimeZone& zone)
{
    return QDateTime(date, time, zone, QDateTime::TransitionResolution::Reject);
}

bool valid(const Intervals& rows)
{
    return std::all_of(rows.begin(), rows.end(), [](auto row) { return row.end > row.begin; });
}

void add(Metrics& total, const Metrics& row)
{
    total.availableMs += row.availableMs;
    total.knownMs += row.knownMs;
    total.occupiedMs += row.occupiedMs;
    total.freeMs += row.freeMs;
    total.unknownMs += row.unknownMs;
    total.calendarOccupiedMs += row.calendarOccupiedMs;
    total.outsideScheduleMs += row.outsideScheduleMs;
    total.conflictMs += row.conflictMs;
    total.calendarComplete &= row.calendarComplete;
}
} // namespace

std::optional<double> Metrics::loadPercent() const
{
    if (!calendarComplete || knownMs == 0) return std::nullopt;
    return 100.0 * double(occupiedMs) / double(knownMs);
}

std::optional<double> Metrics::coveragePercent() const
{
    if (!calendarComplete || availableMs == 0) return std::nullopt;
    return 100.0 * double(knownMs) / double(availableMs);
}

CalendarVersion confirmedDefaultCalendar(const QDate& effectiveFrom)
{
    return {effectiveFrom, {}, {1, 2, 3, 4, 5, 6},
            {{QTime(8, 0), QTime(12, 0)}, {QTime(13, 0), QTime(18, 0)}}};
}

Result calculate(const QList<Bay>& bays, QDate firstDay, QDate lastDay,
                 const QDateTime& now, const QTimeZone& zone)
{
    Result result;
    result.asOf = now.toUTC();
    auto fail = [&](const QString& text) {
        Result failed;
        failed.asOf = result.asOf;
        failed.error = text;
        return failed;
    };
    if (!zone.isValid() || !now.isValid() || !firstDay.isValid() || !lastDay.isValid()
        || lastDay < firstDay || firstDay.daysTo(lastDay) > 36600)
        return fail(QStringLiteral("Invalid report period or time zone"));
    const auto start = boundary(firstDay, QTime(0, 0), zone);
    const auto finish = boundary(lastDay.addDays(1), QTime(0, 0), zone);
    if (!start.isValid() || !finish.isValid())
        return fail(QStringLiteral("Ambiguous or nonexistent report boundary"));
    const auto cutoff = now.toMSecsSinceEpoch();
    const Interval period{start.toMSecsSinceEpoch(), std::min(finish.toMSecsSinceEpoch(), cutoff)};

    struct Entry { QString bay; QString order; Interval time; };
    QList<Entry> entries;
    QSet<QString> ids;
    for (const auto& bay : bays) {
        if (bay.uid.isEmpty() || ids.contains(bay.uid)
            || (bay.inactiveFrom && *bay.inactiveFrom <= bay.activeFrom)
            || !valid(bay.unavailable) || !valid(bay.coverage) || !valid(bay.gaps))
            return fail(QStringLiteral("Invalid bay or coverage intervals"));
        ids.insert(bay.uid);
        for (const auto& row : bay.occupancy) {
            if (row.orderUid.isEmpty() || (row.end && *row.end <= row.begin))
                return fail(QStringLiteral("Invalid occupancy interval"));
            const Interval clipped{std::max(row.begin, period.begin), std::min(row.end.value_or(cutoff), period.end)};
            if (clipped.begin < clipped.end)
                entries.append({bay.uid, row.orderUid, clipped});
        }
    }
    // Exclude conflicts on both resources: a bay AND an order cannot be occupied twice.
    QMap<QString, Intervals> conflicts, byBay, byOrder, orderConflicts;
    for (const auto& entry : entries) {
        byBay[entry.bay].append(entry.time);
        byOrder[entry.order].append(entry.time);
    }
    for (auto it = byBay.cbegin(); it != byBay.cend(); ++it) conflicts[it.key()] = overlaps(it.value());
    for (auto it = byOrder.cbegin(); it != byOrder.cend(); ++it) orderConflicts[it.key()] = overlaps(it.value());
    for (const auto& entry : entries)
        conflicts[entry.bay].append(intersect({entry.time}, orderConflicts.value(entry.order)));
    for (const auto& bay : bays) {
        Metrics metrics;
        const Intervals active = unite({{std::max(period.begin, bay.activeFrom),
                                        std::min(period.end, bay.inactiveFrom.value_or(period.end))}});
        Intervals available;
        for (const auto& calendar : bay.calendars) {
            if (!calendar.from.isValid() || (calendar.until.isValid() && calendar.until <= calendar.from))
                return fail(QStringLiteral("Invalid calendar version"));
            for (int day : calendar.weekdays)
                if (day < 1 || day > 7) return fail(QStringLiteral("Invalid calendar weekday"));
            for (const auto& shift : calendar.shifts)
                if (!shift.begin.isValid() || !shift.end.isValid() || shift.end <= shift.begin)
                    return fail(QStringLiteral("Invalid calendar shift"));
        }
        for (QDate date = firstDay; date <= lastDay && !active.isEmpty(); date = date.addDays(1)) {
            const auto dayStart = boundary(date, QTime(0, 0), zone);
            const auto dayEnd = boundary(date.addDays(1), QTime(0, 0), zone);
            if (!dayStart.isValid() || !dayEnd.isValid()) return fail(QStringLiteral("Invalid calendar day boundary"));
            if (intersect(active, {{dayStart.toMSecsSinceEpoch(), dayEnd.toMSecsSinceEpoch()}}).isEmpty()) continue;
            const CalendarVersion* selected = nullptr;
            for (const auto& calendar : bay.calendars) {
                if (date < calendar.from || (calendar.until.isValid() && date >= calendar.until)) continue;
                if (selected) return fail(QStringLiteral("Overlapping calendar versions"));
                selected = &calendar;
            }
            if (!selected) { metrics.calendarComplete = false; continue; }
            if (!selected->weekdays.contains(date.dayOfWeek())) continue;
            for (const auto& shift : selected->shifts) {
                const auto begin = boundary(date, shift.begin, zone);
                const auto end = boundary(date, shift.end, zone);
                if (!begin.isValid() || !end.isValid())
                    return fail(QStringLiteral("Ambiguous or nonexistent shift boundary requires explicit UTC offset"));
                available.append({begin.toMSecsSinceEpoch(), end.toMSecsSinceEpoch()});
            }
        }
        available = subtract(intersect(unite(available), active), unite(bay.unavailable));
        const auto conflict = intersect(unite(conflicts.value(bay.uid)), active);
        const auto known = subtract(subtract(intersect(unite(bay.coverage), active), unite(bay.gaps)), conflict);
        const auto knownAvailable = intersect(available, known);
        Intervals occupied;
        for (const auto& entry : entries) if (entry.bay == bay.uid) occupied.append(entry.time);
        occupied = unite(occupied);
        metrics.availableMs = duration(available);
        metrics.knownMs = duration(knownAvailable);
        metrics.occupiedMs = duration(intersect(occupied, knownAvailable));
        metrics.freeMs = metrics.knownMs - metrics.occupiedMs;
        metrics.unknownMs = metrics.availableMs - metrics.knownMs;
        metrics.calendarOccupiedMs = duration(intersect(occupied, known));
        metrics.outsideScheduleMs = metrics.calendarOccupiedMs - metrics.occupiedMs;
        metrics.conflictMs = duration(conflict);
        result.bays.insert(bay.uid, metrics);
        add(result.total, metrics);
    }
    return result;
}
} // namespace BayLoadReport
