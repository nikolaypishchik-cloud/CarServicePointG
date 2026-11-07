#include "core/util/DateFormats.h"
#include "ui/tabs/settings/PhoneSyncSettingsPage.h"

#include "core/cloudsync/CloudSyncSettings.h"
#include "core/services/AccessControlService.h"
#include "core/services/ActivityAuditService.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDateTime>
#include "runtime/WorkerNetwork.h"
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QThread>
#include <QVector>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <memory>

namespace {

struct PairingRequest {
    QString serverUrl;
    QString apiToken;
    QString employeeUid;
    QString employeeName;
    QString role;
};

struct PairingResponse {
    bool ok = false;
    QString error;
    QString pairingUri;
    QString expiresAt;
    QByteArray qrPng;
};

QString apiRoot(QString value)
{
    value = value.trimmed();
    while (value.endsWith(QLatin1Char('/'))) value.chop(1);
    if (value.endsWith(QStringLiteral("/api/v1"), Qt::CaseInsensitive)) value.chop(7);
    return value;
}

PairingResponse createPairing(const PairingRequest& request)
{
    PairingResponse result;
    if(!CloudSyncSettings::serverRequestsAllowed()){result.error=QString::fromUtf8("Автономний режим: прив’язка телефону потребує сервера.");return result;}
    const QUrl base(apiRoot(request.serverUrl));
    if (!base.isValid() || base.scheme().toLower() != QStringLiteral("https")) {
        result.error = QString::fromUtf8("Для прив'язки телефону вкажіть публічну HTTPS-адресу сервера.");
        return result;
    }
    if (request.apiToken.trimmed().isEmpty()) {
        result.error = QString::fromUtf8("У налаштуваннях сервера відсутній токен власника.");
        return result;
    }

    QUrl url = base;
    url.setPath(base.path() + QStringLiteral("/api/v1/mobile/pairings"));
    QNetworkRequest networkRequest(url);
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    networkRequest.setRawHeader("Authorization", QByteArray("Bearer ") + request.apiToken.toUtf8());
    networkRequest.setRawHeader("User-Agent", "CarServicePointG-PhonePairing/2.1");

    QJsonObject body;
    body.insert(QStringLiteral("employee_uid"), request.employeeUid);
    body.insert(QStringLiteral("employee_name"), request.employeeName);
    body.insert(QStringLiteral("role"), request.role);
    body.insert(QStringLiteral("server_url"), apiRoot(request.serverUrl));
    body.insert(QStringLiteral("expires_in_minutes"), 10);

    const auto reply=WorkerNetwork::request(networkRequest, QJsonDocument(body).toJson(QJsonDocument::Compact), true, 15000);
    if (reply.stopped) {
        
        result.error = QString::fromUtf8("Сервер не відповів за 15 секунд.");
        
        return result;
    }

    const int status = reply.status;
    const QByteArray responseBody = reply.body;
    if (reply.error != QNetworkReply::NoError || status < 200 || status >= 300) {
        const QJsonDocument document = QJsonDocument::fromJson(responseBody);
        const QString detail = document.isObject() ? document.object().value(QStringLiteral("detail")).toString() : QString();
        result.error = detail.isEmpty() ? reply.message : detail;
        
        return result;
    }
    

    const QJsonDocument document = QJsonDocument::fromJson(responseBody);
    if (!document.isObject()) {
        result.error = QString::fromUtf8("Сервер повернув некоректну відповідь для QR-прив'язки.");
        return result;
    }
    const QJsonObject payload = document.object();
    result.pairingUri = payload.value(QStringLiteral("pairing_uri")).toString();
    result.expiresAt = payload.value(QStringLiteral("expires_at")).toString();
    result.qrPng = QByteArray::fromBase64(payload.value(QStringLiteral("qr_png_base64")).toString().toUtf8());
    result.ok = !result.pairingUri.isEmpty() && !result.qrPng.isEmpty();
    if (!result.ok) result.error = QString::fromUtf8("Сервер не повернув QR-код прив'язки.");
    return result;
}

struct MobileDeviceRow {
    QString employeeName;
    QString role;
    QString deviceName;
    QString lastSeenAt;
    bool online = false;
    QString status;
};

struct DeviceListResponse {
    bool ok = false;
    QString error;
    QVector<MobileDeviceRow> items;
};

QString displayTimestamp(const QString& value)
{
    const QDateTime timestamp = DateFormats::parseDateTime(value);
    return timestamp.isValid()
        ? timestamp.toLocalTime().toString(QStringLiteral("dd.MM.yy HH:mm"))
        : QString::fromUtf8("ще не зафіксовано");
}

QString displayMobileRole(const QString& role)
{
    const QString normalized = role.trimmed().toLower();
    if (normalized == QStringLiteral("owner")) return QString::fromUtf8("Власник");
    if (normalized == QStringLiteral("manager")) return QString::fromUtf8("Менеджер");
    if (normalized == QStringLiteral("admin")) return QString::fromUtf8("Адміністратор");
    if (normalized == QStringLiteral("master")) return QString::fromUtf8("Майстер");
    return role;
}

DeviceListResponse loadMobileDevices(const CloudSyncSettings::Settings& settings)
{
    DeviceListResponse result;
    if(!CloudSyncSettings::serverRequestsAllowed()){result.error=QString::fromUtf8("Автономний режим: серверні пристрої недоступні.");return result;}
    const QUrl base(apiRoot(settings.serverUrl));
    if (!base.isValid() || base.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0 || base.host().isEmpty()) {
        result.error = QString::fromUtf8("Вкажіть публічну HTTPS-адресу в налаштуваннях сервера.");
        return result;
    }
    if (settings.apiToken.trimmed().isEmpty()) {
        result.error = QString::fromUtf8("Увійдіть і прив'яжіть цей ПК до центрального сервера.");
        return result;
    }

    QUrl url = base;
    url.setPath(base.path() + QStringLiteral("/api/v1/mobile/devices"));
    QNetworkRequest networkRequest(url);
    networkRequest.setRawHeader("Authorization", QByteArray("Bearer ") + settings.apiToken.toUtf8());
    networkRequest.setRawHeader("User-Agent", "CarServicePointG-PhoneDevices/2.1");

    const auto reply=WorkerNetwork::request(networkRequest, {}, false, 15000);
    if (reply.stopped) {
        
        result.error = QString::fromUtf8("Сервер не відповів за 15 секунд.");
        
        return result;
    }

    const int httpStatus = reply.status;
    const QByteArray body = reply.body;
    if (reply.error != QNetworkReply::NoError || httpStatus < 200 || httpStatus >= 300) {
        const QJsonDocument errorDocument = QJsonDocument::fromJson(body);
        const QString detail = errorDocument.isObject() ? errorDocument.object().value(QStringLiteral("detail")).toString() : QString();
        result.error = detail.isEmpty() ? reply.message : detail;
        
        return result;
    }
    

    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (!document.isObject()) {
        result.error = QString::fromUtf8("Сервер повернув некоректний список підключень.");
        return result;
    }
    for (const QJsonValue& value : document.object().value(QStringLiteral("items")).toArray()) {
        const QJsonObject item = value.toObject();
        result.items.append({
            item.value(QStringLiteral("employee_name")).toString(),
            item.value(QStringLiteral("role")).toString(),
            item.value(QStringLiteral("device_name")).toString(),
            item.value(QStringLiteral("last_seen_at")).toString(),
            item.value(QStringLiteral("online")).toBool(),
            item.value(QStringLiteral("status")).toString(),
        });
    }
    result.ok = true;
    return result;
}
QWidget* makeCard(QWidget* parent)
{
    auto* card = new QFrame(parent);
    card->setObjectName(QStringLiteral("phoneSyncCard"));
    return card;
}

} // namespace

