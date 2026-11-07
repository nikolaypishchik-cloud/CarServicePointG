#include "ui/tabs/reports/ReportsAsyncSupport.h"
#include "ui/tabs/reports/ReportsTableSupport.h"
#include <functional>

#include "core/cache/ReportsSnapshotCache.h"
#include "core/perf/PerfWatchdog.h"
#include "core/services/AppRuntimeService.h"
#include "runtime/AsyncTaskHandle.h"
#include "runtime/AsyncThreadScope.h"
#include "runtime/RuntimeJobDispatcher.h"

#include <QCryptographicHash>
#include <QTableView>
#include <QThread>

#include <memory>
#include <utility>

namespace {

QString hashExtra(const QString& value)
{
    if (value.trimmed().isEmpty()) return QString();
    return QString::fromLatin1(QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha1).toHex());
}

QString partsCacheExtra(double defaultParts,
                        double defaultOil,
                        const QMap<QString,double>& partsMap,
                        const QMap<QString,double>& oilMap)
{
    Q_UNUSED(defaultParts);
    Q_UNUSED(defaultOil);
    // Global defaults are UI-only historical values for this policy. They must not
    // influence report cache invalidation because they do not influence effective %.
    QString payload = QStringLiteral("canonical-store-policy-v2");
    for (auto it = partsMap.cbegin(); it != partsMap.cend(); ++it) {
        payload += QStringLiteral("|p:%1=%2").arg(it.key(), QString::number(it.value(), 'f', 3));
    }
    for (auto it = oilMap.cbegin(); it != oilMap.cend(); ++it) {
        payload += QStringLiteral("|o:%1=%2").arg(it.key(), QString::number(it.value(), 'f', 3));
    }
    return hashExtra(payload);
}

template <typename Snapshot, typename BuildFn, typename ReadyFn, typename TryGetFn, typename StoreFn>
void startSnapshotWorkerImpl(QObject* owner,
                             const AsyncTaskHandle& asyncHandle,
                             const QString& family,
                             const QString& cacheKey,
                             TryGetFn&& tryGet,
                             StoreFn&& store,
                             BuildFn&& build,
                             ReadyFn&& ready)
{
    Snapshot cached;
    if (tryGet(cacheKey, &cached)) {
        ready(cached);
        return;
    }

    PerfWatchdog::asyncJobStarted(family, cacheKey);
    const auto runtimeTicket = RuntimeJob::RuntimeJobDispatcher::instance().startLatest(
        RuntimeJob::Family::Reports,
        QStringLiteral("ReportsPage/") + family,
        30000,
        50);
    auto snapshot = std::make_shared<Snapshot>();
    auto* worker = AsyncThreadScope::startWorker(owner,
                                               family + QStringLiteral("_") + cacheKey.left(32),
                                               asyncHandle.cancelToken(),
                                               [snapshot, build = std::forward<BuildFn>(build), family, runtimeTicket]() mutable {
        RuntimeJob::RuntimeJobDispatcher::instance().markRunning(runtimeTicket);
        PerfWatchdog::ScopedTimer timer(family + QStringLiteral("/build"), 120);
        *snapshot = build();
        AppRuntimeService::closeThreadConnection();
    });
    QObject::connect(worker, &QThread::finished, worker, [snapshot, family, cacheKey, store = std::forward<StoreFn>(store), asyncHandle, runtimeTicket]() mutable {
        const bool canApply = !asyncHandle.isCancelled()
            && RuntimeJob::RuntimeJobDispatcher::instance().shouldApply(runtimeTicket);
        RuntimeJob::RuntimeJobDispatcher::instance().finish(runtimeTicket, canApply ? RuntimeJob::Status::Succeeded : RuntimeJob::Status::Cancelled);
        if (canApply) {
            store(cacheKey, *snapshot);
        }
        PerfWatchdog::asyncJobFinished(family, cacheKey);
    });
    QObject::connect(worker, &QThread::finished, owner, [snapshot, ready = std::forward<ReadyFn>(ready), asyncHandle, runtimeTicket]() mutable {
        if (!asyncHandle.shouldApply() || !RuntimeJob::RuntimeJobDispatcher::instance().shouldApply(runtimeTicket)) return;
        ready(*snapshot);
    });
    worker->start();
}

} // namespace

