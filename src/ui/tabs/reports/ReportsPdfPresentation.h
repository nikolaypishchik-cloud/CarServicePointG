#pragma once
#include "core/services/ReportsPdfRenderer.h"
class QWidget;
namespace ReportsPdfPresentation {
bool openExportedPdf(QWidget* parent, const ReportsPdfRenderer::ExportResult& result);
}
