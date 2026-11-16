#include "core/services/ReportsPdfExpensesPageSupport.h"
#include "core/services/ReportsDateUtils.h"
#include "core/util/FormatUtils.h"
#include "AppVersion.h"
#include <QAbstractTextDocumentLayout>
#include <QPainter>
#include <QPdfWriter>
#include <QTextDocument>

bool ReportsPdfExpensesPageSupport::renderExpensesPage(QPdfWriter& pdf, QPainter& painter,
    const ReportsPdfLayoutSupport::PdfTheme& theme, const QString& fromIso, const QString& toIso,
    const ReportsPdfRenderer::ExpensesFormData& data)
{
    const auto money = [](double value) { return FormatUtils::money(value, 2) + QStringLiteral(" грн"); };
    const auto meter = [&](int index, double value) {
        if (data.meterInputs.isEmpty()) return FormatUtils::decimalText(value);
        const auto text = data.meterInputs.value(index);
        return text.isEmpty() ? QStringLiteral("Не вказано") : text;
    };
    QString warning = data.inputWarning;
    if (warning.isEmpty() && data.meterInputs.contains(QString()))
        warning = QStringLiteral("Для розрахунку потрібні обидва показники лічильника та тариф.");
    if (warning.isEmpty() && (data.elecCurr < data.elecPrev || data.elecPrev < 0 || data.elecTariff < 0))
        warning = QStringLiteral("Перевірте порядок показників лічильника й тариф.");
    const QString unknown = QStringLiteral("Не розраховано");
    const auto row = [](const QString& name, const QString& value, const QString& note = {}) {
        return QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td></tr>")
            .arg(name.toHtmlEscaped(), value.toHtmlEscaped(), note.toHtmlEscaped());
    };
    const QString start = QStringLiteral("<table border='1' width='100%' cellspacing='0' cellpadding='4'>"
        "<thead><tr><th>Показник</th><th>Значення</th><th>Примітка</th></tr></thead>");
    QString html = QStringLiteral("<h1>Розрахунок витрат</h1><p>Період: %1 — %2</p><h2>Електроенергія</h2>")
        .arg(ReportsDateUtils::displayDate(fromIso), ReportsDateUtils::displayDate(toIso));
    html += start + row("Лічильник минулий, кВт·год", meter(0, data.elecPrev))
        + row("Лічильник поточний, кВт·год", meter(1, data.elecCurr))
        + row("Тариф, грн/кВт·год", meter(2, data.elecTariff))
        + row("Споживання, кВт·год", warning.isEmpty() ? FormatUtils::decimalText(data.elecKwh) : unknown)
        + row("Вартість електроенергії", warning.isEmpty() ? money(data.elecCost) : unknown) + "</table>";
    if (!warning.isEmpty()) html += "<p>" + warning.toHtmlEscaped() + "</p>";
    html += "<h2>Операційні витрати</h2>" + start;
    const auto amount = [&](double value) { return data.inputWarning.isEmpty() ? money(value) : unknown; };
    html += row("Миючі засоби", amount(data.cleaning), data.cleaningNote)
        + row("Програмне забезпечення", amount(data.software), data.softwareNote)
        + row("Опалення", amount(data.heating), data.heatingNote)
        + row("Амортизація", amount(data.amortization), data.amortizationNote)
        + row("Оренда", amount(data.rent), data.rentNote)
        + row("Інші витрати", amount(data.other), data.otherNote) + "</table>";
    html += "<h2>Підсумок</h2>" + start
        + row("Витрати за формою", warning.isEmpty() ? money(data.total) : unknown, "Включають електроенергію та операційні категорії")
        + row("Додаткові за період", money(data.additionalCents / 100.0), "З окремих записів журналу")
        + row("Усього витрат", warning.isEmpty() ? money((qRound64(data.total * 100) + data.additionalCents) / 100.0) : unknown)
        + "</table>";

    QTextDocument document;
    document.documentLayout()->setPaintDevice(&pdf);
    document.setDefaultFont(QFont("Arial", 10));
    document.setDefaultStyleSheet(QStringLiteral("body, th, td, p, h1, h2 { color: #111111; }"));
    const QSizeF area(theme.contentWidth, theme.bottom - theme.top);
    document.setPageSize(area);
    document.setHtml("<body>" + html + "</body>");
    const int pages = document.pageCount();
    for (int i = 0; i < pages; ++i) {
        if (i && !pdf.newPage()) return false;
        painter.save();
        painter.translate(theme.left, theme.top - i * area.height());
        const QRectF clip(0, i * area.height(), area.width(), area.height());
        painter.setClipRect(clip);
        document.drawContents(&painter, clip);
        painter.restore();
        painter.setFont(QFont("Arial", 8));
        painter.drawText(QRect(theme.left, theme.bottom, theme.contentWidth, theme.pageHeight - theme.bottom),
            Qt::AlignVCenter | Qt::AlignRight, QStringLiteral("CarService PointG %1   ·   Сторінка %2 з %3")
                .arg(CarService::AppVersion::versionString()).arg(i + 1).arg(pages));
    }
    return true;
}
