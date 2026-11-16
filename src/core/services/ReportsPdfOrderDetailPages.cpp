#include "core/services/ReportsPdfPeriodPageSupport.h"
#include "core/util/FormatUtils.h"
#include "AppVersion.h"
#include <QAbstractTextDocumentLayout>
#include <QPainter>
#include <QPdfWriter>
#include <QTextDocument>
#include <QTextTable>

namespace {
using ReportsPdfSupport::PeriodPdfRow;

QString money(qint64 cents)
{
    return FormatUtils::money(cents / 100.0, 2);
}

QString table(const QStringList& headers, const QList<QStringList>& lines)
{
    QString result = "<table width='100%' cellpadding='3' cellspacing='0' border='1'><thead><tr>";
    for (const auto& header : headers) result += "<th>" + header + "</th>";
    result += "</tr></thead>";
    for (const auto& line : lines) {
        result += "<tr>";
        for (const auto& cell : line)
            result += "<td>" + (cell.isEmpty() ? QStringLiteral("—") : cell.toHtmlEscaped()) + "</td>";
        result += "</tr>";
    }
    return result + "</table>";
}

struct Section {
    QList<QStringList> lines;
    QList<qint64> amounts;
    qint64 total = 0;

    void add(QStringList line, double amount)
    {
        const auto cents = qRound64(amount * 100);
        line.prepend(QString::number(lines.size() + 1));
        line << money(cents);
        lines.append(line);
        amounts.append(cents);
        total += cents;
    }
};
}

int ReportsPdfPeriodPageSupport::renderOrderDetails(QPdfWriter& pdf, QPainter& painter,
    const ReportsPdfLayoutSupport::PdfTheme& theme, int pageNumber, const QList<PeriodPdfRow>& rows)
{
    qsizetype next = 0;
    while (next < rows.size()) {
        const auto& order = rows[next];
        Section works, parts;
        do {
            const auto& row = rows[next++];
            if (row.hasWork)
                works.add({row.workTitle, row.workQuantity, row.workUnitPrice}, row.worksPrice);
            if (row.hasPart) {
                const auto unitPrice = row.partQuantity > 0 && qIsFinite(row.partQuantity) && qIsFinite(row.partsPrice)
                    ? FormatUtils::money(row.partsPrice / row.partQuantity, 2) : QString();
                parts.add({row.partName, row.qty, unitPrice, FormatUtils::money(row.added, 2)}, row.partsPrice);
            }
        } while (next < rows.size() && rows[next].orderUid == order.orderUid);

        QString html = QStringLiteral("<h2>Роботи</h2>");
        html += table({"№", "Робота", "Кількість", "Ціна за одиницю, грн", "Сума, грн"}, works.lines);
        html += "<p>Разом роботи: " + money(works.total) + " грн</p><h2>Запчастини</h2>";
        html += table({"№", "Запчастина", "Кількість", "Ціна за одиницю, грн", "Доданий %", "Сума клієнту, грн"}, parts.lines);
        html += "<p>Разом запчастини: " + money(parts.total) + " грн</p><h3>Разом за нарядом: "
            + money(works.total + parts.total) + " грн</h3>";

        html += QStringLiteral("<p>Ціна за одиницю запчастини = сума клієнту ÷ кількість. "
            "«Доданий %» показує націнку в гривнях, яка вже входить до суми клієнту. "
            "Прочерк означає відсутність даних.</p>");

        const int band = ReportsPdfLayoutSupport::mmToPx(&pdf, 12.0);
        const QSizeF area(theme.contentWidth, theme.bottom - theme.top - 4 * band);
        QTextDocument doc;
        doc.documentLayout()->setPaintDevice(&pdf);
        doc.setDefaultFont(QFont("Arial", 10));
        doc.setPageSize(area);
        doc.setDefaultStyleSheet(QStringLiteral("body, p, td, th, h1, h2, h3 { color: #111111; }"));
        doc.setHtml("<body>" + html + "</body>");
        const int count = doc.pageCount();
        QList<qint64> pageAmounts(count, 0);
        const QList<const Section*> sections{&works, &parts};
        int section = 0;
        for (auto* frame : doc.rootFrame()->childFrames()) {
            const auto* body = qobject_cast<QTextTable*>(frame);
            if (!body) continue;
            if (section >= sections.size()) return -1;
            const auto& values = sections[section++]->amounts;
            if (body->rows() != values.size() + 1) return -1;
            for (int r = 1; r < body->rows(); ++r) {
                const auto amountCell = body->cellAt(r, body->columns() - 1);
                const auto bounds = doc.documentLayout()->blockBoundingRect(amountCell.lastCursorPosition().block());
                const int page = qBound(0, int((bounds.bottom() - 0.01) / area.height()), count - 1);
                pageAmounts[page] += values[r - 1];
            }
        }
        if (section != 2) return -1;
        qint64 carried = 0;
        const auto heading = QStringLiteral("Деталізація наряду №%1   ·   %2   ·   %3   ·   %4")
            .arg(order.orderNumber, order.car.isEmpty() ? QStringLiteral("—") : order.car,
                 order.plate.isEmpty() ? QStringLiteral("—") : order.plate, order.date);
        for (int i = 0; i < count; ++i) {
            if (!pdf.newPage()) return -1;
            painter.save();
            painter.setFont(QFont("Arial", 11, QFont::Bold));
            painter.drawText(QRect(theme.left, theme.top, theme.contentWidth, band), Qt::AlignVCenter, QStringLiteral("CarService PointG"));
            painter.setFont(QFont("Arial", 14, QFont::Bold));
            painter.drawText(QRect(theme.left, theme.top + band, theme.contentWidth, band), Qt::AlignVCenter, heading);
            painter.setFont(QFont("Arial", 9));
            if (i) painter.drawText(QRect(theme.left, theme.top + 2 * band, theme.contentWidth, band),
                Qt::AlignVCenter, QStringLiteral("Перенесено з попередньої сторінки: %1 грн").arg(money(carried)));
            painter.save();
            painter.translate(theme.left, theme.top + 3 * band - i * area.height());
            const QRectF clip(0, i * area.height(), area.width(), area.height());
            painter.setClipRect(clip);
            doc.drawContents(&painter, clip);
            painter.restore();
            carried += pageAmounts[i];
            if (i + 1 < count)
                painter.drawText(QRect(theme.left, theme.bottom - band, theme.contentWidth, band),
                    Qt::AlignVCenter, QStringLiteral("До перенесення: %1 грн").arg(money(carried)));
            painter.drawText(QRect(theme.left, theme.bottom, theme.contentWidth, theme.pageHeight - theme.bottom),
                Qt::AlignVCenter | Qt::AlignRight, QStringLiteral("CarService PointG %1   ·   Сторінка %2")
                    .arg(CarService::AppVersion::versionString()).arg(++pageNumber));
            painter.restore();
        }
    }
    return pageNumber;
}
