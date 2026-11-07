#pragma once
#include "core/import/InvoiceImportTypes.h"

namespace InvoiceImport {
struct InvoiceRecognitionProfile {
    QString key;
    QString updatedAt;
    InvoiceColumnMappingPreview mapping;
    QList<QStringList> sampleRows;
};

class InvoiceRecognitionProfiles final {
public:
    static QString keyFor(const QList<QStringList>& rows, int headerRow, const QString& documentNumber);
    static QList<InvoiceRecognitionProfile> list();
    static bool load(const QString& key, InvoiceColumnMappingPreview* mapping);
    static bool save(const QList<QStringList>& rows, const InvoiceColumnMappingPreview& mapping, QString* error);
    static bool remove(const QString& key, QString* error);
    static bool validateRows(const QList<QStringList>& rows, const InvoiceColumnMappingPreview& mapping,
                             const ParseLooseNumberFn& parseNumber, QString* error);
    static void splitDescription(const QString& raw, ImportedPartDraft* draft);
};
}
