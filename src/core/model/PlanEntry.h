#pragma once
#include <QString>
#include <QDateTime>

struct PlanEntry {
    int id = 0;
    QDateTime start;
    int durationMin = 0;
    QString note;
};
