#pragma once

#include <QString>

class QSqlDatabase;

namespace OrderStatusService {

bool promoteToInProgressIfNeeded(QSqlDatabase db, int orderNumber, QString* errorText = nullptr);
bool promoteToInProgressIfNeeded(int orderNumber, QString* errorText = nullptr);
bool promoteToInProgressIfNeeded(const QString& connectionName, int orderNumber, QString* errorText = nullptr);
bool setClosed(QSqlDatabase db, int orderNumber, bool closed, QString* errorText = nullptr);

} // namespace OrderStatusService
