#include "core/services/AppRuntimeService.h"
#include "ui/tabs/settings/SupplierConnectionPage.h"
#include "core/cloudsync/CloudSyncCredentials.h"
#include "core/services/SupplierInvoiceStaging.h"
#include <QFileDialog>
#include <QThread>
#include <QPointer>
#include <QCheckBox>
#include <QDate>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPushButton>
#include <QSettings>
#include <QTextEdit>
#include <QUrlQuery>
#include <QUuid>
#include <QVBoxLayout>

namespace {
class SupplierConnectionPage final : public QWidget {
public:
    SupplierConnectionPage(QString supplier,QWidget* parent):QWidget(parent),id(std::move(supplier)) {
        setObjectName("supplierConnection_"+id);
        auto* layout=new QVBoxLayout(this);layout->setSpacing(12);
        auto* form=new QFormLayout;form->setSpacing(10);layout->addLayout(form);
        login=new QLineEdit(this);secret=new QLineEdit(this);secret->setEchoMode(QLineEdit::Password);
        form->addRow(id=="utr"?tr("Логін (email)"):tr("Назва облікового запису"),login);
        form->addRow(id=="utr"?tr("Пароль"):tr("Ключ доступу"),secret);
        auto* show=new QCheckBox(tr("Показати пароль / ключ"),this);layout->addWidget(show);
        connect(show,&QCheckBox::toggled,this,[this](bool on){secret->setEchoMode(on?QLineEdit::Normal:QLineEdit::Password);});
        remember=new QCheckBox(tr("Зберігати пароль / ключ на цьому комп’ютері"),this);layout->addWidget(remember);
        currency=new QCheckBox(tr("Підтверджую: накладні цього облікового запису Омега у гривнях (UAH)"),this);
        currency->setVisible(id=="omega");layout->addWidget(currency);
        order=new QLineEdit(this);
        if(id=="utr"){form->addRow(tr("Замовлення (необов’язково)"),order);order->setPlaceholderText(tr("Порожньо: усі замовлення поточного місяця"));}
        else order->hide();
        auto* note=new QLabel(id=="utr"?tr("Читання номерів накладних за замовленням. Оплати та робоча база не змінюються.")
            :tr("BM Parts: накладні поточного місяця. Омега: останні 30 днів. Сторінки завантажуються послідовно. Перевірка прив’язок виконується тільки на новій копії БД; оплати не змінюються."),this);
        note->setWordWrap(true);layout->addWidget(note);
        auto* save=new QPushButton(tr("Зберегти доступ"),this);layout->addWidget(save);
        check=new QPushButton(tr("Перевірити доступ та отримати документи"),this);layout->addWidget(check);
        audit=new QPushButton(tr("Перевірити прив’язки на новій копії БД"),this);audit->setEnabled(false);layout->addWidget(audit);
        output=new QTextEdit(this);output->setReadOnly(true);layout->addWidget(output,1);
        manager=new QNetworkAccessManager(this);
        QString error;const auto stored=QSettings().value(key()).toString();
        if(!stored.isEmpty()) {
            const auto data=QJsonDocument::fromJson(CloudSyncCredentials::unprotectToken(stored,&error).toUtf8()).object();
            login->setText(data.value("login").toString());secret->setText(data.value("secret").toString());remember->setChecked(data.value("remember").toBool());
            fingerprint=data.value("fingerprint").toString();
            currency->setChecked(data.value("currency_uah_confirmed").toBool());
        }
        if(fingerprint.isEmpty())fingerprint=QUuid::createUuid().toString(QUuid::Id128);
        if(!error.isEmpty())output->setPlainText(tr("Не вдалося прочитати збережений доступ. Введіть його повторно."));
        connect(save,&QPushButton::clicked,this,[this]{
            QString error;const QJsonObject data{{"login",login->text().trimmed()},{"secret",remember->isChecked()?secret->text():QString()},
                {"remember",remember->isChecked()},{"fingerprint",fingerprint},{"currency_uah_confirmed",currency->isChecked()}};
            const auto encrypted=CloudSyncCredentials::protectToken(QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact)),&error);
            if(encrypted.isEmpty()){QMessageBox::warning(this,tr("Доступ"),tr("Не вдалося захистити дані доступу."));return;}
            QSettings settings;settings.setValue(key(),encrypted);settings.sync();
            QMessageBox::information(this,tr("Доступ"),settings.status()==QSettings::NoError?tr("Доступ збережено."):tr("Не вдалося зберегти доступ."));
        });
        connect(check,&QPushButton::clicked,this,[this]{start();});
        connect(audit,&QPushButton::clicked,this,[this]{
            const auto destination=QFileDialog::getSaveFileName(this,tr("Нова тестова копія БД"),"supplier-audit.sqlite","SQLite (*.sqlite)");
            if(destination.isEmpty())return;
            const auto source=AppRuntimeService::activeDatabasePath();const auto provider=id;const auto account=loadedAccount;const auto documents=staged;
            audit->setEnabled(false);check->setEnabled(false);QPointer<SupplierConnectionPage> self(this);
            auto* worker=QThread::create([self,source,destination,provider,account,documents]{
                SupplierInvoiceStaging::Plan plan;QString error;
                const bool ok=SupplierInvoiceStaging::auditCopy(source,destination,provider,account,documents,&plan,&error);
                if(self)QMetaObject::invokeMethod(self,[self,ok,plan,error,destination]{if(!self)return;
                    self->audit->setEnabled(true);self->check->setEnabled(true);
                    const auto message=ok?tr("Тестова копія: %1\nОднозначних прив’язок: %2. Потребують перевірки: %3.\nРобочу БД та оплати не змінено.").arg(destination).arg(plan.links.size()).arg(plan.unresolved):error;
                    self->output->setPlainText(message);
                    QMessageBox::information(self,tr("Перевірка на копії"),message);
                },Qt::QueuedConnection);
            });
            connect(worker,&QThread::finished,worker,&QObject::deleteLater);worker->start();
        });
    }
