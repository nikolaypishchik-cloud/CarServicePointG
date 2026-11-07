#include "core/services/OrderStoragePolicyService.h"

namespace OrderStoragePolicyService {

int activeClosedOrderLimit()
{
    return 1000;
}

QString closedOrderPredicate(const QString& orderAlias)
{
    const QString alias = orderAlias.trimmed().isEmpty() ? QStringLiteral("o") : orderAlias.trimmed();
    return QStringLiteral(
        "(LOWER(TRIM(COALESCE(%1.status,''))) IN ('closed','закритий','закрито','виконаний','виконано','Закритий','Закрито','Виконаний','Виконано','done','completed','paid','оплачено','Оплачено','archived','archive','архів','Архів') "
        "OR (TRIM(COALESCE(%1.status,''))='' AND TRIM(COALESCE(%1.closed_at,''))<>''))")
        .arg(alias);
}

QString rotationCandidatePredicate(const QString& orderAlias, const QString& workspaceAlias)
{
    const QString order = orderAlias.trimmed().isEmpty() ? QStringLiteral("o") : orderAlias.trimmed();
    const QString workspace = workspaceAlias.trimmed().isEmpty() ? QStringLiteral("ws") : workspaceAlias.trimmed();
    return QStringLiteral("%1 AND COALESCE(%2.origin,'native') <> 'legacy_copy'")
        .arg(closedOrderPredicate(order), workspace);
}

} // namespace OrderStoragePolicyService
