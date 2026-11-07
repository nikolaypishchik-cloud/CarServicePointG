#pragma once
#include "core/services/ElitInvoiceReader.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include "runtime/WorkerNetwork.h"
#include <QTimer>
#include <QElapsedTimer>
#include <QUrlQuery>
#include <QRegularExpression>
#include <QTextDocumentFragment>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextDocument>
#include <QTextTable>

// A fresh cookie jar belongs to each synchronization. Browser cookies are never imported.
namespace ElitCabinet {
using Attributes = QMap<QString,QString>;
inline QVector<Attributes> tags(const QString& html, const QString& tag) {
    QVector<Attributes> result;
    const QRegularExpression element("<"+tag+R"(\b([^>]*?)>)",QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression attribute(R"rx(([\w:-]+)\s*=\s*(?:"([^"]*)"|'([^']*)'))rx");
    auto elements=element.globalMatch(html);
    while(elements.hasNext()) {
        Attributes values; auto attributes=attribute.globalMatch(elements.next().captured(1));
        while(attributes.hasNext()) {
            const auto a=attributes.next();
            values.insert(a.captured(1).toLower(),QTextDocumentFragment::fromHtml(a.captured(2).isNull()?a.captured(3):a.captured(2)).toPlainText());
        }
        result.append(values);
    }
    return result;
}
inline bool allowed(const QUrl& url) {
    if(url.scheme()!="https" || !url.userInfo().isEmpty()) return false;
    return ((url.host()=="www.ecat.ua" || url.host()=="ecat.ua") && url.port(443)==443)
        || (url.host()=="ecabinet.ecat.ua" && url.port()==8451);
}
struct Response { QByteArray body,disposition; QUrl url; QString exportParameter; };
struct PrintJob { bool present=false; QString parameter; };
// Match named columns and immutable invoice identity, never infer payment from omission.
inline bool paymentBalance(const QString& html,const ElitInvoiceReader::Invoice& invoice,qint64* remaining) {
    if(invoice.currency!="UAH" || !invoice.detailsComplete || invoice.grossCents<=0) return false;
    QTextDocument document;document.setHtml(html);
    int matches=0;qint64 value=0;
    std::function<void(QTextFrame*)> visit=[&](QTextFrame* frame) {
        if(auto* table=qobject_cast<QTextTable*>(frame)) {
            QMap<QString,int> columns;
            auto cellText=[&](int row,int column) {
                const auto cell=table->cellAt(row,column);
                auto cursor=cell.firstCursorPosition();
                cursor.setPosition(cell.lastCursorPosition().position(),QTextCursor::KeepAnchor);
                return cursor.selectedText().simplified();
            };
            for(int row=0;row<table->rows();++row) {
                for(int column=0;column<table->columns();++column) {
                    const auto text=cellText(row,column);
                    if(text==QString::fromUtf8("Документ Но.")) columns["number"]=column;
                    if(text==QString::fromUtf8("Дата Учета")) columns["date"]=column;
                    if(text==QString::fromUtf8("Сумма")) columns["amount"]=column;
                    if(text==QString::fromUtf8("Сумма Остатка")) columns["remaining"]=column;
                }
                if(columns.size()!=4 || cellText(row,columns["number"])!=invoice.number) continue;
                ++matches;
                auto amount=[&](int column,qint64* result) {
                    auto text=cellText(row,column);text.remove(' ');
                    if(!QRegularExpression("^[0-9]+[.][0-9]{2}$").match(text).hasMatch())return false;
                    return ElitInvoiceReader::money(text,result);
                };
                qint64 gross=0,balance=0;
                if(QDate::fromString(cellText(row,columns["date"]),"dd.MM.yyyy")!=invoice.posted
                    || !amount(columns["amount"],&gross) || gross!=invoice.grossCents
                    || !amount(columns["remaining"],&balance) || balance>gross) {matches+=2;continue;}
                value=balance;
            }
        }
        for(auto* child:frame->childFrames())visit(child);
    };
    visit(document.rootFrame());
    if(matches!=1)return false;
    *remaining=value;return true;
}
inline PrintJob printJob(const QString& html,const QDate& day) {
    PrintJob result;
    const QRegularExpression row(R"(<tr\b[^>]*>[\s\S]*?</tr>)",QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression cell(R"(<td\b[^>]*>([\s\S]*?)</td>)",QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression date(R"(\b[0-9]{2}\.[0-9]{2}\.[0-9]{2}\b)");
    auto rows=row.globalMatch(html);
    while(rows.hasNext()) {
        const auto content=rows.next().captured();auto cells=cell.globalMatch(content);
        if(!cells.hasNext())continue;cells.next();if(!cells.hasNext())continue;
        const auto description=QTextDocumentFragment::fromHtml(cells.next().captured(1)).toPlainText();
        auto dates=date.globalMatch(description);QStringList period;
        while(dates.hasNext())period.append(dates.next().captured());
        if(period!=QStringList{day.toString("dd.MM.yy"),day.toString("dd.MM.yy")})continue;
        result.present=true;result.parameter.clear();
        for(const auto& a:tags(content,"a"))if(a.value("data-action")=="DownloadInvoice"
            && QRegularExpression("^EntryNo=[0-9]+$").match(a.value("data-action-param")).hasMatch())
                result.parameter=a.value("data-action-param");
    }
    return result;
}
class Client {
    QList<QNetworkCookie> cookies;
    std::function<bool()> current;
    QElapsedTimer total;
    QString cabinetLogin;
    bool transientFailure=false;
public:
    QString error;
    explicit Client(std::function<bool()> stillCurrent):current(std::move(stillCurrent)){total.start();}
    Response request(QUrl url,const QUrlQuery& fields={},bool post=false) {
        const bool readOnly=!post || url.path()=="/Customer/DownloadDocuments"
            || (url.path()=="/Customer/Balance" && QStringList{"Filter","DownloadInvoice"}.contains(fields.queryItemValue("Action")));
        for(int attempt=0;attempt<3;++attempt) {
            error.clear();transientFailure=false;
            const auto response=requestOnce(url,fields,post);
            if(error.isEmpty() || !readOnly || !transientFailure || !current() || total.elapsed()>600000 || attempt==2)return response;
            if(!WorkerNetwork::delay((attempt+1)*1000,current))return {};
        }
        return {};
    }
private:
    Response requestOnce(QUrl url,const QUrlQuery& fields,bool post) {
        for(int redirect=0;redirect<6;++redirect) {
            if(!allowed(url) || !current() || total.elapsed()>600000) {
                error=QString::fromUtf8("PDF Еліт: запит скасовано або адресу відхилено.");return {};
            }
            QNetworkRequest request(url);
            request.setAttribute(QNetworkRequest::Http2AllowedAttribute,false);
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
            request.setHeader(QNetworkRequest::UserAgentHeader,"CarServicePointG");
            request.setHeader(QNetworkRequest::ContentTypeHeader,"application/x-www-form-urlencoded");
            QByteArray encoded;
            for(const auto& field:fields.queryItems(QUrl::FullyDecoded)) {
                if(!encoded.isEmpty())encoded+='&';
                encoded+=QUrl::toPercentEncoding(field.first)+'='+QUrl::toPercentEncoding(field.second);
            }
            const auto reply=WorkerNetwork::request(request,encoded,post,20000,[&]{return current() && total.elapsed()<=600000;},&cookies);
            const auto body=reply.body;
            const auto status=reply.status;
            const auto location=reply.redirect;
            const auto disposition=reply.disposition;
            const int networkError=static_cast<int>(reply.error);
            const bool ok=reply.error==QNetworkReply::NoError && !reply.stopped;
            if(!ok){
                transientFailure=(status==0 || status>=500) && networkError!=QNetworkReply::SslHandshakeFailedError;
                error=QString::fromUtf8("PDF Еліт: помилка мережі/TLS (%1), HTTP %2, етап %3.").arg(networkError).arg(status).arg(url.path());return {};
            }
            if(status>=300 && status<400 && !location.isEmpty()) {
                auto next=url.resolved(location);
                if(!allowed(next) || (post && (next.host()!=url.host() || next.port()!=url.port()))
                    || (post && status!=302 && status!=303)) {
                    error=QString::fromUtf8("PDF Еліт: небезпечне перенаправлення відхилено.");return {};
                }
                url=next;post=false;continue;
            }
            if(status!=200){error=QString::fromUtf8("PDF Еліт: сервер повернув HTTP %1.").arg(status);return {};}
            return {body,disposition,url};
        }
        error=QString::fromUtf8("PDF Еліт: забагато перенаправлень.");return {};
    }
public:
    bool paymentStatus(const ElitInvoiceReader::Invoice& invoice,qint64* remaining) {
        if(!QRegularExpression("^[A-Za-z0-9_-]{1,80}$").match(invoice.number).hasMatch()) {
            error=QString::fromUtf8("Некоректний номер накладної для перевірки оплати.");return false;
        }
        QUrlQuery filter;filter.addQueryItem("Action","Filter");filter.addQueryItem("ActiveField","");
        filter.addQueryItem("ActionParam","DocumentNo="+invoice.number+";PostingDateFrom=;PostingDateTo=;");
        const auto response=request(QUrl("https://ecabinet.ecat.ua:8451/Customer/Balance"),filter,true);
        if(!error.isEmpty())return false;
        if(!paymentBalance(QString::fromUtf8(response.body),invoice,remaining)) {
            error=QString::fromUtf8("Статус оплати %1 не підтверджено: номер, дата, сума або залишок у eCat не пройшли перевірку.").arg(invoice.number);
            return false;
        }
        return true;
    }
    bool login(const GeneralSettingsService::ElitCredentials& credentials) {
        if(credentials.company!="ELIT_UA" || credentials.login.isEmpty() || credentials.password.isEmpty()) {
            error=QString::fromUtf8("PDF кабінету підтримує лише компанію ELIT_UA зі збереженим доступом.");return false;
        }
        auto home=request(QUrl("https://www.ecat.ua/"));if(!error.isEmpty())return false;
        QString token;
        for(const auto& input:tags(QString::fromUtf8(home.body),"input"))
            if(input.value("name")=="__RequestVerificationToken")token=input.value("value");
        if(token.isEmpty()){error=QString::fromUtf8("PDF Еліт: форма входу змінилася (CSRF).");return false;}
        QUrlQuery fields; fields.addQueryItem("UserName",credentials.login);fields.addQueryItem("Password",credentials.password);
        fields.addQueryItem("__RequestVerificationToken",token);
        request(QUrl("https://www.ecat.ua/account/logonnow"),fields,true);if(!error.isEmpty())return false;
        request(QUrl("https://www.ecat.ua/elit/ecabinet/index"));if(!error.isEmpty())return false;
        const auto session=request(QUrl("https://www.ecat.ua/Elit/Ecabinet/Url"),{},true);if(!error.isEmpty())return false;
        const auto data=QJsonDocument::fromJson(session.body).object();
        const QUrl cabinet(data.value("url").toString());
        if(!data.value("errorMessage").toString().isEmpty() || cabinet.host()!="ecabinet.ecat.ua"
            || !allowed(cabinet) || QUrlQuery(cabinet).queryItemValue("login")!=credentials.login) {
            error=QString::fromUtf8("PDF Еліт: вхід у кабінет не підтверджено. Перевірте доступ Maxi ECAT.");return false;
        }
        request(cabinet);if(!error.isEmpty())return false;
        cabinetLogin=credentials.login;
        request(QUrl("https://ecabinet.ecat.ua:8451/Customer/Balance"));return error.isEmpty();
    }
    Response preparedDay(const QDate& day,const QString& outdatedExport={}) {
        if(!day.isValid() || day.year()<2000 || day.year()>2099 || cabinetLogin.contains(';') || cabinetLogin.isEmpty()) {
            error=QString::fromUtf8("PDF Еліт: некоректна дата або сеанс підготовки.");return {};
        }
        QUrlQuery queue;queue.addQueryItem("filter","SpecialPrintingType=2;eCatLogin="+cabinetLogin);
        const QUrl balance("https://ecabinet.ecat.ua:8451/Customer/Balance");
        bool submitted=false;
        for(int attempt=0;attempt<81 && current();++attempt) {
            const auto response=request(QUrl("https://ecabinet.ecat.ua:8451/Customer/DownloadDocuments"),queue,true);
            if(!error.isEmpty())return {};
            const auto html=QString::fromUtf8(response.body);
            bool recognized=false;
            for(const auto& a:tags(html,"input"))if(a.value("id")=="print-waiting"
                && (a.value("value")=="0" || a.value("value")=="1"))recognized=true;
            if(!recognized){error=QString::fromUtf8("PDF Еліт: черга підготовки недоступна або сеанс завершено.");return {};}
            const auto job=printJob(html,day);
            if(!job.parameter.isEmpty() && job.parameter!=outdatedExport) {
                QUrlQuery download;download.addQueryItem("Action","DownloadInvoice");
                download.addQueryItem("ActionParam",job.parameter+";");download.addQueryItem("ActiveField","");
                auto result=request(balance,download,true);result.exportParameter=job.parameter;return result;
            }
            if((!job.present || (!outdatedExport.isEmpty() && job.parameter==outdatedExport)) && !submitted) {
                // This is only the cabinet's PDF export job, never an order/payment action.
                QUrlQuery prepare;prepare.addQueryItem("action","AddPrintJob");
                prepare.addQueryItem("actionParam","DateFrom="+day.toString(Qt::ISODate)+";DateTo="+day.toString(Qt::ISODate)
                    +";DownloadType=pdf;PrintingDocNotReceived=false;PrintingOnlyInvoices=true;Login="+cabinetLogin);
                prepare.addQueryItem("returnEmpty","true");
                const auto accepted=request(balance,prepare,true);submitted=true;
                if(!error.isEmpty())return {};
                if(!accepted.body.trimmed().isEmpty()){error=QString::fromUtf8("PDF Еліт: сервер відхилив підготовку документів.");return {};}
            }
            // Reuse pending jobs after cancellation/restart; never resubmit within a poll.
            if(!WorkerNetwork::delay(3000,current))return {};
        }
        error=QString::fromUtf8("PDF Еліт за %1: сервер ще готує пакет або очікування скасовано. Повторна синхронізація перевірить ту саму чергу.").arg(day.toString("dd.MM.yyyy"));return {};
    }
    Response invoice(const ElitInvoiceReader::Invoice& invoice) {
        // Use the same read-only filter and download actions as the cabinet UI.
        if(!QRegularExpression("^[A-Za-z0-9_-]{1,80}$").match(invoice.number).hasMatch()) {
            error=QString::fromUtf8("PDF Еліт: непідтримуваний номер документа.");return {};
        }
        QUrlQuery filter; filter.addQueryItem("Action","Filter");filter.addQueryItem("ActiveField","");
        filter.addQueryItem("ActionParam","DocumentNo="+invoice.number+";PostingDateFrom=;PostingDateTo=;");
        const QUrl balance("https://ecabinet.ecat.ua:8451/Customer/Balance");
        auto page=request(balance,filter,true);if(!error.isEmpty())return {};
        const QString html=QString::fromUtf8(page.body);
        const QRegularExpression row(R"(<tr\b[^>]*>[\s\S]*?</tr>)",QRegularExpression::CaseInsensitiveOption);
        auto rows=row.globalMatch(html);QString parameter;int found=0;
        while(rows.hasNext()) {
            const auto content=rows.next().captured();bool matching=false;
            for(const auto& a:tags(content,"a")) {
                const QUrl url(a.value("href"));
                const auto f=QUrlQuery(url).queryItemValue("filter",QUrl::FullyDecoded);
                if(url.path()=="/Customer/SalesDocumentDetailsLineLookup" && f=="IleDocTypeF=2;DocumentNoF="+invoice.number)matching=true;
            }
            if(!matching)continue;
            ++found;
            for(const auto& a:tags(content,"a"))if(a.value("data-action")=="DownloadInvoice")parameter=a.value("data-action-param");
        }
        if(found!=1 || !QRegularExpression("^CustLedgEntryNo=[0-9]+;$").match(parameter).hasMatch()) {
            error=QString::fromUtf8("PDF %1: у кабінеті немає однозначного готового файлу для завантаження.").arg(invoice.number);return {};
        }
        QUrlQuery download;download.addQueryItem("Action","DownloadInvoice");download.addQueryItem("ActionParam",parameter+";");
        download.addQueryItem("ActiveField","");
        auto result=request(balance,download,true);
        if(error.isEmpty() && (!result.body.startsWith("%PDF-") || !result.disposition.contains((invoice.number+".pdf").toUtf8()))) {
            error=QString::fromUtf8("PDF %1: сервер повернув інший документ або сторінку входу.").arg(invoice.number);return {};
        }
        return result;
    }
};
}
