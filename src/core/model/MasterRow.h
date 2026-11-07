#pragma once

#include <QString>

struct MasterRow {
    QString id;
    QString name;
    QString role;   // master/admin
    double adminPercent = 0.0;
    bool active = true;
    QString activeFrom;
    QString activeTo;
};
