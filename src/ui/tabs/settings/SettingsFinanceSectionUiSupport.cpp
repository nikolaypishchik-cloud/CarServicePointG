#include "ui/tabs/settings/SettingsFinanceSectionUiSupport.h"

#include "ui/theme/ThemeManager.h"

#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>

namespace SettingsFinanceSectionUiSupport {

bool isDarkUi()
{
    return ThemeManager::currentTheme() == QStringLiteral("graphite");
}

QString cPrimary()
{
    return isDarkUi() ? QStringLiteral("#f7fbff") : QStringLiteral("#111111");
}

QString cSecondary()
{
    return isDarkUi() ? QStringLiteral("#c7d0db") : QStringLiteral("#4b5563");
}

QString cPanelBorder()
{
    return isDarkUi() ? QStringLiteral("rgba(222,234,247,0.28)") : QStringLiteral("#3a3a3a");
}

QString cSelBg()
{
    return isDarkUi() ? QStringLiteral("rgba(223,233,245,0.20)") : QStringLiteral("rgba(0,0,0,0.06)");
}

QString panelBg()
{
    return isDarkUi() ? QStringLiteral("#2f3944") : QStringLiteral("#e9eef5");
}

QString titleCaseWords(QString s)
{
    s = s.trimmed().toLower();
    bool makeUpper = true;
    for (int i = 0; i < s.size(); ++i) {
        if (s[i].isSpace() || s[i] == '-' || s[i] == '/' || s[i] == '(') {
            makeUpper = true;
            continue;
        }
        if (makeUpper && s[i].isLetter()) {
            s[i] = s[i].toUpper();
            makeUpper = false;
        }
    }
    return s;
}

QString normalizeOilGrade(const QString& value)
{
    QString s = value.trimmed().toUpper();
    s.remove(' ');
    return s;
}

void styleRaisedButton(QPushButton* button, bool primary)
{
    button->setCursor(Qt::PointingHandCursor);
    button->setMinimumHeight(52);
    button->setFixedHeight(52);
    auto policy = button->sizePolicy();
    policy.setVerticalPolicy(QSizePolicy::Fixed);
    button->setSizePolicy(policy);
    button->setStyleSheet(QString::fromUtf8(
        "QPushButton{" 
        "color:%1;" 
        "font-weight:800;" 
        "font-size:11pt;" 
        "min-height:38px;" 
        "padding:0 18px;" 
        "border-radius:18px;" 
        "background:qlineargradient(x1:0,y1:0,x2:0,y2:1," 
        " stop:0 rgba(107,118,131,1.0)," 
        " stop:0.10 rgba(147,157,169,1.0)," 
        " stop:0.47 rgba(35,46,60,1.0)," 
        " stop:0.52 rgba(8,16,30,1.0)," 
        " stop:1 rgba(6,14,28,1.0));" 
        "border:1px solid rgba(255,255,255,0.98);" 
        "border-top:1px solid rgba(255,255,255,1.0);" 
        "}" 
        "QPushButton:hover{" 
        "background:qlineargradient(x1:0,y1:0,x2:0,y2:1," 
        " stop:0 rgba(124,134,146,1.0)," 
        " stop:0.10 rgba(168,177,188,1.0)," 
        " stop:0.47 rgba(41,54,69,1.0)," 
        " stop:0.52 rgba(12,21,36,1.0)," 
        " stop:1 rgba(6,14,28,1.0));" 
        "border:1px solid rgba(255,255,255,0.98);" 
        "border-top:1px solid rgba(255,255,255,1.0);" 
        "}" 
        "QPushButton:pressed{" 
        "padding-top:1px;" 
        "background:qlineargradient(x1:0,y1:0,x2:0,y2:1," 
        " stop:0 rgba(40,52,68,1.0)," 
        " stop:0.40 rgba(18,29,43,1.0)," 
        " stop:1 rgba(6,14,28,1.0));" 
        "border:1px solid rgba(255,255,255,0.74);" 
        "}" 
        "QPushButton:disabled{" 
        "background:rgba(112,122,136,0.42);" 
        "color:rgba(248,251,255,0.55);" 
        "border:1px solid rgba(255,255,255,0.24);" 
        "}"
    ).arg(primary ? QStringLiteral("#ffffff") : QStringLiteral("#f8fbff")));
}

void styleGlossyPillButton(QPushButton* button, bool primary)
{
    styleRaisedButton(button, primary);
}

void styleFormLabel(QLabel* label)
{
    label->setStyleSheet(QString("font-size:11pt; font-weight:700; color:%1;").arg(cPrimary()));
}

QPushButton* makeSectionBackButton(QWidget* parent)
{
    auto* button = new QPushButton(QString::fromUtf8("← Назад"), parent);
    button->setCursor(Qt::PointingHandCursor);
    styleRaisedButton(button, false);
    button->setFixedSize(120, 40);
    return button;
}

} // namespace SettingsFinanceSectionUiSupport
