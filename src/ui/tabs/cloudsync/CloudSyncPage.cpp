#include "core/util/DateFormats.h"
#include "ui/tabs/cloudsync/CloudSyncPage.h"

#include "app/AppContext.h"

#include "core/cloudsync/CloudSyncClient.h"
#include "core/cloudsync/CloudSyncCredentials.h"
#include <QInputDialog>
#include "core/cloudsync/CloudSyncPageService.h"
#include "core/cloudsync/CloudSyncSettings.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QClipboard>
#include <QDateTime>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QUrl>
#include <QUrlQuery>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QPair>
#include <QSizePolicy>
#include <QTimer>
#include "runtime/WorkerNetwork.h"
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QSet>
#include <QThread>
#include <QVBoxLayout>
#include <QtGlobal>
#include <memory>

namespace {

struct CloudConflictsTableGuard {
    explicit CloudConflictsTableGuard(QTableWidget* table)
        : table(table)
        , blocker(table)
        , sorting(table ? table->isSortingEnabled() : false)
        , selectedUid(table && table->currentRow() >= 0 && table->item(table->currentRow(), 0)
              ? table->item(table->currentRow(), 0)->data(Qt::UserRole).toString()
              : QString())
        , scroll(table && table->verticalScrollBar() ? table->verticalScrollBar()->value() : 0)
    {
        if (!table) return;
        table->setUpdatesEnabled(false);
        table->setSortingEnabled(false);
    }

    ~CloudConflictsTableGuard()
    {
        if (!table) return;
        if (!selectedUid.isEmpty()) {
            for (int row = 0; row < table->rowCount(); ++row) {
                if (table->item(row, 0) && table->item(row, 0)->data(Qt::UserRole).toString() == selectedUid) {
                    table->selectRow(row);
                    break;
                }
            }
        }
        table->setSortingEnabled(sorting);
        if (table->verticalScrollBar()) table->verticalScrollBar()->setValue(scroll);
        table->setUpdatesEnabled(true);
    }

    QTableWidget* table = nullptr;
    QSignalBlocker blocker;
    bool sorting = false;
    QString selectedUid;
    int scroll = 0;
};

QString cloudSyncPageStyleSheet()
{
    return QString::fromUtf8(R"QSS(
        #cloudSyncPage {
            background: transparent;
            color: #f7fbff;
        }
        #cloudSyncPage QScrollArea,
        #cloudSyncPage QScrollArea > QWidget,
        #cloudSyncPage QScrollArea QWidget#cloudSyncScrollContent {
            background: transparent;
            border: 0;
        }
        #cloudSyncPage QLabel {
            color: #f7fbff;
            background: transparent;
        }
        #cloudSyncPage QLabel[role="help"] {
            color: #cbd6e2;
            font-size: 10pt;
            line-height: 140%;
        }
        #cloudSyncPage QLabel[role="sectionTitle"] {
            color: #ffffff;
            font-size: 12pt;
            font-weight: 800;
            padding-bottom: 2px;
        }
        #cloudSyncPage QLabel[role="fieldLabel"] {
            color: #eef4fb;
            font-weight: 700;
        }
        #cloudSyncPage QLabel#cloudSyncConflictEmpty {
            color: #dbe7f3;
            padding: 8px 2px;
            font-weight: 700;
        }
        #cloudSyncPage QWidget[role="section"] {
            background: rgba(16, 22, 30, 0.72);
            border: 1px solid rgba(178, 193, 210, 0.30);
            border-radius: 8px;
        }
        #cloudSyncPage QLineEdit,
        #cloudSyncPage QSpinBox {
            background: #ffffff;
            color: #111111;
            border: 1px solid #9aa6b2;
            border-radius: 6px;
            padding: 6px 8px;
            min-height: 24px;
            selection-background-color: #2563eb;
            selection-color: #ffffff;
        }
        #cloudSyncPage QLineEdit:read-only {
            background: #f1f4f7;
            color: #111111;
            border-color: #9aa6b2;
        }
        #cloudSyncPage QLineEdit:disabled,
        #cloudSyncPage QSpinBox:disabled {
            background: #eef2f6;
            color: #4b5563;
            border-color: #aeb8c3;
        }
        #cloudSyncPage QSpinBox::up-button,
        #cloudSyncPage QSpinBox::down-button {
            width: 18px;
            border: none;
            background: #e5ebf2;
        }
        #cloudSyncPage QCheckBox {
            color: #ffffff;
            font-size: 12pt;
            font-weight: 800;
            spacing: 10px;
            padding: 4px 0;
        }
        #cloudSyncPage QCheckBox::indicator {
            width: 22px;
            height: 22px;
            border-radius: 5px;
            border: 2px solid #ffffff;
            background: #ffffff;
        }
        #cloudSyncPage QCheckBox::indicator:checked {
            background: #2563eb;
            border: 2px solid #dbeafe;
            image: none;
        }
        #cloudSyncPage QCheckBox::indicator:checked:hover {
            background: #1d4ed8;
        }
        #cloudSyncPage QPushButton {
            color: #ffffff;
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                        stop:0 #4b5563, stop:0.48 #303846, stop:1 #151b24);
            border: 1px solid rgba(255,255,255,0.28);
            border-radius: 7px;
            padding: 7px 12px;
            font-weight: 700;
        }
        #cloudSyncPage QPushButton:hover {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                        stop:0 #5b6675, stop:0.48 #394353, stop:1 #1d2530);
        }
        #cloudSyncPage QPushButton:disabled {
            color: #cbd5e1;
            background: #374151;
            border-color: #64748b;
        }
        #cloudSyncPage QTableWidget {
            background: #ffffff;
            alternate-background-color: #f3f6fa;
            color: #111111;
            gridline-color: #c9d2dc;
            border: 1px solid #9aa6b2;
            border-radius: 6px;
            selection-background-color: #2563eb;
            selection-color: #ffffff;
        }
        #cloudSyncPage QTableWidget::item {
            color: #111111;
            padding: 5px 6px;
        }
        #cloudSyncPage QTableWidget::item:selected {
            color: #ffffff;
            background: #2563eb;
        }
        #cloudSyncPage QHeaderView::section {
            background: #e6edf5;
            color: #111111;
            border: 0;
            border-right: 1px solid #c9d2dc;
            border-bottom: 1px solid #9aa6b2;
            padding: 6px 8px;
            font-weight: 800;
        }
        #cloudSyncStatusBadge {
            border-radius: 12px;
            padding: 4px 12px;
            font-weight: 800;
            color: #ffffff;
        }
    )QSS");
}

