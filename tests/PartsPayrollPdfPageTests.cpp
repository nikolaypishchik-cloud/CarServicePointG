#include "core/services/ReportsPdfPartsPageSupport.h"
#include "core/services/ReportsPdfPayrollPageSupport.h"
#include "core/util/FormatUtils.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QPdfWriter>
#include <QPainter>
#include <QProcess>
#include <QDebug>
int main(int argc,char** argv){QGuiApplication app(argc,argv);QTemporaryDir dir;QFont font("DejaVu Sans",8);const QDate from(2026,1,1),to(2026,1,31);
 const auto parts=dir.filePath("parts.pdf"),payroll=dir.filePath("payroll.pdf");
 {QPdfWriter pdf(parts);pdf.setResolution(144);QPainter p(&pdf);auto theme=ReportsPdfLayoutSupport::buildTheme(pdf);ReportsPdfSupport::PartPdfRow row;row.name="Synthetic part";row.orderNumber="7";row.qty="1";row.qtyValue=1;row.purchase=19.35;row.purchaseUnitPrice=19.35;row.added=12.60;row.clientPrice=31.95;
 ReportsPdfPartsPageSupport::renderPartsPages(pdf,p,theme,from,to,{{"Shop",{row}}},false,font,font,font,font,font,font,font,font,font,font);p.end();}
 {QPdfWriter pdf(payroll);pdf.setResolution(144);QPainter p(&pdf);auto theme=ReportsPdfLayoutSupport::buildTheme(pdf);ReportsPdfSupport::PayrollMasterPdf master;master.masterName="Synthetic master";master.masterId="m";master.accrued=12.25;master.toIssue=12.25;master.ordersCount=1;PayrollOrderRow row;row.masterAmount=12.25;row.orderNumber="7";row.works="Synthetic work";master.orders<<row;
 ReportsPdfPayrollPageSupport::PayrollFonts fonts{font,font,font,font,font,font,font,font,font,font,font,font,font};
 ReportsPdfPayrollPageSupport::renderPayrollPages(pdf,p,theme,from,to,{master},{},{},{},{},1,fonts);p.end();}
 bool ok=true;for(const auto& path:{parts,payroll}){QProcess text;text.start("pdftotext",{path,"-"});if(!text.waitForFinished() || text.exitCode()!=0)return 2;const auto output=QString::fromUtf8(text.readAllStandardOutput());for(double amount:path==parts?QList<double>{19.35,12.60,31.95}:QList<double>{12.25})if(!output.contains(FormatUtils::money(amount,2))){qCritical()<<"FAIL PDF cents"<<amount;ok=false;}}
 if(!ok)return 1;qInfo()<<"PASS actual parts/payroll PDF text retains19.35/12.60/31.95 and12.25";
}
