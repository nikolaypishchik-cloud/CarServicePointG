#pragma once
#include <QDate>
#include <QString>
namespace CashOpeningService {
enum class Action { Initial, Count, Adjustment };
struct Command { QString uid; Action action; QDate start; qint64 counted; QString reason; };
struct Result { QString message; bool recorded=false; bool opened=false; };
Result apply(const Command& command);
}
