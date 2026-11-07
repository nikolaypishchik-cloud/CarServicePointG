#include "core/services/InvoiceEvidenceService.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include "core/services/AccessControlService.h"

namespace InvoiceEvidenceService {
QString key(const QString& orderUid, int partId) {
    return orderUid + QLatin1Char(':') + QString::number(partId);
}
namespace {
bool exec(QSqlQuery& q, QString* error) {
    if (q.exec()) return true;
    if (error) *error = q.lastError().text();
    return false;
}
bool schema(QSqlDatabase db, QString* error) {
    QSqlQuery q(db);
    for (const auto& sql : {
        "CREATE TABLE IF NOT EXISTS invoice_part_evidence (document_hash TEXT NOT NULL, order_uid TEXT NOT NULL, part_id INTEGER NOT NULL, part_uid TEXT NOT NULL, invoice_number TEXT NOT NULL, invoice_date TEXT NOT NULL, supplier TEXT NOT NULL, article TEXT NOT NULL, name TEXT NOT NULL, quantity REAL NOT NULL CHECK(quantity>0), purchase_cents INTEGER NOT NULL CHECK(purchase_cents>=0), file_path TEXT NOT NULL, captured_at TEXT NOT NULL, active INTEGER NOT NULL DEFAULT 1, PRIMARY KEY(document_hash,order_uid,part_id))",
        "CREATE INDEX IF NOT EXISTS idx_invoice_evidence_part ON invoice_part_evidence(order_uid,part_id,active)",
        "CREATE INDEX IF NOT EXISTS idx_invoice_evidence_supplier ON invoice_part_evidence(supplier,invoice_date,active)",
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_invoice_evidence_current ON invoice_part_evidence(order_uid,part_id) WHERE active=1"}) {
        if (!q.exec(QString::fromLatin1(sql))) { if(error) *error=q.lastError().text(); return false; }
    }
    return true;
}
}
bool deactivate(QSqlDatabase db, int partId, QString* error) {
    if (!db.tables().contains("invoice_part_evidence")) return true;
    QSqlQuery q(db);
    q.prepare("UPDATE invoice_part_evidence SET active=0 WHERE part_id=? AND order_uid=(SELECT order_uid FROM work_order_parts WHERE id=?)");
    q.addBindValue(partId); q.addBindValue(partId);
    return exec(q,error);
}
bool capture(QSqlDatabase db, int partId, const QString& path, const QString& number,
             const QString& date, QString* error, double confirmedAmount) {
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file) || file.error()!=QFileDevice::NoError) {
        if(error) *error=QString::fromUtf8("Не вдалося підтвердити файл накладної. Збереження скасовано.");
        return false;
    }
    const QString digest=QString::fromLatin1(hash.result().toHex());
    QSqlQuery part(db);
    part.prepare("SELECT order_uid, COALESCE(part_uid,''), COALESCE(store,''), COALESCE(part_number,''), COALESCE(name,''), qty, purchase_amount, COALESCE(ordered_at,'') FROM work_order_parts WHERE id=?");
    part.addBindValue(partId);
    if(!exec(part,error) || !part.next()) {
        if(error && error->isEmpty()) *error=QString::fromUtf8("Запчастину для накладної не знайдено.");
        return false;
    }
    const QString uid=part.value(0).toString();
    const double quantity=part.value(5).toDouble();
    if(confirmedAmount!=-1 && (!std::isfinite(confirmedAmount) || confirmedAmount<0)) {
        if(error) *error=QString::fromUtf8("Сума накладної некоректна. Підтвердження не збережено.");
        return false;
    }
    const double amount=confirmedAmount>=0?confirmedAmount:part.value(6).toDouble();
    if(uid.isEmpty() || !std::isfinite(quantity) || quantity<=0 || !std::isfinite(amount) || amount<0 || amount>9e12) {
        if(error) *error=QString::fromUtf8("Дані рядка накладної некоректні. Підтвердження не збережено.");
        return false;
    }
    if(!schema(db,error) || !deactivate(db,partId,error)) return false;
    QSqlQuery q(db);
    // The first confirmed snapshot is immutable. Reattaching the same document
    // does not turn later price edits into document evidence.
    q.prepare("INSERT INTO invoice_part_evidence(document_hash,order_uid,part_id,part_uid,invoice_number,invoice_date,supplier,article,name,quantity,purchase_cents,file_path,captured_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(document_hash,order_uid,part_id) DO UPDATE SET active=1,file_path=excluded.file_path");
    q.addBindValue(digest); q.addBindValue(uid); q.addBindValue(partId);
    q.addBindValue(part.value(1)); q.addBindValue(number.isNull()?QStringLiteral(""):number.trimmed());
    const QDate invoiceDate=PartsQueryService::parseBusinessDate(date);
    q.addBindValue(invoiceDate.isValid()?invoiceDate.toString("dd.MM.yy"):QStringLiteral(""));
    q.addBindValue(part.value(2)); q.addBindValue(part.value(3)); q.addBindValue(part.value(4));
    q.addBindValue(quantity); q.addBindValue(qRound64(amount*100.0)); q.addBindValue(path);
    q.addBindValue(QDateTime::currentDateTime().toString("dd.MM.yy HH:mm:ss"));
    return exec(q,error);
}
QHash<QString, InvoiceEvidence> load(QSqlDatabase db, const QVector<PartRowSnapshot>& visibleParts, QString* error) {
    QHash<QString,InvoiceEvidence> result;
    if(error) error->clear();
    if(!db.tables().contains("invoice_part_evidence")) return result;
    const auto scope=AccessControlService::currentOrderScope(db);
    QVector<PartRowSnapshot> authorized;
    for(const auto& part:visibleParts) {
        if(scope.restricted && !scope.contains(part.orderUid,part.orderNumber)) continue;
        authorized.append(part);
    }
    // Bound batches stay below SQLite's legacy variable limit and query only authorized rows.
    for(qsizetype start=0;start<authorized.size();start+=200) {
        const auto end=qMin(start+200,authorized.size());
        QStringList predicates;
        for(auto i=start;i<end;++i) predicates.append("(e.order_uid=? AND e.part_id=?)");
        QSqlQuery q(db);
        q.prepare("SELECT e.document_hash,e.invoice_number,e.invoice_date,e.supplier,e.article,e.name,e.quantity,e.purchase_cents,e.file_path,e.order_uid,e.part_id FROM invoice_part_evidence e WHERE ("+predicates.join(" OR ")+") AND e.active=1 AND (NOT EXISTS(SELECT 1 FROM work_order_parts p WHERE p.id=e.part_id AND p.order_uid=e.order_uid) OR EXISTS(SELECT 1 FROM work_order_part_attachments a WHERE a.part_id=e.part_id AND a.file_path=e.file_path))");
        for(auto i=start;i<end;++i) { q.addBindValue(authorized[i].orderUid); q.addBindValue(authorized[i].id); }
        if(!exec(q,error)) return {};
        while(q.next()) {
            InvoiceEvidence fact;
            fact.documentHash=q.value(0).toString(); fact.invoiceNumber=q.value(1).toString();
            fact.invoiceDate=q.value(2).toString(); fact.supplier=q.value(3).toString();
            fact.article=q.value(4).toString(); fact.name=q.value(5).toString();
            fact.quantity=q.value(6).toDouble(); fact.purchaseCents=q.value(7).toLongLong(); fact.filePath=q.value(8).toString();
            result.insert(key(q.value(9).toString(),q.value(10).toInt()),fact);
        }
    }
    return result;
}
}
