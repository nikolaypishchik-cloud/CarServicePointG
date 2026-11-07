#pragma once

#include <QDate>

class PlanningPage;

class PlanningEditorController
{
public:
    explicit PlanningEditorController(PlanningPage* page);

    void openEditorFor(const QDate& date);
    void closeEditor(bool save);
    void updateEditorHeader();

private:
    PlanningPage* m_page{};
};