QLabel* makeHelpLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setProperty("role", "help");
    label->setWordWrap(true);
    return label;
}

QLabel* makeSectionTitle(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setProperty("role", "sectionTitle");
    label->setWordWrap(true);
    return label;
}

QLabel* makeFieldLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setProperty("role", "fieldLabel");
    label->setWordWrap(true);
    return label;
}

QWidget* makeSection(QWidget* parent, const QString& title, const QString& help, QVBoxLayout** bodyLayout)
{
    auto* section = new QWidget(parent);
    section->setProperty("role", "section");
    section->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    auto* layout = new QVBoxLayout(section);
    layout->setContentsMargins(14, 12, 14, 14);
    layout->setSpacing(10);
    layout->addWidget(makeSectionTitle(title, section));
    if (!help.trimmed().isEmpty()) {
        layout->addWidget(makeHelpLabel(help, section));
    }
    if (bodyLayout) *bodyLayout = layout;
    return section;
}

void addFormRow(QGridLayout* grid, int row, const QString& labelText, QWidget* field, QWidget* parent)
{
    auto* label = makeFieldLabel(labelText, parent);
    label->setMinimumWidth(150);
    grid->addWidget(label, row, 0, Qt::AlignRight | Qt::AlignTop);
    grid->addWidget(field, row, 1);
}

void setBadge(QLabel* badge, const QString& text, const QString& background, const QString& border)
{
    if (!badge) return;
    badge->setText(text);
    badge->setStyleSheet(QStringLiteral("#cloudSyncStatusBadge { background:%1; border:1px solid %2; color:#ffffff; border-radius:12px; padding:4px 12px; font-weight:800; }")
                             .arg(background, border));
}

struct OwnerLoginResult {
    bool ok = false;
    QString token;
    QString error;
    QString challenge;
    QString enrollmentSecret;
};

OwnerLoginResult loginOwnerForDesktop(const QString& serverUrl, const QString& email, const QString& password,
                                     const QString& action = QStringLiteral("login"), const QJsonObject& payload = {})
{
    OwnerLoginResult result;
    if(!CloudSyncSettings::serverRequestsAllowed()){result.error=QString::fromUtf8("Увімкнено автономний режим. Для серверного входу змініть режим і перезапустіть застосунок.");return result;}
    QString base = serverUrl.trimmed();
    while (base.endsWith(QLatin1Char('/'))) base.chop(1);
    if (base.endsWith(QStringLiteral("/api/v1"), Qt::CaseInsensitive)) base.chop(7);
    const QUrl root(base);
    if (!root.isValid() || root.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0 || root.host().trimmed().isEmpty()) {
        result.error = QString::fromUtf8("Вкажіть коректну HTTPS-адресу центрального сервера.");
        return result;
    }
    if (action == QStringLiteral("login") && (email.trimmed().isEmpty() || password.isEmpty())) {
        result.error = QString::fromUtf8("Вкажіть email і пароль облікового запису власника на сервері.");
        return result;
    }

    QUrl endpoint = root;
    endpoint.setPath(root.path() + QStringLiteral("/api/v1/auth/") + action);
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("username"), email.trimmed());
    form.addQueryItem(QStringLiteral("password"), password);
    QNetworkRequest request(endpoint);
    const bool isLogin = action == QStringLiteral("login");
    request.setHeader(QNetworkRequest::ContentTypeHeader, isLogin ? QStringLiteral("application/x-www-form-urlencoded") : QStringLiteral("application/json"));
    request.setRawHeader("User-Agent", "CarServicePointG-OwnerSetup/2.1");

    const auto reply=WorkerNetwork::request(request, isLogin ? form.query(QUrl::FullyEncoded).toUtf8() : QJsonDocument(payload).toJson(QJsonDocument::Compact), true, 20000);
    if (reply.stopped) {
        
        result.error = QString::fromUtf8("Сервер не відповів за 20 секунд.");
        return result;
    }

    const int status = reply.status;
    const QByteArray body = reply.body;
    const QJsonDocument response = QJsonDocument::fromJson(body);
    if (reply.error != QNetworkReply::NoError || status < 200 || status >= 300 || !response.isObject()) {
        const QString detail = response.isObject() ? response.object().value(QStringLiteral("detail")).toString() : QString();
        result.error = detail.isEmpty()
            ? QString::fromUtf8("Помилка входу на сервер: %1").arg(reply.message)
            : detail;
        return result;
    }
    result.token = response.object().value(QStringLiteral("access_token")).toString().trimmed();
    result.challenge = response.object().value(QStringLiteral("challenge_token")).toString();
    result.enrollmentSecret = response.object().value(QStringLiteral("secret")).toString();
    if (response.object().value(QStringLiteral("enrollment_required")).toBool()) {
        const auto enrollment = loginOwnerForDesktop(serverUrl, {}, {}, QStringLiteral("mfa/enroll"),
            {{QStringLiteral("challenge_token"), result.challenge}});
        if (!enrollment.error.isEmpty()) { result.error = enrollment.error; return result; }
        result.enrollmentSecret = enrollment.enrollmentSecret;
    }
    result.ok = !result.token.isEmpty();
    if (!result.ok && result.challenge.isEmpty() && result.enrollmentSecret.isEmpty())
        result.error = QString::fromUtf8("Сервер не повернув токен для цього ПК.");
    return result;
}
QJsonObject payloadObject(const QString& json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) return QJsonObject{};
    QJsonObject obj = doc.object();
    QJsonObject payload = obj.value(QStringLiteral("payload")).toObject();
    if (payload.isEmpty()) payload = obj.value(QStringLiteral("data")).toObject();
    if (payload.isEmpty()) payload = obj.value(QStringLiteral("row")).toObject();
    return payload.isEmpty() ? obj : payload;
}

