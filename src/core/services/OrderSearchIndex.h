#pragma once
#include <QSqlDatabase>
#include <QString>

namespace OrderSearchIndex {
bool ensure(QSqlDatabase db, QString* error = nullptr);
bool available(QSqlDatabase db);
}
