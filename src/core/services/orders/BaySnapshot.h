#pragma once
#include "core/services/orders/BayLoadReport.h"
#include <QJsonObject>
#include <functional>
class QObject;

namespace BaySnapshot {
struct Data {
    QList<BayLoadReport::Bay> bays;
    QMap<QString, QString> names;
    QJsonObject states;
    QDateTime asOf;
    QString error;
};
// Reject malformed snapshots rather than turn missing fields into free capacity.
Data parse(const QJsonObject& object);
void remember(const Data& data);
QMap<QString, int> currentPosts(); // Missing is unknown; zero is confirmed off-bay.
void refresh(QObject* owner, const std::function<void()>& updated);
}
