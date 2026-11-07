#pragma once

#include <QString>

class QColor;

namespace StatsPageStyleSupport {

QString cardFrameQss();
QString cardTitleQss();
QString cardValueQss(const QColor& accent);
QString tableQss();
QString headerFrameQss();
QString headerTitleQss();
QString periodButtonQss();
QString dateEditQss();
QString refreshButtonQss();
QString tabWidgetQss();
QString problemsFrameQss();
QString problemsTitleQss();
QString problemItemQss();
QString alertsFrameQss();
QString alertsTitleQss();
QString alertItemQss();

} // namespace StatsPageStyleSupport
