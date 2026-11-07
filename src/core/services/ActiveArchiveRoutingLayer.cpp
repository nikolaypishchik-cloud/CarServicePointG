#include "core/services/ActiveArchiveRoutingLayer.h"

#include "core/services/OrderSearchRepository.h"
#include "infrastructure/db/sqlite/Db.h"
#include "core/diagnostics/RuntimeLoggingControl.h"

#include <QElapsedTimer>
#include <QDebug>

namespace ActiveArchiveRoutingLayer {

QList<OrderSearchHit> searchByVisibleNumber(int visibleNumber,
                                            int limit,
                                            QString* errorText)
{
    return OrderSearchRepository::findByVisibleNumber(Database::db(), visibleNumber, limit, errorText);
}

bool prepareOrderForOpen(const QString& orderUid,
                         int fallbackInternalNumber,
                         int* resolvedInternalNumber,
                         QString* errorText)
{
    QElapsedTimer timer;
    timer.start();
    int resolved = fallbackInternalNumber;
    const QString uid = orderUid.trimmed();

    if (!uid.isEmpty()) {
        if (!Database::ensureOrderAvailableInActiveByUid(uid, errorText)) return false;
        const int byUid = Database::resolveOrderInternalNumberByUid(uid);
        if (byUid > 0) resolved = byUid;
    } else if (resolved > 0) {
        if (!Database::ensureOrderAvailableInActive(resolved, errorText)) return false;
    }

    if (resolvedInternalNumber) *resolvedInternalNumber = resolved;
    if (resolved <= 0) {
        if (errorText && errorText->trimmed().isEmpty()) *errorText = QStringLiteral("Order routing failed");
        return false;
    }
    if (RuntimeLoggingControl::deepDebugEnabled()) {
        qInfo().noquote() << QStringLiteral("PERF ORDER_ROUTE order=%1 uid=%2 elapsed_ms=%3")
                                 .arg(QString::number(resolved), uid.isEmpty() ? QStringLiteral("<empty>") : uid, QString::number(timer.elapsed()));
    }
    return true;
}

} // namespace ActiveArchiveRoutingLayer
