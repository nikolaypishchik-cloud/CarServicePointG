#pragma once

#include "core/services/MutationResult.h"

#include <QSqlDatabase>
#include <QString>
#include <QStringList>

#include <functional>

namespace MutationExecutor {

struct Request {
    QString sourceModule;
    QString entityType;
    QString entityId;
    QString operation;
    QString description;
    QStringList changedTables;
    QString beginErrorFallback;
    QString commitErrorFallback;
    bool systemMaintenance = false;
};

using Result = MutationResult;

using Step = std::function<bool(QSqlDatabase&, QString*)>;

Result execute(QSqlDatabase db,
               const Request& request,
               const Step& step);

} // namespace MutationExecutor
