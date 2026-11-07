#pragma once

#include <QDate>

class PlanningPage;

class PlanningCalendarController
{
public:
    explicit PlanningCalendarController(PlanningPage* page);

    void setMonth(const QDate& anyDayInMonth);
    void alignCurrentWeekTop(bool forceResetToTodayMonth = false);
    void ensureCellPoolSize(int requiredCount);
    void rebuildGrid();

private:
    PlanningPage* m_page{};
};
