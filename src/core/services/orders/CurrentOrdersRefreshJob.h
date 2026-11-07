#pragma once

#include "core/readmodel/TabularReadModel.h"
#include "core/services/orders/CurrentOrdersQueryService.h"

#include <QString>

#include <atomic>
#include <memory>

namespace CurrentOrdersRefreshJob {

struct Result {
    CurrentOrdersQueryResult queryResult;
    TabularReadModel readModel;
    bool cancelled = false;
};

Result run(const QString& filter, const std::shared_ptr<std::atomic_bool>& cancelToken);

} // namespace CurrentOrdersRefreshJob
