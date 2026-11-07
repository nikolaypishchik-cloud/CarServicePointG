#include "core/readmodel/history/HistoryViewModelBuilder.h"

namespace HistoryViewModelBuilder {

TabularReadModel build(const HistoryQueryResult& queryResult)
{
    TabularReadModel model;
    model.headers = queryResult.headers;
    model.rows = queryResult.rows;
    model.errorText = queryResult.errorText;
    return model;
}

} // namespace HistoryViewModelBuilder
