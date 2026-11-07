#include "app/ClickSoundFilter.h"
#include "ui/common/dialogs/MessageBoxUtils.h"
#include "ui/tabs/settings/SettingsPage.h"
#include "ui/tabs/settings/SettingsPersonnelManageSectionSupport.h"
#include "ui/tabs/settings/SettingsDetailUiSupport.h"

#include "app/App.h"
#include "core/services/LegacyMigrationService.h"
#include "core/services/NativeBackupRestoreService.h"
#include "app/AppContext.h"
#include "core/backup/BackupManager.h"
#include "core/backup/CrashRecoveryManager.h"
#include "core/backup/RestoreCoordinator.h"
#include "core/backup/RecoveryCoordinator.h"
#include "ui/tabs/settings/AutoBackupSettingsSection.h"
#include "ui/tabs/settings/SettingsGeneralSection.h"
#include "ui/tabs/settings/SettingsUiSection.h"
#include "ui/tabs/settings/SettingsGoogleSheetsSectionSupport.h"
#include "ui/tabs/settings/SettingsSyncSectionSupport.h"
#include "runtime/AsyncTaskHandle.h"
#include "runtime/AsyncThreadScope.h"
#include "ui/tabs/settings/SettingsBrandingSectionSupport.h"
#include "ui/tabs/settings/SettingsPersonnelSection.h"
#include "ui/tabs/settings/SettingsIntegrationsSection.h"
#include "ui/tabs/settings/SettingsFinanceSection.h"
#include "ui/tabs/settings/SettingsBackupSection.h"
#include "core/services/legacyimport/LegacyImportUiSupport.h"
#include "core/sync/SyncPayloadUtils.h"
#include "core/util/PlateUtils.h"
#include "core/util/OilServiceClassifier.h"
#include "core/services/settings/SyncSettingsService.h"
#include "core/services/settings/BrandingAssetsSettingsService.h"
#include "core/services/settings/BrandingSettingsService.h"
#include "core/services/settings/PersonnelSettingsService.h"
#include "core/services/settings/GeneralSettingsService.h"
#include "core/services/settings/UiSettingsService.h"
#include "core/sync/BluetoothSyncManager.h"
#include "ui/theme/ThemeManager.h"
#include "integrations/opendatabot/OpenDataBotClient.h"
#include "app/update/UpdateService.h"
#include "core/util/FormatUtils.h"
#include "ui/common/dialogs/MasterEditDialog.h"
#include "ui/common/dialogs/ReceivedScansDialog.h"

#include <QDate>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QButtonGroup>
#include <QListWidget>
#include <QStackedWidget>
#include <QSplitter>
#include <QGraphicsDropShadowEffect>
#include <QColor>
#include <QCheckBox>
#include <QSlider>
#include <QToolButton>
#include <QLineEdit>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QDateTime>
#include <QDateEdit>
#include <QElapsedTimer>
#include <QDateTimeEdit>
#include <QDir>
#include <QDesktopServices>
#include <QUrl>
#include <QMessageBox>
#include <QDialog>
#include <QFormLayout>
#include <QComboBox>
#include <QPixmap>
#include <QMouseEvent>
#include <QPainter>
#include <QImage>
#include <QApplication>
#include <QProgressBar>
#include <QGroupBox>
#include <QTableWidget>
#include <QTableView>
#include <QTreeWidget>
#include <QHeaderView>
#include <QDoubleSpinBox>
#include <QJsonDocument>
#include <QListView>
#include <QJsonObject>
#include <QTabWidget>
#include <QToolBar>
#include <QScrollArea>
#include <QScrollBar>
#include <QUuid>
#include <QRegularExpression>
#include <QVector>


#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QJsonArray>
#include <QFontComboBox>
#include <QSpinBox>
#include <QColorDialog>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QStandardPaths>
#include <QPointer>
#include <QProgressDialog>
#include <QThread>
#include <memory>
#include <algorithm>
#include <functional>


// Forward declarations for theme helpers used by outsource settings UI.
static bool isDarkUi();