QString normalizedValue(const QJsonValue& value)
{
    if (value.isUndefined() || value.isNull()) return QString();
    if (value.isString()) return value.toString().trimmed();
    if (value.isBool()) return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (value.isDouble()) return QString::number(value.toDouble(), 'g', 15);
    if (value.isArray()) return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    if (value.isObject()) return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    return QString();
}

QString conflictFieldDiffSummary(const QString& localJson, const QString& remoteJson)
{
    const QJsonObject local = payloadObject(localJson);
    const QJsonObject remote = payloadObject(remoteJson);
    if (local.isEmpty() && remote.isEmpty()) return QString();

    static const QSet<QString> ignored = {
        QStringLiteral("id"), QStringLiteral("created_at"), QStringLiteral("updated_at"),
        QStringLiteral("received_at"), QStringLiteral("applied_at"), QStringLiteral("acked_at"),
        QStringLiteral("payload_json")
    };

    QStringList changed;
    QSet<QString> keys;
    for (auto it = local.constBegin(); it != local.constEnd(); ++it) keys.insert(it.key());
    for (auto it = remote.constBegin(); it != remote.constEnd(); ++it) keys.insert(it.key());
    for (const QString& key : keys) {
        if (ignored.contains(key)) continue;
        const QString lv = normalizedValue(local.value(key));
        const QString rv = normalizedValue(remote.value(key));
        if (lv != rv) changed.append(key);
    }
    changed.sort(Qt::CaseInsensitive);
    return changed.isEmpty() ? QString::fromUtf8("немає різниці по полях") : changed.join(QStringLiteral(", "));
}

} // namespace

