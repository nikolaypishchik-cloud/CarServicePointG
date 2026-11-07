#include "core/readmodel/orders/CurrentOrdersViewModelBuilder.h"

namespace CurrentOrdersViewModelBuilder {

TabularReadModel build(const CurrentOrdersQueryResult& queryResult)
{
    TabularReadModel model;
    model.headers = queryResult.headers;
    model.rows = queryResult.rows;
    model.errorText = queryResult.errorText;
    return model;
}

} // namespace CurrentOrdersViewModelBuilder
