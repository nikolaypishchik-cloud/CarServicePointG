#include "core/readmodel/history/HistoryViewModelBuilder.h"

namespace HistoryViewModelBuilder {

TabularReadModel build(const HistoryQueryResult& queryResult, bool showOrderYear)
{
    TabularReadModel model;
    model.headers = queryResult.headers;
    model.rows = queryResult.rows;
    model.errorText = queryResult.errorText;
    const int yearColumn = model.headers.indexOf(QStringLiteral("__order_year"));
    if (showOrderYear && yearColumn >= 0) {
        model.headers.append(QString::fromUtf8("Рік"));
        for (auto& row : model.rows) {
            const QVariant orderYear = row.value(yearColumn);
            row.append(orderYear);
        }
    }
    return model;
}

} // namespace HistoryViewModelBuilder
