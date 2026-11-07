#include "core/services/ReportsPdfExpensesPageSupport.h"
#include "core/services/ReportsPdfOutsourcePageSupport.h"
#include "core/services/ReportsPdfRenderer.h"
#include "core/util/FormatUtils.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QPdfWriter>
#include <QPainter>
#include <QProcess>
#include <QDebug>
int main(int argc,char** argv) {
 QGuiApplication app(argc,argv);QTemporaryDir dir;QFont font("DejaVu Sans",9);
 const auto expense=dir.filePath("expenses.pdf"),outsource=dir.filePath("outsource.pdf");
 {QPdfWriter pdf(expense);pdf.setResolution(144);QPainter painter(&pdf);const auto theme=ReportsPdfLayoutSupport::buildTheme(pdf);
 ReportsPdfRenderer::ExpensesFormData data;data.cleaning=12.25;data.other=7.10;data.total=19.35;
 ReportsPdfExpensesPageSupport::renderExpensesPage(pdf,painter,theme,"2026-01-01","2026-01-31",data,font,font,font,font);painter.end();}
 {QPdfWriter pdf(outsource);pdf.setResolution(144);QPainter painter(&pdf);const auto theme=ReportsPdfLayoutSupport::buildTheme(pdf);
 ReportsPdfSupport::OutsourcePdfRow row;row.company="Shop";row.orderNumber="7";row.partName="Synthetic";row.repairCost=19.35;row.clientPrice=31.95;
 ReportsPdfOutsourcePageSupport::renderOutsourcePages(pdf,painter,theme,QDate(2026,1,1),QDate(2026,1,31),{{"Shop",{row}}},1,19.35,12.60,font,font,font,font,font,font,font);painter.end();}
 for(const auto& path:{expense,outsource}){QProcess text;text.start("pdftotext",{path,"-"});if(!text.waitForFinished() || text.exitCode()!=0)return 2;const auto output=QString::fromUtf8(text.readAllStandardOutput());
 const QList<double> amounts=path==expense?QList<double>{12.25,7.10,19.35}:QList<double>{19.35,31.95,12.60};
 for(double amount:amounts)if(!output.contains(FormatUtils::money(amount,2))){qCritical()<<"FAIL actual PDF loses cents"<<amount;return 1;}}
 qInfo()<<"PASS actual expense/outsource PDF text preserves line amounts and totals to cents";
}
