#pragma once

#include <QColor>
#include <QFont>
#include <QRect>
#include <QString>
#include <Qt>

class QPaintDevice;
class QPainter;

namespace ReportsPdfPaintingSupport {

qreal ptToPx(const QPaintDevice* dev, double pt);
void drawIssueTextWithUnderline(QPainter& p, const QRect& rect, const QString& text, const QFont& font, qreal underlineWidth);
void drawSingleLineFittedCell(QPainter& p, const QRect& rect, const QString& text, const QFont& font,
                              Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter,
                              const QColor& fill = Qt::white, qreal penWidth = 0.6,
                              qreal minPointSize = 5.8, qreal minHorizontalScale = 0.84);
void drawCenteredText(QPainter& p, const QRect& r, const QString& txt, const QFont& f);
void drawLeftText(QPainter& p, const QRect& r, const QString& txt, const QFont& f);
void drawRightText(QPainter& p, const QRect& r, const QString& txt, const QFont& f);
int wrappedHeight(QPainter& p, const QString& text, const QFont& font, int width, int minHeight = 24);
QString wrapCellText(const QString& text, const QFont& font, int width);
inline constexpr qreal kPdfTablePenWidth = 0.6;
void drawCell(QPainter& p, const QRect& rect, const QString& text, const QFont& font,
              Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter,
              const QColor& fill = Qt::white, qreal penWidth = kPdfTablePenWidth);

} // namespace ReportsPdfPaintingSupport
