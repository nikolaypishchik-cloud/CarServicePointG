#pragma once

#include "core/services/SupplierInvoiceLedger.h"
#include "core/services/PartsQueryService.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QSqlError>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QUuid>

// Staging only: payment state is unknown unless explicitly supplied by the API.
// This module never projects observations into purchase/payment tables.
namespace SupplierInvoiceStaging {
struct Document {
    QString id, number, supplier, currency;
    QDate date;
    qint64 total = 0;
    bool complete = false;
    QVector<ElitInvoiceReader::Line> lines;
};
struct Link { QString document, purchase; int line = 0; };
struct Plan { QVector<Link> links; int unresolved = 0; };
inline QString supplierKey(QString value) {
    value=value.toCaseFolded().simplified();
    value.remove(' ');value.remove('-');
    if(value=="bm" || value=="bmparts")return "bm";
    if(value=="omega" || value==QString::fromUtf8("омега"))return "omega";
    if(value=="utr" || value=="uniqtrade" || value==QString::fromUtf8("юніктрейд"))return "utr";
    return value;
}
inline QString scalar(const QJsonValue& value) {
    return value.isString()?value.toString():value.isDouble()?QString::number(value.toDouble(),'g',16):QString();
}
inline bool money(const QJsonValue& value,qint64* cents) {
    return ElitInvoiceReader::money(scalar(value),cents) && *cents>=0;
}
inline QDate date(const QString& value) {
    auto result=QDate::fromString(value.left(10),Qt::ISODate);
    if(!result.isValid())result=QDate::fromString(value.left(10),"dd.MM.yyyy");
    return result;
}
inline bool parse(const QString& provider,const QJsonArray& rows,QVector<Document>* output,QString* error,const QString& confirmedCurrency={}) {
    output->clear();error->clear();QSet<QString> seen;
    auto fail=[&]{output->clear();*error=QString::fromUtf8("Накладна не пройшла перевірку номера, дати, товарів або сум. Прив’язки не створено.");return false;};
    if(provider!="bm" && provider!="omega" && provider!="utr")return fail();
    for(const auto& value:rows) {
        if(!value.isObject())return fail();
        const auto o=value.toObject();Document d;d.supplier=provider;
        d.id=scalar(o.value(provider=="bm"?"document_uuid":provider=="omega"?"Id":"accountingNumber"));
        d.number=scalar(o.value(provider=="bm"?"number":provider=="omega"?"Number":"documentNumber"));
        d.date=date(o.value(provider=="bm"?"created_at":provider=="omega"?"Date":"date").toString());
        if(d.id.isEmpty() || d.number.trimmed().isEmpty() || !d.date.isValid() || seen.contains(d.id))return fail();
        seen.insert(d.id);
        if(provider=="utr") {output->append(d);continue;} // Order lines are not invoice lines.
        d.currency=provider=="bm"?o.value("currency").toObject().value("name").toString():QString();
        // Omega's expense schema has no currency field. Never assume UAH for matching.
        if(provider=="omega")d.currency=confirmedCurrency;
        if(!money(o.value(provider=="bm"?"total":"Summ"),&d.total))return fail();
        const auto items=o.value(provider=="bm"?"products":"Products");
        if(!items.isArray()){output->append(d);continue;}
        qint64 sum=0;
        for(const auto& item:items.toArray()) {
            const auto row=item.toObject();const auto product=row.value("product").toObject();
            ElitInvoiceReader::Line line;
            line.reference=provider=="bm"?product.value("article").toString():row.value("Code").toString();
            line.description=provider=="bm"?product.value("name").toString():row.value("ProductDescrition").toString();
            bool ok=false;line.quantity=scalar(row.value(provider=="bm"?"quantity":"Count")).toDouble(&ok);
            if(!ok || !std::isfinite(line.quantity) || line.quantity<=0 || line.quantity>1.e9 || line.reference.trimmed().isEmpty())return fail();
            if(provider=="bm") {if(!money(row.value("total"),&line.grossCents))return fail();}
            else {
                qint64 price=0;if(!money(row.value("PiceWithVAT"),&price) || price*line.quantity>1.e14)return fail();
                line.grossCents=qRound64(price*line.quantity);
            }
            sum+=line.grossCents;if(sum>100000000000000LL)return fail();d.lines.append(line);
        }
        if(!d.lines.isEmpty() && qAbs(sum-d.total)>2)return fail();
        d.complete=!d.lines.isEmpty() && d.currency=="UAH";
        output->append(d);
    }
    return true;
}
inline Plan plan(const QVector<Document>& documents,const QVector<SupplierInvoiceLedger::LocalItem>& local) {
    struct Candidate {QString id,key;int line;};
    QVector<Candidate> candidates;QHash<QString,int> uses;Plan result;
    for(const auto& d:documents) {
        if(!d.complete){++result.unresolved;continue;}
        for(int n=0;n<d.lines.size();++n) {
            const auto& line=d.lines[n];QStringList matches;
            for(const auto& item:local) {
                const auto& p=item.purchase;
                if(supplierKey(p.supplier)!=d.supplier || item.attachment || !item.document.isEmpty())continue;
                const auto purchased=PartsQueryService::parseBusinessDate(p.date);
                if(!purchased.isValid() || qAbs(purchased.daysTo(d.date))>31)continue;
                if(SupplierInvoiceLedger::normalizedArticle(p.oem)!=SupplierInvoiceLedger::normalizedArticle(line.reference))continue;
                if(qAbs(p.quantity-line.quantity)>0.000001 || p.totalCents!=line.grossCents)continue;
                matches.append(item.key);
            }
            if(matches.size()==1){candidates.append({d.id,matches[0],n});++uses[matches[0]];}
            else ++result.unresolved;
        }
    }
    for(const auto& c:candidates) {
        if(uses[c.key]==1)result.links.append({c.id,c.key,c.line});else ++result.unresolved;
    }
    return result;
}
inline bool saveCopy(QSqlDatabase copy,const QString& provider,const QString& account,
                     const QVector<Document>& documents,Plan* result,QString* error) {
    error->clear();
    // Only the caller-created isolated connection is accepted, never Database::db().
    if(!copy.connectionName().startsWith("supplier-staging-copy-") || account.trimmed().isEmpty()) {
        *error="An isolated supplier copy and account identity are required";return false;
    }
    const auto local=SupplierInvoiceLedger::localItems(copy,error);if(!error->isEmpty())return false;
    *result=plan(documents,local);
    QSqlQuery q(copy);if(!q.exec("SAVEPOINT supplier_stage")){*error=q.lastError().text();return false;}
    auto fail=[&]{*error=q.lastError().text();q.exec("ROLLBACK TO supplier_stage");q.exec("RELEASE supplier_stage");return false;};
    if(!q.exec("CREATE TABLE IF NOT EXISTS supplier_staged_documents(provider TEXT,account TEXT,id TEXT,number TEXT,date TEXT,total INTEGER,currency TEXT,complete INTEGER,payment_known INTEGER DEFAULT 0,payload BLOB,PRIMARY KEY(provider,account,id))"))return fail();
    if(!q.exec("CREATE TABLE IF NOT EXISTS supplier_staged_links(provider TEXT,account TEXT,document TEXT,line INTEGER,purchase TEXT,PRIMARY KEY(provider,account,document,line),UNIQUE(provider,account,purchase))"))return fail();
    // This is an isolated snapshot, so regenerate the current batch without stale links.
    for(const auto& table:{"supplier_staged_links","supplier_staged_documents"}) {
        q.prepare("DELETE FROM "+QString::fromLatin1(table)+" WHERE provider=? AND account=?");q.addBindValue(provider);q.addBindValue(account);if(!q.exec())return fail();
    }
    for(const auto& d:documents) {
        QJsonArray lines;for(const auto& line:d.lines)lines.append(QJsonObject{{"article",line.reference},{"description",line.description},{"quantity",line.quantity},{"gross_cents",QString::number(line.grossCents)}});
        q.prepare("INSERT INTO supplier_staged_documents(provider,account,id,number,date,total,currency,complete,payload) VALUES(?,?,?,?,?,?,?,?,?)");
        for(const auto& v:QVariantList{provider,account,d.id,d.number,d.date.toString(Qt::ISODate),d.total,d.currency,d.complete,QJsonDocument(lines).toJson(QJsonDocument::Compact)})q.addBindValue(v);
        if(!q.exec())return fail();
    }
    for(const auto& link:result->links) {
        q.prepare("INSERT INTO supplier_staged_links VALUES(?,?,?,?,?)");
        for(const auto& v:QVariantList{provider,account,link.document,link.line,link.purchase})q.addBindValue(v);
        if(!q.exec())return fail();
    }
    if(!q.exec("RELEASE supplier_stage"))return fail();return true;
}
inline bool auditCopy(const QString& source,const QString& destination,const QString& provider,
                      const QString& account,const QVector<Document>& documents,Plan* plan,QString* error) {
    if(!QFileInfo(source).isFile() || QFileInfo::exists(destination) || source==destination) {
        *error="Choose a new copy file; existing databases cannot be overwritten";return false;
    }
    const auto name="supplier-staging-copy-"+QUuid::createUuid().toString(QUuid::Id128);
    bool ok=false;
    {
        auto input=QSqlDatabase::addDatabase("QSQLITE",name+"-source");
        input.setDatabaseName(source);input.setConnectOptions("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=5000");
        if(input.open()) {
            QSqlQuery q(input);q.prepare("VACUUM INTO ?");q.addBindValue(destination);
            ok=q.exec();if(!ok)*error=q.lastError().text();
        } else *error=input.lastError().text();
        input.close();
    }
    QSqlDatabase::removeDatabase(name+"-source");if(!ok)return false;
    {
        auto copy=QSqlDatabase::addDatabase("QSQLITE",name);copy.setDatabaseName(destination);
        ok=copy.open();if(ok)ok=saveCopy(copy,provider,account,documents,plan,error);else *error=copy.lastError().text();
        copy.close();
    }
    QSqlDatabase::removeDatabase(name);return ok;
}
}
