#pragma once

#include "core/services/MutationResult.h"

#include <QSqlDatabase>

namespace MutationPostCommitService {

void runAfterCommit(QSqlDatabase database, const MutationResult& result);

} // namespace MutationPostCommitService
