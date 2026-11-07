#pragma once

#include "core/services/OrderSearchRepository.h"

#include <QList>
#include <QString>

namespace ActiveArchiveRoutingLayer {

QList<OrderSearchHit> searchByVisibleNumber(int visibleNumber,
                                            int limit = 100,
                                            QString* errorText = nullptr);

bool prepareOrderForOpen(const QString& orderUid,
                         int fallbackInternalNumber,
                         int* resolvedInternalNumber,
                         QString* errorText = nullptr);

} // namespace ActiveArchiveRoutingLayer
