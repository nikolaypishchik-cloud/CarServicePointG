#include "core/services/HistoryRefreshJob.h"
#include "core/services/AccessControlService.h"

#include "core/readmodel/history/HistoryViewModelBuilder.h"
#include "core/cache/OrderSnapshotCache.h"
#include "core/services/AppRuntimeService.h"
#include "runtime/AsyncThreadScope.h"
#include "runtime/AppRuntime.h"

namespace HistoryRefreshJob {

Result run(const HistoryQueryParams& params, const std::shared_ptr<std::atomic_bool>& cancelToken)
{
    auto cancelled = [&cancelToken]() { return AsyncThreadScope::isCancellationRequested(cancelToken); };

    Result result;
    if (cancelled()) {
        result.cancelled = true;
        AppRuntimeService::closeThreadConnection();
        return result;
    }

    const QString cacheKey = OrderSnapshotCache::makeHistoryKey(AppRuntime::activeDbFilePath(), params.from, params.to, params.filter)
        + QString("|page=%1|offset=%2|").arg(params.pageSize).arg(params.offset) + AccessControlService::currentScopeKey();
    OrderSnapshotCache::HistorySnapshot cached;
    if (!AccessControlService::currentActorIsMaster() && OrderSnapshotCache::tryGetHistory(cacheKey, &cached)) {
        result.queryResult = cached.queryResult;
        result.readModel = cached.readModel;
        AppRuntimeService::closeThreadConnection();
        return result;
    }

    result.queryResult = HistoryQueryService::loadHistory(params);
    if (cancelled()) {
        result.cancelled = true;
        AppRuntimeService::closeThreadConnection();
        return result;
    }

    result.readModel = HistoryViewModelBuilder::build(result.queryResult, !params.filter.trimmed().isEmpty());
    OrderSnapshotCache::storeHistory(cacheKey, {result.queryResult, result.readModel});
    if (cancelled()) {
        result.cancelled = true;
    }
    AppRuntimeService::closeThreadConnection();
    return result;
}

} // namespace HistoryRefreshJob
