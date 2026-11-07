#pragma once

#include "core/readmodel/TabularReadModel.h"
#include "core/services/HistoryQueryService.h"

#include <atomic>
#include <memory>

namespace HistoryRefreshJob {

struct Result {
    HistoryQueryResult queryResult;
    TabularReadModel readModel;
    bool cancelled = false;
};

Result run(const HistoryQueryParams& params, const std::shared_ptr<std::atomic_bool>& cancelToken);

} // namespace HistoryRefreshJob
