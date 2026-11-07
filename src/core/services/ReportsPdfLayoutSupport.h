#pragma once

#include <QString>

class QPaintDevice;
class QPainter;
class QPdfWriter;

namespace ReportsPdfRenderer { struct ExportResult; }

namespace ReportsPdfLayoutSupport {

struct PdfTheme {
    int left = 0;
    int right = 0;
    int top = 0;
    int bottom = 0;
    int pageWidth = 0;
    int pageHeight = 0;
    int contentWidth = 0;
};

int mmToPx(const QPaintDevice* dev, double mm);
PdfTheme buildTheme(const QPdfWriter& pdf);
void newPdfPage(QPdfWriter& pdf, QPainter& p, int& y, const PdfTheme& theme);
bool ensurePdfOutputPath(const QString& path, QString* errorText);
bool finalizePdf(QPainter& p, const QString& path, ReportsPdfRenderer::ExportResult* result);

} // namespace ReportsPdfLayoutSupport
