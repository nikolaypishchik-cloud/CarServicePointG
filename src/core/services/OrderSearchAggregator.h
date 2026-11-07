#pragma once

#include "core/services/OrderSearchRepository.h"

#include <QList>
#include <QString>

namespace OrderSearchAggregator {

QList<OrderSearchHit> findByVisibleNumber(int visibleNumber,
                                          int limit = 100,
                                          QString* errorText = nullptr);

} // namespace OrderSearchAggregator
