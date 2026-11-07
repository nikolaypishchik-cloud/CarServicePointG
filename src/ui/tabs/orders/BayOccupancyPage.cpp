#include "core/services/settings/UiStateSettingsService.h"
#include "core/services/orders/OrderClosureApplicationService.h"
#include "ui/tabs/orders/BayOccupancyPage.h"
#include "ui/common/dialogs/OrderClosureFlow.h"
#include "ui/common/dialogs/PersonnelDialogStyle.h"
#include "core/cloudsync/CloudSyncSettings.h"
#include "core/services/orders/LocalWorkshopBackend.h"
#include "core/cloudsync/CloudSyncCredentials.h"
#include <QComboBox>
#include <QDateEdit>
#include <QDateTimeEdit>
#include <QLineEdit>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTableWidget>
#include <QUuid>
#include <QVBoxLayout>
#include <QTimer>
#include <QSettings>
#include <QApplication>
#include <QScrollArea>
#include <QScrollBar>

namespace {
class CommandDialog final : public QDialog {
public:
    using QDialog::QDialog;
    std::function<bool()> mayClose;
    void reject() override { if(!mayClose || mayClose())QDialog::reject(); }
};
QString trText(const char* text) { return QString::fromUtf8(text); }
QTableWidget* table(QWidget* parent, const QStringList& headers) {
    auto* result = new QTableWidget(0, headers.size(), parent);
    result->setHorizontalHeaderLabels(headers);
    result->setEditTriggers(QAbstractItemView::NoEditTriggers);
    result->setProperty("allowContextRowRemoval",false);
    result->setSelectionBehavior(QAbstractItemView::SelectRows);
    result->verticalHeader()->hide();
    result->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    result->horizontalHeader()->setStretchLastSection(false);
    return result;
}
void retainColumns(QTableWidget* table, const QList<int>& widths) {
    auto* header=table->horizontalHeader();
    const QString key="ui/bayOccupancy/"+table->objectName()+"/header";
    for(int column=0;column<widths.size();++column) header->resizeSection(column,widths[column]);
    const auto saved=QSettings().value(key).toByteArray();
    if(!saved.isEmpty()) header->restoreState(saved);
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setStretchLastSection(false);
    auto save=[header,key]{QSettings().setValue(key,header->saveState());};
    QObject::connect(header,&QHeaderView::sectionResized,table,save);
    QObject::connect(header,&QHeaderView::sectionMoved,table,save);
}
void row(QTableWidget* table, const QStringList& cells) {
    const int r = table->rowCount(); table->insertRow(r);
    for (int c=0; c<cells.size(); ++c) table->setItem(r,c,new QTableWidgetItem(cells[c]));
}
QString hours(qint64 ms) { return QString::number(ms / 3600000.0, 'f', 2); }
QString percent(std::optional<double> value) { return value ? QString::number(*value, 'f', 1)+" %" : trText("Немає даних"); }
QString date(qint64 ms) { return QDateTime::fromMSecsSinceEpoch(ms,QTimeZone("Europe/Kyiv")).toString("dd.MM.yyyy HH:mm:ss"); }
QString orderStatus(const QString& value) {
    static const QMap<QString,QString> labels={{"open",trText("Відкритий")},{"in_progress",trText("У роботі")},
        {"closed",trText("Завершений")},{"cancelled",trText("Скасований")},{"canceled",trText("Скасований")},
        {"archived",trText("В архіві")},{"new",trText("Новий")},{"waiting",trText("Очікування")}};
    return labels.value(value,trText("Стан не уточнено"));
}
QString actorName(const QString& actor) {
    if(actor=="owner" || actor.startsWith("owner-"))return trText("Власник");
    if(actor=="admin" || actor.startsWith("admin-"))return trText("Адміністратор");
    if(actor=="system" || actor=="bay-authority")return trText("Система");
    if(actor.isEmpty())return trText("Не вказано");
    if(CloudSyncSettings::localBackendEnabled())return actor;
    return trText("Користувач сервера");
}
QString describeChange(const QJsonObject& object,const QMap<QString,QString>& bays,const QMap<QString,QString>& numbers) {
    QStringList lines;
    if(object.contains("provisioned")) {
        QStringList names;for(const auto& id:object["provisioned"].toArray())names<<bays.value(id.toString(),trText("Пост %1").arg(id.toString()));
        lines<<trText("Створено: %1").arg(names.join(", "));
    }
    if(object.contains("order_uid"))lines<<trText("Наряд: %1").arg(numbers.value(object["order_uid"].toString(),trText("номер недоступний")));
    if(!object["bay_uid"].isNull() && object.contains("bay_uid"))lines<<bays.value(object["bay_uid"].toString(),trText("Пост не уточнено"));
    if(object["state"].isObject())lines<<describeChange(object["state"].toObject(),bays,numbers);
    else if(object.contains("state"))lines<<(object["state"]=="on_bay"?trText("На посту"):
        object["state"]=="off_bay"?trText("Поза постом"):trText("Розміщення ще не підтверджено"));
    if(object.contains("status"))lines<<orderStatus(object["status"].toString());
    if(object.contains("coverage"))lines<<trText("Попередня зайнятість невідома");
    if(object.contains("confirmed_from"))lines<<trText("Облік підтверджено з %1").arg(date(object["confirmed_from"].toInteger()));
    if(object.contains("begin_ms"))lines<<trText("Початок: %1").arg(date(object["begin_ms"].toInteger()));
    if(object.contains("end_ms"))lines<<trText("Завершення: %1").arg(object["end_ms"].isNull()?trText("ще на посту"):date(object["end_ms"].toInteger()));
    if(object["interval"].isObject())lines<<describeChange(object["interval"].toObject(),bays,numbers);
    for(const auto& order:object["orders"].toArray())lines<<describeChange(order.toObject(),bays,numbers);
    if(object.contains("released"))lines<<(object["released"].toBool()?trText("Пост звільнено"):trText("Пост не звільняли"));
    for(const auto& value:object["calendars"].toArray()) {
        const auto calendar=value.toObject();QStringList days,shifts;
        const QStringList weekdays={trText("Пн"),trText("Вт"),trText("Ср"),trText("Чт"),trText("Пт"),trText("Сб"),trText("Нд")};
        for(const auto& day:calendar["weekdays"].toArray())if(day.toInt()>=1 && day.toInt()<=7)days<<weekdays[day.toInt()-1];
        for(const auto& value:calendar["shifts"].toArray()) {const auto shift=value.toArray();if(shift.size()==2)shifts<<shift[0].toString()+" - "+shift[1].toString();}
        lines<<trText("Графік з %1 до %2: %3; %4").arg(calendar["from"].toString(),calendar["until"].isNull()?trText("без кінцевої дати"):calendar["until"].toString(),days.join(", "),shifts.join(", "));
        for(const auto& value:calendar["unavailable"].toArray()) {const auto period=value.toObject();lines<<trText("Пост недоступний: %1 - %2; %3").arg(date(period["begin_ms"].toInteger()),date(period["end_ms"].toInteger()),period["reason"].toString());}
    }
    lines.removeDuplicates();
    return lines.isEmpty()?trText("Немає запису"):lines.join("\n");
}

// Rebuilding rows must not reset the user's selection, heights or scroll position.
class PreserveTableView {
    QTableWidget* view;
    int horizontal,vertical,column;
    QString current;
    QSet<QString> selected;
    QMap<QString,int> heights;
public:
    explicit PreserveTableView(QTableWidget* table):view(table),horizontal(table->horizontalScrollBar()->value()),
        vertical(table->verticalScrollBar()->value()),column(table->currentColumn()) {
        for(int r=0;r<view->rowCount();++r)if(auto* item=view->item(r,0)) {
            const auto key=item->data(Qt::UserRole+1).toString();heights[key]=view->rowHeight(r);
            if(view->selectionModel()->isRowSelected(r,{}))selected.insert(key);
            if(r==view->currentRow())current=key;
        }
        view->setUpdatesEnabled(false);
    }
    ~PreserveTableView() {
        for(int r=0;r<view->rowCount();++r)if(auto* item=view->item(r,0)) {
            const auto key=item->data(Qt::UserRole+1).toString();
            if(heights.contains(key))view->setRowHeight(r,heights.value(key));
            if(selected.contains(key))view->selectionModel()->select(view->model()->index(r,0),QItemSelectionModel::Select|QItemSelectionModel::Rows);
            if(!current.isEmpty() && key==current)view->selectionModel()->setCurrentIndex(view->model()->index(r,qMax(0,column)),QItemSelectionModel::NoUpdate);
        }
        view->horizontalScrollBar()->setValue(horizontal);view->verticalScrollBar()->setValue(vertical);
        view->setUpdatesEnabled(true);
    }
};
}

