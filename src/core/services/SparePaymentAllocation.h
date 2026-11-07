#pragma once
#include <QHash>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

namespace SparePaymentAllocation {
struct Distribution {
    qint64 purchaseId = 0, total = 0;
    int partId = 0, orderNumber = 0;
    double quantity = 0;
};
// Read-only FIFO provenance, including the unissued remainder (partId == 0).
bool distribution(QSqlDatabase db, QVector<Distribution>* result, QString* error);
struct Slice {
    qint64 issueId = 0, purchaseId = 0, total = 0, paid = 0;
    int partId = 0;
    QString orderUid;
};
// Receipt payments are authoritative; allocations select which issued units they cover.
bool load(QSqlDatabase db, const QHash<QString, qint64>& balances, QVector<Slice>* slices, QString* error);
bool save(QSqlDatabase db, const QVector<Slice>& slices, QString* error);
void project(const QVector<Slice>& slices, QHash<QString, qint64>* balances);
}
