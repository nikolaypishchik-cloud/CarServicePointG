#pragma once

#include <QList>
#include <QString>

#include "core/repository/ServiceRepository.h"

class QSqlDatabase;

namespace ServiceBatchMutationService {

struct ReplaceRowsResult {
    bool ok = false;
    int lastServiceId = 0;
    QString errorText;
};

ReplaceRowsResult replaceRows(QSqlDatabase db,
                             int orderNumber,
                             const QList<WorkOrderServiceRow>& rows);

ReplaceRowsResult replaceRows(int orderNumber,
                             const QList<WorkOrderServiceRow>& rows);

} // namespace ServiceBatchMutationService
