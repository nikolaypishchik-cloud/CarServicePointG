#pragma once

#include <QDate>
#include <QString>

class PlanningPage;

class PlanningReminderController
{
public:
    explicit PlanningReminderController(PlanningPage* page);

    void checkReminders();
    void setReminderState(const QDate& day, int slotHour, const QString& rawText, const QString& marker);
    bool moveReminderEntry(const QDate& fromDay, int fromHour, const QString& rawText, const QDate& toDay, int toHour);
    void showAnimatedReminder(const QDate& day, int slotHour, const QString& rawText);

private:
    PlanningPage* m_page{};
};