namespace ReportsAsyncSupport {

bool isAsyncTab(int idx)
{
    return idx >= 0 && idx <= 6;
}

QString buildTabKey(int idx, const QDate& from, const QDate& to)
{
    return QStringLiteral("%1|%2|%3").arg(idx).arg(from.toString(Qt::ISODate), to.toString(Qt::ISODate));
}

void showLoadingRow(QTableView* table, ReportsTableSupport::ReportsSnapshotTableModel* model, const QString& text)
{
    ReportsTableSupport::showLoadingRow(table, model, text);
}

void startPeriodLoad(QObject* owner,
                     const AsyncTaskHandle& asyncHandle,
                     const QString& dbPath,
                     const QString& df,
                     const QString& dt,
                     std::function<void(const PeriodSnapshot&)> onReady)
{
    const QString cacheKey = ReportsSnapshotCache::makeReportsKey(QStringLiteral("period"), dbPath, df, dt);
    startSnapshotWorkerImpl<PeriodSnapshot>(owner,
        asyncHandle,
        QStringLiteral("reports.period"),
        cacheKey,
        ReportsSnapshotCache::tryGetPeriod,
        ReportsSnapshotCache::storePeriod,
        [dbPath, df, dt]() {
            return ReportsDataLoader::buildPeriodSnapshot(dbPath, df, dt);
        },
        std::move(onReady));
}

void startOrdersLoad(QObject* owner,
                     const AsyncTaskHandle& asyncHandle,
                     const QString& dbPath,
                     const QString& df,
                     const QString& dt,
                     std::function<void(const OrdersSnapshot&)> onReady,
                     const QString& filter)
{
    const QString cacheKey = ReportsSnapshotCache::makeReportsKey(QStringLiteral("orders"), dbPath, df, dt) + QStringLiteral("|search:") + filter;
    startSnapshotWorkerImpl<OrdersSnapshot>(owner,
        asyncHandle,
        QStringLiteral("reports.orders"),
        cacheKey,
        ReportsSnapshotCache::tryGetOrders,
        ReportsSnapshotCache::storeOrders,
        [dbPath, df, dt, filter]() {
            return ReportsDataLoader::buildOrdersSnapshot(dbPath, df, dt, filter);
        },
        std::move(onReady));
}

void startPartsLoad(QObject* owner,
                    const AsyncTaskHandle& asyncHandle,
                    const QString& dbPath,
                    const QString& df,
                    const QString& dt,
                    double defaultParts,
                    double defaultOil,
                    const QMap<QString,double>& partsMap,
                    const QMap<QString,double>& oilMap,
                    std::function<void(const PartsSnapshot&)> onReady)
{
    const QString cacheKey = ReportsSnapshotCache::makeReportsKey(QStringLiteral("parts"), dbPath, df, dt,
                                                                  partsCacheExtra(defaultParts, defaultOil, partsMap, oilMap));
    startSnapshotWorkerImpl<PartsSnapshot>(owner,
        asyncHandle,
        QStringLiteral("reports.parts"),
        cacheKey,
        ReportsSnapshotCache::tryGetParts,
        ReportsSnapshotCache::storeParts,
        [dbPath, df, dt, defaultParts, defaultOil, partsMap, oilMap]() {
            return ReportsDataLoader::buildPartsSnapshot(dbPath, df, dt, defaultParts, defaultOil, partsMap, oilMap);
        },
        std::move(onReady));
}

void startOutsourceLoad(QObject* owner,
                        const AsyncTaskHandle& asyncHandle,
                        const QString& dbPath,
                        const QString& df,
                        const QString& dt,
                        std::function<void(const OutsourceSnapshot&)> onReady)
{
    const QString cacheKey = ReportsSnapshotCache::makeReportsKey(QStringLiteral("outsource"), dbPath, df, dt);
    startSnapshotWorkerImpl<OutsourceSnapshot>(owner,
        asyncHandle,
        QStringLiteral("reports.outsource"),
        cacheKey,
        ReportsSnapshotCache::tryGetOutsource,
        ReportsSnapshotCache::storeOutsource,
        [dbPath, df, dt]() {
            return ReportsDataLoader::buildOutsourceSnapshot(dbPath, df, dt);
        },
        std::move(onReady));
}

void startExpensesLoad(QObject* owner,
                       const AsyncTaskHandle& asyncHandle,
                       const QString& dbPath,
                       const QString& df,
                       const QString& dt,
                       std::function<void(const ExpensesSnapshot&)> onReady)
{
    const QString cacheKey = ReportsSnapshotCache::makeReportsKey(QStringLiteral("expenses"), dbPath, df, dt);
    startSnapshotWorkerImpl<ExpensesSnapshot>(owner,
        asyncHandle,
        QStringLiteral("reports.expenses"),
        cacheKey,
        ReportsSnapshotCache::tryGetExpenses,
        ReportsSnapshotCache::storeExpenses,
        [dbPath, df, dt]() {
            return ReportsDataLoader::buildExpensesSnapshot(dbPath, df, dt);
        },
        std::move(onReady));
}

void startTotalExpensesLoad(QObject* owner,
                            const AsyncTaskHandle& asyncHandle,
                            const QString& dbPath,
                            const QString& df,
                            const QString& dt,
                            std::function<void(const TotalExpensesSnapshot&)> onReady)
{
    const QString cacheKey = ReportsSnapshotCache::makeReportsKey(QStringLiteral("total_expenses"), dbPath, df, dt);
    startSnapshotWorkerImpl<TotalExpensesSnapshot>(owner,
        asyncHandle,
        QStringLiteral("reports.total_expenses"),
        cacheKey,
        ReportsSnapshotCache::tryGetTotalExpenses,
        ReportsSnapshotCache::storeTotalExpenses,
        [dbPath, df, dt]() {
            return ReportsDataLoader::buildTotalExpensesSnapshot(dbPath, df, dt);
        },
        std::move(onReady));
}

void startPayrollLoad(QObject* owner,
                      const AsyncTaskHandle& asyncHandle,
                      const QString& dbPath,
                      const QString& df,
                      const QString& dt,
                      std::function<void(const PayrollSnapshot&)> onReady)
{
    const QString cacheKey = ReportsSnapshotCache::makeReportsKey(QStringLiteral("payroll"), dbPath, df, dt);
    startSnapshotWorkerImpl<PayrollSnapshot>(owner,
        asyncHandle,
        QStringLiteral("reports.payroll"),
        cacheKey,
        ReportsSnapshotCache::tryGetPayroll,
        ReportsSnapshotCache::storePayroll,
        [dbPath, df, dt]() {
            return ReportsDataLoader::buildPayrollSnapshot(dbPath, df, dt);
        },
        std::move(onReady));
}

} // namespace ReportsAsyncSupport
