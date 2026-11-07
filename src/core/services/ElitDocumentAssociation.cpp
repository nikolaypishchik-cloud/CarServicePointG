#include "core/services/ElitDocumentAssociation.h"
#include "core/services/PartsQueryService.h"
#include "core/services/ActivityAuditService.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlQuery>
#include <QSqlError>
#include <QRegularExpression>
#include <cmath>
#include <limits>

namespace ElitDocumentAssociation {
namespace {
bool fail(QString* error,const QString& text) {if(error)*error=text;return false;}
QString nameKey(QString text) {
    text=text.normalized(QString::NormalizationForm_KC).toCaseFolded();
    text.replace(QRegularExpression("[^\\p{L}\\p{N}]+")," ");
    return text.simplified();
}
bool articleCandidate(const QString& local,const ElitInvoiceReader::Line& line,bool* exact) {
    const auto key=SupplierInvoiceLedger::normalizedArticle(local);
    if(key.isEmpty())return false;
    for(const auto& code:{line.reference,line.itemCode}) {
        if(!code.isEmpty() && key==SupplierInvoiceLedger::normalizedArticle(code)) {*exact=true;return true;}
    }
    // A supplier's explicit VW. catalogue prefix is only a REVIEW candidate,
    // never an automatic OEM equivalence. Arbitrary prefixes are not stripped.
    return line.itemCode.startsWith("VW.",Qt::CaseInsensitive)
        && key==SupplierInvoiceLedger::normalizedArticle(line.itemCode.mid(3));
}
QString fingerprint(const SupplierInvoiceLedger::LocalItem& item,const ElitInvoiceStore::Document& doc,int line) {
    const auto& p=item.purchase;const auto& l=doc.invoice.lines[line];
    const QJsonArray data{item.key,p.supplier,p.date,p.oem,p.name,p.quantity,
        doc.identity,line,l.itemCode,l.reference,l.description,l.quantity,QString::number(l.grossCents)};
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(data).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
}
QString slot(const Candidate& c){return c.invoiceId+":"+QString::number(c.line);}
}
QVector<Candidate> candidates(QSqlDatabase db,const QString& account,QString* error) {
    if(error)error->clear();
    const auto snapshot=ElitInvoiceStore::load(db,account,error);
    if((error && !error->isEmpty()) || snapshot.documents.isEmpty())return {};
    const auto local=SupplierInvoiceLedger::localItems(db,error);
    if(error && !error->isEmpty())return {};
    QSet<QString> financiallyLinked;QHash<QString,QSet<QString>> invoicePurchases;
    QHash<QString,double> used;
    if(db.tables().contains("elit_purchase_invoice_allocations")) {
        QSqlQuery q(db);q.prepare("SELECT purchase_key,invoice_id FROM elit_purchase_invoice_allocations WHERE account=? AND active=1");q.addBindValue(account);
        if(!q.exec()){fail(error,q.lastError().text());return {};}
        while(q.next()){financiallyLinked.insert(q.value(0).toString());invoicePurchases[q.value(1).toString()].insert(q.value(0).toString());}
    }
    if(!invoicePurchases.isEmpty() && db.tables().contains("elit_invoice_links")) {
        QSqlQuery q(db);q.prepare("SELECT invoice_id,links FROM elit_invoice_links WHERE account=?");q.addBindValue(account);
        if(!q.exec()){fail(error,q.lastError().text());return {};}
        while(q.next()) {
            const auto identity=q.value(0).toString();const auto rows=QJsonDocument::fromJson(q.value(1).toByteArray()).array();
            for(int n=0;n<rows.size();++n)for(const auto& value:rows[n].toArray())if(invoicePurchases.value(identity).contains(value.toString()))
                used[identity+":"+QString::number(n)]=std::numeric_limits<double>::max();
        }
    }
    QVector<Candidate> result;
    for(const auto& item:local) {
        const auto& p=item.purchase;
        if(p.source!="part" || !SupplierInvoiceLedger::isElit(p.supplier) || !item.document.isEmpty() || item.attachment
            || financiallyLinked.contains(item.key) || !std::isfinite(p.quantity) || p.quantity<=0)continue;
        const auto date=PartsQueryService::parseBusinessDate(p.date);if(!date.isValid())continue;
        for(const auto& doc:snapshot.documents) {
            const auto& inv=doc.invoice;
            if(doc.missingFromLatest || inv.currency!="UAH" || !inv.detailsComplete || !inv.posted.isValid()
                || qAbs(date.daysTo(inv.posted))>31)continue;
            for(int n=0;n<inv.lines.size();++n) {
                const auto& line=inv.lines[n];bool exactCode=false;
                if(!std::isfinite(line.quantity) || line.quantity-used.value(doc.identity+":"+QString::number(n))<p.quantity-0.000001
                    || !articleCandidate(p.oem,line,&exactCode))continue;
                Candidate c;c.purchaseKey=item.key;c.invoiceId=doc.identity;c.number=inv.number;c.line=n;
                c.orderNumber=p.orderNumber;c.name=p.name;c.article=p.oem;c.invoiceName=line.description;
                c.invoiceArticle=line.reference.isEmpty()?line.itemCode:line.reference;c.partDate=date;c.invoiceDate=inv.posted;
                c.quantity=p.quantity;c.availableQuantity=line.quantity-used.value(doc.identity+":"+QString::number(n));c.fingerprint=fingerprint(item,doc,n);
                c.exact=exactCode && date==inv.posted && !nameKey(p.name).isEmpty() && nameKey(p.name)==nameKey(line.description);
                result.append(c);
            }
        }
    }
    return result;
}
bool confirm(QSqlDatabase db,const QString& account,const QVector<Candidate>& selected,bool automatic,QString* error) {
    if(error)error->clear();
    if(!SupplierDebtReconciliationService::canReconcile(db))return fail(error,QString::fromUtf8("Недостатньо прав для прив’язування накладних."));
    if(selected.isEmpty())return true;
    QSqlQuery tx(db);
    if(!tx.exec("SAVEPOINT elit_document_association"))return fail(error,tx.lastError().text());
    auto abort=[&](const QString& reason){tx.exec("ROLLBACK TO elit_document_association");tx.exec("RELEASE elit_document_association");return fail(error,reason);};
    if(!tx.exec("CREATE TABLE IF NOT EXISTS elit_document_associations(account TEXT NOT NULL,purchase_key TEXT NOT NULL,invoice_id TEXT NOT NULL,line INTEGER NOT NULL,fingerprint TEXT NOT NULL,confirmed_at TEXT NOT NULL,PRIMARY KEY(account,purchase_key))"))return abort(tx.lastError().text());
    PartsQueryService::invalidateCache();
    const auto current=candidates(db,account,error);if(error && !error->isEmpty())return abort(*error);
    QHash<QString,Candidate> byFingerprint;
    for(const auto& c:current)byFingerprint.insert(c.fingerprint,c);
    QHash<QString,QString> assigned;
    QHash<QString,double> quantities;
    QSqlQuery q(db);q.prepare("SELECT purchase_key,fingerprint FROM elit_document_associations WHERE account=?");q.addBindValue(account);
    if(!q.exec())return abort(q.lastError().text());
    while(q.next()) {
        const auto f=q.value(1).toString();
        if(byFingerprint.contains(f)){const auto c=byFingerprint[f];assigned.insert(c.purchaseKey,f);quantities[slot(c)]+=c.quantity;}
    }
    q.finish();
    for(const auto& requested:selected) {
        if(!byFingerprint.contains(requested.fingerprint))return abort(QString::fromUtf8("Дані запчастини або накладної змінилися. Оновіть список і підтвердьте знову."));
        const auto c=byFingerprint.value(requested.fingerprint);
        if(automatic && !c.exact)return abort(QString::fromUtf8("Цей збіг потребує підтвердження."));
        if(assigned.contains(c.purchaseKey)) {
            if(assigned[c.purchaseKey]==c.fingerprint)continue;
            return abort(QString::fromUtf8("Запчастина вже має іншу прив’язку."));
        }
        if(quantities.value(slot(c))+c.quantity>c.availableQuantity+0.000001)return abort(QString::fromUtf8("Кількості в накладній недостатньо для вибраних нарядів."));
        quantities[slot(c)]+=c.quantity;assigned.insert(c.purchaseKey,c.fingerprint);
        q.prepare("INSERT OR REPLACE INTO elit_document_associations VALUES(?,?,?,?,?,strftime('%Y-%m-%dT%H:%M:%fZ','now'))");
        q.addBindValue(account);q.addBindValue(c.purchaseKey);q.addBindValue(c.invoiceId);q.addBindValue(c.line);q.addBindValue(c.fingerprint);
        if(!q.exec())return abort(q.lastError().text());
        if(!ActivityAuditService::record(db,"invoices",automatic?"elit_document_auto_link":"elit_document_confirm_link",
            QString::fromUtf8("Прив’язано документ Еліт без зміни оплат"),c.orderNumber,"part",c.purchaseKey,c.number,
            QString::fromUtf8(QJsonDocument(QJsonArray{c.invoiceId,c.line,c.fingerprint}).toJson(QJsonDocument::Compact)),error))return abort(error?*error:QString());
    }
    if(!tx.exec("RELEASE elit_document_association"))return abort(tx.lastError().text());
    return true;
}
bool autoLink(QSqlDatabase db,const QString& account,int* count,QString* error) {
    if(count)*count=0;
    const auto all=candidates(db,account,error);if(error && !error->isEmpty())return false;
    QHash<QString,int> uses;QHash<QString,double> demand;
    for(const auto& c:all)if(c.exact){++uses[c.purchaseKey];demand[slot(c)]+=c.quantity;}
    const auto existing=links(db,account);
    for(const auto& c:all)if(!c.exact && existing.contains(c.purchaseKey) && existing[c.purchaseKey].number==c.number)demand[slot(c)]+=c.quantity;
    QVector<Candidate> selected;
    for(const auto& c:all)if(c.exact && uses[c.purchaseKey]==1 && demand[slot(c)]<=c.availableQuantity+0.000001 && !existing.contains(c.purchaseKey))selected.append(c);
    // Explicitly confirmed associations take precedence over new automatic candidates.
    if(!confirm(db,account,selected,true,error))return false;
    if(count)*count=selected.size();return true;
}
QHash<QString,SupplierInvoiceLedger::LinkedInvoice> links(QSqlDatabase db,const QString& account) {
    QHash<QString,SupplierInvoiceLedger::LinkedInvoice> result;
    if(!db.tables().contains("elit_document_associations"))return result;
    QString error;const auto all=candidates(db,account,&error);if(!error.isEmpty())return {};
    QHash<QString,Candidate> valid;for(const auto& c:all)valid.insert(c.fingerprint,c);
    QSqlQuery q(db);q.prepare("SELECT fingerprint FROM elit_document_associations WHERE account=?");q.addBindValue(account);
    if(!q.exec())return {};
    while(q.next()) {
        const auto f=q.value(0).toString();if(!valid.contains(f))continue;
        const auto c=valid[f];SupplierInvoiceLedger::LinkedInvoice link;
        link.number=c.number;link.date=c.invoiceDate.toString(Qt::ISODate);
        link.documents.append({c.invoiceId,c.number,link.date});result.insert(c.purchaseKey,link);
    }
    return result;
}
}
