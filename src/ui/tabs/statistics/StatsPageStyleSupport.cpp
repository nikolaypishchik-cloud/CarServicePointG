#include "ui/tabs/statistics/StatsPageStyleSupport.h"

#include <QColor>

namespace StatsPageStyleSupport {

QString cardFrameQss()
{
    return QStringLiteral("QFrame{background:qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 rgba(39,50,64,230), stop:1 rgba(23,31,40,230)); border:1px solid rgba(130,150,170,110); border-radius:18px;}");
}

QString cardTitleQss()
{
    return QStringLiteral("color:#dfe8ef; font-size:13px; font-weight:700;");
}

QString cardValueQss(const QColor& accent)
{
    return QStringLiteral("color:%1; font-size:24px; font-weight:800;").arg(accent.name());
}

QString tableQss()
{
    return QStringLiteral(
        "QTableWidget{background:rgba(244,248,251,245); color:#17202a; border:1px solid rgba(120,140,160,90); border-radius:14px; gridline-color:rgba(120,140,160,55);}"
        "QHeaderView::section{background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #dde6ee, stop:1 #b9c8d6); color:#16202a; padding:8px; border:none; border-bottom:1px solid rgba(120,140,160,90); font-weight:800;}"
    );
}

QString headerFrameQss()
{
    return QStringLiteral("QFrame{background:qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 rgba(33,43,56,220), stop:1 rgba(54,66,82,220)); border:1px solid rgba(120,140,160,120); border-radius:24px;}");
}

QString headerTitleQss()
{
    return QStringLiteral("color:#f2f7fb; font-size:30px; font-weight:800;");
}

QString periodButtonQss()
{
    return QStringLiteral("QPushButton{background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #46576c, stop:1 #283342); color:white; border:1px solid rgba(150,175,200,120); border-radius:14px; padding:8px 14px; font-weight:700;} QPushButton:hover{border-color:#95c8ff;}");
}

QString dateEditQss()
{
    return QStringLiteral("QDateEdit{background:#f4f8fb; color:#18222d; border:1px solid #9fb7cf; border-radius:12px; padding:7px 10px; min-width:120px; font-weight:700;}");
}

QString refreshButtonQss()
{
    return QStringLiteral("QPushButton{background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #76c8db, stop:1 #3a6d95); color:white; border:none; border-radius:14px; padding:8px 18px; font-weight:800;}");
}

QString tabWidgetQss()
{
    return QStringLiteral("QTabWidget::pane{border:none; background:transparent;} QTabBar::tab{background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #4f6278, stop:1 #2b3645); color:white; border:1px solid rgba(120,140,160,120); border-bottom:none; padding:10px 18px; margin-right:6px; border-top-left-radius:14px; border-top-right-radius:14px; font-weight:700;} QTabBar::tab:selected{background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #dfe5eb, stop:1 #9eaab6); color:#101820;}");
}

QString problemsFrameQss()
{
    return QStringLiteral("QFrame{background:qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 rgba(63,39,39,210), stop:1 rgba(33,25,25,210)); border:1px solid rgba(255,128,128,130); border-radius:20px;}");
}

QString problemsTitleQss()
{
    return QStringLiteral("color:#ffd7d7; font-size:18px; font-weight:800;");
}

QString problemItemQss()
{
    return QStringLiteral("color:#f3f5f7; font-size:13px;");
}

QString alertsFrameQss()
{
    return QStringLiteral("QFrame{background:qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 rgba(56,35,35,220), stop:1 rgba(28,20,20,220)); border:1px solid rgba(255,120,120,120); border-radius:22px;}");
}

QString alertsTitleQss()
{
    return QStringLiteral("color:#ffe8e8; font-size:22px; font-weight:800;");
}

QString alertItemQss()
{
    return QStringLiteral("color:#f4f7fa; font-size:14px;");
}

} // namespace StatsPageStyleSupport
