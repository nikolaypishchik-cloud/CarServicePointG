#pragma once
#include <QWidget>

class DayCell : public QWidget {
    Q_OBJECT
public:
    explicit DayCell(QWidget* parent=nullptr) : QWidget(parent) {}
};