CloudSyncPage::CloudSyncPage(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("cloudSyncPage"));
    setProperty("settings_detail_inline_title", QString::fromUtf8("Синхронізація сервера"));
    setStyleSheet(cloudSyncPageStyleSheet());

    auto* pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(0);

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setFrameShape(QFrame::NoFrame);
    pageLayout->addWidget(scrollArea);

    auto* content = new QWidget(scrollArea);
    content->setObjectName(QStringLiteral("cloudSyncScrollContent"));
    content->setMinimumWidth(620);
    content->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    auto* root = new QVBoxLayout(content);
    root->setSizeConstraint(QLayout::SetMinimumSize);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(16);
    root->setAlignment(Qt::AlignTop);
    scrollArea->setWidget(content);

    auto* intro = new QWidget(content);
    intro->setProperty("role", "section");
    intro->setMinimumHeight(92);
    intro->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    auto* introLayout = new QHBoxLayout(intro);
    introLayout->setContentsMargins(14, 12, 14, 14);
    introLayout->setSpacing(12);
    auto* introText = new QVBoxLayout();
    introText->setSpacing(4);
    auto* title = new QLabel(QString::fromUtf8("Синхронізація сервера"), intro);
    title->setStyleSheet(QStringLiteral("font-size:18pt; font-weight:800; color:#f7fbff;"));
    title->setWordWrap(true);
    auto* description = makeHelpLabel(QString::fromUtf8("Оберіть автономну роботу на цьому комп’ютері або обмін даними через сервер. Для серверного обміну потрібен вхід власника. Саме відкриття цієї сторінки не надсилає дані."), intro);
    introText->addWidget(title);
    introText->addWidget(description);
    introLayout->addLayout(introText, 1);
    m_statusBadge = new QLabel(QString::fromUtf8("Вимкнено"), intro);
    m_statusBadge->setObjectName(QStringLiteral("cloudSyncStatusBadge"));
    m_statusBadge->setAlignment(Qt::AlignCenter);
    m_statusBadge->setMinimumWidth(112);
    introLayout->addWidget(m_statusBadge, 0, Qt::AlignTop | Qt::AlignRight);
    root->addWidget(intro);

    m_enabled = new QCheckBox(QString::fromUtf8("Увімкнути хмарну синхронізацію"), content);
    m_offline = new QCheckBox(QString::fromUtf8("Автономний режим — працювати без сервера"), content);
    m_offline->setObjectName("offlineBackendMode");
    m_deviceName = new QLineEdit(content);
    m_deviceUid = new QLineEdit(content);
    m_deviceUid->setReadOnly(true);
    m_serverUrl = new QLineEdit(content);
    m_ownerEmail = new QLineEdit(content);
    m_ownerPassword = new QLineEdit(content);
    m_ownerPassword->setEchoMode(QLineEdit::Password);
    m_ownerPassword->setPlaceholderText(QString::fromUtf8("Пароль власника на сервері"));
    m_rememberOwnerPassword = new QCheckBox(QString::fromUtf8("Зберегти пароль на цьому ПК"), content);
    m_organizationUid = new QLineEdit(content);
    m_branchUid = new QLineEdit(content);
    m_apiToken = new QLineEdit(content);
    m_apiToken->setEchoMode(QLineEdit::Password);
    m_intervalSeconds = new QSpinBox(content);
    m_intervalSeconds->setRange(10, 3600);
    m_intervalSeconds->setSuffix(QString::fromUtf8(" сек"));
    for (QLineEdit* fixedField : {m_serverUrl, m_ownerEmail, m_organizationUid, m_branchUid, m_apiToken}) {
        fixedField->setReadOnly(true);
    }
    m_serverUrl->setToolTip(QString::fromUtf8("Адреса центрального сервера встановлена у застосунку."));
    m_ownerEmail->setToolTip(QString::fromUtf8("Email облікового запису власника встановлено у застосунку."));
    m_organizationUid->setToolTip(QString::fromUtf8("Ідентифікатор організації встановлено у застосунку."));
    m_branchUid->setToolTip(QString::fromUtf8("Ідентифікатор філії встановлено у застосунку."));
    m_apiToken->setToolTip(QString::fromUtf8("Токен створюється сервером після входу та не редагується вручну."));
    m_intervalSeconds->setReadOnly(true);
    m_intervalSeconds->setButtonSymbols(QAbstractSpinBox::NoButtons);

    auto* saveButton = new QPushButton(QString::fromUtf8("Зберегти"), content);
    auto* refreshButton = new QPushButton(QString::fromUtf8("Оновити статус"), content);
    auto* copyDeviceUidButton = new QPushButton(QString::fromUtf8("Скопіювати код пристрою"), content);
    auto* regenerateDeviceUidButton = new QPushButton(QString::fromUtf8("Створити новий код пристрою"), content);
    m_testButton = new QPushButton(QString::fromUtf8("Перевірити з'єднання"), content);
    m_syncNowButton = new QPushButton(QString::fromUtf8("Синхронізувати зараз"), content);
    for (auto* button : {saveButton, refreshButton, copyDeviceUidButton, regenerateDeviceUidButton, m_testButton, m_syncNowButton}) {
        button->setMinimumHeight(34);
        button->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    }

    QVBoxLayout* enableLayout = nullptr;
    auto* enableSection = makeSection(content, QString::fromUtf8("1. Увімкнення"), QString(), &enableLayout);
    enableSection->setMinimumHeight(104);
    enableLayout->addWidget(m_enabled);
    enableLayout->addWidget(m_offline);
    enableLayout->addWidget(makeHelpLabel(QString::fromUtf8("Наряди, клієнтські оплати й пости зберігаються на цьому комп’ютері. Збережіть налаштування та перезапустіть застосунок. Сервер і Docker не потрібні. Синхронізація з іншими пристроями недоступна. Повернення до сервера після автономних операцій потребує перенесення та звірки журналів."),enableSection));
    connect(m_offline,&QCheckBox::toggled,m_enabled,[this](bool checked){m_enabled->setEnabled(!checked);});
    enableLayout->addWidget(makeHelpLabel(QString::fromUtf8("Увімкніть тільки після налаштування сервера та коду пристрою."), enableSection));
    root->addWidget(enableSection);

    QVBoxLayout* mainLayout = nullptr;
    auto* mainSection = makeSection(content, QString::fromUtf8("2. Основні налаштування"), QString::fromUtf8("Адресу центрального сервера та дані організації встановлено у програмі. Ви можете змінити лише назву цього комп'ютера."), &mainLayout);
    mainSection->setMinimumHeight(236);
    auto* form = new QGridLayout();
    form->setColumnStretch(0, 0);
    form->setColumnStretch(1, 1);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(10);
    addFormRow(form, 0, QString::fromUtf8("Назва цього пристрою:"), m_deviceName, mainSection);
    addFormRow(form, 1, QString::fromUtf8("Сервер API:"), m_serverUrl, mainSection);
    addFormRow(form, 2, QString::fromUtf8("Організація:"), m_organizationUid, mainSection);
    addFormRow(form, 3, QString::fromUtf8("Філія:"), m_branchUid, mainSection);
    addFormRow(form, 4, QString::fromUtf8("Інтервал синхронізації:"), m_intervalSeconds, mainSection);
    mainLayout->addLayout(form);
    auto* mainButtons = new QHBoxLayout();
    mainButtons->setSpacing(8);
    mainButtons->addWidget(saveButton);
    mainButtons->addWidget(refreshButton);
    mainButtons->addStretch(1);
    mainLayout->addLayout(mainButtons);
    root->addWidget(mainSection);

    QVBoxLayout* uidLayout = nullptr;
    auto* uidSection = makeSection(content, QString::fromUtf8("3. Код пристрою"), QString::fromUtf8("Цей код потрібен для прив'язки комп'ютера до сервера."), &uidLayout);
    uidSection->setMinimumHeight(144);
    uidLayout->addWidget(m_deviceUid);
    auto* uidButtons = new QHBoxLayout();
    uidButtons->setSpacing(8);
    uidButtons->addWidget(copyDeviceUidButton);
    uidButtons->addWidget(regenerateDeviceUidButton);
    uidButtons->addStretch(1);
    uidLayout->addLayout(uidButtons);
    root->addWidget(uidSection);

    QVBoxLayout* tokenLayout = nullptr;
    auto* tokenSection = makeSection(content, QString::fromUtf8("4. Вхід власника та токен цього ПК"), QString::fromUtf8("Під час першого підключення увійдіть обліковим записом власника на сервері. Після перевірки цей ПК отримає окремий токен пристрою на 90 днів."), &tokenLayout);
    tokenSection->setMinimumHeight(218);
    auto* ownerForm = new QGridLayout();
    ownerForm->setColumnStretch(1, 1);
    ownerForm->setHorizontalSpacing(12);
    ownerForm->setVerticalSpacing(8);
    addFormRow(ownerForm, 0, QString::fromUtf8("Email власника:"), m_ownerEmail, tokenSection);
    addFormRow(ownerForm, 1, QString::fromUtf8("Пароль:"), m_ownerPassword, tokenSection);
    tokenLayout->addLayout(ownerForm);
    tokenLayout->addWidget(m_rememberOwnerPassword);
    auto* ownerSignInButton = new QPushButton(QString::fromUtf8("Увійти та прив'язати цей ПК"), tokenSection);
    tokenLayout->addWidget(ownerSignInButton, 0, Qt::AlignLeft);
    tokenLayout->addWidget(makeHelpLabel(QString::fromUtf8("Поточний токен пристрою зберігається локально у захищених налаштуваннях застосунку."), tokenSection));
    tokenLayout->addWidget(m_apiToken);
    root->addWidget(tokenSection);

    QVBoxLayout* checkLayout = nullptr;
    auto* checkSection = makeSection(content, QString::fromUtf8("5. Перевірка"), QString::fromUtf8("Після збереження налаштувань перевірте зв'язок із сервером або запустіть синхронізацію вручну."), &checkLayout);
    checkSection->setMinimumHeight(178);
    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(8);
    buttons->addWidget(m_testButton);
    buttons->addWidget(m_syncNowButton);
    buttons->addStretch(1);
    checkLayout->addLayout(buttons);
    m_status = new QLabel(checkSection);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_status->setWordWrap(true);
    m_status->setProperty("role", "help");
    checkLayout->addWidget(m_status);
    root->addWidget(checkSection);

    QVBoxLayout* conflictsLayout = nullptr;
    auto* conflictsSection = makeSection(content, QString::fromUtf8("6. Конфлікти синхронізації"), QString::fromUtf8("Тут з'являються зміни, які треба вирішити вручну, якщо локальна і серверна версії не збігаються."), &conflictsLayout);
    conflictsSection->setMinimumHeight(286);
    m_conflictsEmpty = makeHelpLabel(QString::fromUtf8("Конфліктів немає"), conflictsSection);
    m_conflictsEmpty->setObjectName(QStringLiteral("cloudSyncConflictEmpty"));
    m_conflictsTable = new QTableWidget(conflictsSection);
    m_conflictsTable->setColumnCount(8);
    m_conflictsTable->setHorizontalHeaderLabels({QString::fromUtf8("Дата"), QString::fromUtf8("Сутність"), QString::fromUtf8("UID"), QString::fromUtf8("Тип"), QString::fromUtf8("Змінені поля"), QString::fromUtf8("Локальна версія"), QString::fromUtf8("Серверна версія"), QString::fromUtf8("Рішення")});
    m_conflictsTable->setMinimumHeight(190);
    m_conflictsTable->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    m_conflictsTable->setAlternatingRowColors(true);
    m_conflictsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_conflictsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_conflictsTable->setWordWrap(false);
    m_conflictsTable->horizontalHeader()->setStretchLastSection(true);
    m_conflictsTable->horizontalHeader()->setMinimumHeight(32);
    m_conflictsTable->setColumnWidth(0, 138);
    m_conflictsTable->setColumnWidth(1, 110);
    m_conflictsTable->setColumnWidth(2, 160);
    m_conflictsTable->setColumnWidth(3, 130);
    m_conflictsTable->setColumnWidth(4, 210);
    m_conflictsTable->setColumnWidth(5, 180);
    m_conflictsTable->setColumnWidth(6, 180);
    m_conflictsTable->setColumnWidth(7, 110);
    m_conflictsTable->verticalHeader()->setVisible(false);
    m_conflictsTable->verticalHeader()->setDefaultSectionSize(28);
    conflictsLayout->addWidget(m_conflictsEmpty);
    conflictsLayout->addWidget(m_conflictsTable);

    auto* conflictButtons = new QHBoxLayout();
    conflictButtons->setSpacing(8);
    auto* keepLocalButton = new QPushButton(QString::fromUtf8("Залишити локальну"), conflictsSection);
    auto* acceptRemoteButton = new QPushButton(QString::fromUtf8("Прийняти серверну"), conflictsSection);
    auto* mergeButton = new QPushButton(QString::fromUtf8("Об’єднати"), conflictsSection);
    auto* deferButton = new QPushButton(QString::fromUtf8("Відкласти"), conflictsSection);
    for (auto* button : {keepLocalButton, acceptRemoteButton, mergeButton, deferButton}) {
        button->setMinimumHeight(34);
        button->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    }
    conflictButtons->addWidget(keepLocalButton);
    conflictButtons->addWidget(acceptRemoteButton);
    conflictButtons->addWidget(mergeButton);
    conflictButtons->addWidget(deferButton);
    conflictButtons->addStretch(1);
    conflictsLayout->addLayout(conflictButtons);
    root->addWidget(conflictsSection);

    connect(ownerSignInButton, &QPushButton::clicked, this, [this, ownerSignInButton]() {
        if (!m_ownerEmail || !m_ownerPassword || !m_serverUrl) return;
        const QString serverUrl = m_serverUrl->text().trimmed();
        const QString email = m_ownerEmail->text().trimmed();
        const QString password = m_ownerPassword->text();
        const bool rememberPassword = m_rememberOwnerPassword && m_rememberOwnerPassword->isChecked();
        ownerSignInButton->setEnabled(false);
        if (m_status) m_status->setText(QString::fromUtf8("Перевіряємо обліковий запис власника на сервері..."));
        auto result = std::make_shared<OwnerLoginResult>();
        auto* thread = QThread::create([serverUrl, email, password, result]() {
            *result = loginOwnerForDesktop(serverUrl, email, password);
        });
        QPointer<CloudSyncPage> self(this);
        const auto finishLogin = [self, ownerSignInButton, password, rememberPassword](const OwnerLoginResult& loginResult) {
            if (!self) return;
            ownerSignInButton->setEnabled(true);
            if (!loginResult.ok) {
                const QString message = QString::fromUtf8("Не вдалося увійти на сервер: %1").arg(loginResult.error);
                if (self->m_status) self->m_status->setText(message);
                QMessageBox::warning(self, QString::fromUtf8("Підключення до сервера"), message);
                return;
            }
            if (rememberPassword) {
                QString credentialError;
                if (!CloudSyncCredentials::saveOwnerPassword(password, &credentialError)) {
                    QMessageBox::warning(self, QString::fromUtf8("Збереження пароля"), credentialError);
                    if (self->m_rememberOwnerPassword) self->m_rememberOwnerPassword->setChecked(false);
                }
            } else {
                CloudSyncCredentials::clearOwnerPassword();
                if (self->m_ownerPassword) self->m_ownerPassword->clear();
            }
            if (self->m_apiToken) self->m_apiToken->setText(loginResult.token);
            if (self->m_enabled) self->m_enabled->setChecked(true);
            if (self->m_status) self->m_status->setText(QString::fromUtf8("Вхід виконано. Прив'язуємо цей ПК до центрального сервера..."));
            self->runBackgroundSync(false);
        };
        connect(thread, &QThread::finished, this, [self, thread, result, serverUrl, ownerSignInButton, finishLogin]() {
            thread->deleteLater();
            if (!self) return;
            if (result->challenge.isEmpty() || !result->error.isEmpty()) { finishLogin(*result); return; }
            QString prompt = QString::fromUtf8("Введіть 6-значний код із застосунку автентифікації.");
            if (!result->enrollmentSecret.isEmpty()) {
                prompt.prepend(QString::fromUtf8("Додайте ключ TOTP до застосунку автентифікації:\n%1\n\n").arg(result->enrollmentSecret));
            }
            bool accepted = false;
            const QString code = QInputDialog::getText(self, QString::fromUtf8("Двофакторна автентифікація"),
                                                       prompt, QLineEdit::Normal, {}, &accepted).trimmed();
            if (!self) return;
            if (!accepted) {
                ownerSignInButton->setEnabled(true);
                if (self->m_status) self->m_status->setText(QString::fromUtf8("Вхід скасовано."));
                return;
            }
            auto verified = std::make_shared<OwnerLoginResult>();
            auto* verifier = QThread::create([serverUrl, challenge = result->challenge, code, verified]() {
                *verified = loginOwnerForDesktop(serverUrl, {}, {}, QStringLiteral("mfa/verify"),
                    {{QStringLiteral("challenge_token"), challenge}, {QStringLiteral("code"), code}});
            });
            connect(verifier, &QThread::finished, self, [verifier, verified, finishLogin]() {
                verifier->deleteLater();
                finishLogin(*verified);
            });
            verifier->start();
        });
        thread->start();
    });
    connect(copyDeviceUidButton, &QPushButton::clicked, this, [this]() {
        const QString uid = m_deviceUid ? m_deviceUid->text().trimmed() : CloudSyncPageService::ensureIdentity().deviceUid;
        if (!uid.isEmpty() && QApplication::clipboard()) QApplication::clipboard()->setText(uid);
    });
    connect(regenerateDeviceUidButton, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(this, QString::fromUtf8("Код пристрою"),
                                  QString::fromUtf8("Створити новий device_uid для цього ПК? Старий код більше не буде ідентифікувати цей пристрій на сервері.")) != QMessageBox::Yes) {
            return;
        }
        QString uid;
        QString error;
        if (!CloudSyncPageService::regenerateDeviceUid(&uid, &error)) {
            QMessageBox::warning(this, QString::fromUtf8("Код пристрою"), error);
            return;
        }
        if (m_deviceUid) m_deviceUid->setText(uid);
        refresh();
    });

    connect(keepLocalButton, &QPushButton::clicked, this, [this]() { resolveSelectedConflict(QStringLiteral("keep_local")); });
    connect(acceptRemoteButton, &QPushButton::clicked, this, [this]() { resolveSelectedConflict(QStringLiteral("accept_remote")); });
    connect(mergeButton, &QPushButton::clicked, this, [this]() { resolveSelectedConflict(QStringLiteral("merge")); });
    connect(deferButton, &QPushButton::clicked, this, [this]() { resolveSelectedConflict(QStringLiteral("deferred")); });

    connect(saveButton, &QPushButton::clicked, this, [this]() {
        QString error;
        if (!saveSettingsFromUi(&error)) {
            QMessageBox::warning(this, QString::fromUtf8("Синхронізація"), error);
            return;
        }
        emit settingsSaved();
        refresh();
    });
    connect(refreshButton, &QPushButton::clicked, this, [this]() { refresh(); });
    connect(m_testButton, &QPushButton::clicked, this, [this]() { runBackgroundSync(true); });
    connect(m_syncNowButton, &QPushButton::clicked, this, [this]() { runBackgroundSync(false); });

    loadSettingsToUi();
    refresh();
}

