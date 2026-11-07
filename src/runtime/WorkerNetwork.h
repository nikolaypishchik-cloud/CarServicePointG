#pragma once
#include <QCoreApplication>
#include <QThread>
#include <QSemaphore>
#include <QTimer>
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkCookieJar>
#include <QNetworkCookie>
#include <atomic>
#include <functional>
#include <memory>

// Synchronous business workflows may wait ONLY on their background thread.
// Qt network objects live on a dedicated normal event-dispatch thread, never
// in a nested dispatcher on the caller or UI thread.
namespace WorkerNetwork {
inline bool run(const std::function<void(QObject*,std::shared_ptr<std::atomic_bool>,std::function<void()>)>& start,
                const std::function<bool()>& current = []{return true;}) {
    if (!current() || QThread::currentThread()->isInterruptionRequested()) return false;
    if (!QCoreApplication::instance() || QThread::currentThread()==QCoreApplication::instance()->thread()) return false;
    QThread thread;
    auto* context=new QObject;context->moveToThread(&thread);
    auto cancelled=std::make_shared<std::atomic_bool>(false);
    QObject::connect(&thread,&QThread::finished,context,&QObject::deleteLater);
    QObject::connect(&thread,&QThread::started,context,[&thread,context,cancelled,start] {
        start(context,cancelled,[&thread]{thread.quit();});
    });
    QSemaphore stopped;
    QObject::connect(&thread,&QThread::finished,&thread,[&]{stopped.release();},Qt::DirectConnection);
    thread.start();
    while(!stopped.tryAcquire(1,50)) {
        if(QThread::currentThread()->isInterruptionRequested() || !current())cancelled->store(true);
    }
    thread.wait(); // joins an already-finished owned thread; never pumps caller events.
    return true;
}
inline bool delay(int milliseconds,const std::function<bool()>& current) {
    return run([=](QObject* owner,auto cancelled,auto done){
        auto* timer=new QTimer(owner);auto elapsed=std::make_shared<QElapsedTimer>();elapsed->start();
        QObject::connect(timer,&QTimer::timeout,owner,[=]{if(cancelled->load() || elapsed->elapsed()>=milliseconds){timer->stop();done();}});
        timer->start(50);
    },current) && current();
}
struct Response {
    int status=0;
    QByteArray body,disposition;
    QUrl redirect;
    QNetworkReply::NetworkError error=QNetworkReply::UnknownNetworkError;
    QString message;
    bool stopped=true;
};
class CookieJar : public QNetworkCookieJar {
public: using QNetworkCookieJar::allCookies;using QNetworkCookieJar::setAllCookies;
};
inline Response request(const QNetworkRequest& request,const QByteArray& body,bool post,int timeoutMs,
                        const std::function<bool()>& current=[]{return true;},QList<QNetworkCookie>* cookies=nullptr) {
    Response result;
    const bool started=run([&](QObject* owner,auto cancelled,auto done){
        auto* manager=new QNetworkAccessManager(owner);
        auto* jar=new CookieJar; if(cookies)jar->setAllCookies(*cookies);manager->setCookieJar(jar);
        auto* reply=post?manager->post(request,body):manager->get(request);
        auto* timer=new QTimer(owner);auto elapsed=std::make_shared<QElapsedTimer>();elapsed->start();
        auto stopped=std::make_shared<bool>(false);
        QObject::connect(reply,&QNetworkReply::readyRead,owner,[&,reply,stopped]{
            result.body+=reply->readAll();if(result.body.size()>16*1024*1024){*stopped=true;reply->abort();}
        });
        QObject::connect(timer,&QTimer::timeout,owner,[=]{
            if(cancelled->load() || elapsed->elapsed()>=timeoutMs){*stopped=true;reply->abort();}
        });
        QObject::connect(reply,&QNetworkReply::finished,owner,[&,reply,timer,jar,stopped,done]{
            timer->stop();if(reply->isOpen())result.body+=reply->readAll();result.status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            result.redirect=reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
            result.disposition=reply->rawHeader("Content-Disposition");result.error=reply->error();result.message=reply->errorString();
            result.stopped=*stopped || result.body.size()>16*1024*1024;
            if(cookies)*cookies=jar->allCookies();done();
        });
        timer->start(50);
    },current);
    if(!started)result.message=QStringLiteral("Network operation requires a background worker");
    return result;
}
}