BayOccupancyPage::BayOccupancyPage(QWidget* parent) : QWidget(parent), network(new QNetworkAccessManager(this)) {
    setObjectName("bayOccupancyPage");
    const bool dark=qApp->property("carservice.graphiteThemeActive").toBool();
    setStyleSheet(QStringLiteral(
        "QWidget#bayOccupancyPage,QWidget#bayReportPage,QWidget#bayHistoryPage { background:transparent; }"
        "QTabWidget#bayOccupancyTabs,QTabWidget#bayOccupancyTabs QTabBar { background:transparent; border:0; color:%1; }"
        "QTabWidget#bayOccupancyTabs::pane { border:0; background:transparent; }"
        "QTabWidget#bayOccupancyTabs QTabBar::tab { color:%1; }"
        "QTabWidget#bayOccupancyTabs QTabBar::tab:selected { color:%1; background:%2; }"
        "QLabel { color:%1; background:transparent; }")
        .arg(dark ? "#ffffff" : "#101418",dark ? "#354b5b" : "#e5edf5"));
    auto* layout = new QVBoxLayout(this);
    auto* actions = new QHBoxLayout;
    reload = new QPushButton(trText("Оновити з сервера"),this);
    if(CloudSyncSettings::localBackendEnabled())reload->setText(trText("Оновити локальні дані"));
    provision = new QPushButton(trText("Створити три пости"),this);
    change = new QPushButton(trText("Змінити фактичний пост наряду"),this);change->setObjectName("changeActualBay");
    retry = new QPushButton(trText("Повторити непідтверджений запит"),this);
    for (auto* button : {reload,provision,change,retry}) {
        button->setProperty("stay_in_detail_menu",true); actions->addWidget(button);
    }
    layout->addLayout(actions);
    status = new QLabel(trText("Дані ще не отримано. Потрібне з’єднання із сервером."),this);
    if(CloudSyncSettings::localBackendEnabled())status->setText(trText("Автономний режим. Завантаження локальних даних…"));
    status->setWordWrap(true); status->setObjectName("bayOccupancyStatus");status->setAlignment(Qt::AlignLeft|Qt::AlignTop);
    auto* statusArea=new QScrollArea(this);statusArea->setObjectName("bayStatusArea");
    statusArea->setWidget(status);statusArea->setWidgetResizable(true);statusArea->setFrameShape(QFrame::NoFrame);
    statusArea->setStyleSheet("QScrollArea#bayStatusArea,QScrollArea#bayStatusArea > QWidget { background:transparent; border:0; }");
    statusArea->setFixedHeight(status->fontMetrics().lineSpacing()*3+4);layout->addWidget(statusArea);
    auto* tabs = new QTabWidget(this); layout->addWidget(tabs,1);
    tabs->setObjectName("bayOccupancyTabs");
    tabs->setDocumentMode(true);
    resources = table(this,{trText("Пост"),trText("Фактичний стан"),trText("Наряд"),trText("Початок обліку")});
    resources->setObjectName("bayResourcesTable"); tabs->addTab(resources,trText("Поточна зайнятість"));
    auto* reportPage = new QWidget(this); auto* reportLayout = new QVBoxLayout(reportPage);
    reportPage->setObjectName("bayReportPage");
    auto* dates = new QHBoxLayout;
    const auto today = QDateTime::currentDateTimeUtc().toTimeZone(QTimeZone("Europe/Kyiv")).date();
    from = new QDateEdit(today.addDays(-6),this); to = new QDateEdit(today,this);
    for (auto* edit : {from,to}) { edit->setCalendarPopup(true); edit->setDisplayFormat("dd.MM.yyyy"); }
    dates->addWidget(new QLabel(trText("З:"),this)); dates->addWidget(from);
    dates->addWidget(new QLabel(trText("По:"),this)); dates->addWidget(to);
    bayFilter=new QComboBox(this);bayFilter->addItem(trText("Усі пости"),QString());dates->addWidget(bayFilter);dates->addStretch();
    reportLayout->addLayout(dates);
    report = table(this,{trText("Пост"),trText("За графіком,\nгод"),trText("Час з обліком,\nгод"),trText("Зайнято, год"),trText("Вільно, год"),trText("Час без обліку,\nгод"),trText("Завантаження,\n%"),trText("Повнота обліку,\n%"),trText("Поза графіком,\nгод")});
    report->horizontalHeaderItem(6)->setToolTip(trText("Частка зайнятого часу серед годин, за які є підтверджені дані."));
    report->horizontalHeaderItem(7)->setToolTip(trText("Частка робочого часу, за яку відома зайнятість поста."));
    report->setObjectName("bayLoadTable"); reportLayout->addWidget(report);
    tabs->addTab(reportPage,trText("Звіт завантаження"));
    auto* historyPage=new QWidget(this);auto* historyLayout=new QVBoxLayout(historyPage);
    historyPage->setObjectName("bayHistoryPage");
    auto* historyActions=new QHBoxLayout;
    correct=new QPushButton(trText("Виправити вибраний інтервал"),this);correct->setObjectName("correctBayHistory");
    calendar=new QPushButton(trText("Календар і недоступність поста"),this);calendar->setObjectName("editBayCalendar");
    for(auto* button:{correct,calendar}) {button->setProperty("stay_in_detail_menu",true);historyActions->addWidget(button);}
    historyLayout->addLayout(historyActions);
    history = table(this,{trText("Пост"),trText("Наряд"),trText("Початок (Київ)"),trText("Завершення (Київ)"),trText("Календарний час на посту, год"),trText("Хто підтвердив")});
    history->setObjectName("bayHistoryTable");historyLayout->addWidget(history);
    tabs->addTab(historyPage,trText("Історія зайнятості"));
    audit=table(this,{trText("Коли записано"),trText("Коли відбулася зміна"),trText("Хто змінив"),trText("Причина"),trText("Було"),trText("Стало")});
    audit->setObjectName("bayAuditTable");tabs->addTab(audit,trText("Журнал змін"));
    retainColumns(resources,{180,180,160,440});
    retainColumns(report,{180,150,150,150,150,150,160,150,180});
    retainColumns(history,{180,140,210,210,240,180});
    retainColumns(audit,{170,170,180,180,360,360});
    connect(correct,&QPushButton::clicked,this,[this]{correctHistory();});
    connect(calendar,&QPushButton::clicked,this,[this]{editCalendar();});
    connect(reload,&QPushButton::clicked,this,[this]{refresh();});
    connect(change,&QPushButton::clicked,this,[this]{changeOrder();});
    connect(retry,&QPushButton::clicked,this,[this]{request(pendingEndpoint,pendingPayload);});
    connect(provision,&QPushButton::clicked,this,[this]{
        if (QMessageBox::question(this,trText("Три робочі пости"),trText("Створити два автомобільні пости та пост ДВЗ/КПП? Графік: Пн–Сб, 08:00–12:00 і 13:00–18:00. Історичну зайнятість не буде створено."),QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)==QMessageBox::Yes)
            command("provision",{});
    });
    connect(from,&QDateEdit::dateChanged,this,[this]{render();});
    connect(to,&QDateEdit::dateChanged,this,[this]{render();});
    connect(bayFilter,&QComboBox::currentIndexChanged,this,[this]{render();});
    updateEnabled();
}

