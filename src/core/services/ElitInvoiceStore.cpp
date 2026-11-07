#include "core/services/ElitInvoiceStore.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QSqlError>
#include <QUuid>

namespace ElitInvoiceStore {
namespace {
bool fail(QString* error, const QString& text) { if (error) *error = text; return false; }
QString hash(const QJsonArray& values) {
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(values).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
}
QJsonObject encode(const Document& d) {
    const auto& i = d.invoice;
    QJsonArray lines;
    for (const auto& l : i.lines) lines.append(QJsonObject{{"code",l.itemCode},{"reference",l.reference},
        {"description",l.description},{"quantity",l.quantity},{"gross",QString::number(l.grossCents)}});
    return {{"number",i.number},{"currency",i.currency},{"posted",i.posted.toString(Qt::ISODate)},
        {"due",i.due.toString(Qt::ISODate)},{"amount",QString::number(i.amountCents)},
        {"amountVat",QString::number(i.amountVatCents)},{"remaining",QString::number(i.remainingCents)},
        {"gross",QString::number(i.grossCents)},{"lines",lines},{"new",d.isNew},
        {"missingFromLatest",d.missingFromLatest},{"lastSeen",d.lastSeen.toString(Qt::ISODateWithMs)}};
}
bool decode(const QJsonObject& o, Document* d) {
    auto& i = d->invoice;
    i.number=o.value("number").toString(); i.currency=o.value("currency").toString();
    i.posted=QDate::fromString(o.value("posted").toString(),Qt::ISODate);
    i.due=QDate::fromString(o.value("due").toString(),Qt::ISODate);
    bool valid=true;
    constexpr qint64 maxAmount=100000000000000LL;
    auto amount=[&](const QString& field) { bool ok=false; const auto n=o.value(field).toString().toLongLong(&ok); valid &= ok && n>=-maxAmount && n<=maxAmount; return n; };
    i.amountCents=amount("amount"); i.amountVatCents=amount("amountVat");
    i.remainingCents=amount("remaining"); i.grossCents=amount("gross");
    if(!valid || !o.value("lines").isArray() || o.value("lines").toArray().size()>10000) return false;
    qint64 lineSum=0;
    for(const auto& value:o.value("lines").toArray()) {
        const auto l=value.toObject(); bool ok=false;
        ElitInvoiceReader::Line line{l.value("code").toString(),l.value("reference").toString(),
            l.value("description").toString(),l.value("quantity").toDouble(),l.value("gross").toString().toLongLong(&ok)};
        valid &= ok && std::isfinite(line.quantity) && qAbs(line.quantity)<=1.e9
            && line.grossCents>=-maxAmount && line.grossCents<=maxAmount;
        if(!valid) return false;
        lineSum+=line.grossCents;
        if(lineSum < -maxAmount || lineSum > maxAmount) return false;
        i.lines.append(line);
    }
    i.detailsComplete=valid && !i.number.isEmpty() && i.posted.isValid() && !i.currency.isEmpty()
        && !i.lines.isEmpty() && qAbs(lineSum-i.grossCents)<=2;
    d->identity=identity(i); d->isNew=o.value("new").toBool();
    d->missingFromLatest=o.value("missingFromLatest").toBool();
    d->lastSeen=QDateTime::fromString(o.value("lastSeen").toString(),Qt::ISODateWithMs);
    return i.detailsComplete;
}
}
QString accountKey(const GeneralSettingsService::ElitCredentials& c) {
    if(c.company.trimmed().isEmpty() || c.login.trimmed().isEmpty()) return {};
    return hash({ElitSoapRequest::documentedEndpoint().toString(),c.company.trimmed(),c.login.trimmed()});
}
QString identity(const ElitInvoiceReader::Invoice& i) { return hash({i.number,i.posted.toString(Qt::ISODate),i.currency}); }
Snapshot load(QSqlDatabase db,const QString& account,QString* error) {
    if(error) error->clear();
    Snapshot result; result.account=account;
    if(account.isEmpty() || !db.tables().contains("elit_invoice_snapshots")) return result;
    QSqlQuery q(db); q.prepare("SELECT revision,fetched_at,payload FROM elit_invoice_snapshots WHERE account=?"); q.addBindValue(account);
    if(!q.exec()) { fail(error,q.lastError().text()); return {}; }
    if(!q.next()) return result;
    result.revision=q.value(0).toString(); result.fetchedAt=QDateTime::fromString(q.value(1).toString(),Qt::ISODateWithMs);
    QJsonParseError parseError;
    const auto json=QJsonDocument::fromJson(q.value(2).toByteArray(),&parseError);
    if(parseError.error!=QJsonParseError::NoError || !json.isArray() || !result.fetchedAt.isValid()) {
        fail(error,QString::fromUtf8("Збережені дані Еліт пошкоджені. Потрібна перевірка резервної копії.")); return {};
    }
    QSet<QString> seen;
    for(const auto& item:json.array()) {
        Document d;
        if(!decode(item.toObject(),&d) || seen.contains(d.identity)) {
            fail(error,QString::fromUtf8("Збережена накладна Еліт не пройшла перевірку.")); return {};
        }
        if(!d.lastSeen.isValid()) d.lastSeen=result.fetchedAt;
        seen.insert(d.identity); result.documents.append(d);
    }
    return result;
}
bool publish(QSqlDatabase db,const QString& account,const ElitInvoiceReader::Result& result,const QString& expectedRevision,QString* error) {
    if(error) error->clear();
    if(account.isEmpty() || !result.complete || !result.error.isEmpty() || !result.fetchedAt.isValid())
        return fail(error,QString::fromUtf8("Неповний результат Еліт не можна зберігати."));
    QSqlQuery tx(db);
    if(!tx.exec("SAVEPOINT elit_snapshot_publish")) return fail(error,tx.lastError().text());
    auto abort=[&](const QString& reason) { tx.exec("ROLLBACK TO elit_snapshot_publish"); tx.exec("RELEASE elit_snapshot_publish"); return fail(error,reason); };
    if(!tx.exec("CREATE TABLE IF NOT EXISTS elit_invoice_snapshots(account TEXT PRIMARY KEY,revision TEXT NOT NULL,fetched_at TEXT NOT NULL,payload BLOB NOT NULL)")) return abort(tx.lastError().text());
    // Acquire the SQLite writer lock before checking the expected snapshot revision.
    if(!tx.exec("UPDATE elit_invoice_snapshots SET revision=revision WHERE 0")) return abort(tx.lastError().text());
    QString localError;
    const auto previous=load(db,account,&localError);
    if(!localError.isEmpty()) return abort(localError);
    if(previous.revision!=expectedRevision) return abort(QString::fromUtf8("Дані Еліт уже оновлено іншим процесом. Повторіть синхронізацію."));
    QSet<QString> old, seen;
    for(const auto& d:previous.documents) old.insert(d.identity);
    QJsonArray payload;
    for(const auto& i:result.invoices) {
        Document d{i,identity(i),!old.contains(identity(i))}, validated;
        d.lastSeen=result.fetchedAt;
        const auto encoded=encode(d);
        if(!i.detailsComplete || !decode(encoded,&validated) || seen.contains(d.identity))
            return abort(QString::fromUtf8("Повноту накладних Еліт не підтверджено."));
        seen.insert(d.identity); payload.append(encoded);
    }
    // An old observation must never disappear merely because the remote list omitted it.
    for(auto d:previous.documents) if(!seen.contains(d.identity)) {
        for(const auto& incoming:result.invoices) if(incoming.number==d.invoice.number)
            return abort(QString::fromUtf8("Змінилася дата або валюта накладної Еліт %1. Потрібна звірка; дані не змінено.").arg(d.invoice.number));
        d.missingFromLatest=true;
        d.isNew=false;
        payload.append(encode(d));
    }
    const auto revision=QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSqlQuery q(db);
    q.prepare("INSERT INTO elit_invoice_snapshots VALUES(?,?,?,?) ON CONFLICT(account) DO UPDATE SET revision=excluded.revision,fetched_at=excluded.fetched_at,payload=excluded.payload");
    q.addBindValue(account); q.addBindValue(revision); q.addBindValue(result.fetchedAt.toString(Qt::ISODateWithMs));
    q.addBindValue(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    if(!q.exec()) return abort(q.lastError().text());
    if(!tx.exec("CREATE TABLE IF NOT EXISTS elit_sync_runs(id TEXT PRIMARY KEY,account TEXT NOT NULL,completed_at TEXT NOT NULL,document_count INTEGER NOT NULL)")) return abort(tx.lastError().text());
    q.prepare("INSERT INTO elit_sync_runs VALUES(?,?,?,?)"); q.addBindValue(revision); q.addBindValue(account);
    q.addBindValue(result.fetchedAt.toString(Qt::ISODateWithMs)); q.addBindValue(result.invoices.size());
    if(!q.exec()) return abort(q.lastError().text());
    if(!tx.exec("RELEASE elit_snapshot_publish")) return abort(tx.lastError().text());
    return true;
}
}
