#pragma once
#include <QWidget>
#include <functional>
#include "core/services/planning/PlanningDispatch.h"
class QDateEdit;
class QComboBox;
class QLineEdit;
class QLabel;
class QPushButton;
class QTableWidget;
class QListWidget;
class PlanningDesk : public QWidget {
public:
    PlanningDesk(QWidget* parent,std::function<void(QDate)> openDayEditor);
    void refresh();
    void selectDate(QDate day);
private:
    void render();
    void showEntry(int index);
    void edit(PlanningDispatch::Entry entry);
    void selectPeriod(int index);
    bool eventFilter(QObject* watched,QEvent* event) override;
    std::function<void(QDate)> openDayEditor_;
    QDateEdit* day;
    QComboBox* group;
    QLineEdit* search;
    QLabel* metrics[4];
    QPushButton* modes[3];
    QLabel* title;
    QTableWidget* grid;
    QLabel* details;
    QPushButton *modify,*openOrder,*spares,*editDay;
    QListWidget *waiting,*attention;
    QList<PlanningDispatch::Entry> entries,queued;
    QDate first,last;
    int period=1,selected=-1;
};