void BayOccupancyPage::open(QWidget* parent, const QString& orderUid, int preferredPost) {
    CommandDialog dialog(parent); dialog.setWindowTitle(trText("Фактична зайнятість і завантаження постів"));
    auto* layout = new QVBoxLayout(&dialog); auto* page = new BayOccupancyPage(&dialog); layout->addWidget(page);
    dialog.mayClose=[page]{return page->allowClose();};
    page->selectedOrder=orderUid;page->preferredPost=preferredPost;page->offerOrderChange=!orderUid.isEmpty();
    const auto available = dialog.screen()->availableGeometry();
    dialog.resize(qRound(available.width()*0.78),qRound(available.height()*0.70));
    dialog.move(available.center()-dialog.rect().center());
    QTimer::singleShot(0,page,[page]{page->refresh();});
    dialog.exec();
}

bool BayOccupancyPage::closeOrders(QWidget* parent, const QStringList& orderUids, const QString& targetStatus) {
    if(targetStatus=="closed") {
        for(const auto& uid:orderUids)if(!OrderClosureFlow::closeOrder(parent,uid))return false;
        BaySnapshot::refresh(parent,{});
        return !orderUids.isEmpty();
    }
    CommandDialog dialog(parent);dialog.setWindowTitle(trText("Закриття наряду та фактичний пост"));
    auto* layout=new QVBoxLayout(&dialog);auto* page=new BayOccupancyPage(&dialog);layout->addWidget(page);
    dialog.mayClose=[page]{return page->allowClose();};
    page->closingOrders=orderUids;page->closingStatus=targetStatus;
    auto* cancel=new QPushButton(trText("Повернутися"),&dialog);layout->addWidget(cancel);
    connect(cancel,&QPushButton::clicked,&dialog,&QDialog::reject);
    const auto size=dialog.screen()->availableGeometry().size();dialog.resize(size.width()*9/10,size.height()*8/10);
    QTimer::singleShot(0,page,[page]{page->refresh();});dialog.exec();
    if(page->closureConfirmed)BaySnapshot::refresh(parent,{});
    return page->closureConfirmed;
}

