#include "ui/tabs/settings/ToggleSwitch.h"
#include "ui/theme/ThemeManager.h"

#include <QPainter>
#include <QPainterPath>

ToggleSwitch::ToggleSwitch(QWidget* parent)
    : QAbstractButton(parent)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(52, 28);
    setFocusPolicy(Qt::NoFocus);
    connect(this, &QAbstractButton::toggled, this, [this](bool) { update(); });
}

QSize ToggleSwitch::sizeHint() const
{
    return {52, 28};
}

void ToggleSwitch::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const bool on   = isChecked();
    const bool dark = ThemeManager::currentTheme() == QStringLiteral("graphite");

    // ── Track (pill background) ──────────────────────────────────────────────
    const QColor trackOn  = dark ? QColor(0x34, 0xC7, 0x59)   // system green (dark)
                                 : QColor(0x34, 0xC7, 0x59);  // system green (light)
    const QColor trackOff = dark ? QColor(0x3A, 0x3A, 0x3C)   // dark grey
                                 : QColor(0xE5, 0xE5, 0xEA);  // light grey

    const QRectF track(0, 0, width(), height());
    QPainterPath trackPath;
    trackPath.addRoundedRect(track, height() / 2.0, height() / 2.0);
    p.fillPath(trackPath, on ? trackOn : trackOff);

    // ── Knob (white circle) ──────────────────────────────────────────────────
    const int margin = 3;
    const int knobD  = height() - 2 * margin;
    const int knobX  = on ? (width() - margin - knobD) : margin;

    p.setPen(Qt::NoPen);
    p.setBrush(Qt::white);

    // Subtle drop shadow under the knob
    p.save();
    p.setBrush(QColor(0, 0, 0, 40));
    p.drawEllipse(QRectF(knobX + 0.5, margin + 1.5, knobD, knobD));
    p.restore();

    p.drawEllipse(QRectF(knobX, margin, knobD, knobD));
}
