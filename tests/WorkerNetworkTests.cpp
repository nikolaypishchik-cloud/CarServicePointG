#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include "runtime/WorkerNetwork.h"
class WorkerNetworkTests:public QObject {
 Q_OBJECT
private slots:
 void uiRejected(){QVERIFY(WorkerNetwork::request(QNetworkRequest(QUrl("http://127.0.0.1:1")),{},false,100).stopped);}
 void transport(){
 QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));int requests=0;QByteArray seen;
 connect(&server,&QTcpServer::newConnection,this,[&]{auto*s=server.nextPendingConnection();connect(s,&QTcpSocket::readyRead,s,[&,s]{auto data=s->readAll();seen+=data;if(!data.contains("\r\n\r\n"))return;++requests;if(data.contains("/hang"))return;QByteArray code=data.contains("/error")?"500 Error":"200 OK";s->write("HTTP/1.1 "+code+"\r\nContent-Length: 2\r\nSet-Cookie: session=test; Path=/\r\nConnection: close\r\n\r\nOK");s->disconnectFromHost();});connect(s,&QTcpSocket::disconnected,s,&QObject::deleteLater);});
 QString base=QString("http://127.0.0.1:%1").arg(server.serverPort());
 WorkerNetwork::Response get,post,error,timeout,cancel;QList<QNetworkCookie> cookies;std::atomic_bool finished=false;int beats=0;QTimer heartbeat;connect(&heartbeat,&QTimer::timeout,this,[&]{++beats;});heartbeat.start(10);
 auto*worker=QThread::create([&]{get=WorkerNetwork::request(QNetworkRequest(QUrl(base)),{},false,1000,[]{return true;},&cookies);QNetworkRequest postRequest{QUrl(base)};postRequest.setHeader(QNetworkRequest::ContentTypeHeader,"application/x-www-form-urlencoded");post=WorkerNetwork::request(postRequest,"hello",true,1000,[]{return true;},&cookies);error=WorkerNetwork::request(QNetworkRequest(QUrl(base+"/error")),{},false,1000);timeout=WorkerNetwork::request(QNetworkRequest(QUrl(base+"/hang")),{},false,150);QElapsedTimer t;t.start();cancel=WorkerNetwork::request(QNetworkRequest(QUrl(base+"/hang")),{},false,5000,[&]{return t.elapsed()<100;});finished=true;});worker->start();QTRY_VERIFY_WITH_TIMEOUT(finished.load(),5000);QVERIFY(worker->wait(1000));delete worker;
 QCOMPARE(get.status,200);QCOMPARE(get.body,QByteArray("OK"));QVERIFY(!get.stopped);QCOMPARE(post.status,200);QVERIFY(seen.contains("Cookie: session=test"));QVERIFY(seen.contains("POST /"));QCOMPARE(error.status,500);QVERIFY(error.error!=QNetworkReply::NoError);QVERIFY(timeout.stopped);QVERIFY(cancel.stopped);QVERIFY(beats>10);QCOMPARE(requests,5);
 }
 void cancelledBeforeStart(){bool started=false;auto*w=QThread::create([&]{QVERIFY(!WorkerNetwork::run([&](auto,auto,auto){started=true;},[]{return false;}));});w->start();QVERIFY(w->wait(1000));delete w;QVERIFY(!started);}
};
QTEST_GUILESS_MAIN(WorkerNetworkTests)
#include "WorkerNetworkTests.moc"
