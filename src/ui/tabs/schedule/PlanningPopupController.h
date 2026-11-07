#pragma once

#include <QDate>

class QEvent;

class PlanningPage;

class PlanningPopupController
{
public:
    explicit PlanningPopupController(PlanningPage* page);

    void showPreviewFor(const QDate& date);
    void hidePreview();
    void dismissActiveReminder(bool immediate);
    bool handleCellEvent(class PlanningDayCell* cell, QEvent* event);

private:
    PlanningPage* m_page{};
};
