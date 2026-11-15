#pragma once

#include "core/services/ReportsPdfRenderer.h"
#include <QStringList>

namespace ReportsPdfRenderer {
struct PartsRegisterSelection {
    QStringList headers;
    QList<QStringList> rows;
    QString store, payment, search;
};

ExportResult renderPartsRegisterPdf(QDate from, QDate to, const PartsRegisterSelection& selection);
}
