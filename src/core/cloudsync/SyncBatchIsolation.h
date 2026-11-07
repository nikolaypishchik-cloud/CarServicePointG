#pragma once
#include <QJsonArray>

namespace SyncBatchIsolation {
// Retrying immutable event IDs is safe; split only explicit data rejections,
// never an authentication error, timeout or an unknown server failure.
template<class Send, class Reject>
bool deliver(const QJsonArray& events, Send&& send, Reject&& reject)
{
    if (events.isEmpty()) return true;
    const int status = send(events);
    if ((status == 409 || status == 422) && events.size() > 1) {
        QJsonArray left, right;
        for (qsizetype i=0; i<events.size(); ++i)
            (i<events.size()/2 ? left : right).append(events[i]);
        const bool first = deliver(left, send, reject);
        const bool second = deliver(right, send, reject);
        return first && second;
    }
    if (status >= 200 && status < 300) return true;
    reject(events);
    return false;
}
}
