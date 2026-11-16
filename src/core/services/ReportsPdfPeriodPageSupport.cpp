#include "core/services/ReportsPdfPeriodPageSupport.h"

#include "core/services/ReportsPdfLayoutSupport.h"
#include "core/services/ReportsPdfPaintingSupport.h"
#include "core/util/FormatUtils.h"

#include <QPainter>
#include <QPdfWriter>
#include <QPen>
#include <numeric>
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>

namespace {

QString moneyFromCents(qint64 value)
{
    return FormatUtils::money(value/100.0, 2);
}

} // namespace

namespace ReportsPdfPeriodPageSupport {

void renderSummary(QPdfWriter& pdf,QPainter& painter,const ReportsPdfLayoutSupport::PdfTheme& theme,
                   QDate first,QDate last,const ReportsPdfSupport::PeriodSummary& summary)
{
    using ReportsPdfSupport::PeriodAmount;
    const auto dates=[](QDate a,QDate b){return a.toString("dd.MM.yyyy")+" – "+b.toString("dd.MM.yyyy");};
    const auto money=[](qint64 cents){return FormatUtils::money(cents/100.0,2);};
    const auto columns=[&](const PeriodAmount& value){
        return QStringLiteral("<td>%1</td><td>%2</td><td>%3</td><td>%4</td>")
            .arg(value.count).arg(money(value.works),money(value.parts),money(value.total()));
    };
    const auto table=[](QString label,bool change=false){
        return QStringLiteral("<table width='100%' cellspacing='0' cellpadding='4' border='1'><thead><tr>"
            "<th>%1</th><th>Нарядів</th><th>Роботи, грн</th><th>Запчастини, грн</th><th>Разом, грн</th>%2</tr></thead>")
            .arg(label,change?QStringLiteral("<th>Зміна</th>"):QString());
    };
    QMap<QDate,PeriodAmount> weeks;
    PeriodAmount all;
    for(auto it=summary.days.lowerBound(first);it!=summary.days.end() && it.key()<=last;++it) {
        weeks[it.key().addDays(1-it.key().dayOfWeek())].add(it.value());
        all.add(it.value());
    }
    const auto monday=first.addDays(1-first.dayOfWeek());
    if (weeks.isEmpty() && last<=monday.addDays(6)) weeks.insert(monday,PeriodAmount{});
    QString html=QStringLiteral("<h1>Зведення за період</h1><p>%1<br>Усі наряди за обрані дати. Фільтри екрана не застосовано.</p>").arg(dates(first,last));
    html+="<h2>За тижнями</h2>"+table("Період");
    for(auto it=weeks.cbegin();it!=weeks.cend();++it)
        html+="<tr><td>"+dates(qMax(first,it.key()),qMin(last,it.key().addDays(6)))+"</td>"+columns(it.value())+"</tr>";
    html+="<tr><td>Усього за період</td>"+columns(all)+"</tr></table><h2>За майстрами</h2>"+table("Майстер");
    for(auto it=summary.masters.cbegin();it!=summary.masters.cend();++it) {
        auto name=it.key().first;
        if(name.isEmpty()) name=QStringLiteral("Майстра не вказано");
        if(!it.key().second.isEmpty()) name+=" (№"+it.key().second+")";
        html+="<tr><td>"+name.toHtmlEscaped()+"</td>"+columns(it.value())+"</tr>";
    }
    html+="</table>";
    if(!all.count) html+=QStringLiteral("<p>За обраний період нарядів немає.</p>");
    const auto today=QDate::currentDate();
    const auto interval=[&](QDate from,QDate to){
        PeriodAmount value;
        for(auto it=summary.days.lowerBound(from);it!=summary.days.end() && it.key()<=to;++it) value.add(it.value());
        return value;
    };
    for(auto it=weeks.cbegin();it!=weeks.cend();++it) {
        const auto from=qMax(first,it.key());
        const auto to=qMin(qMin(last,it.key().addDays(6)),today);
        if(from>to) continue;
        const auto current=interval(from,to);
        html+="<h2>Порівняння за "+dates(from,to)+"</h2>"+table("Попередній період",true);
        for(int week=1;week<=4;++week) {
            const auto a=from.addDays(-7*week), b=to.addDays(-7*week);
            const auto prior=interval(a,b);
            QString change;
            if (!prior.count) change=QStringLiteral("Немає нарядів");
            else if (!prior.total()) change=QStringLiteral("Нульова база");
            else change=FormatUtils::money(100.0*(current.total()-prior.total())/prior.total(),1)+"%";
            html+="<tr><td>"+dates(a,b)+"</td>"+columns(prior)+"<td>"+change+"</td></tr>";
        }
        html+="</table>";
    }
    html+=QStringLiteral("<p>Деталізація нарядів — на наступних сторінках.</p>");
    QTextDocument document;
    document.documentLayout()->setPaintDevice(&pdf);
    document.setDefaultFont(QFont("Arial",10));
    const QSizeF area(theme.contentWidth,theme.bottom-theme.top);
    document.setPageSize(area);
    document.setHtml(html);
    const int pages=document.pageCount();
    for(int i=0;i<pages;++i) {
        if(i) pdf.newPage();
        painter.save();
        painter.translate(theme.left,theme.top-i*area.height());
        const QRectF clip(0,i*area.height(),area.width(),area.height());
        painter.setClipRect(clip);
        document.drawContents(&painter,clip);
        painter.restore();
    }
}

void renderPeriodPage(
    QPdfWriter& pdf,
    QPainter& painter,
    const ReportsPdfLayoutSupport::PdfTheme& theme,
    const QString& title,
    const QString& subtitle,
    const QList<QStringList>& formattedRowCells,
    const ReportsPdfPeriodSupport::PeriodVisibleTotals& visibleTotals,
    const QFont& titleFont,
    const QFont& subtitleFont,
    const QFont& headerFontDefault,
    const QFont& headerFontWide,
    const QFont& tableFontDefault,
    const QFont& tableFontWide,
    const QFont& tableFontNarrow,
    const QFont& summaryFont,
    const QFont& summaryBoldFont,
    const QFont& totalMarginFont)
{
    using ReportsPdfLayoutSupport::mmToPx;
    using ReportsPdfLayoutSupport::newPdfPage;
    using ReportsPdfPaintingSupport::drawCell;
    using ReportsPdfPaintingSupport::drawCenteredText;
    using ReportsPdfPaintingSupport::drawLeftText;
    using ReportsPdfPaintingSupport::drawRightText;
    using ReportsPdfPaintingSupport::kPdfTablePenWidth;
    using ReportsPdfPaintingSupport::wrappedHeight;

    const int titleTopGap = mmToPx(&pdf, 1.5);
    const int titleH = mmToPx(&pdf, 15.0);
    const int subtitleH = mmToPx(&pdf, 8.5);
    const int titleToSubtitleGap = mmToPx(&pdf, 0.6);
    const int subtitleToTableGap = mmToPx(&pdf, 7.0);
    const int decorativeInset = mmToPx(&pdf, 1.0);
    const int titleUnderlineGap = mmToPx(&pdf, 1.0);
    const int headerRowH = mmToPx(&pdf, 11.0);
    const int summaryRowH = mmToPx(&pdf, 6.0);
    const int summaryGapAfterTable = mmToPx(&pdf, 5.0);
    const int summaryBlockH = summaryGapAfterTable + summaryRowH * 6 + mmToPx(&pdf, 5.0) + mmToPx(&pdf, 8.0);
    const int carryRows = 2;

    const QVector<qreal> columnWeights = {
        16.0, 11.0, 37.0, 19.0,
        50.0, 54.0, 10.0, 19.0,
        16.0, 19.0, 22.0
    };
    QVector<int> w;
    w.reserve(columnWeights.size());
    const qreal weightSum = std::accumulate(columnWeights.begin(), columnWeights.end(), 0.0);
    int usedWidth = 0;
    for (int i = 0; i < columnWeights.size(); ++i) {
        const int cw = (i == columnWeights.size() - 1)
            ? qMax(1, theme.contentWidth - usedWidth)
            : qMax(1, qRound(theme.contentWidth * columnWeights[i] / weightSum));
        w << cw;
        usedWidth += cw;
    }
    const int tableX = theme.left;
    const QStringList headers = ReportsPdfPeriodSupport::periodHeaders();

    auto headerFontForColumn = [&](int index) -> const QFont& {
        if (index == 4 || index == 5) return headerFontWide;
        return headerFontDefault;
    };
    auto tableFontForColumn = [&](int index) -> const QFont& {
        if (index == 2 || index == 3) return tableFontNarrow;
        if (index == 4 || index == 5) return tableFontWide;
        return tableFontDefault;
    };

    auto drawPageIntroAndHeader = [&](int& y) {
        y = theme.top + titleTopGap;
        drawCenteredText(painter, QRect(theme.left, y, theme.contentWidth, titleH), title, titleFont);
        painter.save();
        painter.setPen(QPen(Qt::black, kPdfTablePenWidth));
        const int decorativeLineY = y + titleH + titleUnderlineGap;
        painter.drawLine(theme.left + decorativeInset, decorativeLineY, theme.right - decorativeInset, decorativeLineY);
        painter.restore();
        y += titleH + titleToSubtitleGap;
        drawCenteredText(painter, QRect(theme.left, y, theme.contentWidth, subtitleH), subtitle, subtitleFont);
        y += subtitleH;
        drawCenteredText(painter, QRect(theme.left, y, theme.contentWidth, subtitleToTableGap),
                         QStringLiteral("Усі наряди за обрані дати. Фільтри екрана не застосовано."), summaryFont);
        y += subtitleToTableGap;

        int xx = tableX;
        for (int i = 0; i < headers.size(); ++i) {
            drawCell(painter, QRect(xx, y, w[i], headerRowH), headers[i], headerFontForColumn(i),
                     Qt::AlignLeft | Qt::AlignTop, QColor(220, 220, 220), kPdfTablePenWidth);
            xx += w[i];
        }
        y += headerRowH;
    };

    int y = 0;
    drawPageIntroAndHeader(y);

    int rowsLeft = formattedRowCells.size();
    int rowIndex = 0;
    for (const auto& cells : formattedRowCells) {
        int rowH = mmToPx(&pdf, 8.5);
        for (int i = 0; i < cells.size(); ++i) {
            rowH = qMax(rowH, wrappedHeight(painter, cells[i], tableFontForColumn(i), w[i], mmToPx(&pdf, 7.0)));
        }

        int carryHeight = rowH;
        if (rowIndex + 1 < formattedRowCells.size()) {
            const auto& nextCells = formattedRowCells[rowIndex + 1];
            int nextH = mmToPx(&pdf, 8.5);
            for (int i = 0; i < nextCells.size(); ++i) {
                nextH = qMax(nextH, wrappedHeight(painter, nextCells[i], tableFontForColumn(i), w[i], mmToPx(&pdf, 7.0)));
            }
            carryHeight += nextH;
        }

        if (y + rowH > theme.bottom || (rowsLeft > carryRows && rowsLeft <= carryRows + 2 && y + carryHeight + summaryBlockH > theme.bottom && y > theme.top + headerRowH + mmToPx(&pdf, 8.0))) {
            newPdfPage(pdf, painter, y, theme);
            drawPageIntroAndHeader(y);
        }

        int xx = tableX;
        for (int i = 0; i < cells.size(); ++i) {
            const Qt::Alignment align = (i >= 7) ? (Qt::AlignRight | Qt::AlignTop) : (Qt::AlignLeft | Qt::AlignTop);
            drawCell(painter, QRect(xx, y, w[i], rowH), cells[i], tableFontForColumn(i), align, Qt::white, kPdfTablePenWidth);
            xx += w[i];
        }
        y += rowH;
        --rowsLeft;
        ++rowIndex;
    }

    y += summaryGapAfterTable;
    const int summaryX = tableX;
    const int labelW = mmToPx(&pdf, 52.0);
    const int valueX = summaryX + mmToPx(&pdf, 58.0);
    const int valueW = mmToPx(&pdf, 18.0);
    const int currencyGap = mmToPx(&pdf, 2.0);
    const int currencyX = valueX + valueW + currencyGap;
    const int currencyW = mmToPx(&pdf, 12.0);

    for (const auto& line : ReportsPdfPeriodSupport::buildPeriodSummaryLines(visibleTotals)) {
        const QFont& lf = line.bold ? summaryBoldFont : summaryFont;
        const QFont& vf = line.bold ? summaryBoldFont : summaryFont;
        drawLeftText(painter, QRect(summaryX, y, labelW, summaryRowH), line.label, lf);
        drawRightText(painter, QRect(valueX, y, valueW, summaryRowH), moneyFromCents(line.value), vf);
        drawLeftText(painter, QRect(currencyX, y, currencyW, summaryRowH), QString::fromUtf8("грн"), vf);
        y += summaryRowH;
    }

    y += mmToPx(&pdf, 2.5);
    painter.save();
    painter.setPen(QPen(Qt::black, kPdfTablePenWidth));
    painter.drawLine(summaryX, y, summaryX + mmToPx(&pdf, 43.0), y);
    painter.drawLine(summaryX + mmToPx(&pdf, 58.0), y, summaryX + mmToPx(&pdf, 86.0), y);
    painter.restore();
    y += mmToPx(&pdf, 3.5);

    drawLeftText(painter, QRect(summaryX, y, mmToPx(&pdf, 70.0), mmToPx(&pdf, 8.0)), QString::fromUtf8("Загальна маржа"), totalMarginFont);
    drawRightText(painter, QRect(valueX, y, valueW, mmToPx(&pdf, 8.0)), moneyFromCents(visibleTotals.totalMargin), totalMarginFont);
    drawLeftText(painter, QRect(currencyX, y, currencyW, mmToPx(&pdf, 8.0)), QString::fromUtf8("грн"), totalMarginFont);
}

} // namespace ReportsPdfPeriodPageSupport
