#pragma once

#include <QString>

class QWidget;
class QPushButton;

namespace SettingsDetailUiSupport {

QString standardButtonStyle(bool compactBackButton = false);
QString standardInputStyle();
QPushButton* makeBackButton(QWidget* parent);
QWidget* wrapDetailWithBack(QWidget* inner);
void normalizeDetailContrast(QWidget* root);
void normalizeDetailButtons(QWidget* root);

} // namespace SettingsDetailUiSupport
