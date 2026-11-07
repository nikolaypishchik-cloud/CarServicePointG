#include "core/cache/SnapshotCacheInvalidator.h"

#include "core/cache/PartsQueryCache.h"
#include "core/cache/OrderSnapshotCache.h"
#include "core/cache/ReportsSnapshotCache.h"
#include "core/diagnostics/RuntimeLoggingControl.h"

#include <QDebug>

namespace {

bool touchesOrderHeaderOrServices(const QStringList& tables)
{
    for (const QString& table : tables) {
        const QString normalized = table.trimmed().toLower();
        if (normalized == QStringLiteral("work_orders")
            || normalized == QStringLiteral("router_work_orders")
            || normalized == QStringLiteral("router_cache_work_orders")
            || normalized == QStringLiteral("work_order_services")
            || normalized == QStringLiteral("router_work_order_services")
            || normalized == QStringLiteral("work_order_payments")
            || normalized == QStringLiteral("payments")) {
            return true;
        }
    }
    return false;
}

bool touchesOrderLifecycleState(const QStringList& tables)
{
    for (const QString& table : tables) {
        const QString normalized = table.trimmed().toLower();
        if (normalized == QStringLiteral("work_orders")
            || normalized == QStringLiteral("router_work_orders")
            || normalized == QStringLiteral("router_cache_work_orders")) {
            return true;
        }
    }
    return false;
}

bool touchesPartsArchive(const QStringList& tables)
{
    for (const QString& table : tables) {
        const QString normalized = table.trimmed().toLower();
        if (normalized == QStringLiteral("work_order_parts")
            || normalized == QStringLiteral("router_cache_work_order_parts")
            || normalized == QStringLiteral("router_work_order_parts")
            || normalized == QStringLiteral("work_order_part_attachments")) {
            return true;
        }
    }
    return false;
}

bool touchesReportsOrStats(const QStringList& tables)
{
    for (const QString& table : tables) {
        const QString normalized = table.trimmed().toLower();
        if (normalized == QStringLiteral("settings")
            || normalized == QStringLiteral("finance_settings")
            || normalized == QStringLiteral("work_order_payments")
            || normalized == QStringLiteral("payments")
            || normalized == QStringLiteral("expenses")
            || normalized == QStringLiteral("outsource_repairs")
            || normalized == QStringLiteral("masters")
            || normalized == QStringLiteral("users")
            || normalized == QStringLiteral("roles")) {
            return true;
        }
    }
    return false;
}

} // namespace

namespace SnapshotCacheInvalidator {

void invalidateReportsAndStats(const QString& reason)
{
    ReportsSnapshotCache::invalidateReports();
    ReportsSnapshotCache::invalidateStats();
    if (RuntimeLoggingControl::deepDebugEnabled()) {
        qInfo().noquote() << QStringLiteral("SNAPSHOT_CACHE_INVALIDATED families=reports,stats reason=") << reason;
    }
}

void invalidateOrdersAndParts(const QString& reason)
{
    OrderSnapshotCache::invalidateAll(reason);
    PartsQueryCache::invalidate(reason);
    invalidateReportsAndStats(reason);
    if (RuntimeLoggingControl::deepDebugEnabled()) {
        qInfo().noquote() << QStringLiteral("SNAPSHOT_CACHE_INVALIDATED families=current_orders,history,orders,parts reason=") << reason;
    }
}

void invalidateAfterMutation(const QStringList& changedTables, const QString& reason)
{
    // P1/pass10 cache/invalidation closure: all mutation invalidation routes through this central point,
    // but table families are now scoped. Service/header/payment edits do not clear the parts archive cache;
    // parts/attachment edits clear both order snapshots and parts because overdue/debt summaries depend on them.
    const bool orderLike = touchesOrderHeaderOrServices(changedTables);
    const bool orderLifecycleLike = touchesOrderLifecycleState(changedTables);
    const bool partsLike = touchesPartsArchive(changedTables);
    const bool reportLike = touchesReportsOrStats(changedTables);

    if (partsLike) {
        OrderSnapshotCache::invalidateAll(reason);
        PartsQueryCache::invalidate(reason);
        invalidateReportsAndStats(reason);
        if (RuntimeLoggingControl::deepDebugEnabled()) {
            qInfo().noquote() << QStringLiteral("SNAPSHOT_CACHE_INVALIDATED families=current_orders,history,orders,parts reason=") << reason;
        }
        return;
    }

    if (orderLifecycleLike) {
        // Order lifecycle/status edits affect the overdue policy for parts, so parts cache is also invalidated.
        OrderSnapshotCache::invalidateAll(reason);
        PartsQueryCache::invalidate(reason);
        invalidateReportsAndStats(reason);
        if (RuntimeLoggingControl::deepDebugEnabled()) {
            qInfo().noquote() << QStringLiteral("SNAPSHOT_CACHE_INVALIDATED families=current_orders,history,orders,parts reason=") << reason;
        }
        return;
    }

    if (orderLike) {
        OrderSnapshotCache::invalidateAll(reason);
        invalidateReportsAndStats(reason);
        if (RuntimeLoggingControl::deepDebugEnabled()) {
            qInfo().noquote() << QStringLiteral("SNAPSHOT_CACHE_INVALIDATED families=current_orders,history,orders reason=") << reason;
        }
        return;
    }

    if (reportLike) {
        invalidateReportsAndStats(reason);
    }
}

void invalidateAll(const QString& reason)
{
    OrderSnapshotCache::invalidateAll(reason);
    PartsQueryCache::invalidate(reason);
    ReportsSnapshotCache::clearAll();
    if (RuntimeLoggingControl::deepDebugEnabled()) {
        qInfo().noquote() << QStringLiteral("SNAPSHOT_CACHE_INVALIDATED families=all reason=") << reason;
    }
}

} // namespace SnapshotCacheInvalidator
