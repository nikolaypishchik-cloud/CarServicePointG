#include "core/services/ReportsPdfLayoutSupport.h"

#include "core/services/ReportsPdfRenderer.h"
#include "core/io/SafeFileOps.h"
#include "core/services/PdfWebsiteFooter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QPdfWriter>

namespace ReportsPdfLayoutSupport {

int mmToPx(const QPaintDevice* dev, double mm)
{
    return qRound(mm * dev->logicalDpiX() / 25.4);
}

PdfTheme buildTheme(const QPdfWriter& pdf)
{
    PdfTheme t;
    t.left = mmToPx(&pdf, 20);
    t.right = pdf.width() - mmToPx(&pdf, 10);
    t.top = mmToPx(&pdf, 15);
    t.bottom = pdf.height() - mmToPx(&pdf, 15);
    t.pageWidth = pdf.width();
    t.pageHeight = pdf.height();
    t.contentWidth = t.right - t.left;
    return t;
}

void newPdfPage(QPdfWriter& pdf, QPainter& p, int& y, const PdfTheme& theme)
{
    pdf.newPage();
    p.resetTransform();
    y = theme.top;
}

bool ensurePdfOutputPath(const QString& path, QString* errorText)
{
    const QString trimmedPath = path.trimmed();
    if (trimmedPath.isEmpty()) {
        if (errorText) *errorText = QString::fromUtf8("Не вказано шлях для збереження PDF");
        return false;
    }

    QFileInfo info(trimmedPath);
    QDir dir = info.dir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        if (errorText) {
            *errorText = QString::fromUtf8("Не вдалося створити папку для PDF:\n%1")
                .arg(QDir::toNativeSeparators(dir.absolutePath()));
        }
        return false;
    }

    if (info.exists() && !SafeFileOps::safeRemoveFile(trimmedPath, {info.absolutePath()}, {}, {QStringLiteral(".pdf")})) {
        if (errorText) {
            *errorText = QString::fromUtf8("Не вдалося перезаписати PDF-файл:\n%1")
                .arg(QDir::toNativeSeparators(trimmedPath));
        }
        return false;
    }

    if (errorText) errorText->clear();
    return true;
}

bool finalizePdf(QPainter& p, const QString& path, ReportsPdfRenderer::ExportResult* result)
{
    if (p.isActive()) {
        p.end();
    }

    const QFileInfo info(path);
    const bool ok = info.exists() && info.isFile() && info.size() > 0;

    if (result) {
        result->success = ok;
        result->path = path;
        result->fileSizeBytes = ok ? info.size() : -1;
        if (ok) {
            result->errorText.clear();
        } else if (result->errorText.trimmed().isEmpty()) {
            result->errorText = QString::fromUtf8("PDF не вдалося зберегти:\n%1")
                .arg(QDir::toNativeSeparators(path));
        }
    }

    return ok;
}

} // namespace ReportsPdfLayoutSupport
