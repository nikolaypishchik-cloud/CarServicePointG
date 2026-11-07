#include "OrderClosureFlow.h"
#include "OrderClosureDialog.h"
#include "core/services/orders/OrderClosureApplicationService.h"
#include "core/cloudsync/CloudSyncCredentials.h"
#include "core/cloudsync/CloudSyncSettings.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QMessageBox>
#include <cmath>
#include <QJsonArray>
#include <QLabel>
#include <QVBoxLayout>
#include <QCloseEvent>
#include <QUuid>

namespace {
class Waiting : public QDialog {
public:
    using QDialog::QDialog;
    void reject() override {}
    void closeEvent(QCloseEvent* event) override {event->ignore();}
};
struct Reply {int status=0;QJsonObject body;bool ok=false;};
QString errorMessage(const QJsonObject& body) {
    const auto detail=body["detail"];
    QString message=detail.isObject()?detail.toObject()["message"].toString():detail.toString();
    if(message=="Invalid synchronized amount or quantity; resolve before closing")
        return QString::fromUtf8("Сервер отримав неповні числові дані позиції наряду. Синхронізуйте наряд і повторіть закриття. Якщо помилка повторюється, перевірте кількість і ціну його позицій.");
    return message.isEmpty()?QString::fromUtf8("Не вдалося отримати актуальні дані. Перевірте режим роботи й доступ."):message;
}
Reply call(QWidget* parent,const QString& endpoint,const QJsonObject& payload) {
    if(CloudSyncSettings::localBackendEnabled()) {
        const auto reply=OrderClosureApplicationService::localRequest(endpoint,payload);
        return {reply.status,reply.body,reply.ok};
    }
    if(!CloudSyncSettings::serverRequestsAllowed())return {503,{{"detail",QString::fromUtf8("Перезапустіть застосунок для автономної роботи.")}},false};
    // Use the same protected-token policy as synchronization. Legacy plaintext
    // is usable only after successful DPAPI protection AND persistence.
    const auto settings=CloudSyncSettings::load();
    const auto token=settings.apiToken;
    if(token.isEmpty())return {401,{{"detail",settings.lastError.isEmpty()
        ? QString::fromUtf8("Токен недоступний. Увійдіть до сервера повторно.")
        : settings.lastError}},false};
    QNetworkAccessManager network;
    QNetworkRequest request(QUrl(CloudSyncSettings::fixedServerUrl()+"/api/v1/bays/"+endpoint));
    request.setRawHeader("Authorization","Bearer "+token.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(20000);
    Waiting wait(parent);wait.setWindowTitle(QString::fromUtf8("Закриття наряду"));
    auto* layout=new QVBoxLayout(&wait);
    layout->addWidget(new QLabel(QString::fromUtf8("Очікування сервера…"),&wait));
    auto* reply=network.post(request,QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QObject::connect(reply,&QNetworkReply::finished,&wait,&QDialog::accept);
    wait.exec();
    const auto code=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QJsonParseError parse;const auto doc=QJsonDocument::fromJson(reply->readAll(),&parse);
    return {code,doc.object(),code==200 && reply->error()==QNetworkReply::NoError && parse.error==QJsonParseError::NoError && doc.isObject()};
}
bool matches(const QJsonObject& state,const QJsonObject& preview) {
    if(state.isEmpty() || state["order"].toObject()["status"].toString()!=preview["status"].toString())return false;
    const auto basis=preview["basis"].toObject();
    auto numeric=[](const QJsonValue& a,const QJsonValue& b){bool x,y;double first=a.toVariant().toString().toDouble(&x),second=b.toVariant().toString().toDouble(&y);return x && y && std::isfinite(first) && std::isfinite(second) && qAbs(first-second)<0.00000001;};
    if(!numeric(state["order"].toObject()["discount"],basis["discount"]))return false;
    for(const auto& key:{QString("parts"),QString("services")}) {
        const auto local=state[key].toArray(),remote=basis[key].toArray();if(local.size()!=remote.size())return false;
        for(int i=0;i<local.size();++i) {
            const auto a=local[i].toObject(),b=remote[i].toObject();
            if(a[key=="parts"?"part_uid":"service_uid"]!=b["uid"] || !numeric(a["qty"],b["qty"]) || !numeric(a["price"],b["price"]))return false;
            if(key=="parts" && (a["installed_at"].toString()!=b["installed_at"].toString() || a["closure_issue_evidence"]!=b["closure_issue_evidence"]))return false;
        }
    }
    return true;
}
}
namespace OrderClosureFlow {
void recoverPending(QWidget* parent) {
    QString error;
    const auto orders=OrderClosureApplicationService::pendingOrders(&error);
    if(!error.isEmpty()){QMessageBox::critical(parent,QString::fromUtf8("Відновлення закриття"),error);return;}
    for(const auto& uid:orders)if(!closeOrder(parent,uid))break;
}
bool closeOrder(QWidget* parent,const QString& uid) {
    QString error;
    auto command=OrderClosureApplicationService::pending(uid,&error);
    if(!error.isEmpty()){QMessageBox::critical(parent,QString::fromUtf8("Закриття наряду"),error);return false;}
    if(command.isEmpty()) {
        const auto state=OrderClosureApplicationService::readState(uid,&error);
        if(!error.isEmpty()){QMessageBox::critical(parent,QString::fromUtf8("Закриття наряду"),error);return false;}
        const auto response=call(parent,"closure-preview",{{"order_uid",uid}});
        if(!response.ok){QMessageBox::warning(parent,QString::fromUtf8("Закриття наряду"),errorMessage(response.body));return false;}
        if(!matches(state,response.body)){QMessageBox::warning(parent,QString::fromUtf8("Потрібна синхронізація"),QString::fromUtf8("Дані наряду на сервері та цьому комп’ютері різняться. Синхронізуйте наряд і повторіть закриття."));return false;}
        const auto p=response.body;
        OrderClosurePreview preview{p["order_number"].toString(),p["revision"].toString(),p["total_cents"].toInteger(),p["received_cents"].toInteger(),{}};
        for(const auto& part:p["unresolved_parts"].toArray())preview.unresolvedParts.append(part.toObject()["name"].toString());
        OrderClosureDialog dialog(preview,parent);
        if(dialog.exec()!=QDialog::Accepted)return false;
        bool release=false;
        if(p["occupancy"].toObject()["state"]=="on_bay") {
            const auto answer=QMessageBox::question(parent,QString::fromUtf8("Звільнення поста"),QString::fromUtf8("Автомобіль або агрегат фактично прибрано з поста?"),QMessageBox::Yes|QMessageBox::No|QMessageBox::Cancel,QMessageBox::Cancel);
            if(answer==QMessageBox::Cancel)return false;release=answer==QMessageBox::Yes;
        }
        if(OrderClosureApplicationService::readState(uid,&error)!=state || !error.isEmpty()){QMessageBox::warning(parent,QString::fromUtf8("Дані змінилися"),QString::fromUtf8("Наряд змінено. Повторіть відкриття вікна й підтвердіть нові дані."));return false;}
        const auto choice=dialog.choice();
        command={{"request_uid",QUuid::createUuid().toString(QUuid::WithoutBraces)},{"order_uid",uid},{"expected_revision",choice.expectedRevision},{"release_bay",release},{"payment",QJsonValue(QJsonValue::Null)}};
        // Local intent must never be replayed against the remote authority (or vice versa).
        if(CloudSyncSettings::localBackendEnabled())command["local_authority"]=true;
        if(choice.withPayment)command["payment"]=QJsonObject{{"amount_cents",choice.amountCents},{"method",choice.method},{"paid_on",choice.paidOn.toString(Qt::ISODate)}};
        if(!OrderClosureApplicationService::remember(command,&error)){QMessageBox::critical(parent,QString::fromUtf8("Запис запиту"),error);return false;}
    } else {
        if(QMessageBox::question(parent,QString::fromUtf8("Відновлення закриття"),QString::fromUtf8("Є раніше підтверджений запит закриття. Перевірити його результат і завершити відновлення? Повтор не створить новий платіж."),QMessageBox::Yes|QMessageBox::No,QMessageBox::Yes)!=QMessageBox::Yes)return false;
    }
    for(;;) {
        if(command["local_authority"].toBool()!=CloudSyncSettings::localBackendEnabled()) {
            QMessageBox::warning(parent,QString::fromUtf8("Відновлення закриття"),QString::fromUtf8("Цей запит створено в іншому режимі роботи. Поверніться до нього для перевірки результату; новий платіж не створено."));return false;
        }
        const auto response=call(parent,"closure-commit",command);
        if(response.ok && OrderClosureApplicationService::apply(response.body,&error)) {
            if(!OrderClosureApplicationService::refreshRegistry(uid,&error))
                QMessageBox::warning(parent,QString::fromUtf8("Наряд закрито"),QString::fromUtf8("Закриття збережено, але не вдалося оновити реєстр нарядів. Оновіть звіт.\n")+error);
            QMessageBox::information(parent,QString::fromUtf8("Наряд закрито"),
                QString::fromUtf8("Отримано від клієнта: %1 грн\nЗалишок до сплати: %2 грн")
                .arg(response.body["received_cents"].toInteger()/100.0,0,'f',2)
                .arg(response.body["balance_cents"].toInteger()/100.0,0,'f',2));
            return true;
        }
        if(!response.ok && (response.status==409 || response.status==422)) {
            if(!OrderClosureApplicationService::reject(command["request_uid"].toString(),&error)){QMessageBox::critical(parent,QString::fromUtf8("Відновлення закриття"),error);return false;}
            QMessageBox::warning(parent,QString::fromUtf8("Закриття не виконано"),QString::fromUtf8("Дані змінилися або не пройшли перевірку. Оновіть наряд і підтвердіть нові дані."));return false;
        }
        const QString text=QString::fromUtf8("Результат ще не підтверджено на цьому комп’ютері. Запит збережено для відновлення після перезапуску. Повторити перевірку?")+(error.isEmpty()?QString():"\n"+error);
        if(QMessageBox::warning(parent,QString::fromUtf8("Відновлення закриття"),text,QMessageBox::Retry|QMessageBox::Close,QMessageBox::Retry)!=QMessageBox::Retry)return false;
    }
}
}
