#pragma once

#include <QString>

namespace OrderStoragePolicyService {

int activeClosedOrderLimit();
QString closedOrderPredicate(const QString& orderAlias);
QString rotationCandidatePredicate(const QString& orderAlias, const QString& workspaceAlias);

} // namespace OrderStoragePolicyService
