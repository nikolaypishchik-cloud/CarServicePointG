#include "core/services/DebtAnalysisPdfService.h"
#include <QSaveFile>
#include <QPdfWriter>
#include <QPainter>
#include <QTextDocument>
#include <QTextTable>
#include <QTextBlock>
#include <QAbstractTextDocumentLayout>
#include <QDateTime>

namespace {
QString cellText(QTextTable* table,int row,int column) {
    const auto cell=table->cellAt(row,column);
    auto cursor=cell.firstCursorPosition();
    cursor.setPosition(cell.lastCursorPosition().position(),QTextCursor::KeepAnchor);
    return cursor.selectedText().toHtmlEscaped().replace(QChar::ParagraphSeparator,QStringLiteral("<br>"));
}
QString printableHtml(const QString& sourceHtml) {
    QTextDocument source; source.setHtml(sourceHtml);
    QString html="<html><head><style>body { font-family:'Times New Roman'; font-size:10pt; color:#202a33; }"
        "h2 { font-size:16pt; color:#253746; } h3 { font-size:12pt; color:#253746; }"
        "table { width:100%; } td,th { padding:5px; }"
        "th { color:#202a33; } table { border-color:#b8bec3; }</style></head><body>";
    for(auto it=source.rootFrame()->begin();!it.atEnd();++it) {
        if(auto* table=qobject_cast<QTextTable*>(it.currentFrame())) {
            const int columns=table->columns();
            if(columns>6) {
                for(int row=1;row<table->rows();++row) {
                    if(table->cellAt(row,0).columnSpan()==columns) {
                        html+="<p>"+cellText(table,row,0)+"</p>";
                        continue;
                    }
                    html+="<table width='100%' border='1' cellspacing='0' cellpadding='5'><tr><th width='38%'>"+cellText(table,0,0)
                        +"</th><th width='62%'>"+cellText(table,row,0)+"</th></tr>";
                    for(int column=1;column<columns;++column) {
                        const auto value=cellText(table,row,column).trimmed();
                        if(!value.isEmpty()) html+="<tr><td><b>"+cellText(table,0,column)+"</b></td><td>"+value+"</td></tr>";
                    }
                    html+="</table><p></p>";
                }
            } else {
                html+="<table width='100%' border='1' cellspacing='0' cellpadding='5'>";
                for(int row=0;row<table->rows();++row) {
                    html+="<tr>";
                    const QString tag=row==0?"th":"td";
                    for(int column=0;column<columns;++column)
                        html+="<"+tag+">"+cellText(table,row,column)+"</"+tag+">";
                    html+="</tr>";
                }
                html+="</table>";
            }
        } else {
            const auto block=it.currentBlock();
            if(!block.isValid() || block.text().trimmed().isEmpty()) continue;
            const int heading=block.blockFormat().headingLevel();
            const QString tag=heading>0?QString("h%1").arg(qMin(heading,3)):QString("p");
            html+="<"+tag+">"+block.text().toHtmlEscaped()+"</"+tag+">";
        }
    }
    return html+"</body></html>";
}
}
QString DebtAnalysisPdfService::compactHtml(const QString& html) {
    QTextDocument document; document.setHtml(html);
    // Only flat, unmerged data tables have removable columns. Metric and nested layout tables stay intact.
    for(auto it=document.rootFrame()->begin();!it.atEnd();++it) {
        auto* table=qobject_cast<QTextTable*>(it.currentFrame());
        if(!table || table->rows()<2 || !table->childFrames().isEmpty()) continue;
        bool merged=false;
        for(int r=0;r<table->rows();++r) for(int c=0;c<table->columns();++c)
            merged|=table->cellAt(r,c).columnSpan()>1 || table->cellAt(r,c).rowSpan()>1;
        if(merged) continue;
        for(int c=table->columns()-1;c>0;--c) {
            bool populated=false;
            for(int r=1;r<table->rows();++r) populated|=!cellText(table,r,c).trimmed().isEmpty();
            if(!populated) table->removeColumns(c,1);
        }
    }
    return document.toHtml();
}
bool DebtAnalysisPdfService::save(const QString& path,const QString& analysisHtml,QString* error) {
    if(error) error->clear();
    QSaveFile output(path);
    if(!output.open(QIODevice::WriteOnly)) {
        if(error) *error=QString::fromUtf8("Не вдалося зберегти PDF: %1").arg(output.errorString());
        return false;
    }
    bool success=true;
    {
        QPdfWriter pdf(&output);
        pdf.setPageSize(QPageSize(QPageSize::A4)); pdf.setPageOrientation(QPageLayout::Portrait);
        pdf.setPageMargins(QMarginsF(14,14,14,14)); pdf.setResolution(144);
        pdf.setTitle(QString::fromUtf8("CarService PointG - Аналіз боргів")); pdf.setCreator("CarService PointG");
        QTextDocument document;
        document.documentLayout()->setPaintDevice(&pdf);
        document.setDefaultFont(QFont("Times New Roman",10)); document.setHtml(printableHtml(compactHtml(analysisHtml)));
        const qreal top=90, bottom=40, height=pdf.height()-top-bottom;
        for(auto it=document.rootFrame()->begin();!it.atEnd();++it) {
            if(auto* table=qobject_cast<QTextTable*>(it.currentFrame())) {
                auto format=table->format(); format.setHeaderRowCount(1); table->setFormat(format);
            }
        }
        // Finalize repeating headers before pagination; stale layout can omit intervening text during painting.
        document.setPageSize(QSizeF(pdf.width(),height));
        document.markContentsDirty(0,document.characterCount());
        QPainter painter; success=painter.begin(&pdf);
        const int pages=document.pageCount();
        const QString date=QDateTime::currentDateTime().toString("dd.MM.yy HH:mm");
        for(int page=0;success && page<pages;++page) {
            if(page>0 && !pdf.newPage()) { success=false; break; }
            painter.setPen(QColor("#253746")); painter.setFont(QFont("Times New Roman",18,QFont::Bold));
            painter.drawText(QRectF(12,3,pdf.width()-24,37),Qt::AlignCenter,"CarService PointG");
            painter.setFont(QFont("Times New Roman",10,QFont::Bold));
            painter.drawText(QRectF(12,40,pdf.width()-24,22),Qt::AlignCenter,QString::fromUtf8("Аналіз боргів"));
            painter.setPen(QColor("#aab2b8")); painter.drawLine(QPointF(0,68),QPointF(pdf.width(),68));
            painter.save(); painter.setClipRect(QRectF(0,top,pdf.width(),height));
            painter.translate(0,top-page*height);
            QAbstractTextDocumentLayout::PaintContext context;
            context.clip=QRectF(0,page*height,pdf.width(),height);
            document.documentLayout()->draw(&painter,context); painter.restore();
            painter.setPen(QColor("#536573")); painter.setFont(QFont("Times New Roman",8));
            painter.drawText(QRectF(0,pdf.height()-28,pdf.width(),25),Qt::AlignLeft|Qt::AlignVCenter,date);
            painter.drawText(QRectF(0,pdf.height()-28,pdf.width(),25),Qt::AlignRight|Qt::AlignVCenter,
                QString::fromUtf8("Сторінка %1 з %2").arg(page+1).arg(pages));
        }
        if(painter.isActive()) success=painter.end() && success;
    }
    if(!success || output.error()!=QFileDevice::NoError || output.size()<100) {
        output.cancelWriting(); if(error) *error=QString::fromUtf8("Не вдалося сформувати PDF. Попередній файл не змінено."); return false;
    }
    if(!output.commit()) { if(error) *error=QString::fromUtf8("Не вдалося завершити збереження PDF: %1").arg(output.errorString()); return false; }
    return true;
}