bool BayOccupancyPage::allowClose() {
    if(busy) {status->setText(trText("Дочекайтеся відповіді сервера. Закриття вікна не скасовує вже надіслану команду."));return false;}
    if(pendingEndpoint.isEmpty())return true;
    return QMessageBox::warning(this,trText("Непідтверджена операція"),
        trText("Сервер міг уже зберегти операцію. Рекомендовано повторити запит із тим самим ID. Закрити вікно без підтвердження? Після відновлення зв’язку обов’язково оновіть дані."),
        QMessageBox::Yes|QMessageBox::No,QMessageBox::No)==QMessageBox::Yes;
}

void BayOccupancyPage::confirmClosure() {
    if(closingOrders.isEmpty() || !fresh || busy || !pendingEndpoint.isEmpty())return;
    QJsonArray entries;
    for(const auto& uid:closingOrders) {
        QJsonObject order;
        for(const auto value:raw.value("orders").toArray())if(value.toObject().value("uid").toString()==uid)order=value.toObject();
        if(order.isEmpty() || order.value("revision").toString().isEmpty()) {status->setText(trText("Спочатку синхронізуйте наряд із сервером. Закриття не виконано."));return;}
        const auto state=data.states.value(uid).toObject();bool release=false;
        if(state.value("state")=="on_bay") {
            QMessageBox question(QMessageBox::Question,trText("Звільнення поста"),trText("Наряд №%1. Автомобіль або агрегат фактично прибрано з поста?").arg(order.value("number").toString()),QMessageBox::NoButton,this);
            auto* yes=question.addButton(trText("Так, звільнити пост"),QMessageBox::YesRole);
            question.addButton(trText("Ні, залишається на посту"),QMessageBox::NoRole);
            auto* back=question.addButton(trText("Повернутися"),QMessageBox::RejectRole);question.setDefaultButton(back);question.setEscapeButton(back);question.exec();
            if(question.clickedButton()==back || !question.clickedButton())return;
            release=question.clickedButton()==yes;
        }
        entries.append(QJsonObject{{"order_uid",uid},{"expected_order_revision",order.value("revision")},{"expected_version",state.value("version").toInt(0)},{"release",release}});
    }
    command("close-orders",{{"orders",entries},{"target_status",closingStatus}});
}

void BayOccupancyPage::updateEnabled() {
    const bool unresolved = !pendingEndpoint.isEmpty();
    reload->setEnabled(!busy); retry->setVisible(unresolved); retry->setEnabled(!busy && unresolved);
    provision->setEnabled(closingOrders.isEmpty() && !busy && fresh && !unresolved && data.bays.isEmpty());
    change->setEnabled(closingOrders.isEmpty() && !busy && fresh && !unresolved && !data.bays.isEmpty());
    resources->setEnabled(closingOrders.isEmpty() && !busy && fresh && !unresolved);
    const bool canCorrect=closingOrders.isEmpty() && !busy && fresh && !unresolved && raw.value("can_correct").toBool();
    correct->setEnabled(canCorrect);calendar->setEnabled(canCorrect);
}

