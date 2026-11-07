#pragma once

#include "core/services/AccessControlService.h"

namespace MainWindowMetricsService {

bool loadAnimationsEnabled();
int loadAnimationDurationMs();
bool loadUnpaidPurchasesSummary(int& countOut, double& sumOut);
bool loadUnpaidPurchasesSummary(int& countOut, double& sumOut, const AccessControlService::OrderScope& scope, bool* unresolved = nullptr);

} // namespace MainWindowMetricsService