void CloudSyncPage::loadSettingsToUi()
{
    const auto settings = CloudSyncSettings::load();
    QString identityError;
    const auto identity = CloudSyncPageService::ensureIdentity(&identityError);
    if (m_enabled) m_enabled->setChecked(settings.enabled);
    if (m_offline) m_offline->setChecked(settings.offlineMode);
    if (m_deviceName) m_deviceName->setText(identity.deviceName);
    if (m_deviceUid) m_deviceUid->setText(identity.deviceUid);
    if (m_serverUrl) m_serverUrl->setText(CloudSyncSettings::fixedServerUrl());
    if (m_ownerEmail) m_ownerEmail->setText(CloudSyncSettings::fixedOwnerEmail());
    if (m_organizationUid) m_organizationUid->setText(CloudSyncSettings::fixedOrganizationUid());
    if (m_branchUid) m_branchUid->setText(CloudSyncSettings::fixedBranchUid());
    if (m_apiToken) m_apiToken->setText(settings.apiToken);
    if (m_intervalSeconds) m_intervalSeconds->setValue(CloudSyncSettings::fixedSyncIntervalSeconds());
    if (m_rememberOwnerPassword) {
        QString credentialError;
        const QString savedPassword = CloudSyncCredentials::loadOwnerPassword(&credentialError);
        m_rememberOwnerPassword->setChecked(!savedPassword.isEmpty());
        if (m_ownerPassword && !savedPassword.isEmpty()) m_ownerPassword->setText(savedPassword);
        if (!credentialError.isEmpty() && m_status) m_status->setText(credentialError);
    }
}

