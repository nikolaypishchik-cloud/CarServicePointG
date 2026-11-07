#pragma once

#include <QDate>
#include <QMap>
#include <QString>
#include <QSet>

// SQL-only repository for planning_calendar(day,note).
// note format: plain text lines "HH:00 some text"
class PlanningRepository {
public:
    QString getNote(const QDate& day) const;
    void setNote(const QDate& day, const QString& note) const;

    QMap<int, QString> getDayMap(const QDate& day) const;      // hour -> text
    QMap<int, QString> visibleDayMap(const QDate& day) const;
    QMap<QDate, QMap<int, QString>> visibleMonthMaps(int year, int month) const;
    QMap<QDate, QMap<int, QString>> getMonthMaps(int year, int month) const;
    void setDayMap(const QDate& day, const QMap<int, QString>& map) const;
    void setVisibleDayMap(const QDate& day, const QMap<int, QString>& map) const;

    bool isDayReminderActive(const QDate& day) const;
    QSet<QString> getMonthReminderActiveDayKeys(int year, int month) const;
    void setDayReminderActive(const QDate& day, bool active) const;
};