void BayOccupancyPage::refresh() { request("snapshot"); }
void BayOccupancyPage::command(const QString& endpoint, QJsonObject payload) {
    if (busy || !fresh || !pendingEndpoint.isEmpty()) return;
    payload.insert("request_uid",QUuid::createUuid().toString(QUuid::WithoutBraces));
    pendingEndpoint = endpoint; pendingPayload = payload; request(endpoint,payload);
}
void BayOccupancyPage::request(const QString& endpoint, QJsonObject payload) {
    if (busy || endpoint.isEmpty()) return;
    if(CloudSyncSettings::localBackendEnabled()) {
        const auto reply=OrderClosureApplicationService::localRequest(endpoint,payload);
        if(!reply.ok) {
            pendingEndpoint.clear();pendingPayload={};fresh=false;
            status->setText(reply.body["detail"].toString());updateEnabled();return;
        }
        if(endpoint=="snapshot") {
            if(displaySnapshot(reply.body) && offerOrderChange){offerOrderChange=false;QTimer::singleShot(0,this,[this]{changeOrder();});}
            if(fresh && !closingOrders.isEmpty())QTimer::singleShot(0,this,[this]{confirmClosure();});
        } else {
            pendingEndpoint.clear();pendingPayload={};
            if(endpoint=="close-orders"){displaySnapshot(reply.body["snapshot"].toObject());closureConfirmed=true;if(auto* d=qobject_cast<QDialog*>(parentWidget()))d->accept();}
            else refresh();
        }
        updateEnabled();return;
    }
    if(!CloudSyncSettings::serverRequestsAllowed()){fresh=false;status->setText(trText("Перезапустіть застосунок для автономної роботи."));updateEnabled();return;}
    // Read credentials without the legacy token-migration writes in Settings::load().
    const auto stored = UiStateSettingsService::loadValue("settings.cloudsync.api_token",QString()).toString().trimmed();
    QString error;
    const auto token = stored.startsWith("dpapi:v1:") ? CloudSyncCredentials::unprotectToken(stored,&error) : stored;
    if (token.isEmpty()) { fresh=false;status->setText(trText("Увійдіть у серверну синхронізацію. Зайнятість не змінено."));updateEnabled();return; }
    QNetworkRequest req(QUrl(CloudSyncSettings::fixedServerUrl()+"/api/v1/bays/"+endpoint));
    req.setRawHeader("Authorization","Bearer "+token.toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    req.setTransferTimeout(20000);
    busy=true;fresh=false;updateEnabled();status->setText(trText("Очікування підтвердження сервера…"));
    auto* reply = payload.isEmpty() ? network->get(req) : network->post(req,QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply,&QNetworkReply::finished,this,[this,reply,endpoint]{
        busy=false;
        const int code=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QJsonParseError parseError;
        const auto document=QJsonDocument::fromJson(reply->readAll(),&parseError);
        const bool success=reply->error()==QNetworkReply::NoError && code==200 && parseError.error==QJsonParseError::NoError && document.isObject();
        const auto body=document.object();reply->deleteLater();
        if (!success) {
            // A lost response may have committed. Keep the exact command ID for retry.
            if (endpoint!="snapshot" && code>=400 && code<500) { pendingEndpoint.clear();pendingPayload={}; }
            status->setText(code==409?trText("Дані вже змінилися на іншому комп’ютері. Оновіть їх і повторіть дію."):
                code==401 || code==403?trText("Немає доступу. Перевірте вхід і права користувача."):
                trText("Не вдалося отримати підтвердження сервера. Перевірте з’єднання й повторіть запит. Повтор не створить дубль операції."));
            updateEnabled();return;
        }
        if(endpoint=="snapshot") {
            if(displaySnapshot(body) && offerOrderChange) {offerOrderChange=false;QTimer::singleShot(0,this,[this]{changeOrder();});}
            if(fresh && !closingOrders.isEmpty())QTimer::singleShot(0,this,[this]{confirmClosure();});
        }
        else {
            pendingEndpoint.clear();pendingPayload={};
            if(endpoint=="close-orders") {
                if(body.value("snapshot").isObject())displaySnapshot(body.value("snapshot").toObject());
                closureConfirmed=true;if(auto* dialog=qobject_cast<QDialog*>(parentWidget()))dialog->accept();
            }
            else refresh();
        }
        updateEnabled();
    });
}

bool BayOccupancyPage::displaySnapshot(const QJsonObject& snapshot) {
    auto parsed=BaySnapshot::parse(snapshot);
    if(!parsed.error.isEmpty()) { fresh=false;status->setText(trText("Не вдалося прочитати дані сервера. Попередні дані збережено. Спробуйте оновити пізніше."));updateEnabled();return false; }
    data=parsed;raw=snapshot;fresh=true;
    BaySnapshot::remember(data);
    { const QSignalBlocker block(bayFilter);const auto selected=bayFilter->currentData().toString();bayFilter->clear();bayFilter->addItem(trText("Усі пости"),QString());
      for(auto it=data.names.begin();it!=data.names.end();++it) bayFilter->addItem(it.value(),it.key());
      bayFilter->setCurrentIndex(qMax(0,bayFilter->findData(selected))); }
    render();updateEnabled();return true;
}

void BayOccupancyPage::render() {
    if(!data.asOf.isValid()) return;
    PreserveTableView keepResources(resources),keepHistory(history),keepReport(report),keepAudit(audit);
    resources->setRowCount(0);history->setRowCount(0);report->setRowCount(0);audit->setRowCount(0);
    QMap<QString,QString> numbers,orderStatuses;
    for(const auto value:raw.value("orders").toArray()) {const auto o=value.toObject();numbers[o.value("uid").toString()]=o.value("number").toString();orderStatuses[o.value("uid").toString()]=o.value("status").toString();}
    QStringList warnings;
    for(const auto value:raw.value("bays").toArray()) {
        const auto b=value.toObject(); const auto uid=b.value("uid").toString();
        QString order;
        for(const auto& bay:data.bays) if(bay.uid==uid) for(const auto& interval:bay.occupancy) if(!interval.end) order=interval.orderUid;
        const bool initialized=!b.value("confirmed_from").isNull();
        row(resources,{data.names.value(uid),!initialized?trText("Не підтверджено"):(order.isEmpty()?trText("Вільний"):trText("Зайнятий")),order.isEmpty()?QString():numbers.value(order,trText("Номер недоступний")),initialized?date(b.value("confirmed_from").toInteger()):QString()});
        resources->item(resources->rowCount()-1,0)->setData(Qt::UserRole+1,uid);
        if(!order.isEmpty() && QStringList{"closed","cancelled","canceled","archived"}.contains(orderStatuses.value(order)))
            warnings.append(trText("Пост %1: завершений/скасований наряд досі займає пост.").arg(uid));
        if(!initialized) {
            auto* host=new QWidget(resources);auto* buttons=new QHBoxLayout(host);buttons->setContentsMargins(0,0,0,0);
            auto* free=new QPushButton(trText("Вільний зараз"),host);auto* occupied=new QPushButton(trText("Зайнятий зараз"),host);
            buttons->addWidget(free);buttons->addWidget(occupied);resources->setCellWidget(resources->rowCount()-1,3,host);
            connect(free,&QPushButton::clicked,this,[this,uid]{
                if(QMessageBox::question(this,trText("Початкова звірка"),trText("Підтвердити: пост %1 фізично вільний зараз? Минулі періоди залишаться невідомими.").arg(uid),QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)==QMessageBox::Yes)
                    command("confirm-free",{{"bay_uid",uid}});
            });
            connect(occupied,&QPushButton::clicked,this,[this,uid]{changeOrder(uid);});
            resources->setRowHeight(resources->rowCount()-1,host->sizeHint().height()+4);
        }
    }
    for(const auto value:raw.value("intervals").toArray()) {
        const auto i=value.toObject();const auto uid=i.value("order_uid").toString();
        const auto end=i.value("end_ms").isNull()?data.asOf.toMSecsSinceEpoch():i.value("end_ms").toInteger();
        row(history,{data.names.value(i.value("bay_uid").toString()),numbers.value(uid,trText("Номер недоступний")),date(i.value("begin_ms").toInteger()),i.value("end_ms").isNull()?trText("Триває"):date(end),hours(end-i.value("begin_ms").toInteger()),actorName(i.value("actor").toString())});
        history->item(history->rowCount()-1,0)->setData(Qt::UserRole,i);
        history->item(history->rowCount()-1,0)->setData(Qt::UserRole+1,i.value("uid").toString());
    }
    for(const auto value:raw.value("audit").toArray()) {
        const auto a=value.toObject();
        row(audit,{date(a.value("recorded_ms").toInteger()),date(a.value("effective_ms").toInteger()),actorName(a.value("actor").toString()),a.value("after").toObject().value("reason").toString(),
            describeChange(a.value("before").toObject(),data.names,numbers),describeChange(a.value("after").toObject(),data.names,numbers)});
        audit->item(audit->rowCount()-1,0)->setData(Qt::UserRole+1,a.value("uid").toString());
        for(int c=0;c<audit->columnCount();++c)audit->item(audit->rowCount()-1,c)->setToolTip(audit->item(audit->rowCount()-1,c)->text());
    }
    const auto selected=bayFilter->currentData().toString();
    // Detect cross-bay conflicts before applying a display filter.
    auto result=BayLoadReport::calculate(data.bays,from->date(),to->date(),data.asOf);
    if(!result.error.isEmpty()) {status->setText(result.error);return;}
    if(!selected.isEmpty()) {const auto metrics=result.bays.value(selected);result.bays={{selected,metrics}};result.total=metrics;}
    auto add=[this](const QString& key,const QString& name,const BayLoadReport::Metrics& m){ row(report,{name,hours(m.availableMs),hours(m.knownMs),hours(m.occupiedMs),hours(m.freeMs),hours(m.unknownMs),percent(m.loadPercent()),percent(m.coveragePercent()),hours(m.outsideScheduleMs)});report->item(report->rowCount()-1,0)->setData(Qt::UserRole+1,key); };
    for(auto it=result.bays.cbegin();it!=result.bays.cend();++it) add(it.key(),data.names.value(it.key()),it.value());
    add("total",trText("Разом: %1 постів").arg(result.bays.size()),result.total);
    if(result.total.unknownMs>0) warnings.append(trText("Часткові дані: завантаження розраховано лише за відомий час."));
    if(result.total.conflictMs>0) warnings.append(trText("Конфлікт історії: інтервали накладаються; конфліктний час виключено."));
    status->setText(trText("Дані оновлено: %1 (час Києва).").arg(date(data.asOf.toMSecsSinceEpoch()))+"\n"+warnings.join("\n"));
}

void BayOccupancyPage::changeOrder(const QString& initialBay) {
    if(!fresh || busy || !pendingEndpoint.isEmpty()) return;
    QDialog dialog(this);PersonnelDialogStyle::apply(dialog);dialog.setWindowTitle(trText("Фактичне розміщення наряду"));auto* layout=new QFormLayout(&dialog);
    auto* orders=new QComboBox(&dialog);orders->setMinimumContentsLength(32);
    QSet<QString> present;
    for(const auto value:raw.value("orders").toArray()) {const auto o=value.toObject();if(o["status"]=="deleted")continue;const auto uid=o.value("uid").toString();present.insert(uid);orders->addItem(trText("Наряд №%1 · %2").arg(o.value("number").toString(),orderStatus(o.value("status").toString())),uid);}
    // Closed/archived orders remain releasable even when absent from this workstation.
    for(auto it=data.states.begin();it!=data.states.end();++it) if(!present.contains(it.key()) && it.value().toObject().value("state")=="on_bay") orders->addItem(trText("Наряд без номера · %1").arg(data.names.value(it.value().toObject().value("bay_uid").toString())),it.key());
    if(!selectedOrder.isEmpty()) {
        const int index=orders->findData(selectedOrder);
        if(index<0) {status->setText(trText("Спочатку синхронізуйте цей наряд із сервером. Зайнятість не змінено."));return;}
        orders->setCurrentIndex(index);orders->setEnabled(false);
    }
    auto* target=new QComboBox(&dialog);target->addItem(trText("Поза постом"),QString());
    for(const auto b:raw.value("bays").toArray()) {
        const auto bay=b.toObject();const auto uid=bay.value("uid").toString();
        if(!bay.value("confirmed_from").isNull() || uid==initialBay) target->addItem(data.names.value(uid),uid);
    }
    if(!initialBay.isEmpty()) {target->setCurrentIndex(target->findData(initialBay));target->setEnabled(false);}
    else {
        const auto current=data.states.value(orders->currentData().toString()).toObject().value("bay_uid").toString();
        const int index=target->findData(preferredPost<0?current:(preferredPost==0?QString():QString::number(preferredPost)));
        if(preferredPost>0 && index<0) {status->setText(trText("Спочатку підтвердьте початковий стан цього поста на вкладці «Поточна зайнятість»."));return;}
        target->setCurrentIndex(qMax(0,index));
    }
    auto* current=new QLabel(&dialog); current->setWordWrap(true);
    auto update=[this,orders,current]{const auto s=data.states.value(orders->currentData().toString()).toObject();current->setText(trText("Поточний стан: ")+(s.isEmpty()?trText("Невідомо"):(s.value("state")=="off_bay"?trText("Поза постом"):data.names.value(s.value("bay_uid").toString()))));};
    connect(orders,&QComboBox::currentIndexChanged,&dialog,update);update();
    layout->addRow(trText("Наряд:"),orders);layout->addRow(current);layout->addRow(trText("Фактично зараз:"),target);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,&dialog);layout->addRow(buttons);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    dialog.resize(qMax(520,dialog.sizeHint().width()),dialog.sizeHint().height());
    if(dialog.exec()!=QDialog::Accepted || orders->currentIndex()<0) return;
    const auto uid=orders->currentData().toString();const auto bay=target->currentData().toString();
    command(initialBay.isEmpty()?"change":"confirm-occupied",{{"order_uid",uid},{"target_state",bay.isEmpty()?"off_bay":"on_bay"},{"target_bay_uid",bay.isEmpty()?QJsonValue(QJsonValue::Null):QJsonValue(bay)},{"expected_version",data.states.value(uid).toObject().value("version").toInt(0)}});
}

