#pragma once

#include <QString>

class QLabel;
class QPushButton;
class QWidget;

namespace SettingsFinanceSectionUiSupport {

bool isDarkUi();
QString cPrimary();
QString cSecondary();
QString cPanelBorder();
QString cSelBg();
QString panelBg();
QString titleCaseWords(QString s);
QString normalizeOilGrade(const QString& value);
void styleRaisedButton(QPushButton* button, bool primary = false);
void styleGlossyPillButton(QPushButton* button, bool primary = false);
void styleFormLabel(QLabel* label);
QPushButton* makeSectionBackButton(QWidget* parent);

} // namespace SettingsFinanceSectionUiSupport
