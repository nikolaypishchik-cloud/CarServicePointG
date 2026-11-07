#include "ui/tabs/settings/AutoBackupSettingsSection.h"
#include "runtime/AppRuntime.h"
#include "ui/tabs/settings/LabeledToggle.h"

#include "app/App.h"
#include "app/AppContext.h"
#include "core/backup/BackupManager.h"
#include "core/backup/BackupManifestService.h"
#include "core/backup/BackupPaths.h"
#include "core/backup/CrashRecoveryManager.h"
#include "core/backup/RecoveryCoordinator.h"
#include "core/backup/RestoreCoordinator.h"
#include "core/services/settings/AutoBackupSettingsService.h"
#include "ui/common/dialogs/MessageBoxUtils.h"
#include "ui/tabs/settings/RecoveryManagerDialog.h"
#include "ui/tabs/settings/RestoreManagerDialog.h"
#include "ui/tabs/settings/SettingsPageDataSupport.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QResizeEvent>
#include <QSpinBox>
#include <QStorageInfo>
#include <QUrl>
#include <QVBoxLayout>

namespace {

QString sectionGroupStyle()
{
    return QStringLiteral(
        "QGroupBox { color: white; font-size: 12pt; font-weight: 700; margin-top: 10px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; }"
    );
}

void setAdvancedGroupBodyVisible(QGroupBox* group, bool visible)
{
    if (!group) return;
    const auto children = group->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget* child : children) {
        if (child) child->setVisible(visible);
    }
    group->setMinimumHeight(visible ? 0 : 44);
    group->updateGeometry();
}

QString settingsInputStyle()
{
    return QStringLiteral(
        "background:#ffffff;"
        "color:#111111;"
        "border:1px solid rgba(22,28,35,0.22);"
        "border-radius:10px;"
        "padding:6px 10px;"
        "selection-background-color:rgba(128,170,235,0.28);"
        "selection-color:#111111;"
    );
}

QString settingsSpinStyle()
{
    return settingsInputStyle()
        + QStringLiteral(
            "QSpinBox{padding-right:34px;}"
            "QSpinBox::up-button,QSpinBox::down-button{width:22px; subcontrol-origin:border; background:#eef2f7; border-left:1px solid #c7ccd4;}"
            "QSpinBox::up-button{border-top-right-radius:10px;}"
            "QSpinBox::down-button{border-bottom-right-radius:10px;}"
            "QSpinBox::up-arrow,QSpinBox::down-arrow{width:9px; height:9px;}"
        );
}

QString compactActionButtonStyle()
{
    return QStringLiteral(
        "QPushButton{"
        "min-height:34px; padding:6px 14px; font-weight:700; color:#f8fbff;"
        "background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "stop:0 rgba(113,129,149,0.98),"
        "stop:0.16 rgba(87,102,121,0.98),"
        "stop:0.54 rgba(50,60,73,0.99),"
        "stop:1 rgba(25,32,40,0.99));"
        "border:1px solid rgba(255,255,255,0.30);"
        "border-radius:10px;"
        "}"
        "QPushButton:hover{border:1px solid rgba(255,255,255,0.45);}"
        "QPushButton:pressed{padding-top:7px; background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(34,42,52,0.99), stop:1 rgba(17,22,29,0.99));}"
    );
}

void styleOptionCheckBox(LabeledToggle* cb)
{
    if (!cb) return;
    cb->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    cb->setMinimumHeight(32);
}

void styleActionButton(QPushButton* button)
{
    if (!button) return;
    button->setMinimumHeight(58);
    button->setMinimumWidth(168);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button->setStyleSheet(QStringLiteral(
        "text-align:center; padding:8px 12px; font-weight:700;"
        "border: 1px solid rgba(255,255,255,0.26);"
        "border-radius: 8px;"
    ));
}
}