void BayOccupancyPage::correctHistory() {
    if(!correct->isEnabled() || history->currentRow()<0) return;
    const auto original=history->item(history->currentRow(),0)->data(Qt::UserRole).toJsonObject();
    QDialog dialog(this);PersonnelDialogStyle::apply(dialog);dialog.setWindowTitle(trText("Виправлення історії · час Києва"));
    auto* form=new QFormLayout(&dialog);
    auto* bay=new QComboBox(&dialog);for(auto it=data.names.begin();it!=data.names.end();++it)bay->addItem(it.value(),it.key());
    bay->setCurrentIndex(bay->findData(original.value("bay_uid").toString()));
    auto* begin=new QDateTimeEdit(&dialog);auto* end=new QDateTimeEdit(&dialog);
    for(auto* edit:{begin,end}) {edit->setTimeZone(QTimeZone("Europe/Kyiv"));edit->setDisplayFormat("dd.MM.yyyy HH:mm:ss.zzz");edit->setCalendarPopup(true);}
    begin->setDateTime(QDateTime::fromMSecsSinceEpoch(original.value("begin_ms").toInteger(),QTimeZone("Europe/Kyiv")));
    end->setDateTime(original.value("end_ms").isNull()?data.asOf:QDateTime::fromMSecsSinceEpoch(original.value("end_ms").toInteger(),QTimeZone("Europe/Kyiv")));
    auto* ongoing=new QCheckBox(trText("Інтервал досі відкритий"),&dialog);ongoing->setChecked(original.value("end_ms").isNull());end->setEnabled(!ongoing->isChecked());
    connect(ongoing,&QCheckBox::toggled,end,[end](bool checked){end->setEnabled(!checked);});
    auto* reason=new QLineEdit(&dialog);reason->setMaxLength(1000);
    form->addRow(trText("Пост:"),bay);form->addRow(trText("Початок:"),begin);form->addRow(ongoing);form->addRow(trText("Кінець:"),end);form->addRow(trText("Причина виправлення:"),reason);
    auto* error=new QLabel(&dialog);error->setWordWrap(true);form->addRow(error);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,&dialog);form->addRow(buttons);
    QJsonObject payload;
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{
        const QDateTime start(begin->date(),begin->time(),QTimeZone("Europe/Kyiv"),QDateTime::TransitionResolution::Reject);
        const QDateTime finish(end->date(),end->time(),QTimeZone("Europe/Kyiv"),QDateTime::TransitionResolution::Reject);
        if(!start.isValid() || (!ongoing->isChecked() && (!finish.isValid() || finish<=start)) || reason->text().trimmed().size()<3) {
            error->setText(trText("Перевірте межі й причину. Неоднозначний час переведення годинника не приймається."));return;
        }
        payload={{"interval_uid",original.value("uid")},{"expected_revision",original.value("revision")},{"bay_uid",bay->currentData().toString()},
            {"begin_ms",start.toMSecsSinceEpoch()},{"end_ms",ongoing->isChecked()?QJsonValue(QJsonValue::Null):QJsonValue(finish.toMSecsSinceEpoch())},{"reason",reason->text().trimmed()}};
        dialog.accept();
    });
    dialog.resize(qMax(660,dialog.sizeHint().width()),dialog.sizeHint().height());
    if(dialog.exec()==QDialog::Accepted)command("correct-interval",payload);
}

