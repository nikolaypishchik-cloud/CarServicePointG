#pragma once
#include <QWidget>
#include <QPointer>
#include <functional>
#include "core/services/planning/PlanningDispatch.h"
class QDateEdit;
class QTimer;
class QRubberBand;
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
    void updateDragTarget(QPoint global);
    void cancelDrag();
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
    QTimer* dragScroll;
    QPoint pressPosition;
    int firstMinute = 480;
    int dropMinute = -1;
    QPointer<QPushButton> draggedCard;
    QRubberBand* dropIndicator = nullptr;
    QPoint dragPosition;
    PlanningDispatch::Entry draggedEntry;
    int scrollDirection = 0;
    int grabColumnOffset = 0;
    QPushButton *modify,*openOrder,*spares,*editDay;
    QListWidget *waiting,*attention;
    QList<PlanningDispatch::Entry> entries,queued;
    QDate first,last;
    int period=1,selected=-1;
};
