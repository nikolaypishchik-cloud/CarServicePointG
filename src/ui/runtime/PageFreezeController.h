#pragma once

class QStackedWidget;
class QWidget;

class PageFreezeController
{
public:
    static void apply(QStackedWidget* stack, int activeIndex);
    static void freeze(QWidget* page);
    static void thaw(QWidget* page);
};
