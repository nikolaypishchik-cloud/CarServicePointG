#pragma once

#include "core/readmodel/TabularReadModel.h"
#include "core/services/HistoryQueryService.h"

namespace HistoryViewModelBuilder {

TabularReadModel build(const HistoryQueryResult& queryResult, bool showOrderYear = false);

} // namespace HistoryViewModelBuilder
