#pragma once

#include <QString>

class QWidget;
class QLineEdit;

namespace ReportsUiSupport {

QString money0(double value);
double parseUaNumber(const QString& input);
QLineEdit* makeWordField(const QString& placeholder = QString());
int widgetMmToPx(const QWidget* w, double mm);

} // namespace ReportsUiSupport
