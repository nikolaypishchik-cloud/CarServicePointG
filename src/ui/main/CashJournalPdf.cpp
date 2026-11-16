#include "CashOperationsWidget.h"
#include "core/services/orders/CashReport.h"
#include "core/finance/ExactMoney.h"
#include "core/services/AccessControlService.h"
#include "core/services/orders/ClientReceiptReader.h"
#include <QPdfWriter>
#include <QSaveFile>
#include <QTextDocument>

static QString saveCashPdfHtml(const QString& path,const QString& title,const QString& html);

QString writeCashJournalPdf(const QString& path,QDate first,QDate last,const QString& search,
                            const QString& kind,const QString& method)
{
    if (!AccessControlService::currentUserHasPermission("cash.export"))
        return QObject::tr("Немає права експорту каси.");
    const auto result = CashOperations::exportSelection(first,last,search,kind,method);
    if (!result.error.isEmpty()) return result.error;
    QString html = "<h1>Журнал касових операцій</h1>";
    html += QObject::tr("<p>Період %1 — %2 · Операція %3 · Спосіб %4 · Пошук %5</p>")
        .arg(first.toString(Qt::ISODate), last.toString(Qt::ISODate),
             kind.isEmpty() ? QObject::tr("усі") : CashOperations::title(kind).toHtmlEscaped(),
             method.isEmpty() ? QObject::tr("усі") : CashOperations::methodTitle(method).toHtmlEscaped(),
             search.isEmpty() ? QObject::tr("без обмеження") : search.toHtmlEscaped());
    html += "<table width='100%' cellspacing='0' cellpadding='3' border='1'><thead><tr>"
            "<th>Дата</th><th>Клієнт / наряд</th><th>Операція</th><th>Спосіб</th><th>Сума документа</th>"
            "<th>Рух коштів клієнта</th><th>Автор</th></tr></thead>";
    qint64 total = 0;
    for (const auto& row : result.rows) {
        const auto sum = ExactMoney::sum({total,row.net});
        if (auto value = std::get_if<ExactMoney::Cents>(&sum)) total = *value;
        else return QObject::tr("Переповнення підсумку.");
        const QString order = row.number.isEmpty() ? row.order : "№"+row.number;
        const QStringList values{row.day,(row.name+" "+order).trimmed(),CashOperations::title(row.kind),
            CashOperations::methodTitle(row.method),ClientReceiptReader::money(row.amount),
            ClientReceiptReader::money(row.net),row.actor};
        html += "<tr>";
        for (const auto& text : values) html += "<td>"+text.toHtmlEscaped()+"</td>";
        html += "</tr>";
    }
    html += QObject::tr("</table><p>Кількість операцій %1 · Чистий рух коштів клієнта %2</p>")
        .arg(result.rows.size()).arg(ClientReceiptReader::money(total));
    return saveCashPdfHtml(path,QObject::tr("Журнал касових операцій"),html);
}
QString writeCashReportPdf(const QString& path,const CashReport::Table& report)
{
    if(!report.error.isEmpty())return report.error;
    QString html="<h1>"+report.title.toHtmlEscaped()+"</h1><p>"+report.scope.toHtmlEscaped()+"</p>"
        "<table width='100%' border='1' cellpadding='3' cellspacing='0'><thead><tr>";
    for(const auto& name:report.columns)html+="<th>"+name.toHtmlEscaped()+"</th>";
    html+="</tr></thead>";
    for(const auto& row:report.rows){
        html+="<tr>";
        for(auto text:row){
            // Give long stable identifiers soft wrap opportunities in narrow PDF cells.
            if(!text.contains(' ') && text.size()>20)for(int i=text.size()-8;i>0;i-=8)text.insert(i,QChar(0x200b));
            html+="<td>"+text.toHtmlEscaped()+"</td>";
        }
        html+="</tr>";
    }
    html+="</table><p>"+QObject::tr("Рядків %1").arg(report.rows.size())+" · "+report.totals.join(" · ").toHtmlEscaped()+"</p>";
    return saveCashPdfHtml(path,report.title,html);
}
static QString saveCashPdfHtml(const QString& path,const QString& title,const QString& html)
{
    if (!AccessControlService::currentUserHasPermission("cash.view") || !AccessControlService::currentUserHasPermission("cash.export"))
        return QObject::tr("Немає права експорту каси.");
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) return output.errorString();
    {
        QPdfWriter pdf(&output);
        if (!pdf.setPageLayout(QPageLayout(QPageSize(QPageSize::A4),QPageLayout::Landscape,
                                           QMarginsF(12,12,12,12),QPageLayout::Millimeter)))
            return QObject::tr("Не вдалося встановити формат PDF.");
        pdf.setTitle(title);
        QTextDocument document;
        document.setDefaultFont(QFont("Arial",10));
        document.setHtml(html);
        document.print(&pdf);
    }
    if (!output.size() || !output.commit()) return QObject::tr("Не вдалося зберегти PDF. %1").arg(output.errorString());
    return {};
}
