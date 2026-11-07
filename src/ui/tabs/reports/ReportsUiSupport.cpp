#include "ui/tabs/reports/ReportsUiSupport.h"

#include "core/util/FormatUtils.h"

#include <QLineEdit>
#include <QtMath>

namespace ReportsUiSupport {

QString money0(double value)
{
    return FormatUtils::money(value, 0);
}

double parseUaNumber(const QString& input)
{
    QString s = input.trimmed();
    if (s.isEmpty()) return 0.0;
    s.remove(' ');
    s.replace(',', '.');
    bool ok = false;
    const double v = s.toDouble(&ok);
    return ok ? v : 0.0;
}

QLineEdit* makeWordField(const QString& placeholder)
{
    auto* e = new QLineEdit;
    e->setPlaceholderText(placeholder);
    e->setClearButtonEnabled(false);
    e->setMinimumHeight(34);
    e->setStyleSheet(QStringLiteral(
        "QLineEdit { background: #ffffff; color: #111111; border: 1px solid #c7ccd4; border-radius: 6px; padding: 6px 10px; selection-background-color: #dfe6ef; selection-color: #111111; }"
        "QLineEdit:read-only { background: #f3f5f8; color: #111111; }"
        "QLineEdit::placeholder { color: #6d7682; }"));
    return e;
}

int widgetMmToPx(const QWidget* w, double mm)
{
    if (!w) return qRound(mm * 96.0 / 25.4);
    return qRound(mm * w->logicalDpiX() / 25.4);
}

} // namespace ReportsUiSupport
