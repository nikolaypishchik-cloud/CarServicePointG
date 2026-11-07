#pragma once

#include <QSqlDatabase>
#include <QString>

namespace RemoteMutationApplyService {

struct ApplyStats {
    int applied = 0;
    int ignoredSelf = 0;
    int conflicts = 0;
    int failed = 0;
};

ApplyStats applyPendingEvents(QSqlDatabase db = QSqlDatabase(), int limit = 50, QString* errorText = nullptr);

} // namespace RemoteMutationApplyService