QWidget* PhoneSyncSettingsPage::build(QWidget* owner)
{
    auto* page = new QWidget(owner);
    page->setObjectName(QStringLiteral("phoneSyncPage"));
    page->setProperty("settings_detail_inline_title", QString::fromUtf8("Синхронізація телефону"));
    page->setStyleSheet(QStringLiteral(
        "#phoneSyncPage{background:transparent;color:#f3f7fb;}"
        "#phoneSyncPage QScrollArea,#phoneSyncPage QScrollArea>QWidget>QWidget{background:transparent;border:0;}"
        "#phoneSyncPage QFrame#phoneSyncCard{background:#2a3b4b;border:1px solid #71889a;border-radius:6px;}"
        "#phoneSyncPage QLabel{color:#f3f7fb;background:transparent;}"
        "#phoneSyncPage QLabel[role=help]{color:#d2e0eb;font-size:10pt;}"
        "#phoneSyncPage QComboBox{background:#ffffff;color:#14202a;border:1px solid #b8c7d4;border-radius:5px;padding:7px 10px;min-height:28px;}"
        "#phoneSyncPage QPushButton{background:#253f56;color:#ffffff;border:1px solid #adc1d1;border-radius:5px;padding:8px 14px;font-weight:700;}"
        "#phoneSyncPage QPushButton:hover{background:#315673;}"
        "#phoneSyncPage QLabel#phoneSyncQr{background:#ffffff;border:8px solid #ffffff;border-radius:4px;color:#14202a;}"));

    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    outer->addWidget(scroll);

    auto* content = new QWidget(scroll);
    auto* root = new QVBoxLayout(content);
    root->setContentsMargins(18, 16, 18, 18);
    root->setSpacing(12);
    scroll->setWidget(content);

    const auto settings = CloudSyncSettings::load();
    const ActivityAuditActor activeActor = ActivityAuditService::currentActor();
    const bool canManage = AccessControlService::currentUserHasPermission( QStringLiteral("access.manage"));
    const bool canPairSelf = activeActor.isValid()
        && AccessControlService::currentUserHasPermission( QStringLiteral("mobile.pair"));
    const bool canPair = canManage || canPairSelf;
    const bool selfPairingOnly = canPair && !canManage;

    auto* intro = makeCard(content);
    auto* introLayout = new QVBoxLayout(intro);
    introLayout->setContentsMargins(14, 12, 14, 12);
    const QString pairingDescription = selfPairingOnly
        ? QString::fromUtf8("Підключіть лише свій Android-телефон. QR-код діє 10 хвилин, використовується один раз і не містить постійного токена.")
        : QString::fromUtf8("Прив'яжіть Android-телефон працівника одним QR-кодом. Код діє 10 хвилин, використовується один раз і не містить постійного токена.");
    auto* description = new QLabel(pairingDescription, intro);
    description->setProperty("role", "help");
    description->setWordWrap(true);
    introLayout->addWidget(description);
    auto* server = new QLabel(QString::fromUtf8("Сервер: %1").arg(settings.serverUrl.trimmed().isEmpty() ? QString::fromUtf8("не налаштовано") : apiRoot(settings.serverUrl)), intro);
    server->setTextInteractionFlags(Qt::TextSelectableByMouse);
    introLayout->addWidget(server);
    root->addWidget(intro);

    auto* pairingCard = makeCard(content);
    auto* pairingLayout = new QVBoxLayout(pairingCard);
    pairingLayout->setContentsMargins(14, 12, 14, 14);
    pairingLayout->setSpacing(10);
    auto* heading = new QLabel(selfPairingOnly
        ? QString::fromUtf8("Підключення мого Android")
        : QString::fromUtf8("Нове підключення Android"), pairingCard);
    heading->setStyleSheet(QStringLiteral("font-size:13pt;font-weight:800;"));
    pairingLayout->addWidget(heading);

    auto* employee = new QComboBox(pairingCard);
    QString accountsError;
    const auto accounts = AccessControlService::accounts( &accountsError);
    if (selfPairingOnly) {
        for (const AccessAccountRow& account : accounts) {
            if (!account.active || account.id != activeActor.accountId) continue;
            const QString employeeUid = account.masterId.trimmed().isEmpty() ? account.id : account.masterId.trimmed();
            employee->addItem(QString::fromUtf8("%1 (%2)").arg(account.displayName, account.roleTitle), employeeUid);
            employee->setItemData(employee->count() - 1, account.roleId, Qt::UserRole + 1);
            employee->setItemData(employee->count() - 1, account.displayName, Qt::UserRole + 2);
            break;
        }
        employee->setEnabled(false);
    } else {
        for (const AccessAccountRow& account : accounts) {
            if (!account.active || account.roleId == QStringLiteral("owner")) continue;
            const QString employeeUid = account.masterId.trimmed().isEmpty() ? account.id : account.masterId.trimmed();
            employee->addItem(QString::fromUtf8("%1 (%2)").arg(account.displayName, account.roleTitle), employeeUid);
            employee->setItemData(employee->count() - 1, account.roleId, Qt::UserRole + 1);
            employee->setItemData(employee->count() - 1, account.displayName, Qt::UserRole + 2);
        }
    }
    if (employee->count() == 0) employee->addItem(selfPairingOnly
        ? QString::fromUtf8("Поточний обліковий запис недоступний")
        : QString::fromUtf8("Немає активних працівників"));
    pairingLayout->addWidget(new QLabel(selfPairingOnly ? QString::fromUtf8("Ваш обліковий запис:") : QString::fromUtf8("Працівник:"), pairingCard));
    pairingLayout->addWidget(employee);
    const QUrl configuredServer(apiRoot(settings.serverUrl));
    const bool hasPublicHttpsServer = configuredServer.isValid()
        && configuredServer.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0
        && !configuredServer.host().isEmpty();
    const bool hasEmployee = employee->count() > 0 && employee->currentData().isValid();
    QString pairingBlockReason;
    if (!canPair) {
        pairingBlockReason = QString::fromUtf8("Для прив'язки телефона потрібне право «Прив'язувати свій Android-телефон».");
    } else if (!hasEmployee) {
        pairingBlockReason = QString::fromUtf8("Немає активного працівника для прив'язки телефона.");
    } else if (!hasPublicHttpsServer) {
        pairingBlockReason = QString::fromUtf8("Спочатку налаштуйте та збережіть публічну HTTPS-адресу в «Синхронізація сервера».");
    } else if (settings.apiToken.trimmed().isEmpty()) {
        pairingBlockReason = QString::fromUtf8("У «Синхронізація сервера» виконайте «Увійти та прив'язати цей ПК», щоб отримати токен центрального сервера.");
    }

    auto* generate = new QPushButton(selfPairingOnly
        ? QString::fromUtf8("Створити QR-код для мого телефону")
        : QString::fromUtf8("Створити QR-код прив'язки"), pairingCard);
    generate->setEnabled(pairingBlockReason.isEmpty());
    generate->setToolTip(pairingBlockReason);
    pairingLayout->addWidget(generate, 0, Qt::AlignLeft);
    auto* status = new QLabel(pairingCard);
    status->setWordWrap(true);
    status->setProperty("role", "help");
    status->setText(pairingBlockReason.isEmpty()
                        ? (selfPairingOnly
                            ? QString::fromUtf8("QR-код для вашого телефона буде одноразовим і діятиме 10 хвилин.")
                            : QString::fromUtf8("QR-код буде одноразовим і діятиме 10 хвилин."))
                        : QString::fromUtf8("QR-код поки не створено: %1").arg(pairingBlockReason));
    pairingLayout->addWidget(status);
    root->addWidget(pairingCard);

    auto* qrCard = makeCard(content);
    auto* qrLayout = new QVBoxLayout(qrCard);
    qrLayout->setContentsMargins(14, 12, 14, 14);
    auto* qrHeading = new QLabel(QString::fromUtf8("QR-код для телефону"), qrCard);
    qrHeading->setStyleSheet(QStringLiteral("font-size:13pt;font-weight:800;"));
    qrLayout->addWidget(qrHeading);
    auto* qr = new QLabel(pairingBlockReason.isEmpty()
        ? (selfPairingOnly ? QString::fromUtf8("Створіть одноразовий QR-код для свого телефона.") : QString::fromUtf8("Оберіть працівника та створіть одноразовий QR-код."))
        : QString::fromUtf8("QR-код з'явиться після підключення цього ПК до центрального HTTPS-сервера."), qrCard);
    qr->setObjectName(QStringLiteral("phoneSyncQr"));
    qr->setAlignment(Qt::AlignCenter);
    qr->setMinimumSize(280, 280);
    qr->setMaximumSize(360, 360);
    qr->setWordWrap(true);
    qrLayout->addWidget(qr, 0, Qt::AlignHCenter);
    root->addWidget(qrCard);

    if (canManage) {
    auto* devicesCard = makeCard(content);
    auto* devicesLayout = new QVBoxLayout(devicesCard);
    devicesLayout->setContentsMargins(14, 12, 14, 14);
    devicesLayout->setSpacing(8);
    auto* devicesHeader = new QHBoxLayout();
    auto* devicesHeading = new QLabel(QString::fromUtf8("Підключені телефони"), devicesCard);
    devicesHeading->setStyleSheet(QStringLiteral("font-size:13pt;font-weight:800;"));
    devicesHeader->addWidget(devicesHeading);
    devicesHeader->addStretch(1);
    auto* refreshDevices = new QPushButton(QString::fromUtf8("Оновити"), devicesCard);
    devicesHeader->addWidget(refreshDevices);
    devicesLayout->addLayout(devicesHeader);
    auto* devicesStatus = new QLabel(QString::fromUtf8("Завантажуємо відомості з центрального сервера..."), devicesCard);
    devicesStatus->setProperty("role", "help");
    devicesStatus->setWordWrap(true);
    devicesLayout->addWidget(devicesStatus);
    auto* devicesTable = new QTableWidget(devicesCard);
    devicesTable->setColumnCount(5);
    devicesTable->setHorizontalHeaderLabels({
        QString::fromUtf8("Працівник"), QString::fromUtf8("Роль"), QString::fromUtf8("Телефон"),
        QString::fromUtf8("Стан"), QString::fromUtf8("Остання активність")
    });
    devicesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    devicesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    devicesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    devicesTable->verticalHeader()->setVisible(false);
    devicesTable->horizontalHeader()->setStretchLastSection(true);
    devicesTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    devicesTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    devicesTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    devicesTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    devicesTable->setMinimumHeight(180);
    devicesLayout->addWidget(devicesTable);
    root->addWidget(devicesCard);

    const auto refreshConnections = [page, devicesTable, devicesStatus]() {
        const auto syncSettings = CloudSyncSettings::load();
        devicesStatus->setText(QString::fromUtf8("Оновлюємо список підключених телефонів..."));
        auto response = std::make_shared<DeviceListResponse>();
        auto* thread = QThread::create([syncSettings, response]() { *response = loadMobileDevices(syncSettings); });
        QPointer<QWidget> guard(page);
        QObject::connect(thread, &QThread::finished, page, [guard, thread, response, devicesTable, devicesStatus]() {
            thread->deleteLater();
            if (!guard) return;
            devicesTable->setRowCount(0);
            if (!response->ok) {
                devicesStatus->setText(QString::fromUtf8("Список телефонів недоступний: %1").arg(response->error));
                return;
            }
            int onlineCount = 0;
            for (const MobileDeviceRow& item : response->items) {
                const int row = devicesTable->rowCount();
                devicesTable->insertRow(row);
                const bool revoked = item.status.trimmed().compare(QStringLiteral("active"), Qt::CaseInsensitive) != 0;
                const QString state = revoked ? QString::fromUtf8("Відкликано") : (item.online ? QString::fromUtf8("Онлайн") : QString::fromUtf8("Неактивний"));
                if (item.online) ++onlineCount;
                devicesTable->setItem(row, 0, new QTableWidgetItem(item.employeeName));
                devicesTable->setItem(row, 1, new QTableWidgetItem(displayMobileRole(item.role)));
                devicesTable->setItem(row, 2, new QTableWidgetItem(item.deviceName));
                devicesTable->setItem(row, 3, new QTableWidgetItem(state));
                devicesTable->setItem(row, 4, new QTableWidgetItem(displayTimestamp(item.lastSeenAt)));
            }
            devicesStatus->setText(response->items.isEmpty()
                ? QString::fromUtf8("Ще жоден телефон не прив'язано.")
                : QString::fromUtf8("Підключено: %1. Онлайн зараз: %2.").arg(response->items.size()).arg(onlineCount));
        });
        thread->start();
    };
    QObject::connect(refreshDevices, &QPushButton::clicked, page, [refreshConnections]() { refreshConnections(); });
    refreshConnections();
    }
    root->addStretch(1);

    QObject::connect(generate, &QPushButton::clicked, page, [employee, generate, status, qr, page]() {
        PairingRequest request;
        const auto syncSettings = CloudSyncSettings::load();
        request.serverUrl = syncSettings.serverUrl;
        request.apiToken = syncSettings.apiToken;
        request.employeeUid = employee->currentData().toString();
        request.employeeName = employee->currentData(Qt::UserRole + 2).toString();
        request.role = employee->currentData(Qt::UserRole + 1).toString();
        if (request.employeeUid.isEmpty() || request.role.isEmpty()) return;
        generate->setEnabled(false);
        status->setText(QString::fromUtf8("Створюємо захищений QR-код..."));
        auto response = std::make_shared<PairingResponse>();
        auto* thread = QThread::create([request, response]() { *response = createPairing(request); });
        QPointer<QWidget> guard(page);
        QObject::connect(thread, &QThread::finished, page, [guard, thread, response, generate, status, qr]() {
            thread->deleteLater();
            if (!guard) return;
            generate->setEnabled(true);
            if (!response->ok) {
                status->setText(QString::fromUtf8("Не вдалося створити QR-код: %1").arg(response->error));
                return;
            }
            QPixmap pixmap;
            pixmap.loadFromData(response->qrPng, "PNG");
            qr->setPixmap(pixmap.scaled(qr->size() - QSize(16, 16), Qt::KeepAspectRatio, Qt::SmoothTransformation));
            const QDateTime expires = DateFormats::parseDateTime(response->expiresAt);
            status->setText(QString::fromUtf8("QR-код готовий. Відскануйте його в Android-застосунку до %1.")
                                .arg(expires.isValid() ? expires.toLocalTime().toString(QStringLiteral("HH:mm")) : QString::fromUtf8("завершення строку")));
        });
        thread->start();
    });

    return page;
}
