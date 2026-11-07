#pragma once

#include "core/services/MutationResult.h"

#include <QString>

namespace MutationAuditTrail {

void appendEvent(const MutationResult& result, const QString& status);

} // namespace MutationAuditTrail
