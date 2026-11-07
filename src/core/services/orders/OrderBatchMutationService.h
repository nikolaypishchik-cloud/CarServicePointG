#pragma once

#include "core/services/MutationResult.h"

#include <QList>
#include <QString>

class OrderBatchMutationService {
public:
    static MutationResult reopen(int orderNumber, const QString& status);
    static MutationResult setClosedDetailed(const QList<int>& orderNumbers,
                                           bool closed);
    static bool setClosed(const QList<int>& orderNumbers,
                          bool closed,
                          QString* errorText = nullptr);
};
