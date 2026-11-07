#include "core/services/ElitInvoicePdf.h"
#include <QtTest>
#include <QUuid>
#include <QPdfWriter>
#include <QPainter>
#include <QThread>
#include <QFontDatabase>

class ElitInvoiceStorageTests : public QObject {
    Q_OBJECT
    QTemporaryDir temp;
    QSqlDatabase db;
    QString connection, source, root;
    QByteArray bytes = "%PDF-1.4\nverified legacy fixture\n%%EOF";
    ElitInvoiceStore::Snapshot snapshot;
    void setRoot(const QString& value) {
        QSqlQuery q(db);
        q.prepare("INSERT OR REPLACE INTO settings(key,value) VALUES('settings.int.directories.invoices_dir',?)");
        q.addBindValue(value);QVERIFY(q.exec());
    }
    QString mapped() { return ElitInvoicePdf::paths(db,"test-account").value("test-document"); }
private slots:
    void paymentStatusAndCachedDetails() {
        ElitInvoiceReader::Invoice invoice;
        invoice.number="KAM26F056797";invoice.currency="UAH";invoice.posted=QDate(2026,10,14);
        invoice.detailsComplete=true;invoice.grossCents=477186;invoice.amountCents=397655;
        invoice.amountVatCents=79531;invoice.remainingCents=187872;
        invoice.lines.append({"TEST","","Part",1,477186});
        const auto header=QString::fromUtf8("<tr><th>Дата Учета</th><th>Документ Но.</th><th>Сумма</th><th>Сумма Остатка</th></tr>");
        const auto row=QString("<tr><td>14.10.2026</td><td><a href='/document'>KAM26F056797</a></td><td>4&nbsp;771.86</td><td>%1</td></tr>");
        auto html=[&](const QString& balance){return "<table>"+header+row.arg(balance)+"</table>";};
        qint64 remaining=-1;
        QVERIFY(ElitCabinet::paymentBalance(html("0.00"),invoice,&remaining));QCOMPARE(remaining,qint64(0));
        QVERIFY(ElitCabinet::paymentBalance(html("1 878.72"),invoice,&remaining));QCOMPARE(remaining,qint64(187872));
        QVERIFY(ElitCabinet::paymentBalance(html("4 771.86"),invoice,&remaining));QCOMPARE(remaining,invoice.grossCents);
        for(const auto& invalid:{html(""),html("0,00"),html("-1.00"),html("9 999.99"),
            html("0.00").replace("14.10.2026","15.10.2026"),html("0.00").replace("771.86","771.85"),
            html("0.00").replace("KAM26F056797","KAM26F056798"),html("0.00")+html("0.00"),QString("<html>Login</html>")})
            QVERIFY(!ElitCabinet::paymentBalance(invalid,invoice,&remaining));
        auto current=invoice;current.lines.clear();current.detailsComplete=false;current.grossCents=0;current.remainingCents=0;
        QVERIFY(ElitInvoiceReader::reuseDetails(&current,invoice));QCOMPARE(current.remainingCents,qint64(0));
        QCOMPARE(current.lines.size(),1);QCOMPARE(current.grossCents,invoice.grossCents);
        current.amountVatCents++;
        QVERIFY(!ElitInvoiceReader::reuseDetails(&current,invoice));
    }
    void missingPdfIsRetriedRegardlessOfInvoiceAgeOrPayment() {
        QSqlQuery q(db);QVERIFY(q.exec("DELETE FROM elit_invoice_pdf_files"));
        for(qint64 remaining:{qint64(0),qint64(49200)}) {
            snapshot.documents[0].invoice.remainingCents=remaining;
            auto result=ElitInvoicePdf::synchronize(db,{},snapshot,[]{return true;});
            QCOMPARE(result.skipped,0);QCOMPARE(result.downloaded,0);
            QCOMPARE(result.missing,1);QVERIFY(!result.errors.isEmpty());
        }
        snapshot.documents[0].isNew=true;
        snapshot.documents[0].missingFromLatest=true;
        QCOMPARE(ElitInvoicePdf::synchronize(db,{},snapshot,[]{return true;}).missing,1);
        snapshot.documents[0].missingFromLatest=false;
        const auto fresh=ElitInvoicePdf::synchronize(db,{},snapshot,[]{return true;});
        QCOMPARE(fresh.skipped,0);QCOMPARE(fresh.missing,1); // New document reaches credential validation.
        QVERIFY(!fresh.errors.isEmpty());
        snapshot.documents[0].isNew=false;
        const auto manual=ElitInvoicePdf::synchronize(db,{},snapshot,[]{return true;});
        QCOMPARE(manual.skipped,0);QCOMPARE(manual.missing,1);
    }
    void init() {
        QVERIFY(temp.isValid());
        connection=QUuid::createUuid().toString(QUuid::WithoutBraces);
        db=QSqlDatabase::addDatabase("QSQLITE",connection);
        db.setDatabaseName(temp.filePath(connection+".sqlite"));QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE settings(key TEXT PRIMARY KEY,value TEXT)"));
        QVERIFY(q.exec("CREATE TABLE payments(id INTEGER PRIMARY KEY,amount INTEGER)"));
        QVERIFY(q.exec("INSERT INTO payments VALUES(1,11900)"));
        QVERIFY(q.exec("CREATE TABLE elit_invoice_pdf_files(account TEXT,invoice_id TEXT,file_path TEXT,sha256 TEXT,PRIMARY KEY(account,invoice_id))"));
        source=temp.filePath(connection+"-legacy.pdf");
        QFile f(source);QVERIFY(f.open(QIODevice::WriteOnly));QCOMPARE(f.write(bytes),bytes.size());f.close();
        q.prepare("INSERT INTO elit_invoice_pdf_files VALUES(?,?,?,?)");
        q.addBindValue("test-account");q.addBindValue("test-document");q.addBindValue(source);q.addBindValue(ElitInvoicePdf::digest(bytes));QVERIFY(q.exec());
        root=temp.filePath(connection+"-configured-invoices");setRoot(root);
        snapshot={};snapshot.account="test-account";
        ElitInvoiceStore::Document doc;doc.identity="test-document";doc.invoice.number="KAM26F057048";
        doc.invoice.posted=QDate(2026,10,30);
        snapshot.documents.append(doc);
    }
    void cleanup() {
        QSqlQuery q(db);QVERIFY(q.exec("SELECT amount FROM payments WHERE id=1"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),11900);
        q.finish();q=QSqlQuery();db.close();db=QSqlDatabase();QSqlDatabase::removeDatabase(connection);
    }
    void cachedFilesArchivedWithoutNetworkAndPreserved() {
        const auto result=ElitInvoicePdf::synchronize(db,{},snapshot,[]{return true;});
        QVERIFY2(result.errors.isEmpty(),qPrintable(result.errors.join("; ")));
        QCOMPARE(result.existing,1);QCOMPARE(result.downloaded,0);QCOMPARE(result.missing,0);
        QCOMPARE(QFileInfo(mapped()).absolutePath(),QDir(root).absolutePath());
        QCOMPARE(QFileInfo(mapped()).fileName(),QString::fromUtf8("Еліт_KAM26F057048_30.10.2026.pdf"));
        QVERIFY(QFileInfo::exists(source));
        QFile f(mapped());QVERIFY(f.open(QIODevice::ReadOnly));QCOMPARE(f.readAll(),bytes);f.close();
        const auto first=mapped();
        const auto repeat=ElitInvoicePdf::synchronize(db,{},snapshot,[]{return true;});
        QCOMPARE(repeat.existing,1);QCOMPARE(mapped(),first);
        QCOMPARE(QDir(root).entryList({"*.pdf"},QDir::Files).size(),1);
        setRoot(root+"-changed");
        QCOMPARE(ElitInvoicePdf::synchronize(db,{},snapshot,[]{return true;}).existing,1);
        QCOMPARE(QFileInfo(mapped()).absolutePath(),QDir(root+"-changed").absolutePath());
        QVERIFY(QFileInfo::exists(first));
    }
    void absentOrInvalidRootDoesNotFallback() {
        setRoot("");
        auto result=ElitInvoicePdf::synchronize(db,{},snapshot,[]{return true;});
        QCOMPARE(result.missing,1);QCOMPARE(result.existing,0);QVERIFY(!result.errors.isEmpty());QCOMPARE(mapped(),source);
        setRoot(source); // A file cannot serve as the configured directory.
        result=ElitInvoicePdf::synchronize(db,{},snapshot,[]{return true;});
        QCOMPARE(result.missing,1);QVERIFY(!result.errors.isEmpty());QCOMPARE(mapped(),source);
    }
    void cancelledLeavesIndexUntouched() {
        const auto result=ElitInvoicePdf::synchronize(db,{},snapshot,[]{return false;});
        QVERIFY(result.cancelled);QCOMPARE(mapped(),source);QVERIFY(!QDir(root).exists());
    }
    void conflictingFileIsNeverOverwritten() {
        QString error;
        const auto doc=snapshot.documents.first();
        QVERIFY(ElitInvoicePdf::archiveFile(db,snapshot.account,doc,source,ElitInvoicePdf::digest(bytes),[]{return true;},&error));
        const QString target=mapped();
        QFile other(target);QVERIFY(other.open(QIODevice::WriteOnly));other.write("manual attachment");other.close();
        QVERIFY(ElitInvoicePdf::archiveFile(db,snapshot.account,doc,source,ElitInvoicePdf::digest(bytes),[]{return true;},&error));
        QVERIFY(mapped()!=target);QVERIFY(other.open(QIODevice::ReadOnly));QCOMPARE(other.readAll(),QByteArray("manual attachment"));
    }
    void validatedDownloadUsesConfiguredRoot() {
        const QString pdfPath=temp.filePath(connection+"-download.pdf");
        {
            QPdfWriter writer(pdfPath);QPainter painter(&writer);
#ifdef Q_OS_WIN
            const int fontId=QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR")+"/Fonts/arial.ttf");
            QVERIFY(fontId>=0);
            painter.setFont(QFont(QFontDatabase::applicationFontFamilies(fontId).first(),12));
#endif
            painter.drawText(100,300,"Invoice KAM26F057048 Date: 23.10.2026 Total: 298.50 UAH");painter.end();
        }
        QFile input(pdfPath);QVERIFY(input.open(QIODevice::ReadOnly));const auto download=input.readAll();input.close();
        auto doc=snapshot.documents.first();doc.invoice.posted=QDate(2026,10,23);doc.invoice.grossCents=29850;
        const QString databasePath=db.databaseName();QString error;bool saved=false;
        auto* worker=QThread::create([&] {
            const QString name=connection+"-worker";
            {
                auto local=QSqlDatabase::addDatabase("QSQLITE",name);local.setDatabaseName(databasePath);
                if(local.open())saved=ElitInvoicePdf::save(local,snapshot.account,doc,download,[]{return true;},&error);
                local.close();
            }
            QSqlDatabase::removeDatabase(name);
        });
        worker->start();worker->wait();delete worker;
        QVERIFY2(saved,qPrintable(error));
        QCOMPARE(QFileInfo(mapped()).absolutePath(), QDir(root).absolutePath());
        QCOMPARE(QFileInfo(mapped()).fileName(), QString::fromUtf8("Еліт_KAM26F057048_23.10.2026.pdf"));
        QFile archived(mapped());QVERIFY(archived.open(QIODevice::ReadOnly));QCOMPARE(archived.readAll(),download);
    }
};
QTEST_MAIN(ElitInvoiceStorageTests)
#include "ElitInvoiceStorageTests.moc"
