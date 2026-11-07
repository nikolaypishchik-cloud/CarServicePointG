#pragma once
#include <QString>
class QWidget;
namespace OrderClosureFlow {
bool closeOrder(QWidget* parent,const QString& orderUid);
void recoverPending(QWidget* parent);
}
