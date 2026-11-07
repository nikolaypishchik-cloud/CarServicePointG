#include "core/services/ElitStartupSync.h"
#include "core/services/ElitSyncVoice.h"
#include "core/services/ElitInvoicePdf.h"
#include "core/services/SupplierInvoiceLedger.h"
#include "core/services/AccessControlService.h"
#include "core/services/AppRuntimeService.h"
#include "core/services/SupplierDebtReconciliationService.h"
#include "infrastructure/db/sqlite/Db.h"
#include "runtime/AsyncThreadScope.h"
#include "runtime/WorkerNetwork.h"
#include <QTimer>

namespace ElitStartupSync {
namespace { bool running=false; bool started=false; std::weak_ptr<std::atomic_bool> activeCancellation; }
bool isRunning() { return running; }
void cancel() { if(auto token=activeCancellation.lock()) token->store(true); }
void start(QObject* owner,const std::function<void(bool,const QString&)>& finished,
           const std::function<void(const QString&)>& progress) {
    if(started || !owner || qEnvironmentVariable("CS_ELIT_DISABLE_STARTUP")=="1") return;
    if(owner->findChild<QTimer*>("elitStartupRetry",Qt::FindDirectChildrenOnly)) return;
    auto* timer=new QTimer(owner);timer->setObjectName("elitStartupRetry");timer->setInterval(10000);
    const auto attempt=[=] {
        if(started || RuntimeJob::RuntimeJobDispatcher::instance().isShutdownRequested()) {
            timer->stop();timer->deleteLater();return;
        }
        GeneralSettingsService::ElitCredentials credentials;
        QString reason;
        if(!SupplierDebtReconciliationService::canReconcile(Database::db()))
            reason=QString::fromUtf8("Еліт: автоматична синхронізація очікує входу адміністратора або власника.");
        else if(!GeneralSettingsService::loadElitCredentials(&credentials)
                || credentials.login.trimmed().isEmpty() || credentials.password.isEmpty())
            reason=QString::fromUtf8("Еліт: для автоматичної синхронізації збережіть логін і пароль в Інтеграціях.");
        if(!reason.isEmpty()) {
            if(timer->property("reason").toString()!=reason && progress) progress(reason);
            timer->setProperty("reason",reason);return;
        }
        if(request(owner,finished,progress)) {timer->stop();timer->deleteLater();}
    };
    QObject::connect(timer,&QTimer::timeout,owner,attempt);
    timer->start();attempt();
}
bool request(QObject* owner,const std::function<void(bool,const QString&)>& finished,
             const std::function<void(const QString&)>& progress) {
    if(!owner) return false;
    if(running) { finished(false,QString::fromUtf8("Синхронізація Еліт уже виконується.")); return false; }
    auto rejected=[&](const QString& message){ElitSyncVoice::play(ElitSyncVoice::Outcome::Error);finished(false,message);return false;};
    auto mainDb=Database::db();
    if(!SupplierDebtReconciliationService::canReconcile(mainDb)) {
        return rejected(QString::fromUtf8("Недостатньо прав для синхронізації Еліт."));
    }
    GeneralSettingsService::ElitCredentials credentials;
    QString error;
    if(!GeneralSettingsService::loadElitCredentials(&credentials,&error)) {
        return rejected(QString::fromUtf8("Еліт: не вдалося прочитати збережений доступ."));
    }
    if(credentials.login.trimmed().isEmpty() || credentials.password.isEmpty()) {
        return rejected(QString::fromUtf8("Вкажіть логін та збережіть пароль у налаштуваннях API Еліт."));
    }
    started=true; running=true;
    const auto account=ElitInvoiceStore::accountKey(credentials);
    const auto scope=AccessControlService::currentScopeKey();
    const auto databasePath=mainDb.databaseName();
    if(progress) progress(QString::fromUtf8("Еліт: фонова синхронізація розпочата. Отримання накладних…"));
    const QPointer<QObject> safeOwner(owner);
    const auto report=[safeOwner,scope,progress](const QString& message) {
        QMetaObject::invokeMethod(QCoreApplication::instance(),[=] {
            if(safeOwner && scope==AccessControlService::currentScopeKey() && progress) progress(message);
        },Qt::QueuedConnection);
    };
    auto cancel=std::make_shared<std::atomic_bool>(false);
    activeCancellation=cancel;
    struct Outcome { bool success=false; QString message; ElitInvoiceReader::Failure failure=ElitInvoiceReader::Failure::Other; };
    auto outcome=std::make_shared<Outcome>();
    auto* worker=AsyncThreadScope::startWorker(owner,"elit_startup_sync",cancel,
        [=] {
        struct Cleanup { ~Cleanup(){ AppRuntimeService::closeThreadConnection(); } } cleanup;
        QString dbError;
        auto db=Database::db(&dbError);
        if(!dbError.isEmpty() || db.databaseName()!=databasePath) {
            outcome->message=QString::fromUtf8("Еліт: база даних недоступна або змінилася."); return;
        }
        auto current=[&] {
            if(cancel->load() || RuntimeJob::RuntimeJobDispatcher::instance().isShutdownRequested()
                || scope!=AccessControlService::currentScopeKey()) return false;
            GeneralSettingsService::ElitCredentials now; QString credentialError;
            return GeneralSettingsService::loadElitCredentials(&now,&credentialError)
                && ElitInvoiceStore::accountKey(now)==account && now.password==credentials.password;
        };
        QString error;
        const auto previous=ElitInvoiceStore::load(db,account,&error);
        if(!error.isEmpty()) { outcome->message=error; return; }
        if(!current()) { outcome->message=QString::fromUtf8("Синхронізацію Еліт скасовано."); return; }
        ElitInvoiceReader::Options options; options.details=true;
        for(const auto& document:previous.documents) {
            options.knownNumbers.insert(document.invoice.number);
            options.cachedInvoices.insert(document.invoice.number,document.invoice);
        }
        ElitInvoiceReader::Result fetched;
        WorkerNetwork::run([&](QObject* networkOwner,auto cancelled,auto finish) {
            auto* cancellation=new QTimer(networkOwner);
            auto* job=ElitInvoiceReader::start(networkOwner,credentials,[&,cancellation,finish](auto result){
                fetched=std::move(result);cancellation->stop();finish();
            },options);
            QObject::connect(cancellation,&QTimer::timeout,networkOwner,[job,cancelled]{if(cancelled->load())job->cancel();});
            cancellation->start(50);
        },current);
        if(!fetched.complete) {
            outcome->message=fetched.error;
            outcome->failure=fetched.failure;
            return;
        }
        // SOAP can omit settled invoices. Check their explicit cabinet balance without downloading documents.
        QSet<QString> received;
        for(const auto& invoice:fetched.invoices)received.insert(invoice.number);
        int cabinetChecked=0,cabinetUnconfirmed=0;
        QString cabinetError;
        QVector<ElitInvoiceReader::Invoice> omitted;
        for(const auto& document:previous.documents)
            if(!received.contains(document.invoice.number))omitted.append(document.invoice);
        if(!omitted.isEmpty()) {
            report(QString::fromUtf8("Еліт: перевірка оплати %1 збережених накладних у eCat, без завантаження документів…").arg(omitted.size()));
            ElitCabinet::Client cabinet(current);
            if(cabinet.login(credentials)) {
                for(auto invoice:omitted) {
                    if(!current()) {outcome->message=QString::fromUtf8("Перевірку оплат Еліт скасовано.");return;}
                    qint64 remaining=0;
                    if(cabinet.paymentStatus(invoice,&remaining)) {
                        invoice.remainingCents=remaining;
                        fetched.invoices.append(invoice);++cabinetChecked;
                    } else {
                        ++cabinetUnconfirmed;
                        if(cabinetError.isEmpty())cabinetError=cabinet.error;
                    }
                }
            } else {cabinetUnconfirmed=omitted.size();cabinetError=cabinet.error;}
        }
        if(!current()) {outcome->message=QString::fromUtf8("Синхронізацію Еліт скасовано.");return;}
        fetched.fetchedAt=QDateTime::currentDateTime();
        int newDocuments=0;
        for(const auto& invoice:fetched.invoices)if(!options.cachedInvoices.contains(invoice.number))++newDocuments;
        int linked=0,unresolved=0;
        outcome->success=SupplierInvoiceLedger::publishAndLink(db,account,fetched,previous.revision,current,&linked,&unresolved,&error);
        outcome->message=outcome->success
            ? QString::fromUtf8("Еліт: перевірено %1 накладних; нових документів: %4; нових прив’язок: %2; потребують звірки: %3. Локальні оплати не змінено.")
                .arg(fetched.invoices.size()).arg(linked).arg(unresolved).arg(newDocuments)
            : error;
        if(outcome->success) {
            outcome->message+=QString::fromUtf8("\nПеревірено статус у eCat: %1; не підтверджено: %2.").arg(cabinetChecked).arg(cabinetUnconfirmed);
            if(!cabinetError.isEmpty())outcome->message+=QStringLiteral("\n")+cabinetError;
            report(outcome->message+QString::fromUtf8(" Завантаження відсутніх PDF із eCat…"));
            const auto snapshot=ElitInvoiceStore::load(db,account,&error);
            if(!error.isEmpty()){outcome->success=false;outcome->message+=QStringLiteral("\n")+error;return;}
            int retained=0;
            for(const auto& document:snapshot.documents) if(document.missingFromLatest) ++retained;
            if(retained) outcome->message+=QString::fromUtf8("\nЗбережено %1 накладних, відсутніх у поточному списку Еліт. Їхній статус оплати не підтверджено; попередні залишки збережено.").arg(retained);
            const auto pdf=ElitInvoicePdf::synchronize(db,credentials,snapshot,current);
            outcome->message+=QString::fromUtf8("\nPDF: завантажено %1; уже збережено %2; недоступно %3.")
                .arg(pdf.downloaded).arg(pdf.existing).arg(pdf.missing);
            if(pdf.missing || pdf.cancelled) {
                outcome->success=false;
                outcome->failure=pdf.cancelled?ElitInvoiceReader::Failure::Cancelled:ElitInvoiceReader::Failure::Incomplete;
                outcome->message+=QStringLiteral("\n")+pdf.errors.join(QStringLiteral("\n"));
            }
            if(cabinetUnconfirmed) {
                outcome->success=false;
                outcome->failure=ElitInvoiceReader::Failure::Incomplete;
            }
        }
    });
    QObject::connect(worker,&QThread::finished,QCoreApplication::instance(),[=] {
        running=false; activeCancellation.reset();
        if(!safeOwner || scope!=AccessControlService::currentScopeKey()) return;
        GeneralSettingsService::ElitCredentials now;
        if(!RuntimeJob::RuntimeJobDispatcher::instance().isShutdownRequested()
            && GeneralSettingsService::loadElitCredentials(&now)
            && ElitInvoiceStore::accountKey(now)==account) {
            // A cancellation arriving after commit must not announce a rolled-back run.
            ElitSyncVoice::play(ElitSyncVoice::classify(outcome->success,cancel->load(),outcome->failure));
        }
        finished(outcome->success,outcome->message.isEmpty()
            ? QString::fromUtf8("Синхронізацію Еліт перервано. Попередні дані збережено.") : outcome->message);
    });
    worker->start();
    return true;
}
}