bool CloudSyncPage::saveSettingsFromUi(QString* errorText)
{
    CloudSyncSettings::Settings settings = CloudSyncSettings::load();
    settings.enabled = m_enabled && m_enabled->isChecked();
    settings.offlineMode = m_offline && m_offline->isChecked();
    settings.serverUrl = CloudSyncSettings::fixedServerUrl();
    settings.organizationUid = CloudSyncSettings::fixedOrganizationUid();
    settings.branchUid = CloudSyncSettings::fixedBranchUid();
    settings.apiToken = m_apiToken ? m_apiToken->text().trimmed() : QString();
    settings.intervalSeconds = CloudSyncSettings::fixedSyncIntervalSeconds();
    const bool saved = CloudSyncPageService::saveSettings(settings, m_deviceName ? m_deviceName->text() : QString(), errorText);
    if (saved) AppContext::instance().refreshCloudSync();
    if(saved && settings.offlineMode!=CloudSyncSettings::localBackendEnabled())
        QMessageBox::information(this,QString::fromUtf8("Режим роботи"),QString::fromUtf8("Збережено. Закрийте та знову відкрийте застосунок, щоб змінити режим роботи."));
    return saved;
}

void CloudSyncPage::runBackgroundSync(bool onlyTestConnection)
{
    if (m_syncInProgress) return;
    QString saveError;
    if (!saveSettingsFromUi(&saveError)) {
        QMessageBox::warning(this, QString::fromUtf8("Синхронізація"), saveError);
        return;
    }
    m_syncInProgress = true;
    if (m_testButton) m_testButton->setEnabled(false);
    if (m_syncNowButton) m_syncNowButton->setEnabled(false);
    if (m_status) m_status->setText(QString::fromUtf8("Синхронізація виконується у фоновому потоці..."));

    auto result = std::make_shared<QPair<bool, QString>>(false, QString());
    auto* thread = QThread::create([onlyTestConnection, result]() {
        QString error;
        const bool ok = onlyTestConnection ? CloudSyncClient::testConnection(&error) : CloudSyncClient::syncNow(&error);
        result->first = ok;
        result->second = ok ? QString() : error;
    });
    QPointer<CloudSyncPage> self(this);
    connect(thread, &QThread::finished, this, [self, thread, result]() {
        thread->deleteLater();
        if (!self) return;
        self->m_syncInProgress = false;
        if (self->m_testButton) self->m_testButton->setEnabled(true);
        if (self->m_syncNowButton) self->m_syncNowButton->setEnabled(true);
        if (self->m_status) {
            self->m_status->setText(result->first
                ? QString::fromUtf8("Операцію синхронізації завершено успішно.")
                : QString::fromUtf8("Помилка синхронізації: %1").arg(result->second.trimmed()));
        }
        if (!result->first) {
            QMessageBox::warning(self, QString::fromUtf8("Синхронізація"),
                                 QString::fromUtf8("Операцію не виконано: %1").arg(result->second.trimmed()));
        }
        self->refresh();
    });
    thread->start();
}