SettingsPage::SettingsPage(QWidget* parent)
    : QWidget(parent)
{
    m_itemsByCategory[Category::ExternalDisplay] = {{"ui.external_display", QString::fromUtf8("Налаштування зовнішнього екрана")}};
    // Items (right list) for each category
    m_itemsByCategory[Category::General] = {
        {"general.sounds",      QString::fromUtf8("Звуки та голос")},
        {"general.planning_reminders", QString::fromUtf8("Нагадування планування")},
        {"general.animations",  QString::fromUtf8("Ефекти / Анімації")},
        {"general.branding",    QString::fromUtf8("Брендинг СТО")},
        {"general.print",       QString::fromUtf8("PDF / друк")},
        {"general.updates",     QString::fromUtf8("Оновлення")},
        {"general.work_log",    QString::fromUtf8("Журнал робіт")},
    };

    m_itemsByCategory[Category::Ui] = {
        {"ui.font",             QString::fromUtf8("Шрифт")},
        {"ui.theme",            QString::fromUtf8("Тема інтерфейсу")},
        {"ui.tables",           QString::fromUtf8("Таблиці та відображення")},
    };

    m_itemsByCategory[Category::Personnel] = {
        {"staff.manage", QString::fromUtf8("Майстри та адміністратори")},
        {"staff.posts", QString::fromUtf8("Працівники за постами")},
        {"staff.occupancy", QString::fromUtf8("Фактична зайнятість і завантаження")},
    };

    m_itemsByCategory[Category::Integrations] = {
        {"int.invoices",        QString::fromUtf8("Розпізнавання накладних")},
        {"int.apikeys",         QString::fromUtf8("API")},
        {"int.suppliers",       QString::fromUtf8("Постачальники")},
        {"int.directories",     QString::fromUtf8("Діректорія")},
        {"int.autobackup",      QString::fromUtf8("Автобекап")},
        {"int.sync",            QString::fromUtf8("Хмарна синхронізація")},
    };

    m_itemsByCategory[Category::Finance] = {
        {"fin.defaults",        QString::fromUtf8("Націнка запчастин")},
        {"out.defaults",        QString::fromUtf8("Аутсорс")},
    };

    m_itemsByCategory[Category::Reports] = {
        {"rep.pdf",             QString::fromUtf8("PDF / друк")},
    };

    m_itemsByCategory[Category::Outsource] = {
    };

    m_itemsByCategory[Category::Synchronization] = {
        {"sync.server",         QString::fromUtf8("Синхронізація сервера")},
        {"sync.phone",          QString::fromUtf8("Синхронізація телефону")},
    };

    m_itemsByCategory[Category::Security] = {
        {"sec.access",          QString::fromUtf8("Захист доступу")},
        {"sec.roles_access",    QString::fromUtf8("Користувачі, ролі та права")},
        {"sec.data",            QString::fromUtf8("Безпека даних")},
        {"sec.encrypt_params",  QString::fromUtf8("Параметри шифрування")},
    };

    m_refreshAllDebounce = new QTimer(this);
    m_refreshAllDebounce->setSingleShot(true);
    connect(m_refreshAllDebounce, &QTimer::timeout, this, [this]() { emit requestRefreshAll(); });

    setUpdatesEnabled(false);
    buildUi();
    setCategory(Category::General);
    setUpdatesEnabled(true);
}

void SettingsPage::queueRefreshAll(int delayMs)
{
    if (!m_refreshAllDebounce) {
        emit requestRefreshAll();
        return;
    }
    if (delayMs <= 0) {
        m_refreshAllDebounce->stop();
        m_refreshAllDebounce->start(0);
        return;
    }
    if (!m_refreshAllDebounce->isActive() || m_refreshAllDebounce->remainingTime() > delayMs) {
        m_refreshAllDebounce->start(delayMs);
    }
}

QString SettingsPage::categoryTitle(Category c)
{
    switch (c) {
    case Category::General:      return QString::fromUtf8("Загальні");
    case Category::Ui:           return QString::fromUtf8("Інтерфейс");
    case Category::Personnel:    return QString::fromUtf8("Персонал");
    case Category::Integrations: return QString::fromUtf8("Інтеграції");
    case Category::Finance:      return QString::fromUtf8("Фінанси");
    case Category::Reports:      return QString::fromUtf8("Звіти");
    case Category::Outsource:    return QString::fromUtf8("Аутсорс");
    case Category::Security:     return QString::fromUtf8("Безпека та шифрування");
    case Category::Synchronization: return QString::fromUtf8("Синхронізація");
    case Category::ExternalDisplay: return QString::fromUtf8("Зовнішній екран");
    }
    return QString();
}