AutoBackupSettingsSection::AutoBackupSettingsSection(BackupManager* backupManager,
                                                     CrashRecoveryManager* crashRecoveryManager,
                                                     RestoreCoordinator* restoreCoordinator,
                                                     RecoveryCoordinator* recoveryCoordinator,
                                                     QWidget* parent)
    : QWidget(parent)
    , m_backupManager(backupManager)
    , m_crashRecoveryManager(crashRecoveryManager)
    , m_restoreCoordinator(restoreCoordinator)
    , m_recoveryCoordinator(recoveryCoordinator)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(18);

    auto* title = new QLabel(QString::fromUtf8("Автобекап та відновлення"), this);
    title->setStyleSheet(QStringLiteral("font-size:14pt; font-weight:800; color:white;"));
    root->addWidget(title);

    auto* intro = new QLabel(QString::fromUtf8(
        "Професійний захист даних: backup збереженої БД після commit, recovery незбережених змін UI та контрольоване відновлення з rollback."), this);
    intro->setWordWrap(true);
    intro->setStyleSheet(QStringLiteral("color: rgba(255,255,255,0.88); font-size: 10.5pt; padding-bottom: 4px;"));
    root->addWidget(intro);


    auto* modeGroup = new QGroupBox(QString::fromUtf8("Основний режим захисту"), this);
    modeGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    modeGroup->setStyleSheet(sectionGroupStyle());
    auto* modeLayout = new QVBoxLayout(modeGroup);
    modeLayout->setContentsMargins(12, 12, 12, 12);
    modeLayout->setSpacing(8);
    m_protectionMode = new QComboBox(modeGroup);
    m_protectionMode->addItem(QString::fromUtf8("Авто — рекомендовано"), QStringLiteral("auto"));
    m_protectionMode->addItem(QString::fromUtf8("Максимальний захист"), QStringLiteral("maximum"));
    m_protectionMode->addItem(QString::fromUtf8("Економний / Безпечний"), QStringLiteral("economy"));
    m_protectionMode->addItem(QString::fromUtf8("Ручний"), QStringLiteral("manual"));
    m_protectionMode->setMinimumHeight(38);
    m_protectionMode->setStyleSheet(settingsInputStyle());
    auto* modeText = new QLabel(QString::fromUtf8(
        "Авто: backup після важливих змін без заважання роботі. Максимальний: частіше і довше зберігання. "
        "Економний: рідше, переважно коли застосунок неактивний. Ручний: автоматичний backup вимкнено, ручні дії доступні."), modeGroup);
    modeText->setWordWrap(true);
    modeText->setStyleSheet(QStringLiteral("color: rgba(255,255,255,0.86); font-size: 10.5pt;"));
    modeLayout->addWidget(m_protectionMode);
    modeLayout->addWidget(modeText);
    root->addWidget(modeGroup);

    auto* backupGroup = new QGroupBox(QString::fromUtf8("Додатково — резервні копії"), this);
    backupGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    backupGroup->setStyleSheet(sectionGroupStyle());
    backupGroup->setCheckable(true);
    backupGroup->setChecked(false);
    backupGroup->setToolTip(QString::fromUtf8("Розширені технічні параметри backup. За замовчуванням приховано."));
    auto* backupForm = new QFormLayout(backupGroup);
    backupForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    backupForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    backupForm->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    backupForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    backupForm->setHorizontalSpacing(16);
    backupForm->setVerticalSpacing(12);

    m_enabled = new LabeledToggle(QString::fromUtf8("Увімкнути autobackup"), backupGroup);
    m_emergencyBeforeRestore = new LabeledToggle(QString::fromUtf8("Emergency backup перед restore"), backupGroup);
    m_keepPrev = new LabeledToggle(QString::fromUtf8("Зберігати останній snapshot попереднього дня"), backupGroup);
    m_cleanupOnStart = new LabeledToggle(QString::fromUtf8("Автоочистка під час старту"), backupGroup);
    m_cleanupAfterBackup = new LabeledToggle(QString::fromUtf8("Автоочистка після backup"), backupGroup);
    m_showNotifications = new LabeledToggle(QString::fromUtf8("Показувати backup-сповіщення"), backupGroup);
    m_detailedLogging = new LabeledToggle(QString::fromUtf8("Детальне логування"), backupGroup);

    for (auto* cb : {m_enabled, m_emergencyBeforeRestore, m_keepPrev, m_cleanupOnStart, m_cleanupAfterBackup, m_showNotifications, m_detailedLogging}) {
        styleOptionCheckBox(cb);
    }

    m_backupDir = new QLineEdit(backupGroup);
    m_backupDir->setMinimumHeight(38);
    m_backupDir->setStyleSheet(settingsInputStyle());
    auto* browseBackupDir = new QPushButton(QString::fromUtf8("Огляд"), backupGroup);
    browseBackupDir->setMinimumHeight(38);
    browseBackupDir->setStyleSheet(compactActionButtonStyle());

    auto* backupDirRow = new QWidget(backupGroup);
    auto* backupDirLay = new QHBoxLayout(backupDirRow);
    backupDirLay->setContentsMargins(0, 0, 0, 0);
    backupDirLay->setSpacing(10);
    backupDirLay->addWidget(m_backupDir, 1);
    backupDirLay->addWidget(browseBackupDir);

    m_maxPerDay = new QSpinBox(backupGroup);
    m_maxPerDay->setRange(1, 5000);
    m_maxPerDay->setMinimumHeight(38);
    m_maxPerDay->setStyleSheet(settingsSpinStyle());

    m_maxTotalMb = new QSpinBox(backupGroup);
    m_maxTotalMb->setRange(128, 102400);
    m_maxTotalMb->setMinimumHeight(38);
    m_maxTotalMb->setStyleSheet(settingsSpinStyle());

    backupForm->addRow(m_enabled);
    backupForm->addRow(m_emergencyBeforeRestore);
    backupForm->addRow(m_keepPrev);
    backupForm->addRow(m_cleanupOnStart);
    backupForm->addRow(m_cleanupAfterBackup);
    backupForm->addRow(m_showNotifications);
    backupForm->addRow(m_detailedLogging);
    backupForm->addRow(QString::fromUtf8("Директорія backup:"), backupDirRow);
    backupForm->addRow(QString::fromUtf8("Макс. backup-ів за день:"), m_maxPerDay);
    backupForm->addRow(QString::fromUtf8("Макс. розмір (МБ):"), m_maxTotalMb);
    root->addWidget(backupGroup);
    setAdvancedGroupBodyVisible(backupGroup, false);
    connect(backupGroup, &QGroupBox::toggled, this, [backupGroup](bool checked) {
        setAdvancedGroupBodyVisible(backupGroup, checked);
    });

    auto* recoveryGroup = new QGroupBox(QString::fromUtf8("Додатково — recovery незбережених даних"), this);
    recoveryGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    recoveryGroup->setStyleSheet(sectionGroupStyle());
    recoveryGroup->setCheckable(true);
    recoveryGroup->setChecked(false);
    recoveryGroup->setToolTip(QString::fromUtf8("Розширені технічні параметри recovery. За замовчуванням приховано."));
    auto* recoveryForm = new QFormLayout(recoveryGroup);
    recoveryForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    recoveryForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    recoveryForm->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    recoveryForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    recoveryForm->setHorizontalSpacing(16);
    recoveryForm->setVerticalSpacing(12);

    m_recoveryEnabled = new LabeledToggle(QString::fromUtf8("Увімкнути recovery незбережених даних"), recoveryGroup);
    m_recoveryAutosaveEnabled = new LabeledToggle(QString::fromUtf8("Autosave drafts під час редагування"), recoveryGroup);
    m_recoveryShowDialog = new LabeledToggle(QString::fromUtf8("Показувати startup recovery dialog"), recoveryGroup);
    for (auto* cb : {m_recoveryEnabled, m_recoveryAutosaveEnabled, m_recoveryShowDialog}) {
        styleOptionCheckBox(cb);
    }

    m_autosaveInterval = new QSpinBox(recoveryGroup);
    m_autosaveInterval->setRange(5, 300);
    m_autosaveInterval->setSingleStep(5);
    m_autosaveInterval->setSuffix(QString::fromUtf8(" сек"));
    m_autosaveInterval->setMinimumHeight(38);
    m_autosaveInterval->setStyleSheet(settingsSpinStyle());

    m_recoveryRetention = new QSpinBox(recoveryGroup);
    m_recoveryRetention->setRange(1, 30);
    m_recoveryRetention->setMinimumHeight(38);
    m_recoveryRetention->setStyleSheet(settingsSpinStyle());

    recoveryForm->addRow(m_recoveryEnabled);
    recoveryForm->addRow(m_recoveryAutosaveEnabled);
    recoveryForm->addRow(m_recoveryShowDialog);
    recoveryForm->addRow(QString::fromUtf8("Інтервал autosave:"), m_autosaveInterval);
    recoveryForm->addRow(QString::fromUtf8("Зберігати drafts (днів):"), m_recoveryRetention);
    root->addWidget(recoveryGroup);
    setAdvancedGroupBodyVisible(recoveryGroup, false);
    connect(recoveryGroup, &QGroupBox::toggled, this, [recoveryGroup](bool checked) {
        setAdvancedGroupBodyVisible(recoveryGroup, checked);
    });

    auto* statusGroup = new QGroupBox(QString::fromUtf8("Статус захисту"), this);
    statusGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    statusGroup->setStyleSheet(sectionGroupStyle());
    auto* statusL = new QVBoxLayout(statusGroup);
    statusL->setContentsMargins(12, 12, 12, 12);
    statusL->setSpacing(8);

    m_status = new QLabel(statusGroup);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_status->setStyleSheet(QStringLiteral("color:white; line-height:1.45; padding:4px 2px; font-size:10.5pt;"));
    m_status->setMinimumHeight(210);
    statusL->addWidget(m_status);
    root->addWidget(statusGroup);

    auto* actionsGroup = new QGroupBox(QString::fromUtf8("Ручні дії"), this);
    actionsGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    actionsGroup->setStyleSheet(sectionGroupStyle());
    auto* actionsOuter = new QVBoxLayout(actionsGroup);
    actionsOuter->setContentsMargins(12, 12, 12, 12);
    actionsOuter->setSpacing(8);

    auto* buttonsWrap = new QWidget(actionsGroup);
    m_actionsGrid = new QGridLayout(buttonsWrap);
    m_actionsGrid->setContentsMargins(0, 0, 0, 0);
    m_actionsGrid->setHorizontalSpacing(14);
    m_actionsGrid->setVerticalSpacing(14);
    actionsOuter->addWidget(buttonsWrap);
    root->addWidget(actionsGroup);
    root->addStretch(1);

    auto* createNow = new QPushButton(QString::fromUtf8("Створити\nbackup зараз"), buttonsWrap);
    createNow->setToolTip(QString::fromUtf8("Створити backup зараз"));
    auto* openBackupDir = new QPushButton(QString::fromUtf8("Відкрити\nпапку backup"), buttonsWrap);
    openBackupDir->setToolTip(QString::fromUtf8("Відкрити директорію backup"));
    auto* restoreManager = new QPushButton(QString::fromUtf8("Відновити\nз backup"), buttonsWrap);
    restoreManager->setToolTip(QString::fromUtf8("Відкрити менеджер restore"));
    auto* cleanupBackups = new QPushButton(QString::fromUtf8("Очистити\nстарі backup"), buttonsWrap);
    cleanupBackups->setToolTip(QString::fromUtf8("Очистити старі backup-и"));
    auto* validate = new QPushButton(QString::fromUtf8("Перевірити\nbackup"), buttonsWrap);
    validate->setToolTip(QString::fromUtf8("Перевірити цілісність backup-ів"));
    auto* recoveryManager = new QPushButton(QString::fromUtf8("Відкрити\nrecovery"), buttonsWrap);
    recoveryManager->setToolTip(QString::fromUtf8("Відкрити менеджер recovery"));
    auto* clearRecovery = new QPushButton(QString::fromUtf8("Очистити\nrecovery drafts"), buttonsWrap);
    clearRecovery->setToolTip(QString::fromUtf8("Очистити recovery-дані"));
    auto* openRecovery = new QPushButton(QString::fromUtf8("Відкрити\nпапку recovery"), buttonsWrap);
    openRecovery->setToolTip(QString::fromUtf8("Відкрити директорію recovery"));
    auto* save = new QPushButton(QString::fromUtf8("Зберегти"), buttonsWrap);
    save->setToolTip(QString::fromUtf8("Зберегти налаштування"));
    auto* cancel = new QPushButton(QString::fromUtf8("Скасувати"), buttonsWrap);
    cancel->setToolTip(QString::fromUtf8("Скасувати"));

    m_actionButtons = {createNow, openBackupDir, restoreManager, cleanupBackups, validate,
                       recoveryManager, clearRecovery, openRecovery, save, cancel};
    for (auto* button : m_actionButtons) {
        styleActionButton(button);
    }
    rebuildActionButtonsLayout();

    connect(browseBackupDir, &QPushButton::clicked, this, [this]() {
        const QString picked = QFileDialog::getExistingDirectory(this, QString::fromUtf8("Оберіть backup directory"), effectiveBackupDir());
        if (!picked.isEmpty()) {
            m_backupDir->setText(QDir::toNativeSeparators(picked));
            refreshStatus();
        }
    });
    connect(createNow, &QPushButton::clicked, this, [this]() {
        QString errorText;
        const bool notificationsEnabled = AutoBackupSettingsService::load().showBackupNotifications;
        const bool ok = m_backupManager && m_backupManager->requestManualBackup(QStringLiteral("Manual backup from settings"), &errorText);
        if (!ok && !errorText.trimmed().isEmpty()) {
            QMessageBox box(QMessageBox::Warning, QString::fromUtf8("Автобекап"), errorText, QMessageBox::Ok, this);
            styleMessageBoxReadable(&box);
            box.exec();
        } else if (ok && notificationsEnabled) {
            QMessageBox box(QMessageBox::Information,
                            QString::fromUtf8("Автобекап"),
                            QString::fromUtf8("Ручний backup поставлено в чергу на виконання."),
                            QMessageBox::Ok,
                            this);
            styleMessageBoxReadable(&box);
            box.exec();
        }
        refreshStatus();
    });
    connect(openBackupDir, &QPushButton::clicked, this, [this]() {
        const QString dir = effectiveBackupDir();
        QDir().mkpath(dir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    connect(restoreManager, &QPushButton::clicked, this, [this]() {
        RestoreManagerDialog dlg(m_backupManager, m_restoreCoordinator, this);
        dlg.exec();
        refreshStatus();
    });
    connect(cleanupBackups, &QPushButton::clicked, this, [this]() {
        QMessageBox confirm(QMessageBox::Question,
                            QString::fromUtf8("Очистити backup-и"),
                            QString::fromUtf8("Очистити старі backup-и згідно retention policy? Копії старші за 7 діб буде видалено. Якщо нових копій немає, остання придатна копія залишиться для відновлення."),
                            QMessageBox::Yes | QMessageBox::No,
                            this);
        styleMessageBoxReadable(&confirm);
        if (confirm.exec() != QMessageBox::Yes) {
            refreshStatus();
            return;
        }
        QString errorText;
        if (m_backupManager) m_backupManager->runCleanup(&errorText);
        if (!errorText.trimmed().isEmpty()) {
            QMessageBox box(QMessageBox::Warning, QString::fromUtf8("Очистка backup-ів"), errorText, QMessageBox::Ok, this);
            styleMessageBoxReadable(&box);
            box.exec();
        }
        refreshStatus();
    });
    connect(validate, &QPushButton::clicked, this, [this]() {
        BackupValidationSummary summary;
        QString errorText;
        if (m_backupManager && !m_backupManager->validateSnapshots(&summary, &errorText) && !errorText.trimmed().isEmpty()) {
            QMessageBox box(QMessageBox::Warning, QString::fromUtf8("Перевірка backup-ів"), errorText, QMessageBox::Ok, this);
            styleMessageBoxReadable(&box);
            box.exec();
        } else {
            QString text = QString::fromUtf8("Перевірено: %1\nValid: %2\nInvalid: %3\nMissing: %4")
                               .arg(summary.checkedCount)
                               .arg(summary.validCount)
                               .arg(summary.invalidCount)
                               .arg(summary.missingCount);
            if (!summary.problems.isEmpty()) {
                text += QString::fromUtf8("\n\nПроблеми:\n- ") + summary.problems.join(QString::fromUtf8("\n- "));
            }
            QMessageBox box(QMessageBox::Information, QString::fromUtf8("Перевірка backup-ів"), text, QMessageBox::Ok, this);
            styleMessageBoxReadable(&box);
            box.exec();
        }
        refreshStatus();
    });
    connect(recoveryManager, &QPushButton::clicked, this, [this]() {
        RecoveryManagerDialog dlg(m_crashRecoveryManager, m_recoveryCoordinator, this);
        dlg.exec();
        refreshStatus();
    });
    connect(clearRecovery, &QPushButton::clicked, this, [this]() {
        QMessageBox confirm(QMessageBox::Question,
                            QString::fromUtf8("Очистити recovery drafts"),
                            QString::fromUtf8("Очистити прострочені recovery drafts згідно retention policy? Актуальні незбережені дані не повинні видалятися."),
                            QMessageBox::Yes | QMessageBox::No,
                            this);
        styleMessageBoxReadable(&confirm);
        if (confirm.exec() != QMessageBox::Yes) {
            refreshStatus();
            return;
        }
        QString errorText;
        if (m_crashRecoveryManager) m_crashRecoveryManager->cleanupExpiredData(&errorText);
        if (!errorText.trimmed().isEmpty()) {
            QMessageBox box(QMessageBox::Warning, QString::fromUtf8("Recovery cleanup"), errorText, QMessageBox::Ok, this);
            styleMessageBoxReadable(&box);
            box.exec();
        }
        refreshStatus();
    });
    connect(openRecovery, &QPushButton::clicked, this, [this]() {
        const QString dir = BackupPaths::configuredRecoveryRoot();
        QDir().mkpath(dir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    connect(save, &QPushButton::clicked, this, &AutoBackupSettingsSection::saveSettings);
    connect(cancel, &QPushButton::clicked, this, &AutoBackupSettingsSection::reloadFromSettings);
    connect(m_backupDir, &QLineEdit::textChanged, this, [this]() { refreshStatus(); });
    if (m_backupManager) {
        connect(m_backupManager, &BackupManager::backupStatusChanged, this, &AutoBackupSettingsSection::refreshStatus);
    }

    reloadFromSettings();
}

void AutoBackupSettingsSection::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    rebuildActionButtonsLayout();
}

QString AutoBackupSettingsSection::effectiveBackupDir() const
{
    const QString textDir = m_backupDir ? m_backupDir->text().trimmed() : QString();
    if (!textDir.isEmpty()) {
        return QDir::cleanPath(QDir::fromNativeSeparators(textDir));
    }
    return BackupPaths::configuredBackupsRoot();
}

void AutoBackupSettingsSection::rebuildActionButtonsLayout()
{
    if (!m_actionsGrid) return;

    const int availableWidth = qMax(400, width());
    int columns = 2;
    if (availableWidth >= 1320) {
        columns = 5;
    } else if (availableWidth >= 1080) {
        columns = 4;
    } else if (availableWidth >= 820) {
        columns = 3;
    }

    if (m_lastActionColumns == columns && m_actionsGrid->count() == m_actionButtons.size()) {
        return;
    }
    m_lastActionColumns = columns;

    while (QLayoutItem* item = m_actionsGrid->takeAt(0)) {
        delete item;
    }

    for (int i = 0; i < m_actionButtons.size(); ++i) {
        const int row = i / columns;
        const int column = i % columns;
        m_actionsGrid->addWidget(m_actionButtons.at(i), row, column);
    }
    const int rows = (m_actionButtons.size() + columns - 1) / columns;
    for (int column = 0; column < columns; ++column) {
        m_actionsGrid->setColumnStretch(column, 1);
        m_actionsGrid->setColumnMinimumWidth(column, columns >= 5 ? 150 : 168);
    }
    for (int row = 0; row < rows; ++row) {
        m_actionsGrid->setRowMinimumHeight(row, 58);
    }
}

void AutoBackupSettingsSection::reloadFromSettings()
{
    const auto payload = AutoBackupSettingsService::load();
    m_enabled->setChecked(payload.enabled);
    m_emergencyBeforeRestore->setChecked(payload.emergencyBeforeRestore);
    m_keepPrev->setChecked(payload.keepPreviousDayLastSnapshot);
    m_cleanupOnStart->setChecked(true);
    m_cleanupAfterBackup->setChecked(true);
    m_cleanupOnStart->setEnabled(false);
    m_cleanupAfterBackup->setEnabled(false);
    m_cleanupOnStart->setToolTip(QString::fromUtf8("Резервні копії зберігаються протягом останніх 7 діб."));
    m_cleanupAfterBackup->setToolTip(m_cleanupOnStart->toolTip());
    m_showNotifications->setChecked(payload.showBackupNotifications);
    m_detailedLogging->setChecked(payload.detailedLogging);
    m_backupDir->setText(payload.backupDir);
    m_maxPerDay->setValue(payload.maxBackupsPerDay);
    m_maxTotalMb->setValue(payload.maxTotalSizeMb);
    m_recoveryEnabled->setChecked(payload.recoveryEnabled);
    m_recoveryAutosaveEnabled->setChecked(payload.recoveryAutosaveEnabled);
    const int modeIndex = m_protectionMode->findData(payload.protectionMode.trimmed().isEmpty() ? QStringLiteral("auto") : payload.protectionMode.trimmed());
    m_protectionMode->setCurrentIndex(modeIndex >= 0 ? modeIndex : 0);
    m_recoveryShowDialog->setChecked(payload.recoveryShowStartupDialog);
    m_autosaveInterval->setValue(qBound(5, payload.recoveryAutosaveIntervalMs / 1000, 300));
    m_recoveryRetention->setValue(payload.recoveryRetentionDays);
    refreshStatus();
}

void AutoBackupSettingsSection::refreshStatus()
{
    const BackupStatusSnapshot s = m_backupManager ? m_backupManager->status() : BackupStatusSnapshot{};
    const QString backupDir = effectiveBackupDir();
    QStorageInfo storage(backupDir);
    const qint64 freeMb = storage.isValid() ? storage.bytesAvailable() / 1024 / 1024 : 0;
    const QString protectionState = s.engineDegraded
        ? QString::fromUtf8("Потрібна увага")
        : ((m_enabled && m_enabled->isChecked()) ? QString::fromUtf8("Увімкнено") : QString::fromUtf8("Вимкнено"));
    m_status->setText(QString::fromUtf8(
        "Захист даних: %1\nОстання успішна копія: %2\n"
        "Папка резервних копій: %3\nПапка відновлення: %4\n"
        "Розмір резервних копій: %5 МБ\nВільне місце на диску: %6 МБ\n"
        "Зберігання: останні 7 діб")
        .arg(protectionState,
             s.lastBackupAt.isValid() ? s.lastBackupAt.toString(QStringLiteral("dd.MM.yy HH:mm:ss")) : QString::fromUtf8("Немає"),
             QDir::toNativeSeparators(backupDir), QDir::toNativeSeparators(BackupPaths::configuredRecoveryRoot()))
        .arg(s.totalSizeBytes / (1024 * 1024)).arg(freeMb));
}

void AutoBackupSettingsSection::saveSettings()
{
    AutoBackupSettingsService::AutoBackupSettingsPayload payload;
    payload.protectionMode = m_protectionMode ? m_protectionMode->currentData().toString() : QStringLiteral("auto");
    payload.enabled = payload.protectionMode == QStringLiteral("manual") ? false : m_enabled->isChecked();
    payload.emergencyBeforeRestore = m_emergencyBeforeRestore->isChecked();
    payload.keepPreviousDayLastSnapshot = m_keepPrev->isChecked();
    payload.cleanupOnStart = m_cleanupOnStart->isChecked();
    payload.cleanupAfterBackup = m_cleanupAfterBackup->isChecked();
    payload.showBackupNotifications = m_showNotifications->isChecked();
    payload.detailedLogging = m_detailedLogging->isChecked();
    payload.backupDir = m_backupDir->text().trimmed();
    payload.maxBackupsPerDay = m_maxPerDay->value();
    payload.maxTotalSizeMb = m_maxTotalMb->value();
    payload.recoveryEnabled = m_recoveryEnabled->isChecked();
    payload.recoveryAutosaveEnabled = m_recoveryAutosaveEnabled->isChecked();
    payload.recoveryShowStartupDialog = m_recoveryShowDialog->isChecked();
    payload.recoveryAutosaveIntervalMs = m_autosaveInterval->value() * 1000;
    payload.recoveryRetentionDays = m_recoveryRetention->value();

    const QString effectiveDir = payload.backupDir.trimmed().isEmpty() ? AutoBackupSettingsService::effectiveBackupDir() : payload.backupDir.trimmed();
    if (!effectiveDir.trimmed().isEmpty()) {
        QString probeError;
        if (!BackupPaths::probeBackupRoot(effectiveDir.trimmed(), &probeError)) {
            QMessageBox box(QMessageBox::Warning, QString::fromUtf8("Backup-папка"), probeError, QMessageBox::Ok, this);
            styleMessageBoxReadable(&box);
            box.exec();
            return;
        }
    }

    bool changed = false;
    AutoBackupSettingsService::save(payload, &changed);

    if (changed) {
        SettingsPageDataSupport::publishSettingsBackupEvent(QStringLiteral("autobackup_settings_save"),
                                                            QStringLiteral("integration_settings"),
                                                            QStringLiteral("settings.int.autobackup"),
                                                            QString::fromUtf8("Збережено налаштування автобекапу та crash recovery"),
                                                            {QStringLiteral("settings")});
    }

    if (m_crashRecoveryManager) {
        m_crashRecoveryManager->reloadPolicy();
    }
    refreshStatus();

    QMessageBox box(QMessageBox::Information,
                    QString::fromUtf8("Автобекап"),
                    QString::fromUtf8("Налаштування autobackup / recovery збережено."),
                    QMessageBox::Ok,
                    this);
    styleMessageBoxReadable(&box);
    box.exec();
}
