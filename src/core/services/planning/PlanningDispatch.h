#pragma once
#include <QDate>
#include <QString>
#include <QList>
namespace PlanningDispatch {
struct Entry {
    QDate day;
    int hour=0, start=0, minutes=0, bay=0;
    QString raw, vehicle, plate, client, work, mechanic, mechanicId, queueId;
    bool cancelled=false;
};
Entry decode(QDate day,int hour,const QString& text);
QString encode(const Entry& entry);
bool overlaps(const Entry& a,const Entry& b);
QList<Entry> queue(QString* error);
bool save(const Entry& before,const Entry& after,QString* error);
}