void BayOccupancyPage::editCalendar() {
    if(!calendar->isEnabled())return;
    QDialog dialog(this);PersonnelDialogStyle::apply(dialog);dialog.setWindowTitle(trText("Календар поста · час Києва"));auto* layout=new QVBoxLayout(&dialog);
    auto* choice=new QComboBox(&dialog);for(auto it=data.names.begin();it!=data.names.end();++it)choice->addItem(it.value(),it.key());layout->addWidget(choice);
    auto* versions=table(&dialog,{trText("Чинний з"),trText("До (не включно)"),trText("Дні: 1=Пн … 7=Нд"),trText("Робочі інтервали")});
    versions->setEditTriggers(QAbstractItemView::DoubleClicked|QAbstractItemView::EditKeyPressed);
    versions->setToolTip(trText("Дати: дд.мм.рррр. Дні через кому. Години: 08:00-12:00;13:00-18:00. Порожній кінець означає безстроково."));
    layout->addWidget(versions);
    auto* spans=table(&dialog,{trText("Недоступний з (Київ)"),trText("До (Київ)"),trText("Причина")});
    spans->setEditTriggers(QAbstractItemView::DoubleClicked|QAbstractItemView::EditKeyPressed);
    spans->setToolTip(trText("Дата й час: дд.мм.рррр гг:хх:сс.мілісекунди. Недоступність зменшує робочий фонд, але не звільняє пост."));
    auto addControls=[&](QTableWidget* target,const QString& addText,const std::function<void()>& add){
        auto* line=new QHBoxLayout;auto* plus=new QPushButton(addText,&dialog);auto* minus=new QPushButton(trText("Прибрати вибраний рядок"),&dialog);
        line->addWidget(plus);line->addWidget(minus);line->addStretch();layout->addLayout(line);
        connect(plus,&QPushButton::clicked,&dialog,add);
        connect(minus,&QPushButton::clicked,&dialog,[target]{if(target->currentRow()>=0)target->removeRow(target->currentRow());});
    };
    addControls(versions,trText("Додати версію графіка"),[&]{row(versions,{to->date().toString("dd.MM.yyyy"),{},"1,2,3,4,5,6","08:00-12:00;13:00-18:00"});});
    layout->addWidget(spans);
    addControls(spans,trText("Додати недоступність"),[&]{const auto now=data.asOf.toTimeZone(QTimeZone("Europe/Kyiv"));row(spans,{now.toString("dd.MM.yyyy HH:mm:ss.zzz"),now.addSecs(3600).toString("dd.MM.yyyy HH:mm:ss.zzz"),{}});});
    QJsonObject original;
    auto load=[&]{
        versions->setRowCount(0);spans->setRowCount(0);
        for(const auto v:raw.value("bays").toArray())if(v.toObject().value("uid").toString()==choice->currentData().toString())original=v.toObject();
        for(const auto v:original.value("calendars").toArray()) {
            const auto c=v.toObject();QStringList days,shifts;
            for(const auto d:c.value("weekdays").toArray())days<<QString::number(d.toInt());
            for(const auto s:c.value("shifts").toArray())shifts<<s.toArray()[0].toString()+"-"+s.toArray()[1].toString();
            row(versions,{QDate::fromString(c.value("from").toString(),Qt::ISODate).toString("dd.MM.yyyy"),QDate::fromString(c.value("until").toString(),Qt::ISODate).toString("dd.MM.yyyy"),days.join(","),shifts.join(";")});
        }
        for(const auto v:original.value("unavailable").toArray()) {const auto s=v.toObject();row(spans,{QDateTime::fromMSecsSinceEpoch(s.value("begin_ms").toInteger(),QTimeZone("Europe/Kyiv")).toString("dd.MM.yyyy HH:mm:ss.zzz"),QDateTime::fromMSecsSinceEpoch(s.value("end_ms").toInteger(),QTimeZone("Europe/Kyiv")).toString("dd.MM.yyyy HH:mm:ss.zzz"),s.value("reason").toString()});}
        versions->resizeColumnsToContents();spans->resizeColumnsToContents();
    };
    connect(choice,&QComboBox::currentIndexChanged,&dialog,load);load();
    auto* reason=new QLineEdit(&dialog);reason->setPlaceholderText(trText("Обов’язкова причина зміни календаря"));reason->setMaxLength(1000);layout->addWidget(reason);
    auto* error=new QLabel(&dialog);error->setWordWrap(true);layout->addWidget(error);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,&dialog);layout->addWidget(buttons);
    QJsonObject payload;
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{
        auto text=[](QTableWidget* t,int r,int c){return t->item(r,c)?t->item(r,c)->text().trimmed():QString();};
        QJsonArray calendars,unavailable;bool valid=versions->rowCount()>0 && reason->text().trimmed().size()>=3;
        for(int r=0;r<spans->rowCount();++r) {
            auto local=[](const QString& value){const auto d=QDateTime::fromString(value,"dd.MM.yyyy HH:mm:ss.zzz");return QDateTime(d.date(),d.time(),QTimeZone("Europe/Kyiv"),QDateTime::TransitionResolution::Reject);};
            const auto start=local(text(spans,r,0)),end=local(text(spans,r,1));valid=valid && start.isValid() && end.isValid() && end>start && text(spans,r,2).size()>=3;
            unavailable.append(QJsonObject{{"begin_ms",start.toMSecsSinceEpoch()},{"end_ms",end.toMSecsSinceEpoch()},{"reason",text(spans,r,2)}});
        }
        for(int r=0;r<versions->rowCount();++r) {
            const auto start=QDate::fromString(text(versions,r,0),"dd.MM.yyyy"),end=QDate::fromString(text(versions,r,1),"dd.MM.yyyy");
            valid=valid && start.isValid() && (text(versions,r,1).isEmpty() || (end.isValid() && end>start));
            QJsonArray days,shifts;
            for(const auto& d:text(versions,r,2).split(',',Qt::SkipEmptyParts)) {bool ok;int n=d.trimmed().toInt(&ok);valid=valid && ok && n>=1 && n<=7;days.append(n);}
            for(const auto& s:text(versions,r,3).split(';',Qt::SkipEmptyParts)) {const auto parts=s.split('-');if(parts.size()!=2){valid=false;continue;}shifts.append(QJsonArray{parts[0].trimmed(),parts[1].trimmed()});}
            calendars.append(QJsonObject{{"from",start.toString(Qt::ISODate)},{"until",end.isValid()?QJsonValue(end.toString(Qt::ISODate)):QJsonValue(QJsonValue::Null)},
                {"weekdays",days},{"shifts",shifts},{"timezone","Europe/Kyiv"},{"unavailable",r==0?unavailable:QJsonArray()}});
        }
        if(!valid){error->setText(trText("Перевірте дати, час та причини. Неоднозначний час переходу на літній/зимовий час не приймається."));return;}
        payload={{"bay_uid",choice->currentData().toString()},{"expected_revision",original.value("calendar_revision")},{"calendars",calendars},{"reason",reason->text().trimmed()}};dialog.accept();
    });
    const auto available=dialog.screen()->availableGeometry().size();dialog.resize(qMin(1050,available.width()-40),qMin(700,available.height()-60));
    if(dialog.exec()==QDialog::Accepted)command("correct-calendar",payload);
}
