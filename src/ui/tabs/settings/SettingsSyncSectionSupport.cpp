#include "ui/tabs/settings/SettingsSyncSectionSupport.h"
#include <functional>

#include "ui/tabs/settings/SettingsPageDataSupport.h"
#include "core/sync/SyncPayloadUtils.h"
#include "core/util/PlateUtils.h"
#include "core/services/settings/SyncSettingsService.h"
#include "core/services/settings/UiSettingsService.h"
#include "core/sync/BluetoothSyncManager.h"
#include "ui/theme/ThemeManager.h"
#include "ui/common/dialogs/ReceivedScansDialog.h"
#include "runtime/AsyncTaskHandle.h"
#include "runtime/AsyncThreadScope.h"

#include <QApplication>
#include <algorithm>
#include <limits>
#include <memory>
#include <QButtonGroup>
#include "ui/tabs/settings/LabeledToggle.h"
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMap>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

namespace {

static QString contrastCheckboxRuleBlock(const QString& selector = QStringLiteral("QCheckBox"))
{
    return QStringLiteral(
        "%1::indicator{width:18px; height:18px;}"
        "%1::indicator:unchecked{background:#ffffff; border:2px solid #111111; border-radius:3px;}"
        "%1::indicator:checked{background:#ffffff; border:2px solid #111111; border-radius:3px; image:url(:/assets/checkbox_checked_black.png);}"
        "%1::indicator:checked:hover{background:#f8fafc;}"
    ).arg(selector);
}

bool isDarkUi()
{
    return ThemeManager::currentTheme() == QStringLiteral("graphite");
}

QString cPrimary()
{
    return isDarkUi() ? QStringLiteral("#f7fbff") : QStringLiteral("#111111");
}

QString cSecondary()
{
    return isDarkUi() ? QStringLiteral("#c7d0db") : QStringLiteral("#4b5563");
}

QString cListText()
{
    return isDarkUi() ? QStringLiteral("#f7fbff") : QStringLiteral("#111111");
}

struct WindowsWifiScanResult {
    QList<SyncSettingsService::WindowsWifiNetworkItem> networks;
};

static bool looksLikeMobileBluetoothName(const QString& rawName)
{
    const QString name = rawName.trimmed();
    if (name.isEmpty()) return false;
    const QString lower = name.toLower();
    const QStringList positive = {
        QStringLiteral("iphone"), QStringLiteral("ipad"), QStringLiteral("samsung"), QStringLiteral("galaxy"),
        QStringLiteral("redmi"), QStringLiteral("xiaomi"), QStringLiteral("mi "), QStringLiteral("poco"),
        QStringLiteral("huawei"), QStringLiteral("honor"), QStringLiteral("pixel"), QStringLiteral("oneplus"),
        QStringLiteral("motorola"), QStringLiteral("nokia"), QStringLiteral("realme"), QStringLiteral("oppo"),
        QStringLiteral("vivo"), QStringLiteral("phone"), QStringLiteral("mobile")
    };
    for (const QString& token : positive) if (lower.contains(token)) return true;
    const QStringList negative = {
        QStringLiteral("tv"), QStringLiteral("headset"), QStringLiteral("speaker"), QStringLiteral("audio"),
        QStringLiteral("soundbar"), QStringLiteral("watch"), QStringLiteral("printer"), QStringLiteral("mouse"),
        QStringLiteral("keyboard"), QStringLiteral("earbuds"), QStringLiteral("voombox")
    };
    for (const QString& token : negative) if (lower.contains(token)) return false;
    return false;
}

static int bluetoothDeviceUiPriority(const BluetoothSyncManager::DeviceItem& device)
{
    int score = 0;
    if (device.connected) score += 200;
    if (device.preferred) score += 140;
    if (device.paired) score += 80;
    if (looksLikeMobileBluetoothName(device.name)) score += 60;
    const QString lower = device.name.trimmed().toLower();
    if (lower.startsWith(QStringLiteral("bluetooth "))) score -= 20;
    if (lower.contains(QStringLiteral("tv")) || lower.contains(QStringLiteral("speaker")) || lower.contains(QStringLiteral("audio"))) score -= 40;
    return score;
}

static QPixmap makePlaceholderQr(const QString& line1, const QString& line2 = QString())
{
    QPixmap pm(296, 296);
    pm.fill(Qt::white);
    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QColor(0,0,0));
    QFont f = painter.font();
    f.setPointSize(14);
    f.setBold(true);
    painter.setFont(f);
    painter.drawRect(0,0,295,295);
    painter.drawText(pm.rect().adjusted(18,60,-18,-120), Qt::AlignCenter | Qt::TextWordWrap, line1);
    if (!line2.isEmpty()) {
        f.setPointSize(10);
        f.setBold(false);
        painter.setFont(f);
        painter.drawText(pm.rect().adjusted(18,180,-18,-18), Qt::AlignCenter | Qt::TextWordWrap, line2);
    }
    return pm;
}

