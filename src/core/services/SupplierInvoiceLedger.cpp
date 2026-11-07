#include "infrastructure/db/sqlite/Db.h"
#include "core/services/SupplierInvoiceLedger.h"
#include "core/services/ElitDocumentAssociation.h"
#include "core/services/ElitInvoicePdf.h"
#include "core/services/PartsQueryService.h"
#include "core/services/InvoiceEvidenceService.h"
#include "core/services/ActivityAuditService.h"
#include "core/services/SupplierDebtReconciliationService.h"
#include "core/services/AccessControlService.h"
#include "core/services/SparePaymentAllocation.h"
#include "core/services/orders/PartMarkupPolicyService.h"
#include "core/util/StoreNameNormalizer.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QRegularExpression>
#include <cmath>

namespace SupplierInvoiceLedger {
namespace {
QString supplierName(const QString& s) { return StoreNameNormalizer::canonicalDisplayNameOrDefault(s,QString::fromUtf8("Без магазину")); }
bool fail(QString* error,const QString& text) { if(error) *error=text; return false; }
bool eligible(const LocalItem& item,const Entry& entry,const ElitInvoiceReader::Line& line) {
    const auto& p=item.purchase;
    if(!isElit(p.supplier) || entry.currency!="UAH" || !std::isfinite(p.quantity) || !std::isfinite(line.quantity)
        || p.quantity<=0 || line.quantity<=0 || p.totalCents<0 || line.grossCents<0) return false;
    // ELIT UA can omit crossReferenceNo while returning the catalogue code in no.
    // Use that exact code only as a fallback; never strip brand prefixes or guess aliases.
    const auto article=normalizedArticle(line.reference.trimmed().isEmpty()?line.itemCode:line.reference);
    if(article.isEmpty() || normalizedArticle(p.oem)!=article) return false;
    if(!item.document.isEmpty() && item.document.trimmed()!=entry.number) return false;
    if(item.document.isEmpty() && item.attachment) return false;
    if(!item.document.isEmpty() && item.documentDate.isValid() && item.documentDate!=entry.date) return false;
    const auto date=PartsQueryService::parseBusinessDate(p.date);
    if(!date.isValid() || qAbs(date.daysTo(entry.date))>31) return false;
    return qAbs(p.totalCents/p.quantity-line.grossCents/line.quantity)<=1.0;
}
bool automaticEligible(const LocalItem& item,const Entry& entry,const ElitInvoiceReader::Line& line) {
    // Previously confirmed document identities remain authoritative; discovering
    // a new link across different business dates requires explicit confirmation.
    return eligible(item,entry,line) && (!item.document.isEmpty()
        || PartsQueryService::parseBusinessDate(item.purchase.date)==entry.date);
}
bool validLinks(const Entry& entry,const QVector<LocalItem>& local,const QVector<QVector<QString>>& links,
                QHash<QString,double>* used,QVector<LocalItem>* selected,QVector<LineAllocation>* allocations=nullptr) {
    if(links.size()!=entry.lines.size()) return false;
    QHash<QString,LocalItem> byKey;
    for(const auto& item:local) byKey.insert(item.key,item);
    QHash<QString,double> consumed;
    QVector<LineAllocation> split;
    QVector<LocalItem> matched;
    qint64 allocatedTotal=0,documentedTotal=0;
    for(int n=0;n<links.size();++n) {
        if(links[n].isEmpty()) continue;
        double qty=0; qint64 amount=0;QSet<QString> lineKeys;
        for(const auto& key:links[n]) {
            if(!byKey.contains(key) || lineKeys.contains(key)) return false;
            lineKeys.insert(key);
            const auto& item=byKey[key];
            if(!eligible(item,entry,entry.lines[n])) return false;
            const double before=used->value(key)+consumed.value(key);
            const double take=qMin(item.purchase.quantity-before,entry.lines[n].quantity-qty);
            if(take<=0.000001)continue;
            // Cumulative rounding conserves the original purchase value across lines.
            const auto cents=qRound64(item.purchase.totalCents*((before+take)/item.purchase.quantity))
                -qRound64(item.purchase.totalCents*(before/item.purchase.quantity));
            consumed[key]+=take;qty+=take;amount+=cents;
            split.append({n,key,take,cents});
        }
        if(qAbs(qty-entry.lines[n].quantity)>0.000001 || qAbs(amount-entry.lines[n].grossCents)>2) return false;
        allocatedTotal+=amount;documentedTotal+=entry.lines[n].grossCents;
    }
    if(split.isEmpty() || qAbs(allocatedTotal-documentedTotal)>2) return false;
    for(const auto& row:links)for(const auto& key:row)
        if(consumed.value(key)<=0.000001)return false;
    for(auto it=consumed.cbegin();it!=consumed.cend();++it) {
        auto item=byKey.value(it.key());const auto original=item.purchase;
        const double before=used->value(it.key()),after=before+it.value();
        if(after>original.quantity+0.000001)return false;
        auto portion=[&](qint64 cents){return qRound64(cents*(after/original.quantity))-qRound64(cents*(before/original.quantity));};
        item.apportioned=qAbs(it.value()-original.quantity)>0.000001;
        item.purchase.quantity=it.value();item.purchase.totalCents=portion(original.totalCents);
        // Comparison-only proportional allocation. This does not claim which
        // invoice a real payment settled and never updates payment records.
        item.purchase.paidCents=qBound<qint64>(0,portion(qBound<qint64>(0,original.paidCents,original.totalCents)),item.purchase.totalCents);
        matched.append(item);
    }
    for(auto it=consumed.cbegin();it!=consumed.cend();++it)(*used)[it.key()]+=it.value();
    *selected=matched;if(allocations)*allocations=split;return true;
}
Entry remoteEntry(const ElitInvoiceStore::Document& d) {
    const auto& i=d.invoice; Entry e;
    e.key=d.identity; e.supplier=QString::fromUtf8("Еліт"); e.number=i.number; e.currency=i.currency;
    e.date=i.posted; e.due=i.due; e.total=i.grossCents; e.remaining=i.remainingCents;
    e.remote=true; e.isNew=d.isNew; e.lines=i.lines;
    e.missingFromLatest=d.missingFromLatest; return e;
}
}
QString normalizedArticle(QString value) { value=value.toUpper(); value.remove(QRegularExpression("[\\s.\\-/]")); return value; }
bool isElit(const QString& supplier) {
    const auto value=supplierName(supplier).trimmed();
    return value.compare(QString::fromUtf8("Еліт"),Qt::CaseInsensitive)==0 || value.compare("ELIT",Qt::CaseInsensitive)==0;
}
QVector<LocalItem> localItems(QSqlDatabase db,QString* error) {
    if(error) error->clear();
    const auto parts=PartsQueryService::loadRows(db,{},{},{},error);
    if(error && !error->isEmpty()) return {};
    const auto evidence=InvoiceEvidenceService::load(db,parts,error);
    if(error && !error->isEmpty()) return {};
    QSet<QString> issued;
    QHash<QString,QSet<QString>> partKeys;
    for(const auto& part:parts) partKeys[QString::number(part.id)+":"+QString::number(part.orderNumber)]
        .insert(PurchasePaymentService::key("part",part.id,part.orderUid));
    QHash<qint64,QVector<int>> stockOrders;
    const auto tables=db.tables();
    if(tables.contains("workshop_spare_movements")) {
        QVector<SparePaymentAllocation::Distribution> distribution;
        if(!SparePaymentAllocation::distribution(db,&distribution,error))return {};
        for(const auto& slice:distribution) {
            if(!slice.partId)continue;
            // loadRows includes archived parts; a current-table join loses their UID.
            const auto candidates=partKeys.value(QString::number(slice.partId)+":"+QString::number(slice.orderNumber));
            if(candidates.size()==1) issued.unite(candidates);
            auto& orders=stockOrders[slice.purchaseId];
            if(!orders.contains(slice.orderNumber)) orders.append(slice.orderNumber);
        }
    }
    QHash<QString,QDate> due;
    if(tables.contains("purchase_payment_terms")) {
        QSqlQuery q(db);
        if(!q.exec("SELECT payment_key,due_date FROM purchase_payment_terms")) { fail(error,q.lastError().text()); return {}; }
        while(q.next()) due.insert(q.value(0).toString(),PartsQueryService::parseBusinessDate(q.value(1).toString()));
    }
    QVector<LocalItem> result;
    for(const auto& row:parts) {
        if(PartMarkupPolicyService::isInventoryStore(row.store) || PartMarkupPolicyService::isCustomerSupplied(row.store)) continue;
        LocalItem item; item.purchase=PurchasePaymentService::fromPart(row);
        item.key=PurchasePaymentService::key("part",row.id,row.orderUid);
        if(issued.contains(item.key)) continue;
        item.purchase.dueDate=due.value(item.key);
        const auto fact=evidence.value(InvoiceEvidenceService::key(row.orderUid,row.id));
        item.document=fact.invoiceNumber; item.documentDate=PartsQueryService::parseBusinessDate(fact.invoiceDate);
        item.attachment=row.hasInvoiceAttachment || !fact.filePath.isEmpty(); item.orders={row.orderNumber};
        result.append(item);
    }
    if(tables.contains("workshop_inventory_purchases")) {
        const auto paid=PurchasePaymentService::balances(db,error);
        if(error && !error->isEmpty()) return {};
        const auto metadata=db.record("workshop_inventory_purchases").indexOf("metadata_json")>=0?"metadata_json":"'{}'";
        QSqlQuery q(db);
        const QString returnedFilter=tables.contains("returned_inventory_purchases")?" WHERE id NOT IN (SELECT purchase_id FROM returned_inventory_purchases)":"";
        if(!q.exec(QString("SELECT id,purchased_on,material_name,quantity,unit_cost,supplier,%1 FROM workshop_inventory_purchases").arg(metadata)+returnedFilter)) {
            fail(error,q.lastError().text()); return {};
        }
        while(q.next()) {
            LocalItem item; auto& p=item.purchase;
            p.source="inventory"; p.id=q.value(0).toLongLong(); p.date=q.value(1).toString(); p.name=q.value(2).toString();
            p.quantity=q.value(3).toDouble(); p.totalCents=PurchasePaymentService::cents(p.quantity*q.value(4).toDouble());
            p.supplier=q.value(5).toString(); item.key=PurchasePaymentService::key(p.source,p.id);
            p.paidCents=paid.value(item.key,p.totalCents); p.dueDate=due.value(item.key);
            const auto data=QJsonDocument::fromJson(q.value(6).toString().toUtf8()).object();
            p.oem=data.value("article").toString(); item.document=data.value("document").toString();
            item.orders=stockOrders.value(p.id); result.append(item);
        }
    }
    return result;
}
Ledger load(QSqlDatabase db,const QString& account) {
    Ledger ledger;
    ledger.local=localItems(db,&ledger.error); if(!ledger.error.isEmpty()) return ledger;
    ledger.snapshot=ElitInvoiceStore::load(db,account,&ledger.error); if(!ledger.error.isEmpty()) return ledger;
    QHash<QString,QVector<QVector<QString>>> confirmed;
    QHash<QString,QHash<QString,QPair<double,qint64>>> originalPurchases;
    if(db.tables().contains("elit_purchase_invoice_allocations")) {
        QSqlQuery q(db);q.prepare("SELECT invoice_id,purchase_key,purchase_quantity,purchase_total_cents FROM elit_purchase_invoice_allocations WHERE account=?");q.addBindValue(account);
        if(!q.exec()){ledger.error=q.lastError().text();return ledger;}
        while(q.next())originalPurchases[q.value(0).toString()].insert(q.value(1).toString(),{q.value(2).toDouble(),q.value(3).toLongLong()});
    }
    if(db.tables().contains("elit_invoice_links")) {
        QSqlQuery q(db); q.prepare("SELECT invoice_id,links FROM elit_invoice_links WHERE account=?"); q.addBindValue(account);
        if(!q.exec()) { ledger.error=q.lastError().text(); return ledger; }
        while(q.next()) {
            QVector<QVector<QString>> links;
            for(const auto& row:QJsonDocument::fromJson(q.value(1).toByteArray()).array()) {
                QVector<QString> keys; for(const auto& key:row.toArray()) keys.append(key.toString()); links.append(keys);
            }
            confirmed.insert(q.value(0).toString(),links);
        }
    }
    QHash<QString,double> used;
    QHash<QString,PurchaseDebt> countedParts;
    QSet<QString> changedPurchases;
    for(const auto& originals:originalPurchases)for(const auto& item:ledger.local)if(originals.contains(item.key)) {
        const auto original=originals.value(item.key);
        if(qAbs(original.first-item.purchase.quantity)>0.000001 || original.second!=item.purchase.totalCents)changedPurchases.insert(item.key);
    }
    QHash<QString,int> candidateUses;
    QHash<QString,double> candidateDemand;
    QSet<QString> ambiguousCandidates;
    for(const auto& d:ledger.snapshot.documents) {
        auto e=remoteEntry(d);
        QSet<QString> invoiceCandidates;
        for(const auto& line:e.lines) {
            QStringList candidates;
            for(const auto& item:ledger.local)if(automaticEligible(item,e,line))candidates.append(item.key);
            for(const auto& key:candidates) {
                invoiceCandidates.insert(key);candidateDemand[key]+=line.quantity;
                if(candidates.size()!=1)ambiguousCandidates.insert(key);
            }
        }
        for(const auto& key:invoiceCandidates)++candidateUses[key];
    }
    auto documents=ledger.snapshot.documents;
    std::stable_sort(documents.begin(),documents.end(),[&](const auto& a,const auto& b){
        if(confirmed.contains(a.identity)!=confirmed.contains(b.identity))return confirmed.contains(a.identity);
        return a.invoice.posted!=b.invoice.posted?a.invoice.posted<b.invoice.posted:a.identity<b.identity;
    });
    for(const auto& d:documents) {
        auto e=remoteEntry(d);
        e.links=confirmed.value(e.key);
        bool originalsValid=true;
        for(const auto& item:ledger.local)if(originalPurchases.value(e.key).contains(item.key)) {
            const auto original=originalPurchases.value(e.key).value(item.key);
            if(qAbs(original.first-item.purchase.quantity)>0.000001 || original.second!=item.purchase.totalCents)originalsValid=false;
        }
        if(!originalsValid || !validLinks(e,ledger.local,e.links,&used,&e.items,&e.allocations)) {
            if(confirmed.contains(e.key)) {
                e.matched=false; e.counted=false; e.discrepancy=true;
                e.reason=QString::fromUtf8("Збережена прив’язка змінилася. Потрібна ручна перевірка.");
                ledger.hasUnresolved=true; ledger.entries.append(e); continue;
            }
            e.links.clear(); e.items.clear();
            for(const auto& line:e.lines) {
                QVector<QString> candidates;
                for(const auto& item:ledger.local)
                    if(!changedPurchases.contains(item.key) && used.value(item.key)<item.purchase.quantity-0.000001 && automaticEligible(item,e,line)
                        && (candidateUses.value(item.key)==1 || (!ambiguousCandidates.contains(item.key)
                            && candidateDemand.value(item.key)<=item.purchase.quantity+0.000001))) {
                        candidates.append(item.key);
                    }
                std::sort(candidates.begin(),candidates.end());
                e.links.append(candidates);
            }
            e.matched=validLinks(e,ledger.local,e.links,&used,&e.items,&e.allocations);
            if(!e.matched) {
                // Preserve independently matchable lines when another OEM group
                // is incomplete or ambiguous; do not publish a partial purchase.
                QHash<QString,int> uses;
                for(const auto& row:e.links)for(const auto& key:row)++uses[key];
                for(int n=0;n<e.links.size();++n) {
                    double qty=0;qint64 total=0;bool unique=true;
                    for(const auto& item:ledger.local)if(e.links[n].contains(item.key)) {
                        qty+=item.purchase.quantity;total+=item.purchase.totalCents;unique &= uses.value(item.key)==1;
                    }
                    if(!unique || qAbs(qty-e.lines[n].quantity)>0.000001 || qAbs(total-e.lines[n].grossCents)>2)e.links[n].clear();
                }
                e.matched=validLinks(e,ledger.local,e.links,&used,&e.items,&e.allocations);
            }
        } else e.matched=true;
        for(const auto& item:e.items) e.localRemaining+=item.purchase.remainingCents();
        e.discrepancy=!e.matched || qAbs(e.remaining-e.localRemaining)>2 || e.currency!="UAH" || e.remaining<0 || e.remaining>e.total;
        // Unresolved invoices remain visible, but are not silently added to possibly overlapping local debt.
        const bool allLines=std::all_of(e.links.cbegin(),e.links.cend(),[](const auto& keys){return !keys.isEmpty();});
        e.counted=e.matched && allLines && e.currency=="UAH" && e.remaining>=0 && e.remaining<=e.total;
        if(!e.counted) {
            ledger.hasUnresolved=true;
            e.reason=QString::fromUtf8("Потрібна звірка: суму не додано до підтвердженого боргу");
        } else if(e.discrepancy) e.reason=QString::fromUtf8("Оплата Еліт відрізняється від локального обліку");
        else e.reason=QString::fromUtf8("Збігається");
        for(const auto& item:e.items)if(item.apportioned) {
            e.discrepancy=true;ledger.hasUnresolved=true;
            e.reason=QString::fromUtf8("Розподілена закупівля. Локальна оплата розподілена пропорційно лише для порівняння; оплати не змінено.");break;
        }
        if(e.counted) for(const auto& item:e.items) {
            auto& counted=countedParts[item.key];counted.quantity+=item.purchase.quantity;
            counted.totalCents+=item.purchase.totalCents;counted.paidCents+=item.purchase.paidCents;
        }
        if(d.missingFromLatest) {
            ledger.hasUnresolved=true;
            e.reason=QString::fromUtf8("Статус оплати не підтверджено: накладної немає в останньому списку Еліт. Останні дані: %1. Збережено попередній залишок; оплати не змінено. ")
                .arg(d.lastSeen.toLocalTime().toString("dd.MM.yy HH:mm"))+e.reason;
        }
        ledger.entries.append(e);
    }
    QMap<QString,Entry> groups;
    for(auto item:ledger.local) {
        const auto counted=countedParts.value(item.key);
        item.purchase.quantity-=counted.quantity;item.purchase.totalCents-=counted.totalCents;item.purchase.paidCents-=counted.paidCents;
        if(item.purchase.quantity<=0.000001)continue;
        item.apportioned=counted.quantity>0;
        const auto& p=item.purchase;
        if(p.totalCents<=0) continue;
        const auto supplier=supplierName(p.supplier);
        const auto identity=item.document.isEmpty()?item.key:QString::fromUtf8(QJsonDocument(QJsonArray{supplier,item.document,item.documentDate.toString(Qt::ISODate)}).toJson(QJsonDocument::Compact));
        auto& e=groups[identity]; e.key=identity; e.supplier=supplier; e.number=item.document;
        e.date=item.documentDate.isValid()?item.documentDate:PartsQueryService::parseBusinessDate(p.date);
        if(!e.due.isValid() || (p.dueDate.isValid() && p.dueDate<e.due)) e.due=p.dueDate;
        e.total+=p.totalCents; e.remaining+=p.remainingCents(); e.localRemaining=e.remaining;
        e.items.append(item); e.reason=QString::fromUtf8("Локальний облік");
    }
    for(const auto& e:groups) ledger.entries.append(e);
    return ledger;
}
QVector<LocalItem> linkCandidates(const Ledger& ledger,const Entry& entry,int line) {
    if(!entry.remote || line<0 || line>=entry.lines.size()) return {};
    QHash<QString,double> used;
    for(const auto& other:ledger.entries) if(other.remote && other.key!=entry.key && other.matched)
        for(const auto& item:other.items) used[item.key]+=item.purchase.quantity;
    QVector<LocalItem> result;
    for(auto item:ledger.local)
        if(used.value(item.key)<item.purchase.quantity-0.000001 && eligible(item,entry,entry.lines[line])) {
            const auto original=item.purchase;const double consumed=used.value(item.key);
            item.purchase.quantity-=consumed;
            item.purchase.totalCents-=qRound64(original.totalCents*(consumed/original.quantity));
            item.purchase.paidCents-=qRound64(original.paidCents*(consumed/original.quantity));
            result.append(item);
        }
    return result;
}
namespace {
bool writeProjection(QSqlDatabase db,const QString& account,const Ledger& ledger,QString* error) {
    QSqlQuery q(db);
    for(const auto& sql:{
        "CREATE TABLE IF NOT EXISTS elit_purchase_invoice_links(account TEXT NOT NULL,purchase_key TEXT NOT NULL,invoice_id TEXT NOT NULL,invoice_number TEXT NOT NULL,invoice_date TEXT NOT NULL,PRIMARY KEY(account,purchase_key))",
        "CREATE TABLE IF NOT EXISTS elit_purchase_invoice_allocations(account TEXT NOT NULL,purchase_key TEXT NOT NULL,invoice_id TEXT NOT NULL,invoice_number TEXT NOT NULL,invoice_date TEXT NOT NULL,quantity REAL NOT NULL,total_cents INTEGER NOT NULL,paid_cents INTEGER NOT NULL,apportioned INTEGER NOT NULL,purchase_quantity REAL NOT NULL,purchase_total_cents INTEGER NOT NULL,active INTEGER NOT NULL DEFAULT 1,PRIMARY KEY(account,purchase_key,invoice_id))"})
        if(!q.exec(QString::fromLatin1(sql)))return fail(error,q.lastError().text());
    for(const auto& sql:{"DELETE FROM elit_purchase_invoice_links WHERE account=?","UPDATE elit_purchase_invoice_allocations SET active=0 WHERE account=?"}) {
        q.prepare(QString::fromLatin1(sql));q.addBindValue(account);
        if(!q.exec())return fail(error,q.lastError().text());
    }
    for(const auto& entry:ledger.entries)if(entry.remote && entry.matched && entry.currency=="UAH" && entry.remaining>=0 && entry.remaining<=entry.total) {
        q.prepare("DELETE FROM elit_purchase_invoice_allocations WHERE account=? AND invoice_id=?");q.addBindValue(account);q.addBindValue(entry.key);
        if(!q.exec())return fail(error,q.lastError().text());
        for(const auto& item:entry.items) {
            q.prepare("INSERT INTO elit_purchase_invoice_allocations(account,purchase_key,invoice_id,invoice_number,invoice_date,quantity,total_cents,paid_cents,apportioned,purchase_quantity,purchase_total_cents) VALUES(?,?,?,?,?,?,?,?,?,?,?)");
            q.addBindValue(account);q.addBindValue(item.key);q.addBindValue(entry.key);q.addBindValue(entry.number);q.addBindValue(entry.date.toString(Qt::ISODate));
            q.addBindValue(item.purchase.quantity);q.addBindValue(item.purchase.totalCents);q.addBindValue(item.purchase.paidCents);q.addBindValue(item.apportioned?1:0);
            for(const auto& original:ledger.local)if(original.key==item.key){q.addBindValue(original.purchase.quantity);q.addBindValue(original.purchase.totalCents);break;}
            if(!q.exec())return fail(error,q.lastError().text());
            // Compatibility projection only for an unsplit purchase. Never let
            // the old one-document key silently choose a document from a split.
            if(item.apportioned)continue;
            q.prepare("INSERT INTO elit_purchase_invoice_links VALUES(?,?,?,?,?)");
            q.addBindValue(account);q.addBindValue(item.key);q.addBindValue(entry.key);q.addBindValue(entry.number);q.addBindValue(entry.date.toString(Qt::ISODate));
            if(!q.exec())return fail(error,q.lastError().text());
        }
    }
    return true;
}
}
bool confirmLinks(QSqlDatabase db,const QString& account,const QString& revision,const QString& invoiceIdentity,
                  const QVector<QVector<QString>>& links,QString* error) {
    if(error) error->clear();
    if(!SupplierDebtReconciliationService::canReconcile(db)) return fail(error,QString::fromUtf8("Недостатньо прав для звірки."));
    QSqlQuery tx(db);
    if(!tx.exec("SAVEPOINT elit_confirm_links")) return fail(error,tx.lastError().text());
    auto abort=[&](const QString& text) { tx.exec("ROLLBACK TO elit_confirm_links"); tx.exec("RELEASE elit_confirm_links"); return fail(error,text); };
    if(!tx.exec("CREATE TABLE IF NOT EXISTS elit_invoice_links(account TEXT NOT NULL,invoice_id TEXT NOT NULL,links BLOB NOT NULL,PRIMARY KEY(account,invoice_id))")) return abort(tx.lastError().text());
    if(!tx.exec("UPDATE elit_invoice_links SET links=links WHERE 0")) return abort(tx.lastError().text());
    PartsQueryService::invalidateCache();
    const auto ledger=load(db,account);
    if(!ledger.error.isEmpty()) return abort(ledger.error);
    if(ledger.snapshot.revision!=revision) return abort(QString::fromUtf8("Накладні оновлено. Відкрийте звірку повторно."));
    QHash<QString,double> used;
    Entry selected;
    for(const auto& e:ledger.entries) if(e.remote) {
        if(e.key==invoiceIdentity) selected=e;
        else if(e.matched) for(const auto& item:e.items) used[item.key]+=item.purchase.quantity;
    }
    QVector<LocalItem> items;
    if(selected.key.isEmpty() || selected.remaining<0 || selected.remaining>selected.total || !validLinks(selected,ledger.local,links,&used,&items))
        return abort(QString::fromUtf8("OEM, кількість, сума або прив’язки змінилися. Перевірте позиції."));
    QJsonArray data;
    for(const auto& row:links) { QJsonArray keys; for(const auto& key:row) keys.append(key); data.append(keys); }
    QSqlQuery q(db); q.prepare("INSERT INTO elit_invoice_links VALUES(?,?,?) ON CONFLICT(account,invoice_id) DO UPDATE SET links=excluded.links");
    q.addBindValue(account); q.addBindValue(invoiceIdentity); q.addBindValue(QJsonDocument(data).toJson(QJsonDocument::Compact));
    if(!q.exec()) return abort(q.lastError().text());
    if(db.tables().contains("elit_purchase_invoice_allocations")) {
        q.prepare("DELETE FROM elit_purchase_invoice_allocations WHERE account=? AND invoice_id=?");q.addBindValue(account);q.addBindValue(invoiceIdentity);
        if(!q.exec())return abort(q.lastError().text());
    }
    const auto refreshed=load(db,account);
    if(!refreshed.error.isEmpty())return abort(refreshed.error);
    if(!writeProjection(db,account,refreshed,error))return abort(error?*error:QString());
    if(!ActivityAuditService::record(db,"invoices","elit_invoice_link",QString::fromUtf8("Прив’язано накладну Еліт"),0,"invoice",invoiceIdentity,selected.number,QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact)),error)) return abort(error?*error:QString());
    if(!tx.exec("RELEASE elit_confirm_links")) return abort(tx.lastError().text());
    return true;
}
qint64 debtTotal(const Ledger& ledger) {
    qint64 total=0;
    for(const auto& e:ledger.entries) if(e.counted && e.currency=="UAH") total+=qMax<qint64>(0,e.remaining);
    return total;
}
bool hasRemoteBalances(const Ledger& ledger) {
    if(ledger.snapshot.revision.isEmpty()) return false;
    for(const auto& document:ledger.snapshot.documents) {
        const auto& invoice=document.invoice;
        if(invoice.currency!="UAH" || invoice.remainingCents<0 || invoice.remainingCents>invoice.grossCents) return false;
    }
    return true;
}
Summary summarize(const Ledger& ledger,const QVector<PurchaseDebt>& debts,
                  const QMap<QString,SupplierDebtReconciliation>& reconciliations) {
    Summary result;
    const auto local=SupplierDebtReconciliationService::effectiveTotals(debts,reconciliations);
    result.remote=hasRemoteBalances(ledger);result.unresolved=ledger.hasUnresolved;
    if(!result.remote) {result.totals=local;result.count=debts.size();}
    else {
        // The complete remote balance replaces this supplier's local estimate,
        // not its purchases or payments. Unlinked purchases may be the same debt.
        for(const auto& debt:debts) if(!isElit(debt.supplier)) {
            const auto name=supplierName(debt.supplier);
            result.totals[name]+=debt.remainingCents();
            if(debt.remainingCents()>0 && !reconciliations.contains(name)) ++result.count;
        }
        for(const auto& document:ledger.snapshot.documents) {
            result.totals[QString::fromUtf8("Еліт")]+=document.invoice.remainingCents;
            if(document.invoice.remainingCents>0) ++result.count;
        }
    }
    // Supplier-level manual facts are not additive to remote invoice balances.
    for(auto it=reconciliations.cbegin();it!=reconciliations.cend();++it) {
        if(result.remote && isElit(it.key())) continue;
        result.manualSuppliers.insert(it.key());result.totals[it.key()]=local.value(it.key());
        if(result.remote && local.value(it.key())>0) {
            int count=0;for(const auto& debt:debts) if(supplierName(debt.supplier)==it.key() && debt.remainingCents()>0) ++count;
            result.count+=qMax(1,count);
        }
    }
    return result;
}
Summary summary(QSqlDatabase db,const QVector<PurchaseDebt>& debts,
                const QMap<QString,SupplierDebtReconciliation>& reconciliations,QString* error) {
    if(error) error->clear();
    if(AccessControlService::currentOrderScope(db).restricted || !db.tables().contains("elit_invoice_snapshots"))
        return summarize({},debts,reconciliations);
    GeneralSettingsService::ElitCredentials credentials;
    if(!GeneralSettingsService::loadElitCredentials(&credentials,error)) return {};
    Ledger ledger;
    ledger.snapshot=ElitInvoiceStore::load(db,ElitInvoiceStore::accountKey(credentials),&ledger.error);
    if(!ledger.error.isEmpty()){if(error)*error=ledger.error;return {};}
    return summarize(ledger,debts,reconciliations);
}
QVector<PurchaseDebt> reportingDebts(QSqlDatabase db,const QVector<PurchaseDebt>& local,
    QMap<QString,SupplierDebtReconciliation>* facts,QString* error) {
    if(error) error->clear();
    if(AccessControlService::currentOrderScope(db).restricted) return local;
    GeneralSettingsService::ElitCredentials credentials;
    if(!GeneralSettingsService::loadElitCredentials(&credentials,error)) return local;
    Ledger ledger;
    ledger.snapshot=ElitInvoiceStore::load(db,ElitInvoiceStore::accountKey(credentials),error);
    if((error && !error->isEmpty()) || !hasRemoteBalances(ledger)) return local;
    QVector<PurchaseDebt> result;
    for(const auto& debt:local) if(!isElit(debt.supplier)) result.append(debt);
    if(facts) for(auto it=facts->begin();it!=facts->end();) {
        if(isElit(it.key())) it=facts->erase(it); else ++it;
    }
    for(const auto& document:ledger.snapshot.documents) {
        const auto& invoice=document.invoice;
        if(invoice.remainingCents<=0) continue;
        PurchaseDebt debt;
        debt.source="remote_invoice";debt.orderUid=document.identity;
        debt.supplier=QString::fromUtf8("Еліт");debt.invoice=invoice.number;
        debt.name=QString::fromUtf8("Накладна %1 (Еліт)").arg(invoice.number);
        debt.date=invoice.posted.toString(Qt::ISODate);debt.dueDate=invoice.due;
        debt.totalCents=invoice.grossCents;debt.paidCents=invoice.grossCents-invoice.remainingCents;
        result.append(debt);
    }
    std::stable_sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.supplier<b.supplier;});
    return result;
}
bool publishAndLink(QSqlDatabase db,const QString& account,const ElitInvoiceReader::Result& result,
                    const QString& revision,const std::function<bool()>& stillCurrent,
                    int* linked,int* unresolved,QString* error) {
    if(error) error->clear();
    if(linked) *linked=0;
    if(unresolved) *unresolved=0;
    auto allowed=[&] { return stillCurrent && stillCurrent() && SupplierDebtReconciliationService::canReconcile(db); };
    if(!allowed()) return fail(error,QString::fromUtf8("Синхронізацію скасовано: змінився користувач або доступ."));
    QSqlQuery tx(db);
    if(!tx.exec("SAVEPOINT elit_sync_publish")) return fail(error,tx.lastError().text());
    auto abort=[&](const QString& reason) { tx.exec("ROLLBACK TO elit_sync_publish"); tx.exec("RELEASE elit_sync_publish"); return fail(error,reason); };
    if(!ElitInvoiceStore::publish(db,account,result,revision,error)) return abort(error?*error:QString());
    PartsQueryService::invalidateCache();
    const auto ledger=load(db,account);
    if(!ledger.error.isEmpty()) return abort(ledger.error);
    QSet<QString> saved;
    if(db.tables().contains("elit_invoice_links")) {
        QSqlQuery q(db); q.prepare("SELECT invoice_id FROM elit_invoice_links WHERE account=?"); q.addBindValue(account);
        if(!q.exec()) return abort(q.lastError().text());
        while(q.next()) saved.insert(q.value(0).toString());
    }
    int added=0, pending=0;
    for(const auto& entry:ledger.entries) {
        if(!entry.remote) continue;
        if(!entry.counted || entry.missingFromLatest) ++pending;
        if(entry.missingFromLatest) continue;
        if(!entry.matched || entry.currency!="UAH" || entry.remaining<0 || entry.remaining>entry.total) continue;
        if(!allowed()) return abort(QString::fromUtf8("Синхронізацію скасовано. Попередні дані збережено."));
        if(!saved.contains(entry.key)) {
            if(!confirmLinks(db,account,ledger.snapshot.revision,entry.key,entry.links,error)) return abort(error?*error:QString());
            ++added;
        }
    }
    const auto refreshed=load(db,account);
    if(!refreshed.error.isEmpty())return abort(refreshed.error);
    if(!writeProjection(db,account,refreshed,error))return abort(error?*error:QString());
    int documentLinks=0;
    if(!ElitDocumentAssociation::autoLink(db,account,&documentLinks,error))return abort(error?*error:QString());
    added+=documentLinks;
    if(!allowed()) return abort(QString::fromUtf8("Синхронізацію скасовано. Попередні дані збережено."));
    if(!tx.exec("RELEASE elit_sync_publish")) return abort(tx.lastError().text());
    if(linked) *linked=added;
    if(unresolved) *unresolved=pending;
    return true;
}
QHash<QString,LinkedInvoice> savedLinks(QSqlDatabase db,const QString& account) {
    auto links=ElitDocumentAssociation::links(db,account);
    QSqlQuery q(db);
    if(db.tables().contains("elit_purchase_invoice_links")) { q.prepare("SELECT purchase_key,invoice_number,invoice_date FROM elit_purchase_invoice_links WHERE account=?");
    q.addBindValue(account);
    if(q.exec()) while(q.next()) links.insert(q.value(0).toString(),{q.value(1).toString(),q.value(2).toString()});
    }
    if(db.tables().contains("elit_purchase_invoice_allocations")) {
        q.prepare("SELECT purchase_key,invoice_id,invoice_number,invoice_date,quantity,total_cents,paid_cents,apportioned,purchase_quantity,purchase_total_cents FROM elit_purchase_invoice_allocations WHERE account=? AND active=1 ORDER BY invoice_date,invoice_id");q.addBindValue(account);
        if(q.exec())while(q.next()) {
            auto& link=links[q.value(0).toString()];
            if(link.documents.isEmpty()){link.number.clear();link.date=q.value(3).toString();}
            if(!link.number.isEmpty())link.number+=QStringLiteral("; ");link.number+=q.value(2).toString();
            if(link.date!=q.value(3).toString())link.date.clear();
            link.apportioned |= q.value(7).toBool();
            link.purchaseQuantity=q.value(8).toDouble();link.purchaseTotal=q.value(9).toLongLong();
            link.documents.append({q.value(1).toString(),q.value(2).toString(),q.value(3).toString(),q.value(4).toDouble(),q.value(5).toLongLong(),q.value(6).toLongLong()});
        }
    }
    const auto pdfs=ElitInvoicePdf::paths(db,account);
    for(auto& link:links) {
        for(auto& document:link.documents)document.filePath=pdfs.value(document.identity);
        if(link.documents.size()==1)link.filePath=link.documents.first().filePath;
    }
    return links;
}
QHash<QString,LinkedInvoice> activeLinks(QSqlDatabase db) {
    if(!db.tables().contains("elit_purchase_invoice_links") && !db.tables().contains("elit_document_associations")) return {};
    GeneralSettingsService::ElitCredentials credentials;
    if(!GeneralSettingsService::loadElitCredentials(&credentials)) return {};
    return savedLinks(db,ElitInvoiceStore::accountKey(credentials));
}
}

namespace SupplierInvoiceLedger {
QHash<QString,LinkedInvoice> activeLinks() { return activeLinks(Database::db()); }
}
