#pragma once
#include "core/services/ElitInvoiceReader.h"
#include <QSqlDatabase>

// Remote observations are deliberately separate from purchase payment events.
namespace ElitInvoiceStore {
struct Document {
    ElitInvoiceReader::Invoice invoice;
    QString identity;
    bool isNew = false;
    bool missingFromLatest = false;
    QDateTime lastSeen;
};
struct Snapshot {
    QString account, revision;
    QDateTime fetchedAt;
    QVector<Document> documents;
};
QString accountKey(const GeneralSettingsService::ElitCredentials& credentials);
QString identity(const ElitInvoiceReader::Invoice& invoice);
Snapshot load(QSqlDatabase db, const QString& account, QString* error = nullptr);
bool publish(QSqlDatabase db, const QString& account, const ElitInvoiceReader::Result& result,
             const QString& expectedRevision, QString* error = nullptr);
}
