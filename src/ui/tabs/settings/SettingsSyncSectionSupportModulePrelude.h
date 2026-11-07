#pragma once

// P2 architecture module prelude.
// Generated during monolith-removal p2 so converted implementation slices
// compile as separate .cpp modules without being text-included by the owner.
// Owner: src/ui/tabs/settings/SettingsSyncSectionSupport.cpp

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
#include <QCheckBox>
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

// File-local helpers required by converted slices.
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

}
