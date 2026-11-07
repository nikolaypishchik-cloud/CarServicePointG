#include "ui/common/dialogs/ElitInvoicePdfOpen.h"
#include "core/services/ElitPdfOpenService.h"
#include "runtime/AsyncThreadScope.h"
#include <QDesktopServices>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSet>
#include <QUrl>

void openElitInvoicePdf(QWidget* owner,const QString& identity,std::function<void(const QString&)> preview) {
    if(!owner || identity.isEmpty()) return;
    ElitPdfOpenService::Request request;
    QString error;
    if(!ElitPdfOpenService::prepare(identity,&request,&error)) {
        if(!error.isEmpty()) QMessageBox::warning(owner,QString::fromUtf8("Накладна Еліт"),QString::fromUtf8("Не вдалося прочитати налаштування Еліт."));
        return;
    }
    static QSet<QString> active;
    const auto key=request.databasePath+":"+request.account+":"+identity;
    if(active.contains(key)) return;
    active.insert(key);
    auto cancel=std::make_shared<std::atomic_bool>(false);
    auto result=std::make_shared<ElitPdfOpenService::Result>();
    auto* progress=new QProgressDialog(QString::fromUtf8("Відкриття PDF Еліт. Якщо файла немає, його буде завантажено з eCat…"),QString::fromUtf8("Скасувати"),0,0,owner);
    progress->setObjectName("elitPdfOpenProgress");
    progress->setStyleSheet(QStringLiteral("QProgressDialog#elitPdfOpenProgress QLabel { color: #ffffff; }"));
    progress->setWindowTitle(QString::fromUtf8("Накладна Еліт"));
    progress->setWindowModality(Qt::WindowModal);progress->setMinimumDuration(0);progress->setValue(0);
    QObject::connect(progress,&QProgressDialog::canceled,progress,[cancel]{cancel->store(true);});
    auto* worker=AsyncThreadScope::startWorker(owner,"elit_open_pdf",cancel,[=] {
        *result=ElitPdfOpenService::execute(request,[cancel]{return AsyncThreadScope::shouldContinue(cancel);});
    });
    const QPointer<QWidget> safeOwner(owner);
    const QPointer<QProgressDialog> safeProgress(progress);
    QObject::connect(worker,&QThread::finished,QCoreApplication::instance(),[=] {
        active.remove(key);
        if(safeProgress) {safeProgress->reset();safeProgress->deleteLater();}
        if(!safeOwner || cancel->load() || !ElitPdfOpenService::isCurrent(request)) return;
        if(result->path.isEmpty()) {
            QMessageBox::warning(safeOwner,QString::fromUtf8("Накладна Еліт"),result->error.isEmpty()?QString::fromUtf8("Не вдалося відкрити накладну."):result->error);
        } else if(preview) {
            preview(result->path);
        } else if(!QDesktopServices::openUrl(QUrl::fromLocalFile(result->path))) {
            QMessageBox::warning(safeOwner,QString::fromUtf8("Накладна Еліт"),QString::fromUtf8("PDF збережено, але не вдалося запустити переглядач PDF."));
        }
    });
    worker->start();
}
