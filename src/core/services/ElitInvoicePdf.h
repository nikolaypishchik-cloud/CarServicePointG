#pragma once
#include "core/services/ElitCabinetClient.h"
#include "core/services/ElitInvoiceStore.h"
#include "core/services/ElitPdfArchive.h"
#include "core/import/InvoiceImportEngine.h"
#include "core/services/storage/StorageDirectoriesGuard.h"
#include <QCryptographicHash>
#include <QSqlQuery>
#include <QSqlError>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QCoreApplication>

namespace ElitInvoicePdf {
inline QString digest(const QByteArray& data){return QString::fromLatin1(QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex());}
inline QString invoiceRoot(QSqlDatabase db,QString* error) {
    QSqlQuery query(db);
    query.prepare("SELECT value FROM settings WHERE key='settings.int.directories.invoices_dir'");
    if(!query.exec()){*error=query.lastError().text();return {};}
    const QString root=query.next()?query.value(0).toString().trimmed():QString();
    if(!StorageDirectoriesGuard::validateCandidateDirectory(root,StorageDirectoriesGuard::labelForKind(StorageDirectoriesGuard::StorageKind::Invoice),error))return {};
    return root;
}
inline bool archiveFile(QSqlDatabase db,const QString& account,const ElitInvoiceStore::Document& document,
                        const QString& source,const QString& hash,const std::function<bool()>& current,QString* error) {
    if(!current()){*error=QString::fromUtf8("Завантаження PDF скасовано.");return false;}
    const QString root=invoiceRoot(db,error);
    if(root.isEmpty())return false;
    // The storage copy verifies content and preserves conflicting documents.
    const QString name=QString::fromUtf8("Еліт_%1_%2.pdf")
        .arg(document.invoice.number,document.invoice.posted.toString("dd.MM.yyyy"));
    const QString path=StorageDirectoriesGuard::copyFileToManagedStorageAsAtRoot(root,
        StorageDirectoriesGuard::StorageKind::Invoice,0,"elit",name,source,error);
    if(path.isEmpty())return false;
    if(!current()){*error=QString::fromUtf8("Завантаження PDF скасовано.");return false;}
    QSqlQuery q(db);
    if(!q.exec("CREATE TABLE IF NOT EXISTS elit_invoice_pdf_files(account TEXT NOT NULL,invoice_id TEXT NOT NULL,file_path TEXT NOT NULL,sha256 TEXT NOT NULL,PRIMARY KEY(account,invoice_id))")){*error=q.lastError().text();return false;}
    q.prepare("INSERT INTO elit_invoice_pdf_files(account,invoice_id,file_path,sha256) VALUES(?,?,?,?) ON CONFLICT(account,invoice_id) DO UPDATE SET file_path=excluded.file_path,sha256=excluded.sha256");
    q.addBindValue(account);q.addBindValue(document.identity);q.addBindValue(path);q.addBindValue(hash);
    if(!q.exec()){*error=q.lastError().text();return false;}return true;
}
inline QHash<QString,QString> paths(QSqlDatabase db,const QString& account) {
    QHash<QString,QString> result;
    if(!db.tables().contains("elit_invoice_pdf_files"))return result;
    QSqlQuery q(db);q.prepare("SELECT invoice_id,file_path,sha256 FROM elit_invoice_pdf_files WHERE account=?");q.addBindValue(account);
    if(q.exec())while(q.next()) {
        QFile file(q.value(1).toString());
        if(file.size()>16*1024*1024 || !file.open(QIODevice::ReadOnly))continue;
        if(digest(file.readAll())==q.value(2).toString())result.insert(q.value(0).toString(),q.value(1).toString());
    }
    return result;
}
inline bool matchesText(QString text,const ElitInvoiceReader::Invoice& invoice) {
    text=text.simplified();
    const auto number=QRegularExpression("(?<![A-Za-z0-9])"+QRegularExpression::escape(invoice.number)+"(?![A-Za-z0-9])");
    const QStringList months={QString::fromUtf8("січня"),QString::fromUtf8("лютого"),QString::fromUtf8("березня"),QString::fromUtf8("квітня"),QString::fromUtf8("травня"),QString::fromUtf8("червня"),QString::fromUtf8("липня"),QString::fromUtf8("серпня"),QString::fromUtf8("вересня"),QString::fromUtf8("жовтня"),QString::fromUtf8("листопада"),QString::fromUtf8("грудня")};
    if(!invoice.posted.isValid() || invoice.grossCents<=0 || !number.match(text).hasMatch())return false;
    const QString date=QString::number(invoice.posted.day())+" "+months.at(invoice.posted.month()-1)+" "+QString::number(invoice.posted.year());
    if(!text.contains(date) && !text.contains(invoice.posted.toString("dd.MM.yyyy")))return false;
    QString compact=text;compact.remove(' ');compact.replace(',','.');
    const auto amount=QRegularExpression("(?<![0-9.])"+QRegularExpression::escape(QString::number(invoice.grossCents/100.0,'f',2))+"(?![0-9])");
    return amount.match(compact).hasMatch();
}
inline bool save(QSqlDatabase db,const QString& account,const ElitInvoiceStore::Document& document,
                 const QByteArray& bytes,const std::function<bool()>& current,QString* error) {
    if(!db.isOpen() || QFileInfo(db.databaseName()).canonicalFilePath().isEmpty()) {
        *error=QString::fromUtf8("PDF Еліт: база недоступна.");return false;
    }
    if(!current() || bytes.size()>16*1024*1024 || !bytes.startsWith("%PDF-")){*error=QString::fromUtf8("PDF Еліт: файл відхилено.");return false;}
    QTemporaryDir temp;
    if(!temp.isValid()){*error=QString::fromUtf8("PDF Еліт: тимчасове сховище недоступне.");return false;}
    const QString temporary=temp.filePath("invoice.pdf");QFile file(temporary);
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size()){*error=file.errorString();return false;}file.close();
    if(!InvoiceImport::validateInvoicePdf(temporary,error))return false;
    const auto text=InvoiceImport::invoicePdfText(temporary,error);
    if(!matchesText(text,document.invoice)){*error=QString::fromUtf8("PDF Еліт: номер, дата або сума не відповідають накладній.");return false;}
    if(!current()){*error=QString::fromUtf8("Завантаження PDF скасовано.");return false;}
    return archiveFile(db,account,document,temporary,digest(bytes),current,error);
}
struct Result { int downloaded=0,existing=0,missing=0; QStringList errors; bool cancelled=false; int skipped=0; };
inline Result synchronize(QSqlDatabase db,const GeneralSettingsService::ElitCredentials& credentials,
                          const ElitInvoiceStore::Snapshot& snapshot,const std::function<bool()>& current) {
    Result result;const auto cached=paths(db,snapshot.account);QVector<ElitInvoiceStore::Document> pending;
    for(const auto& doc:snapshot.documents) {
        if(!current()){result.cancelled=true;return result;}
        if(!cached.contains(doc.identity)){
            // Metadata synchronization does not guarantee that a PDF was saved.
            // Retry missing/deleted/corrupt files, including settled and retained invoices.
            pending.append(doc);
            continue;
        }
        QFile file(cached.value(doc.identity));QString error;
        if(!file.open(QIODevice::ReadOnly))error=file.errorString();
        else if(archiveFile(db,snapshot.account,doc,file.fileName(),digest(file.readAll()),current,&error)) {++result.existing;continue;}
        ++result.missing;if(result.errors.size()<5)result.errors.append(error);
    }
    if(pending.isEmpty())return result;
    QString storageError;
    if(invoiceRoot(db,&storageError).isEmpty()) {result.missing+=pending.size();result.errors.append(storageError);return result;}
    ElitCabinet::Client client(current);
    if(!client.login(credentials)){result.missing+=pending.size();result.errors.append(client.error);result.cancelled=!current();return result;}
    QMap<QDate,QVector<ElitInvoiceStore::Document>> preparation;
    for(int i=0;i<pending.size();++i) {
        if(!current()){result.cancelled=true;result.missing+=pending.size()-i;break;}
        client.error.clear();const auto response=client.invoice(pending[i].invoice);QString error=client.error;
        if(error.isEmpty() && save(db,snapshot.account,pending[i],response.body,current,&error))++result.downloaded;
        else preparation[pending[i].invoice.posted].append(pending[i]);
    }
    for(auto it=preparation.cbegin();it!=preparation.cend();++it) {
        if(!current()) {result.cancelled=true;result.missing+=it.value().size();continue;}
        client.error.clear();auto response=client.preparedDay(it.key());QString error=client.error;
        QMap<QString,QByteArray> files;QStringList numbers;
        for(const auto& doc:it.value())numbers.append(doc.invoice.number);
        auto readFiles=[&] {
            files.clear();
            if(response.body.startsWith("%PDF-")) {
                for(const auto& number:numbers)if(response.disposition.contains(("filename="+number+".pdf").toUtf8())
                    || response.disposition.contains(("filename=\""+number+".pdf\"").toUtf8()))files.insert(number,response.body);
            } else files=ElitPdfArchive::unpack(response.body,numbers,current,&error);
        };
        if(error.isEmpty())readFiles();
        if(error.isEmpty() && files.size()<numbers.size() && !response.exportParameter.isEmpty() && current()) {
            // A completed same-day export may predate a newly posted invoice.
            // Refresh it once, never repeatedly submit a missing/failed document.
            client.error.clear();response=client.preparedDay(it.key(),response.exportParameter);error=client.error;
            if(error.isEmpty())readFiles();
        }
        for(const auto& doc:it.value()) {
            QString documentError=error;
            if(files.contains(doc.invoice.number) && save(db,snapshot.account,doc,files.value(doc.invoice.number),current,&documentError))++result.downloaded;
            else {
                ++result.missing;
                if(documentError.isEmpty())documentError=QString::fromUtf8("PDF %1: у підготовленому пакеті документа немає.").arg(doc.invoice.number);
                if(result.errors.size()<5)result.errors.append(documentError);
            }
        }
    }
    result.cancelled=result.cancelled || !current();
    return result;
}
}
