#pragma once

#include "core/readmodel/TabularReadModel.h"
#include "core/services/orders/CurrentOrdersQueryService.h"

namespace CurrentOrdersViewModelBuilder {

TabularReadModel build(const CurrentOrdersQueryResult& queryResult);

} // namespace CurrentOrdersViewModelBuilder