static QPixmap makeScannablePairingQr(const QString& desktopName, const QString& uuidText, const QString& codeText)
{
    return makePlaceholderQr(QString::fromUtf8("PAIR QR"), desktopName + QStringLiteral("\n") + uuidText.left(18) + QStringLiteral("\n") + codeText);
}

static QPixmap makeScannableWifiQr(const QString& ssidText, const QString& passwordText)
{
    Q_UNUSED(passwordText);
    return makePlaceholderQr(QString::fromUtf8("WIFI QR"), ssidText);
}

} // namespace

QWidget* SettingsSyncSectionSupport::build(QWidget* owner, const std::function<void()>& onRefreshAll)
{
        auto* outer = new QWidget(owner);
        auto* outerL = new QVBoxLayout(outer);
        outerL->setContentsMargins(0,0,0,0);
        outerL->setSpacing(0);

        auto* scroll = new QScrollArea(outer);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
        outerL->addWidget(scroll, 1);

        auto* w = new QWidget(scroll);
        scroll->setWidget(w);
        w->setStyleSheet(QStringLiteral(
            "QFrame#syncCard{background:rgba(18,24,31,0.18); border:1px solid rgba(255,255,255,26); border-radius:14px;}"
            "QToolButton#syncSectionToggle{background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(96,110,128,0.94), stop:1 rgba(55,66,80,0.98));"
            " border:1px solid rgba(255,255,255,0.14); border-radius:10px; color:#f2f6fb; font-weight:700; padding:8px 12px; text-align:left;}"
            "QToolButton#syncSectionToggle:hover{border:1px solid rgba(200,220,255,0.40);}"
            "QTableWidget{background:rgba(255,255,255,0.88); color:#10161d; border:1px solid rgba(255,255,255,0.16); border-radius:10px; gridline-color:rgba(25,31,39,0.10);}"
            "QHeaderView::section{background:rgba(245,248,252,0.94); color:#111822; padding:6px; border:1px solid rgba(20,28,36,0.08); font-weight:700;}"
        ));
        auto* l = new QVBoxLayout(w);
        l->setContentsMargins(18,18,18,18);
        l->setSpacing(12);

        const QString defaultInboxRoot = QDir::toNativeSeparators(SyncPayloadUtils::defaultSyncInboxDir());

        const auto networkUiFontSettings = UiSettingsService::loadFontSettings();
        const QString networkUiFontFamily = networkUiFontSettings.fontFamily;
        const int networkUiBasePt = std::max(9, networkUiFontSettings.fontSize);
        const int networkUiSmallPt = std::max(8, networkUiBasePt - 3);
        const int networkUiBodyPt = std::max(9, networkUiBasePt - 2);
        const int networkUiLabelPt = std::max(10, networkUiBasePt - 1);
        const int networkUiInfoPt = std::max(10, networkUiBasePt);
        const int networkUiTitlePt = std::max(13, networkUiBasePt + 2);

        auto makeNetworkFont = [&](int pt, QFont::Weight weight = QFont::Normal) {
            QFont f(networkUiFontFamily);
            f.setPointSize(pt);
            f.setWeight(weight);
            return f;
        };

        auto* title = new QLabel(QString::fromUtf8("Підключення телефону / мережа"), w);
        title->setStyleSheet(QString("font-size:14pt; font-weight:800; color:%1;").arg(cPrimary()));
        title->hide();
        l->addWidget(title);

        auto* syncTopActions = new QHBoxLayout();
        auto* openAdvancedSync = new QPushButton(QString::fromUtf8("Розширені налаштування"), w);
        openAdvancedSync->setFixedWidth(260);
        openAdvancedSync->hide();
        syncTopActions->addStretch(1);
        syncTopActions->addWidget(openAdvancedSync, 0);
        l->addLayout(syncTopActions);

        auto* enabled = new LabeledToggle(QString::fromUtf8("Увімкнути модуль сінхронізації"), w);
        const auto syncSettings = SyncSettingsService::load();
        enabled->setChecked(syncSettings.enabled);

        auto* form = new QFormLayout();
        form->setHorizontalSpacing(12);
        form->setVerticalSpacing(10);

        auto* desktopName = new QLineEdit(syncSettings.desktopName, w);
        auto* pairingCode = new QLineEdit(syncSettings.desktopCode, w);
        auto* wifiSsid = new QLineEdit(syncSettings.wifiSsid, w);
        auto* wifiPassword = new QLineEdit(syncSettings.wifiPassword, w);
        auto* transport = new QComboBox(w);
        transport->addItem(QString::fromUtf8("Wi‑Fi / JSON inbox"), QStringLiteral("local_json"));
        transport->addItem(QString::fromUtf8("Bluetooth"), QStringLiteral("bluetooth"));
        const QString savedTransport = syncSettings.transport;
        const int transportIndex = transport->findData(savedTransport);
        transport->setCurrentIndex(transportIndex >= 0 ? transportIndex : 0);

        auto* inboxPath = new QLineEdit(syncSettings.inboxDir.isEmpty() ? defaultInboxRoot : syncSettings.inboxDir, w);
        inboxPath->setPlaceholderText(defaultInboxRoot);

        auto* inboxRow = new QWidget(w);
        auto* inboxLay = new QHBoxLayout(inboxRow);
        inboxLay->setContentsMargins(0,0,0,0);
        inboxLay->setSpacing(8);
        auto* browseInbox = new QPushButton(QString::fromUtf8("Вибрати"), inboxRow);
        browseInbox->setFixedWidth(120);
        auto* openInbox = new QPushButton(QString::fromUtf8("Відкрити"), inboxRow);
        openInbox->setFixedWidth(120);
        auto* openProcessed = new QPushButton(QString::fromUtf8("Processed"), inboxRow);
        openProcessed->setFixedWidth(120);
        inboxLay->addWidget(inboxPath, 1);
        inboxLay->addWidget(browseInbox, 0);
        inboxLay->addWidget(openInbox, 0);
        inboxLay->addWidget(openProcessed, 0);

        auto* autoPlate = new LabeledToggle(QString::fromUtf8("Автозаповнення номера авто у формі нового наряду"), w);
        autoPlate->setChecked(syncSettings.autoPlate);
        auto* autoParts = new LabeledToggle(QString::fromUtf8("Автоімпорт запчастин з накладної / штрихкоду у наряд після створення"), w);
        autoParts->setChecked(syncSettings.autoParts);
        auto* queuePhotos = new LabeledToggle(QString::fromUtf8("Приймати фото / PDF-скани до наряду та зберігати їх локально"), w);
        queuePhotos->setChecked(syncSettings.acceptPhotos);

        form->addRow(QString::fromUtf8("Назва десктопного вузла:"), desktopName);
        form->addRow(QString::fromUtf8("Код пристрою / каналу:"), pairingCode);
        form->addRow(QString::fromUtf8("Wi‑Fi SSID / хотспот:"), wifiSsid);
        form->addRow(QString::fromUtf8("Пароль Wi‑Fi:"), wifiPassword);
        form->addRow(QString::fromUtf8("Канал обміну:"), transport);
        form->addRow(QString::fromUtf8("Вхідна папка JSON:"), inboxRow);

        auto makeCard = [w]() {
            auto* frame = new QFrame(w);
            frame->setObjectName(QStringLiteral("syncCard"));
            auto* frameL = new QVBoxLayout(frame);
            frameL->setContentsMargins(14,14,14,14);
            frameL->setSpacing(10);
            return std::make_pair(frame, frameL);
        };
        auto makeCollapsible = [w](const QString& title, bool expanded) {
            auto* host = new QWidget(w);
            auto* hostL = new QVBoxLayout(host);
            hostL->setContentsMargins(0,0,0,0);
            hostL->setSpacing(8);
            auto* toggle = new QToolButton(host);
            toggle->setObjectName(QStringLiteral("syncSectionToggle"));
            toggle->setText(title);
            toggle->setCheckable(true);
            toggle->setChecked(expanded);
            toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            toggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
            toggle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            auto* body = new QWidget(host);
            auto* bodyL = new QVBoxLayout(body);
            bodyL->setContentsMargins(0,0,0,0);
            bodyL->setSpacing(10);
            body->setVisible(expanded);
            QObject::connect(toggle, &QToolButton::toggled, body, &QWidget::setVisible);
            QObject::connect(toggle, &QToolButton::toggled, toggle, [toggle](bool on) {
                toggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
            });
            hostL->addWidget(toggle);
            hostL->addWidget(body);
            return std::make_tuple(host, body, bodyL);
        };


        return outer;
    

}
