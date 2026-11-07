#pragma once
#include <QString>
#include <functional>
class QWidget;
void openElitInvoicePdf(QWidget* owner, const QString& identity,
                        std::function<void(const QString&)> preview = {});