void CloudSyncPage::refreshConflicts()
{
    if (!m_conflictsTable) return;
    QString error;
    const auto rows = CloudSyncPageService::listConflicts(50, &error);
    CloudConflictsTableGuard guard(m_conflictsTable);
    m_conflictsTable->setRowCount(0);
    if (!error.trimmed().isEmpty()) {
        if (m_conflictsEmpty) {
            m_conflictsEmpty->setText(QString::fromUtf8("Не вдалося завантажити конфлікти: %1").arg(error.trimmed()));
            m_conflictsEmpty->setVisible(true);
        }
        m_conflictsTable->setVisible(true);
        return;
    }
    const bool hasConflicts = !rows.isEmpty();
    if (m_conflictsEmpty) {
        m_conflictsEmpty->setText(QString::fromUtf8("Конфліктів немає"));
        m_conflictsEmpty->setVisible(!hasConflicts);
    }
    m_conflictsTable->setVisible(true);
    for (const auto& conflict : rows) {
        const int row = m_conflictsTable->rowCount();
        m_conflictsTable->insertRow(row);
        auto* marker = new QTableWidgetItem(conflict.createdAt);
        marker->setData(Qt::UserRole, conflict.conflictUid);
        m_conflictsTable->setItem(row, 0, marker);
        m_conflictsTable->setItem(row, 1, new QTableWidgetItem(conflict.entityType));
        m_conflictsTable->setItem(row, 2, new QTableWidgetItem(conflict.entityUid));
        m_conflictsTable->setItem(row, 3, new QTableWidgetItem(conflict.conflictType));
        const QString localPayload = conflict.localPayloadJson;
        const QString remotePayload = conflict.remotePayloadJson;
        const QString diffSummary = conflictFieldDiffSummary(localPayload, remotePayload);
        auto* diffItem = new QTableWidgetItem(diffSummary.left(220));
        diffItem->setToolTip(diffSummary);
        auto* localItem = new QTableWidgetItem(localPayload.left(180));
        localItem->setToolTip(localPayload);
        auto* remoteItem = new QTableWidgetItem(remotePayload.left(180));
        remoteItem->setToolTip(remotePayload);
        m_conflictsTable->setItem(row, 4, diffItem);
        m_conflictsTable->setItem(row, 5, localItem);
        m_conflictsTable->setItem(row, 6, remoteItem);
        m_conflictsTable->setItem(row, 7, new QTableWidgetItem(conflict.resolution));
    }
}

void CloudSyncPage::resolveSelectedConflict(const QString& resolution)
{
    if (!m_conflictsTable || !m_conflictsTable->currentItem()) return;
    const int row = m_conflictsTable->currentRow();
    if (row < 0 || !m_conflictsTable->item(row, 0)) return;
    const QString conflictUid = m_conflictsTable->item(row, 0)->data(Qt::UserRole).toString();
    if (conflictUid.trimmed().isEmpty()) return;
    QString error;
    if (!CloudSyncPageService::resolveConflict(conflictUid, resolution, &error)) {
        QMessageBox::warning(this, QString::fromUtf8("Конфлікти"), error);
        return;
    }
    refresh();
}