private:
    QString key()const{return "integrations/suppliers/"+id+"/credentials_dpapi";}
    void fail(const QString& message){check->setEnabled(true);audit->setEnabled(false);staged.clear();token.clear();accessSecret.clear();output->setPlainText(message);QMessageBox::warning(this,tr("Підключення постачальника"),message);}
    void request(const QUrl& url,const QJsonObject& body={},bool authentication=false) {
        QNetworkRequest req(url);req.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
        req.setRawHeader("Accept","application/json");req.setTransferTimeout(30000);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
        if(id=="bm")req.setRawHeader("Authorization",accessSecret.toUtf8());
        if(id=="utr" && !authentication)req.setRawHeader("Authorization","Bearer "+token.toUtf8());
        auto* reply=body.isEmpty()?manager->get(req):manager->post(req,QJsonDocument(body).toJson(QJsonDocument::Compact));
        connect(reply,&QNetworkReply::readyRead,reply,[reply]{if(reply->bytesAvailable()>8*1024*1024)reply->abort();});
        connect(reply,&QNetworkReply::finished,this,[this,reply,authentication]{
            const int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const auto bytes=reply->readAll();const auto networkError=reply->error();reply->deleteLater();
            if(networkError!=QNetworkReply::NoError || status<200 || status>=300){fail(tr("Доступ не підтверджено. HTTP %1. Перевірте ключ, логін, пароль, права доступу та з’єднання.").arg(status));return;}
            QJsonParseError error;const auto doc=QJsonDocument::fromJson(bytes,&error);
            if(error.error!=QJsonParseError::NoError){fail(tr("Сервер повернув відповідь не у форматі JSON."));return;}
            if(authentication) {
                token=doc.object().value("token").toString();
                if(token.isEmpty()){fail(tr("Сервер не видав токен авторизації."));return;}
                if(orderCode.isEmpty()){utrListing=true;currentPage=1;fetchUtrOrders();}
                else {utrOrders.append(orderCode);fetchUtrInvoice();}return;
            }
            QJsonArray rows;
            if(utrListing) {
                if(!doc.isArray()){fail(tr("Некоректний список замовлень Юнік Трейд."));return;}
                if(doc.array().isEmpty()){utrListing=false;fetchUtrInvoice();return;}
                for(const auto& value:doc.array()) {
                    const auto code=value.toObject().value("externalCode").toString();
                    if(code.isEmpty() || utrOrders.contains(code)){fail(tr("Повторена сторінка замовлень Юнік Трейд."));return;}
                    utrOrders.append(code);
                }
                ++currentPage;fetchUtrOrders();return;
            }
            if(detailIndex>=0) {
                if(!doc.object().value("Success").toBool() || !doc.object().value("Data").isArray()){fail(tr("Не вдалося отримати товари накладної Омега."));return;}
                auto header=collected[detailIndex].toObject();header.insert("Products",doc.object().value("Data"));collected[detailIndex]=header;
                ++detailIndex;fetchDetail();return;
            }
            if(id=="bm" && doc.object().value("documents").isArray())rows=doc.object().value("documents").toArray();
            else if(id=="omega" && doc.object().value("Success").toBool() && doc.object().value("Data").toObject().value("Result").isArray())rows=doc.object().value("Data").toObject().value("Result").toArray();
            else if(id=="utr" && doc.isArray())rows=doc.array();
            else {fail(tr("Відповідь не підтверджує доступ до документів. Перевірте права API або актуальність його протоколу."));return;}
            for(const auto& value:rows) {
                const auto object=value.toObject();const auto key=SupplierInvoiceStaging::scalar(object.value(id=="bm"?"document_uuid":id=="omega"?"Id":"accountingNumber"));
                if(id=="utr" && seen.contains(key)) {
                    bool identical=false;for(const auto& old:collected)if(old==value){identical=true;break;}
                    if(identical)continue;
                }
                if(key.isEmpty() || seen.contains(key)){fail(tr("Повторена сторінка або некоректний ідентифікатор накладної. Імпорт зупинено."));return;}
                seen.insert(key);collected.append(value);
            }
            if(id=="bm" && !rows.isEmpty()){++currentPage;fetchPage();return;}
            if(id=="omega") {
                const auto total=doc.object().value("Data").toObject().value("Total");
                if(!total.isDouble() || total.toDouble()<collected.size()){fail(tr("Некоректна кількість накладних Омега."));return;}
                if(collected.size()<total.toInt()) {
                    if(rows.isEmpty()){fail(tr("Неповний список накладних Омега."));return;}
                    ++currentPage;fetchPage();return;
                }
                detailIndex=0;fetchDetail();return;
            }
            if(id=="utr"){++utrIndex;fetchUtrInvoice();return;}
            finish();
        });
    }
    void start() {
        accessSecret=secret->text();orderCode=order->text().trimmed();
        confirmedCurrency=currency->isChecked()?QString("UAH"):QString();
        if(accessSecret.isEmpty() || (id=="utr" && login->text().trimmed().isEmpty())) {fail(tr("Заповніть дані доступу та необхідні параметри."));return;}
        loadedAccount=QString::fromLatin1(QCryptographicHash::hash((id+"\n"+login->text().trimmed()+"\n"+accessSecret).toUtf8(),QCryptographicHash::Sha256).toHex());
        collected={};seen.clear();staged.clear();detailIndex=-1;currentPage=id=="bm"?1:0;
        utrOrders.clear();utrIndex=0;utrListing=false;
        audit->setEnabled(false);check->setEnabled(false);output->setPlainText(tr("Отримання документів…"));
        if(id=="bm" || id=="omega")fetchPage();
        else {QUrl url("https://order24-api.utr.ua/api/login_check");QUrlQuery query;query.addQueryItem("browser_fingerprint",fingerprint);url.setQuery(query);
            request(url,{{"email",login->text().trimmed()},{"password",accessSecret}},true);}
    }
    void fetchPage() {
        if(currentPage>100 || collected.size()>10000){fail(tr("Перевищено безпечний обсяг завантаження. Дані не застосовано."));return;}
        if(id=="bm")request(QUrl("https://api.bm.parts/documents/sales_with_products?period=month&per_page=100&page="+QString::number(currentPage)));
        else request(QUrl("https://public.omega.page/public/api/v1.0/expense/getExpenseDocumentList"),
            {{"Key",accessSecret},{"StartDate",QDate::currentDate().addDays(-29).toString("dd.MM.yyyy")},{"EndDate",QDate::currentDate().toString("dd.MM.yyyy")},{"Index",currentPage}});
    }
    void fetchUtrOrders() {
        if(currentPage>100){fail(tr("Перевищено обсяг замовлень Юнік Трейд. Імпорт зупинено."));return;}
        const auto today=QDate::currentDate();
        request(QUrl("https://order24-api.utr.ua/api/order-list/"+QDate(today.year(),today.month(),1).toString(Qt::ISODate)+"?page="+QString::number(currentPage)+"&limit=100&mode=period"));
    }
    void fetchUtrInvoice() {
        if(utrIndex>=utrOrders.size()){finish();return;}
        if(collected.size()>10000){fail(tr("Перевищено обсяг накладних Юнік Трейд."));return;}
        request(QUrl("https://order24-api.utr.ua/api/accounting_numbers_by_order/"+QString::fromLatin1(QUrl::toPercentEncoding(utrOrders[utrIndex]))));
    }
    void fetchDetail() {
        if(detailIndex>=collected.size()){detailIndex=-1;finish();return;}
        request(QUrl("https://public.omega.page/public/api/v1.0/expense/getExpenseDocumentDetails"),
            {{"Key",accessSecret},{"DocId",collected[detailIndex].toObject().value("Id")}});
    }
    void finish() {
        QString error;
        if(!SupplierInvoiceStaging::parse(id,collected,&staged,&error,confirmedCurrency)){fail(error);return;}
        token.clear();accessSecret.clear();check->setEnabled(true);audit->setEnabled(!staged.isEmpty());
        int complete=0;for(const auto& document:staged)if(document.complete)++complete;
        output->setPlainText(tr("Отримано накладних: %1. Готові до перевірки прив’язок: %2.\nОплати невідомі та не змінюються. Прив’язки створюються лише в окремій тестовій копії.\nОмега: потрібне підтвердження валюти. Юнік Трейд: потрібні товарні рядки саме накладної, а не замовлення.").arg(staged.size()).arg(complete));
    }
    QString id,fingerprint,token,accessSecret,orderCode,confirmedCurrency;
    QJsonArray collected;QSet<QString> seen;int currentPage=0,detailIndex=-1;QString loadedAccount;
    QStringList utrOrders;int utrIndex=0;bool utrListing=false;
    QVector<SupplierInvoiceStaging::Document> staged;
    QLineEdit *login,*secret,*order;QCheckBox *remember,*currency;QPushButton *check,*audit;QTextEdit* output;QNetworkAccessManager* manager;
};
}
QWidget* buildSupplierConnectionPage(const QString& supplier,QWidget* parent){return new SupplierConnectionPage(supplier,parent);}
