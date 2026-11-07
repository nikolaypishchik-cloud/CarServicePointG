#pragma once
#include <QObject>
#include <QString>
#include <functional>

namespace ElitStartupSync {
// One startup run per process. The callback runs on the owner's UI thread.
void start(QObject* owner, const std::function<void(bool,const QString&)>& finished,
           const std::function<void(const QString&)>& progress = {});
bool request(QObject* owner, const std::function<void(bool,const QString&)>& finished,
             const std::function<void(const QString&)>& progress = {});
bool isRunning();
void cancel();
}
