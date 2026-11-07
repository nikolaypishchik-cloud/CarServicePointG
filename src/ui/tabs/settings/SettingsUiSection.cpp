#include "ui/tabs/settings/SettingsUiSection.h"
#include "ui/tabs/settings/SettingsUiFontSectionSupport.h"
#include "ui/tabs/settings/SettingsDetailUiSupport.h"
#include <functional>

#include "ui/tabs/settings/SettingsPageDataSupport.h"
#include "core/services/settings/GeneralSettingsService.h"
#include "ui/theme/ThemeManager.h"

#include "ui/tabs/settings/LabeledToggle.h"
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
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

}

QWidget* SettingsUiSection::build(const QString& id,
                                  QWidget* owner,
                                  const std::function<void()>& onFontBack,
                                  const std::function<void()>& onFontRefreshAll)
{
    if (id == QStringLiteral("ui.font")) {
        return SettingsUiFontSectionSupport::build(owner, onFontBack, onFontRefreshAll);
    }
    if (id == QStringLiteral("ui.theme")) {
        auto* w = new QWidget(owner);
        auto* l = new QVBoxLayout(w);
        l->setContentsMargins(16,16,16,16);
        l->setSpacing(12);

        auto* top = new QHBoxLayout();
        top->setSpacing(12);
        auto* btnBack = SettingsDetailUiSupport::makeBackButton(w);
        auto* title = new QLabel(QString::fromUtf8("Тема інтерфейсу"), w);
        title->setStyleSheet(QString("font-size:16pt; font-weight:800; color:%1;").arg(cPrimary()));
        top->addWidget(btnBack);
        top->addWidget(title, 1);
        l->addLayout(top);

        auto* row = new QHBoxLayout();
        row->setSpacing(12);
        row->addWidget(new QLabel(QString::fromUtf8("Тема:"), w));

        auto* cb = new QComboBox(w);
        cb->setFixedSize(200, 52);
        cb->addItem(QString::fromUtf8("Графіт (темна)"), "graphite");
        cb->addItem(QString::fromUtf8("Світла"), "light");

        const QString cur = ThemeManager::currentTheme();
        const int idx = cb->findData(cur);
        if (idx >= 0) cb->setCurrentIndex(idx);

        row->addWidget(cb);
        row->addStretch(1);
        l->addLayout(row);

        auto* hint = new QLabel(QString::fromUtf8("Після зміни теми інтерфейс застосовується одразу."), w);
        hint->setStyleSheet(QString("color:%1;").arg(cSecondary()));
        l->addWidget(hint);

        l->addStretch(1);

        QObject::connect(cb, &QComboBox::currentIndexChanged, w, [cb](int) {
            const QString previousTheme = ThemeManager::currentTheme();
            const QString theme = cb->currentData().toString();
            ThemeManager::setTheme(theme);
            if (previousTheme != theme) {
                SettingsPageDataSupport::publishSettingsBackupEvent(QStringLiteral("ui_theme_save"),
                                                                   QStringLiteral("ui_settings"),
                                                                   QStringLiteral("settings.ui.theme"),
                                                                   QString::fromUtf8("Змінено тему інтерфейсу: %1 → %2").arg(previousTheme, theme),
                                                                   {QStringLiteral("settings")});
            }
        });

        return w;
    }
    if (id == QStringLiteral("ui.tables")) {
        auto* w = new QWidget(owner);
        auto* l = new QVBoxLayout(w);
        l->setContentsMargins(16,16,16,16);
        l->setSpacing(12);

        auto* btnBack = SettingsDetailUiSupport::makeBackButton(w);
        l->addWidget(btnBack, 0, Qt::AlignLeft);

        auto* title = new QLabel(QString::fromUtf8("Таблиці та відображення"), w);
        title->setStyleSheet(QString("font-size:16pt; font-weight:800; color:%1;").arg(cPrimary()));
        l->addWidget(title);

        auto* cb = new LabeledToggle(QString::fromUtf8("Робити таблиці світлими (білий фон)"), w);
        cb->setChecked(GeneralSettingsService::loadWhiteTablesEnabled());
        l->addWidget(cb);

        auto* info = new QLabel(QString::fromUtf8("Це налаштування застосовується до всіх таблиць (QTableView/QTableWidget) одразу."), w);
        info->setWordWrap(true);
        info->setStyleSheet(QString("color:%1;").arg(cSecondary()));
        l->addWidget(info);

        l->addStretch(1);

        QObject::connect(cb, &LabeledToggle::toggled, w, [](bool on) {
            bool changed = false;
            GeneralSettingsService::saveWhiteTablesEnabled(on, &changed);
            if (changed) {
                SettingsPageDataSupport::publishSettingsBackupEvent(QStringLiteral("ui_tables_save"),
                                                                   QStringLiteral("ui_settings"),
                                                                   QStringLiteral("settings.ui.white_tables"),
                                                                   on
                                                                       ? QString::fromUtf8("Увімкнено світлий режим таблиць")
                                                                       : QString::fromUtf8("Вимкнено світлий режим таблиць"),
                                                                   {QStringLiteral("settings")});
            }
            ThemeManager::setTheme(ThemeManager::currentTheme());
        });

        return w;
    }
    return nullptr;
}