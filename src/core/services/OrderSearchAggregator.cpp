#include "core/services/OrderSearchAggregator.h"

#include "core/services/ActiveArchiveRoutingLayer.h"

namespace OrderSearchAggregator {

QList<OrderSearchHit> findByVisibleNumber(int visibleNumber,
                                          int limit,
                                          QString* errorText)
{
    return ActiveArchiveRoutingLayer::searchByVisibleNumber(visibleNumber, limit, errorText);
}

} // namespace OrderSearchAggregator