void CloudSyncPage::refresh()
{
    if (m_refreshInProgress) {
        m_refreshPending = true;
        return;
    }
    m_refreshInProgress = true;
    const quint64 generation = ++m_refreshGeneration;
    QPointer<CloudSyncPage> self(this);
    auto* thread = QThread::create([self, generation]() {
        QString err;
        const auto snapshot = CloudSyncPageService::status(&err);
        QString conflictsError;
        const auto rows = CloudSyncPageService::listConflicts(50, &conflictsError);
        QMetaObject::invokeMethod(qApp, [self, generation, snapshot, err, rows, conflictsError]() {
            if (!self || self->m_refreshGeneration != generation) return;
            self->m_refreshInProgress = false;

            const int pending = snapshot.pendingOutbox;
            const int inboxPending = snapshot.pendingInbox;
            const int inboxConflicts = snapshot.inboxConflicts;
            const int conflicts = snapshot.pendingConflicts;
            const QString uid = snapshot.deviceUid;
            if (self->m_deviceUid && self->m_deviceUid->text().trimmed() != uid) self->m_deviceUid->setText(uid);
            if (self->m_deviceName) {
                const QString name = snapshot.deviceName;
                if (self->m_deviceName->text().trimmed().isEmpty() || self->m_deviceName->text().trimmed() == QStringLiteral("CarService Desktop")) self->m_deviceName->setText(name);
            }
            const QString enabled = snapshot.enabled ? QString::fromUtf8("увімкнена") : QString::fromUtf8("вимкнена");
            const bool hasError = !err.trimmed().isEmpty() || !snapshot.lastError.trimmed().isEmpty();
            if (CloudSyncSettings::localBackendEnabled()) {
                setBadge(self->m_statusBadge, QString::fromUtf8("Автономно"), QStringLiteral("#047857"), QStringLiteral("#a7f3d0"));
            } else if (hasError) {
                setBadge(self->m_statusBadge, QString::fromUtf8("Помилка"), QStringLiteral("#b91c1c"), QStringLiteral("#fecaca"));
            } else if (snapshot.enabled && (pending > 0 || inboxPending > 0 || inboxConflicts > 0 || conflicts > 0)) {
                setBadge(self->m_statusBadge, QString::fromUtf8("Очікує обміну"), QStringLiteral("#92400e"), QStringLiteral("#fde68a"));
            } else if (snapshot.enabled && !snapshot.lastSuccessfulSyncAt.isEmpty()) {
                setBadge(self->m_statusBadge, QString::fromUtf8("Підключено"), QStringLiteral("#047857"), QStringLiteral("#a7f3d0"));
            } else if (snapshot.enabled) {
                setBadge(self->m_statusBadge, QString::fromUtf8("Увімкнено"), QStringLiteral("#2563eb"), QStringLiteral("#bfdbfe"));
            } else {
                setBadge(self->m_statusBadge, QString::fromUtf8("Вимкнено"), QStringLiteral("#4b5563"), QStringLiteral("#cbd5e1"));
            }
            if (self->m_status) {
                self->m_status->setText(QString::fromUtf8("Стан: %1\nОстання відправка на сервер: %7\nОстаннє отримання із сервера: %8\nОстання перевірка: %9\n\nТехнічна інформація:\nКод пристрою: %2\nОчікують відправки: %3\nОчікують обробки із сервера: %4\nКонфлікти у вхідних змінах: %5\nНерозв'язані конфлікти: %6%10%11")
                                            .arg(enabled, uid)
                                            .arg(pending < 0 ? 0 : pending)
                                            .arg(inboxPending < 0 ? 0 : inboxPending)
                                            .arg(inboxConflicts < 0 ? 0 : inboxConflicts)
                                            .arg(conflicts < 0 ? 0 : conflicts)
                                            .arg(snapshot.lastPushAt.isEmpty() ? QString::fromUtf8("немає") : snapshot.lastPushAt,
                                                 snapshot.lastPullAt.isEmpty() ? QString::fromUtf8("немає") : snapshot.lastPullAt,
                                                 QDateTime::currentDateTime().toString(QStringLiteral("dd.MM.yy HH:mm:ss")),
                                                 err.trimmed().isEmpty() ? QString() : QString::fromUtf8("\nПомилка SQLite: %1").arg(err),
                                                 snapshot.lastError.trimmed().isEmpty() ? QString() : QString::fromUtf8("\nОстання помилка sync: %1").arg(snapshot.lastError))
                                       + QString::fromUtf8("\nОстанній завершений обмін: %1").arg(
                                           snapshot.lastSuccessfulSyncAt.isEmpty() ? QString::fromUtf8("немає")
                                               : DateFormats::parseDateTime(snapshot.lastSuccessfulSyncAt)
                                                     .toLocalTime().toString(QStringLiteral("dd.MM.yy HH:mm:ss"))));
            }

            if (self->m_conflictsTable) {
                CloudConflictsTableGuard guard(self->m_conflictsTable);
                self->m_conflictsTable->setRowCount(0);
                if (!conflictsError.trimmed().isEmpty()) {
                    if (self->m_conflictsEmpty) {
                        self->m_conflictsEmpty->setText(QString::fromUtf8("Не вдалося завантажити конфлікти: %1").arg(conflictsError.trimmed()));
                        self->m_conflictsEmpty->setVisible(true);
                    }
                    self->m_conflictsTable->setVisible(true);
                } else {
                    const bool hasConflicts = !rows.isEmpty();
                    if (self->m_conflictsEmpty) {
                        self->m_conflictsEmpty->setText(QString::fromUtf8("Конфліктів немає"));
                        self->m_conflictsEmpty->setVisible(!hasConflicts);
                    }
                    self->m_conflictsTable->setVisible(true);
                    for (const auto& conflict : rows) {
                        const int row = self->m_conflictsTable->rowCount();
                        self->m_conflictsTable->insertRow(row);
                        auto* marker = new QTableWidgetItem(conflict.createdAt);
                        marker->setData(Qt::UserRole, conflict.conflictUid);
                        self->m_conflictsTable->setItem(row, 0, marker);
                        self->m_conflictsTable->setItem(row, 1, new QTableWidgetItem(conflict.entityType));
                        self->m_conflictsTable->setItem(row, 2, new QTableWidgetItem(conflict.entityUid));
                        self->m_conflictsTable->setItem(row, 3, new QTableWidgetItem(conflict.conflictType));
                        const QString localPayload = conflict.localPayloadJson;
                        const QString remotePayload = conflict.remotePayloadJson;
                        const QString diffSummary = conflictFieldDiffSummary(localPayload, remotePayload);
                        auto* diffItem = new QTableWidgetItem(diffSummary.left(220));
                        diffItem->setToolTip(diffSummary);
                        auto* localItem = new QTableWidgetItem(localPayload.left(180));
                        localItem->setToolTip(localPayload);
                        auto* remoteItem = new QTableWidgetItem(remotePayload.left(180));
                        remoteItem->setToolTip(remotePayload);
                        self->m_conflictsTable->setItem(row, 4, diffItem);
                        self->m_conflictsTable->setItem(row, 5, localItem);
                        self->m_conflictsTable->setItem(row, 6, remoteItem);
                        self->m_conflictsTable->setItem(row, 7, new QTableWidgetItem(conflict.resolution));
                    }
                }
            }

            if (self->m_refreshPending) {
                self->m_refreshPending = false;
                self->refresh();
            }
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}
