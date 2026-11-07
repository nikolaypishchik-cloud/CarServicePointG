#pragma once

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QMap>
#include <QSet>
#include <QString>
#include <QTime>
#include <QTimeZone>
#include <optional>

namespace BayLoadReport {

// All intervals are UTC milliseconds, half-open [begin, end).
struct Interval { qint64 begin = 0; qint64 end = 0; };
struct Shift { QTime begin; QTime end; };
struct CalendarVersion {
    QDate from;
    QDate until; // Exclusive; invalid means no end date.
    QSet<int> weekdays;
    QList<Shift> shifts; // Breaks are represented by separate shifts.
};
struct Occupancy {
    QString orderUid;
    qint64 begin = 0;
    std::optional<qint64> end;
};
struct Bay {
    QString uid;
    qint64 activeFrom = 0;
    std::optional<qint64> inactiveFrom;
    QList<CalendarVersion> calendars;
    QList<Interval> unavailable;
    QList<Interval> coverage;
    QList<Interval> gaps;
    QList<Occupancy> occupancy;
};
struct Metrics {
    qint64 availableMs = 0;
    qint64 knownMs = 0;
    qint64 occupiedMs = 0;
    qint64 freeMs = 0;
    qint64 unknownMs = 0;
    qint64 calendarOccupiedMs = 0;
    qint64 outsideScheduleMs = 0;
    qint64 conflictMs = 0;
    bool calendarComplete = true;
    std::optional<double> loadPercent() const;
    std::optional<double> coveragePercent() const;
};
struct Result {
    QDateTime asOf;
    QMap<QString, Metrics> bays;
    Metrics total;
    QString error;
};

CalendarVersion confirmedDefaultCalendar(const QDate& effectiveFrom);
// Pure calculation: never reads or writes an application database or infers coverage.
Result calculate(const QList<Bay>& bays, QDate firstDay, QDate lastDay,
                 const QDateTime& now, const QTimeZone& zone = QTimeZone("Europe/Kyiv"));

} // namespace BayLoadReport
