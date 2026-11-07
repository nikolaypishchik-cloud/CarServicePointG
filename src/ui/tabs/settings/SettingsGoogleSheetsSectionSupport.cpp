#include "ui/tabs/settings/SettingsGoogleSheetsSectionSupport.h"
#include <functional>

#include "core/services/settings/GeneralSettingsService.h"
#include "ui/tabs/settings/SettingsPageDataSupport.h"
#include "ui/tabs/settings/SettingsDetailUiSupport.h"
#include "ui/theme/ThemeManager.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {

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

} // namespace

QWidget* SettingsGoogleSheetsSectionSupport::build(
    QWidget* owner,
    const std::function<void(const QString&, QWidget*)>& importStarter)
{
    auto* w = new QWidget(owner);
    auto* l = new QVBoxLayout(w);
    l->setContentsMargins(18, 18, 18, 18);
    l->setSpacing(12);

    auto* title = new QLabel(QString::fromUtf8("Google Drive / legacy import"), w);
    title->setStyleSheet(QString("font-size:14pt; font-weight:800; color:%1;").arg(cPrimary()));
    l->addWidget(title);

    auto* form = new QFormLayout();
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(10);

    auto* folderPath = new QLineEdit(GeneralSettingsService::loadLegacySourceFolder(), w);
    folderPath->setPlaceholderText(QString::fromUtf8("Папка для вибору legacy БД / зовнішніх джерел"));

    auto* pickRow = new QWidget(w);
    auto* pickLay = new QHBoxLayout(pickRow);
    pickLay->setContentsMargins(0, 0, 0, 0);
    pickLay->setSpacing(10);
    auto* browse = new QPushButton(QString::fromUtf8("Вибрати папку"), pickRow);
    browse->setFixedSize(230, 52);
    browse->setCursor(Qt::PointingHandCursor);
    browse->setStyleSheet(SettingsDetailUiSupport::standardButtonStyle(false));
    pickLay->addWidget(folderPath, 1);
    pickLay->addWidget(browse, 0);

    form->addRow(QString::fromUtf8("Папка джерел:"), pickRow);
    l->addLayout(form);

    auto* row = new QHBoxLayout();
    auto* save = new QPushButton(QString::fromUtf8("Зберегти"), w);
    auto* open = new QPushButton(QString::fromUtf8("Відкрити папку"), w);
    auto* importOldDb = new QPushButton(QString::fromUtf8("Імпорт старої БД"), w);
    save->setFixedSize(120, 40);
    save->setCursor(Qt::PointingHandCursor);
    save->setStyleSheet(SettingsDetailUiSupport::standardButtonStyle(false));
    open->setFixedSize(180, 52);
    open->setCursor(Qt::PointingHandCursor);
    open->setStyleSheet(SettingsDetailUiSupport::standardButtonStyle(false));
    importOldDb->setFixedSize(216, 52);
    importOldDb->setCursor(Qt::PointingHandCursor);
    importOldDb->setStyleSheet(SettingsDetailUiSupport::standardButtonStyle(false));
    row->addWidget(save);
    row->addWidget(open);
    row->addWidget(importOldDb);
    row->addStretch(1);
    l->addLayout(row);

    auto* info = new QLabel(
        QString::fromUtf8("Native backup / restore перенесено у вкладку «Автобекап». У цій секції лишено лише сценарій legacy import та папку джерел для зовнішніх БД."),
        w);
    info->setWordWrap(true);
    info->setStyleSheet(QString("color:%1;").arg(cSecondary()));
    l->addWidget(info);

    l->addStretch(1);

    QObject::connect(browse, &QPushButton::clicked, w, [w, folderPath]() {
        const QString startDir = folderPath->text().trimmed().isEmpty()
            ? QDir::homePath()
            : folderPath->text().trimmed();
        const QString picked = QFileDialog::getExistingDirectory(
            w,
            QString::fromUtf8("Оберіть папку джерел"),
            startDir,
            QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
        if (!picked.isEmpty()) {
            folderPath->setText(QDir::toNativeSeparators(picked));
        }
    });

    QObject::connect(save, &QPushButton::clicked, w, [w, folderPath]() {
        const QString newSourceFolder = folderPath->text().trimmed();
        bool changed = false;
        GeneralSettingsService::saveLegacySourceFolder(newSourceFolder, &changed);
        if (changed) {
            SettingsPageDataSupport::publishSettingsBackupEvent(
                QStringLiteral("legacy_source_folder_save"),
                QStringLiteral("integration_settings"),
                QStringLiteral("settings.int.gsheets.source_folder"),
                QString::fromUtf8("Збережено папку джерел для legacy import"),
                {QStringLiteral("settings")});
        }
        QMessageBox::information(
            w,
            QString::fromUtf8("Google Drive / legacy import"),
            QString::fromUtf8("Папку джерел збережено."));
    });

    QObject::connect(open, &QPushButton::clicked, w, [w, folderPath]() {
        const QString path = folderPath->text().trimmed();
        if (path.isEmpty()) {
            QMessageBox::warning(
                w,
                QString::fromUtf8("Google Drive / legacy import"),
                QString::fromUtf8("Спочатку виберіть папку джерел."));
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });

    QObject::connect(importOldDb, &QPushButton::clicked, w, [w, folderPath, importStarter]() {
        const QString initial = folderPath->text().trimmed();
        const QString picked = QFileDialog::getOpenFileName(
            w,
            QString::fromUtf8("Оберіть legacy БД для імпорту"),
            initial.isEmpty() ? QDir::homePath() : initial,
            QString::fromUtf8("SQLite DB (*.db *.sqlite *.sqlite3);;Усі файли (*.*)"));
        if (picked.isEmpty()) {
            return;
        }
        if (importStarter) {
            importStarter(picked, w);
        }
    });

    return w;
}