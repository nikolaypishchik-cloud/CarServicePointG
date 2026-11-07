#include "core/services/InvoiceRegistryService.h"
#include "core/services/InvoiceEvidenceService.h"
#include "core/services/SupplierInvoiceLedger.h"
#include "core/services/PartsQueryService.h"
#include "core/util/PlateUtils.h"
#include "core/util/StoreNameNormalizer.h"
#include "core/services/AccessControlService.h"
#include <QJsonObject>
#include <QSqlRecord>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFileInfo>
#include <algorithm>

namespace InvoiceRegistryService {
QDate paymentDeadline(const RegisteredInvoice& invoice) {
    if (invoice.dueDate.isValid()) return invoice.dueDate;
    QDate earliest;
    for (const auto& part : invoice.parts) {
        if (part.remainingCents()>0 && part.dueDate.isValid()
            && (!earliest.isValid() || part.dueDate<earliest)) earliest=part.dueDate;
    }
    return earliest.isValid() ? earliest : invoice.date.addDays(7);
}

QVector<RegisteredInvoice> sortedForReport(QVector<RegisteredInvoice> invoices, ReportSort mode, const QDate& today) {
    if (mode==ReportSort::NoDebt) {
        invoices.erase(std::remove_if(invoices.begin(),invoices.end(),[](const auto& invoice) {
            return invoice.remainingCents()>0;
        }),invoices.end());
    }
    std::stable_sort(invoices.begin(),invoices.end(),[mode,today](const auto& a,const auto& b) {
        if (mode==ReportSort::Supplier) {
            const int comparison=a.supplier.localeAwareCompare(b.supplier);
            if(comparison) return comparison<0;
        }
        if (mode==ReportSort::LongestOverdue) {
            const bool debtA=a.remainingCents()>0, debtB=b.remainingCents()>0;
            if(debtA!=debtB) return debtA;
            if(debtA) {
                const auto dueA=paymentDeadline(a), dueB=paymentDeadline(b);
                const auto daysA=dueA.isValid()?qMax<qint64>(0,dueA.daysTo(today)):0;
                const auto daysB=dueB.isValid()?qMax<qint64>(0,dueB.daysTo(today)):0;
                if(daysA!=daysB) return daysA>daysB;
                if(dueA.isValid()!=dueB.isValid()) return dueA.isValid();
                if(dueA!=dueB) return dueA<dueB;
            }
        }
        if(a.date.isValid()!=b.date.isValid()) return a.date.isValid();
        if(a.date!=b.date) return mode==ReportSort::DateAscending ? a.date<b.date : a.date>b.date;
        const int comparison=a.number.localeAwareCompare(b.number);
        return comparison ? comparison<0 : a.key<b.key;
    });
    return invoices;
}

namespace {
QString identityFor(const QString& supplier,const QString& number,const QDate& date,const QString& fallback) {
    return QString::fromUtf8(QJsonDocument(QJsonArray{
        StoreNameNormalizer::canonicalDisplayNameOrDefault(supplier,QString::fromUtf8("Без магазину")).simplified().toCaseFolded(),
        number.trimmed().toCaseFolded(),date.toString(Qt::ISODate),
        number.trimmed().isEmpty() || !date.isValid()?fallback:QString()
    }).toJson(QJsonDocument::Compact));
}
}
QVector<RegisteredInvoice> load(QSqlDatabase db, QString* error) {
    if (error) error->clear();
    QString localError;
    // Schema preparation and payment reconciliation belong to explicit write operations.
    const auto rows = PartsQueryService::loadRows(db, {}, {}, {}, &localError);
    const auto evidence = localError.isEmpty() ? InvoiceEvidenceService::load(db, rows, &localError)
                                              : QHash<QString, InvoiceEvidence>();
    if (!localError.isEmpty()) { if (error) *error = localError; return {}; }
    struct Attachment { QString number, date, path; };
    QHash<QString, Attachment> attachments;
    if (db.tables().contains("work_order_part_attachments")) {
        QSqlQuery q(db);
        if (!q.exec("SELECT a.part_id,a.invoice_number,a.invoice_date,a.file_path,p.order_uid FROM work_order_part_attachments a JOIN work_order_parts p ON p.id=a.part_id")) {
            if (error) *error = q.lastError().text(); return {};
        }
        while (q.next()) attachments.insert(InvoiceEvidenceService::key(q.value(4).toString(), q.value(0).toInt()),
            {q.value(1).toString(), q.value(2).toString(), q.value(3).toString()});
    }
    QMap<QString, RegisteredInvoice> groups;
    QHash<QString,qint64> documentedAmounts;
    QSet<QString> distributedParts;
    auto issuedKey=[](int part,int order){return QString::number(part)+":"+QString::number(order);};
    const auto remoteLinks=SupplierInvoiceLedger::activeLinks(db);
    auto appendSplit=[&](const PurchaseDebt& purchase,const SupplierInvoiceLedger::LinkedInvoice& link,
                         const QVector<SparePaymentAllocation::Distribution>& movements) {
        if(qAbs(purchase.quantity-link.purchaseQuantity)>0.000001 || purchase.totalCents!=link.purchaseTotal || purchase.quantity<=0) {
            localError=QString::fromUtf8("Закупівля змінилася після розподілу накладних. Виконайте звірку.");return;
        }
        double documentStart=0;
        for(const auto& document:link.documents) {
            const auto date=PartsQueryService::parseBusinessDate(document.date);
            const auto identity=identityFor(purchase.supplier,document.number,date,document.identity);
            auto& invoice=groups[identity];invoice.key=identity;invoice.number=document.number;invoice.date=date;invoice.supplier=purchase.supplier;invoice.apportioned=true;
            if(invoice.filePath.isEmpty())invoice.filePath=document.filePath;
            auto part=purchase;part.quantity=document.quantity;part.totalCents=document.total;
            const double documentEnd=documentStart+document.quantity;
            // Recompute comparison values from the CURRENT local balance, not
            // the payment observation captured at the last synchronization.
            part.paidCents=qBound<qint64>(0,qRound64(purchase.paidCents*(documentEnd/purchase.quantity))-qRound64(purchase.paidCents*(documentStart/purchase.quantity)),part.totalCents);
            invoice.parts.append(part);invoice.totalCents+=part.totalCents;invoice.paidCents+=part.paidCents;
            double movementStart=0;
            for(const auto& movement:movements)if(movement.purchaseId==purchase.id) {
                const double begin=qMax(documentStart,movementStart),end=qMin(documentEnd,movementStart+movement.quantity);
                if(end-begin>0.000001) {
                    auto slice=movement;slice.quantity=end-begin;
                    slice.total=qRound64(purchase.totalCents*(end/purchase.quantity))-qRound64(purchase.totalCents*(begin/purchase.quantity));
                    invoice.distribution.append(slice);
                    if(slice.partId)distributedParts.insert(issuedKey(slice.partId,slice.orderNumber));
                }
                movementStart+=movement.quantity;
            }
            documentStart=documentEnd;
        }
    };
    QHash<int,QString> stockSuppliers;
    if(db.tables().contains("workshop_spare_movements")) {
        QSqlQuery q(db);
        if(!q.exec("SELECT m.part_id,p.order_uid,s.supplier,s.document,s.ordered_on FROM workshop_spare_movements m "
                   "JOIN workshop_spare_orders s ON s.id=m.spare_id JOIN work_order_parts p ON p.id=m.part_id "
                   "AND p.order_number=m.order_number WHERE m.operation='issue'")) {
            if(error) *error=q.lastError().text();return {};
        }
        while(q.next()) {
            stockSuppliers.insert(q.value(0).toInt(),q.value(2).toString());
            auto& attachment=attachments[InvoiceEvidenceService::key(q.value(1).toString(),q.value(0).toInt())];
            if(attachment.number.trimmed().isEmpty()) attachment.number=q.value(3).toString();
            if(attachment.date.trimmed().isEmpty()) attachment.date=q.value(4).toString();
        }
    }
    for (const auto& row : rows) {
        const auto split=remoteLinks.value(PurchasePaymentService::key("part",row.id,row.orderUid));
        if(split.apportioned) {
            if(!AccessControlService::currentOrderScope(db).restricted)appendSplit(PurchasePaymentService::fromPart(row),split,{});
            continue;
        }
        auto fact = evidence.constFind(InvoiceEvidenceService::key(row.orderUid, row.id));
        auto attachment = attachments.value(InvoiceEvidenceService::key(row.orderUid, row.id));
        if (fact != evidence.cend()) attachment = {fact->invoiceNumber, fact->invoiceDate, fact->filePath};
        if(attachment.number.trimmed().isEmpty() && attachment.path.trimmed().isEmpty()) {
            const auto linked=remoteLinks.value(PurchasePaymentService::key("part",row.id,row.orderUid));
            attachment.number=linked.number; attachment.date=linked.date;
            attachment.path=linked.filePath;
        }
        if (attachment.path.trimmed().isEmpty() && attachment.number.trimmed().isEmpty()) continue;
        const auto date = PartsQueryService::parseBusinessDate(attachment.date);
        const QString supplier = stockSuppliers.contains(row.id) ? stockSuppliers.value(row.id).trimmed()
            : (fact == evidence.cend() ? row.store.trimmed() : fact->supplier.trimmed());
        // Document number alone is not unique across suppliers or years. Copies
        // of one numbered invoice share identity even when their paths differ.
        const QString identity = identityFor(supplier,attachment.number,date,
            fact == evidence.cend()?QFileInfo(attachment.path).absoluteFilePath():fact->documentHash);
        auto& invoice = groups[identity];
        invoice.key = identity; invoice.number = attachment.number.trimmed();
        invoice.supplier = supplier; invoice.date = date;
        if (invoice.filePath.isEmpty() || (!QFileInfo::exists(invoice.filePath) && QFileInfo::exists(attachment.path)))
            invoice.filePath = attachment.path;
        auto debt = PurchasePaymentService::fromPart(row);
        const qint64 documented = fact == evidence.cend() ? debt.totalCents : fact->purchaseCents;
        documentedAmounts.insert(PurchasePaymentService::key("part",row.id,row.orderUid),documented);
        invoice.amountMismatch |= documented != debt.totalCents;
        invoice.totalCents += documented;
        invoice.paidCents += qMin(documented, debt.paidCents);
        invoice.parts.append(debt);
        invoice.orders.insert(row.orderNumber, row.orderDisplay.isEmpty() ? QString::number(row.orderNumber) : row.orderDisplay);
        const QString vehicle = (row.plate + " " + PlateUtils::normalizePlate(row.plate) + " " + row.carModel).simplified();
        if (!vehicle.isEmpty() && !invoice.vehicles.contains(vehicle)) invoice.vehicles.append(vehicle);
    }
    if(!AccessControlService::currentOrderScope(db).restricted && db.tables().contains("workshop_inventory_purchases")) {
        QSet<qint64> issued;
        QHash<qint64,Attachment> stockDocuments;
        QVector<SparePaymentAllocation::Distribution> distribution;
        if(!SparePaymentAllocation::distribution(db,&distribution,&localError)){if(error)*error=localError;return {};}
        QHash<qint64,qint64> receiptSpares;
        QHash<qint64,QSet<QString>> receiptDocuments;
        QSet<qint64> distributedSpares;
        if(db.tables().contains("workshop_spare_movements")) {
            QSqlQuery q(db);
            if(!q.exec("SELECT DISTINCT r.purchase_id FROM workshop_spare_movements r JOIN workshop_spare_movements i ON i.spare_id=r.spare_id AND i.operation='issue' WHERE r.operation='receipt'")) {
                if(error)*error=q.lastError().text();return {};
            }
            while(q.next()) issued.insert(q.value(0).toLongLong());
            const bool files=db.tables().contains("workshop_spare_attachments");
            if(!q.exec(QString("SELECT r.purchase_id,s.document,s.ordered_on,%1,r.spare_id FROM workshop_spare_movements r JOIN workshop_spare_orders s ON s.id=r.spare_id %2 WHERE r.operation='receipt'")
                .arg(files?"COALESCE(a.file_path,'')":"''",files?"LEFT JOIN workshop_spare_attachments a ON a.spare_id=s.id":""))) {
                if(error)*error=q.lastError().text();return {};
            }
            while(q.next()) {
                const auto purchase=q.value(0).toLongLong(),spare=q.value(4).toLongLong();
                stockDocuments.insert(purchase,{q.value(1).toString(),q.value(2).toString(),q.value(3).toString()});
                receiptSpares.insert(purchase,spare);
            }
        }
        auto receiptDocumentFor=[&](qint64 purchase,const QString& date,const QJsonObject& data) {
            auto document=stockDocuments.value(purchase);
            const auto number=data.value("document").toString().trimmed();
            if(!number.isEmpty() && number!=document.number.trimmed()) {
                document.number=number;document.date=date;document.path.clear();
            }
            if(document.number.trimmed().isEmpty() && document.path.isEmpty()) {
                const auto linked=remoteLinks.value(PurchasePaymentService::key("inventory",purchase));
                document.number=linked.number;document.date=linked.date;
                document.path=linked.filePath;
            }
            if(document.date.isEmpty())document.date=date;
            return document;
        };
        // Keep the existing single-document order-payment contract. Only receipts
        // belonging to different documents replace the legacy issued-part rows.
        if(db.record("workshop_inventory_purchases").indexOf("metadata_json")>=0) {
            QSqlQuery documents(db);
            if(!documents.exec("SELECT id,metadata_json,purchased_on,supplier FROM workshop_inventory_purchases")) {
                if(error)*error=documents.lastError().text();return {};
            }
            while(documents.next()) {
                const auto purchase=documents.value(0).toLongLong();
                if(!receiptSpares.contains(purchase))continue;
                const auto document=receiptDocumentFor(purchase,documents.value(2).toString(),QJsonDocument::fromJson(documents.value(1).toString().toUtf8()).object());
                receiptDocuments[receiptSpares.value(purchase)].insert(identityFor(documents.value(3).toString(),document.number,
                    PartsQueryService::parseBusinessDate(document.date),document.path));
            }
            for(auto it=receiptDocuments.cbegin();it!=receiptDocuments.cend();++it)
                if(it.value().size()>1)distributedSpares.insert(it.key());
        }
        const auto balances=PurchasePaymentService::balances(db,&localError);
        if(!localError.isEmpty()){if(error)*error=localError;return {};}
        const auto metadata=db.record("workshop_inventory_purchases").indexOf("metadata_json")>=0?"metadata_json":"'{}'";
        QSqlQuery q(db);
        if(!q.exec(QString("SELECT id,purchased_on,material_name,quantity,unit_cost,supplier,%1 FROM workshop_inventory_purchases").arg(metadata))) {
            if(error)*error=q.lastError().text();return {};
        }
        while(q.next()) {
            PurchaseDebt debt;debt.source="inventory";debt.id=q.value(0).toLongLong();
            const bool legacyIssued=issued.contains(debt.id) && !distributedSpares.contains(receiptSpares.value(debt.id));
            debt.date=q.value(1).toString();debt.name=q.value(2).toString();debt.quantity=q.value(3).toDouble();
            debt.totalCents=PurchasePaymentService::cents(debt.quantity*q.value(4).toDouble());debt.supplier=q.value(5).toString();
            const auto key=PurchasePaymentService::key("inventory",debt.id);
            debt.paidCents=balances.value(key,debt.totalCents);
            const auto data=QJsonDocument::fromJson(q.value(6).toString().toUtf8()).object();debt.oem=data.value("article").toString();
            if(remoteLinks.value(key).apportioned) {appendSplit(debt,remoteLinks.value(key),distribution);continue;}
            auto document=receiptDocumentFor(debt.id,debt.date,data);
            if(document.number.trimmed().isEmpty() && document.path.isEmpty()) continue;
            if(document.date.isEmpty()) document.date=debt.date;
            const auto date=PartsQueryService::parseBusinessDate(document.date);
            const auto identity=identityFor(debt.supplier,document.number,date,document.path.isEmpty()?key:QFileInfo(document.path).absoluteFilePath());
            if(legacyIssued) {
                auto existing=groups.find(identity);
                if(existing!=groups.end()) {
                    if(existing->filePath.isEmpty())existing->filePath=document.path;
                    for(const auto& slice:distribution)
                        if(slice.purchaseId==debt.id)existing->distribution.append(slice);
                    continue;
                }
            }
            auto& invoice=groups[identity];invoice.key=identity;invoice.number=document.number.trimmed();invoice.date=date;invoice.supplier=debt.supplier;
            if(invoice.filePath.isEmpty()) invoice.filePath=document.path;
            invoice.totalCents+=debt.totalCents;invoice.paidCents+=qMin(debt.totalCents,debt.paidCents);invoice.parts.append(debt);
            bool hasDistribution=false;
            for(const auto& slice:distribution) if(slice.purchaseId==debt.id) {
                hasDistribution=true;invoice.distribution.append(slice);
                if(slice.partId) {
                    distributedParts.insert(issuedKey(slice.partId,slice.orderNumber));
                    invoice.orders.insert(slice.orderNumber,QString::number(slice.orderNumber));
                    for(const auto& row:rows) if(row.id==slice.partId && row.orderNumber==slice.orderNumber) {
                        invoice.orders[slice.orderNumber]=row.orderDisplay.isEmpty()?QString::number(row.orderNumber):row.orderDisplay;
                        const auto vehicle=(row.plate+" "+PlateUtils::normalizePlate(row.plate)+" "+row.carModel).simplified();
                        if(!vehicle.isEmpty() && !invoice.vehicles.contains(vehicle))invoice.vehicles.append(vehicle);
                    }
                }
            }
            if(!hasDistribution)invoice.distribution.append({debt.id,debt.totalCents,0,0,debt.quantity});
        }
    }
    // The receipt owns the liability. Issued parts are destinations, not a second purchase.
    for(auto it=groups.begin();it!=groups.end();) {
        auto& invoice=it.value();
        for(int index=invoice.parts.size()-1;index>=0;--index) {
            const auto part=invoice.parts[index];
            if(part.source!="part" || !distributedParts.contains(issuedKey(part.id,part.orderNumber)))continue;
            const auto amount=documentedAmounts.value(PurchasePaymentService::key("part",part.id,part.orderUid),part.totalCents);
            invoice.totalCents-=amount;invoice.paidCents-=qMin(amount,part.paidCents);invoice.parts.removeAt(index);
        }
        if(invoice.parts.isEmpty()) {it=groups.erase(it);continue;}
        invoice.orders.clear();invoice.vehicles.clear();
        for(const auto& row:rows) {
            bool included=false;
            for(const auto& part:invoice.parts)if(part.source=="part" && part.id==row.id && part.orderUid==row.orderUid)included=true;
            for(const auto& slice:invoice.distribution)if(slice.partId==row.id && slice.orderNumber==row.orderNumber)included=true;
            if(!included)continue;
            invoice.orders.insert(row.orderNumber,row.orderDisplay.isEmpty()?QString::number(row.orderNumber):row.orderDisplay);
            const auto vehicle=(row.plate+" "+PlateUtils::normalizePlate(row.plate)+" "+row.carModel).simplified();
            if(!vehicle.isEmpty() && !invoice.vehicles.contains(vehicle))invoice.vehicles.append(vehicle);
        }
        for(const auto& slice:invoice.distribution)if(slice.partId && !invoice.orders.contains(slice.orderNumber))
            invoice.orders.insert(slice.orderNumber,QString::number(slice.orderNumber));
        ++it;
    }
    if(!localError.isEmpty()){if(error)*error=localError;return {};}
    auto result = groups.values().toVector();
    QHash<QString,QDate> deadlines;
    if(db.tables().contains("purchase_payment_terms")) {
    QSqlQuery terms(db);
    if(!terms.exec("SELECT payment_key,due_date FROM purchase_payment_terms")) {
        if(error)*error=terms.lastError().text();return {};
    }
    while(terms.next()) deadlines.insert(terms.value(0).toString(),QDate::fromString(terms.value(1).toString(),Qt::ISODate));
    if(terms.lastError().isValid()){if(error)*error=terms.lastError().text();return {};}
    }
    for(auto& invoice:result) for(auto& part:invoice.parts)
        part.dueDate=deadlines.value(PurchasePaymentService::key(part.source,part.id,part.orderUid));
    QHash<QString,QPair<qint64,QDateTime>> paymentDates;
    if(db.tables().contains("purchase_payment_events")) {
    QSqlQuery events(db);
    if(!events.exec("SELECT payment_key,SUM(amount_cents),MAX(CASE WHEN amount_cents>0 THEN datetime(created_at,'localtime') END) FROM purchase_payment_events GROUP BY payment_key")) {
        if(error)*error=events.lastError().text();return {};
    }
    while(events.next())paymentDates.insert(events.value(0).toString(),{events.value(1).toLongLong(),QDateTime::fromString(events.value(2).toString(),"yyyy-MM-dd HH:mm:ss")});
    if(events.lastError().isValid()){if(error)*error=events.lastError().text();return {};}
    }
    for(auto& invoice:result)if(invoice.paid() && !invoice.apportioned) {
        bool known=true;QDateTime completed;
        for(const auto& part:invoice.parts) {
            const auto payment=paymentDates.value(PurchasePaymentService::key(part.source,part.id,part.orderUid));
            if(!payment.second.isValid() || payment.first<part.totalCents){known=false;break;}
            if(completed<payment.second)completed=payment.second;
        }
        if(known)invoice.paidAt=completed;
    }
    std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.date != b.date ? a.date > b.date : a.number.localeAwareCompare(b.number) < 0;
    });
    return result;
}
}
