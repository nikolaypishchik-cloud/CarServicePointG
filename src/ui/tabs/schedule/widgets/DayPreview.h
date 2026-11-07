#pragma once
#include <QWidget>

class DayPreview : public QWidget {
    Q_OBJECT
public:
    explicit DayPreview(QWidget* parent=nullptr) : QWidget(parent) {}
};
