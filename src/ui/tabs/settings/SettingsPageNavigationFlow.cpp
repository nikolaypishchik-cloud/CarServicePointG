// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

#include "ui/tabs/settings/SettingsPageModulePrelude.h"
#include "ui/tabs/settings/SettingsSecuritySection.h"
#include "ui/tabs/settings/AccessControlPage.h"
#include "ui/tabs/cloudsync/CloudSyncPage.h"
#include "ui/tabs/settings/PhoneSyncSettingsPage.h"
#include "ui/tabs/settings/InvoiceRecognitionSettingsPage.h"
#include "ui/main/MainWindow.h"


namespace {
static bool settingsNavFlowIsDarkUi()
{
    return ThemeManager::currentTheme() == QStringLiteral("graphite");
}
static QString cSettingsTextSecondary()
{
    return settingsNavFlowIsDarkUi() ? QStringLiteral("#e8eef6") : QStringLiteral("#4b5563");
}
static void applySettingsContrastPalette(QWidget* root)
{
    if (!root) return;
    const QColor text(settingsNavFlowIsDarkUi() ? QStringLiteral("#f7fbff") : QStringLiteral("#101418"));
    QPalette pal = root->palette();
    pal.setColor(QPalette::WindowText, text);
    pal.setColor(QPalette::ButtonText, text);
    root->setPalette(pal);
    const auto children = root->findChildren<QWidget*>();
    for (auto* child : children) {
        QPalette cp = child->palette();
        cp.setColor(QPalette::WindowText, text);
        cp.setColor(QPalette::ButtonText, text);
        child->setPalette(cp);
    }
}
} // namespace

QWidget* SettingsPage::buildPersonnelManageSection()
{
    return SettingsPersonnelManageSectionSupport::build(this, [this]() { queueRefreshAll(); });
}

QWidget* SettingsPage::buildIntegrationSyncSection()
{
    return new CloudSyncPage(this);
}


QWidget* SettingsPage::buildPlaceholderWidget(const QString& text)
{
    auto* w = new QWidget(this);
    auto* l = new QVBoxLayout(w);
    l->setContentsMargins(20,20,20,20);
    auto* lbl = new QLabel(text, w);
    lbl->setWordWrap(true);
    lbl->setStyleSheet(QString("color:%1; font-size:11pt;").arg(cSettingsTextSecondary()));
    l->addWidget(lbl);
    l->addStretch(1);
    return w;
}


void SettingsPage::setCategory(Category c)
{
    m_category = c;

    if (m_catGroup) {
        if (auto* b = m_catGroup->button(static_cast<int>(c))) b->setChecked(true);
    }

    m_hdrTitle->setText(categoryTitle(c));
    rebuildItems();
}

void SettingsPage::rebuildItems()
{
    m_items->clear();
    const auto items = m_itemsByCategory.value(m_category);
    for (const auto& it : items) {
        auto* li = new QListWidgetItem(it.title, m_items);
        li->setData(Qt::UserRole, it.id);
        m_items->addItem(li);
    }

    QWidget* page = ensureCategoryItemsPage(m_category);
    m_itemGroup = m_categoryButtonGroups.value(m_category, nullptr);
    if (m_itemsCategoryStack && page) {
        m_itemsCategoryStack->setCurrentWidget(page);
    }
    if (m_items) m_items->setCurrentRow(-1);
    if (m_itemGroup) {
        for (auto* btn : m_itemGroup->buttons()) btn->setChecked(false);
    }
    m_detail->setCurrentIndex(0);
    if (m_contentStack) m_contentStack->setCurrentWidget(m_itemsPage);
    m_footer->setText(QString::fromUtf8("Налаштування: %1").arg(categoryTitle(m_category)));
    applySettingsContrastPalette(page ? page : m_itemsPage);
}

void SettingsPage::showItem(const QString& id)
{
    QElapsedTimer timer;
    timer.start();
    if (!m_detailIndexById.contains(id)) {
        QWidget* w = buildDetailWidget(id);
        const auto buttons = w->findChildren<QPushButton*>();
        bool hasBackButton = false;
        for (auto* btn : buttons) {
            const QString text = btn->text().trimmed();
            if (text == QString::fromUtf8("← Назад") || text == QString::fromUtf8("Назад")) {
                hasBackButton = true;
            }
        }
        if (!hasBackButton) {
            w = SettingsDetailUiSupport::wrapDetailWithBack(w);
        }

        const auto wrappedButtons = w->findChildren<QPushButton*>();
        for (auto* btn : wrappedButtons) {
            const QString text = btn->text().trimmed();
            if ((text == QString::fromUtf8("Зберегти") || text == QString::fromUtf8("Скасувати") || text == QString::fromUtf8("← Назад") || text == QString::fromUtf8("Назад"))
                    && !btn->property("stay_in_detail_menu").toBool()) {
                connect(btn, &QPushButton::clicked, this, [this]() { returnToItemsMenu(); });
            }
        }
        SettingsDetailUiSupport::normalizeDetailButtons(w);
        const int idx = m_detail->addWidget(w);
        m_detailIndexById[id] = idx;
    }
    int row = -1;
    for (int i = 0; i < m_items->count(); ++i) {
        if (m_items->item(i)->data(Qt::UserRole).toString() == id) {
            row = i;
            break;
        }
    }
    if (row >= 0 && row < m_items->count()) {
        m_items->setCurrentRow(row);
    }
    if (auto* btn = m_itemButtonById.value(id, nullptr)) {
        btn->setChecked(true);
    }
    m_detail->setCurrentIndex(m_detailIndexById.value(id, 0));
    if (m_contentStack) m_contentStack->setCurrentWidget(m_detailPage);

    const auto* cur = (row >= 0 && row < m_items->count()) ? m_items->item(row) : nullptr;
    m_footer->setText(cur ? QString::fromUtf8("Налаштування: %1").arg(cur->text())
                          : QString::fromUtf8("Налаштування"));
    applySettingsContrastPalette(m_detail->currentWidget());
    qInfo() << "SettingsPage showItem elapsed_ms" << timer.elapsed() << "item" << id;
}


void SettingsPage::openDirectoriesSettings()
{
    setCategory(Category::Integrations);
    showItem(QStringLiteral("int.directories"));
}

void SettingsPage::returnToItemsMenu()
{
    if (m_itemGroup) {
        for (auto* btn : m_itemGroup->buttons()) {
            btn->setChecked(false);
        }
    }
    if (m_items) {
        m_items->setCurrentRow(-1);
    }
    if (m_itemsCategoryStack) {
        QWidget* page = ensureCategoryItemsPage(m_category);
        if (page) m_itemsCategoryStack->setCurrentWidget(page);
    }
    if (m_contentStack) {
        m_contentStack->setCurrentWidget(m_itemsPage);
    }
    if (m_footer) {
        m_footer->setText(QString::fromUtf8("Налаштування: %1").arg(categoryTitle(m_category)));
    }
    applySettingsContrastPalette(m_itemsCategoryStack && m_itemsCategoryStack->currentWidget() ? m_itemsCategoryStack->currentWidget() : m_itemsPage);
}

static bool getSoundEnabledSetting()
{
    return UiSettingsService::loadSoundEnabled();
}
static void setSoundEnabledSetting(bool on)
{
    UiSettingsService::saveSoundEnabled(on);
}

QWidget* SettingsPage::buildDetailWidget(const QString& id)
{
    if (id == QStringLiteral("ui.external_display")) {
        if (auto* main = qobject_cast<MainWindow*>(window())) return main->externalDisplaySettings(this);
        return buildPlaceholderWidget(QString::fromUtf8("Налаштування доступні в головному вікні застосунку."));
    }
    if (id == QStringLiteral("int.invoices")) return buildInvoiceRecognitionSettingsPage(this);
    if (id == QStringLiteral("diagnostics.report_logs")) {
        return SettingsIntegrationsSection::buildDiagnosticReportPage(this);
    }

    if (auto* section = SettingsPersonnelSection::build(id, this, [this]() { return buildPersonnelManageSection(); }, [this]() { queueRefreshAll(); })) {
        return section;
    }

    if (auto* section = SettingsGeneralSection::build(id, this, [this]() { return SettingsBrandingSectionSupport::build(this, [this]() { queueRefreshAll(); }); })) {
        return section;
    }

    if (auto* section = SettingsUiSection::build(id, this,
                                                  [this]() { m_items->setCurrentRow(-1); },
                                                  [this]() { queueRefreshAll(); })) {
        return section;
    }

    if (auto* section = SettingsIntegrationsSection::build(id, this,
                                                             [this]() {
                                                                 return SettingsGoogleSheetsSectionSupport::build(this, [this](const QString& picked, QWidget* sectionGuard) {
                                                                     executeLegacyImport(picked, sectionGuard);
                                                                 });
                                                             },
                                                             [this]() { return SettingsBackupSection::build(QStringLiteral("int.autobackup"), this); },
                                                             [this]() { return buildIntegrationSyncSection(); })) {
        return section;
    }

    if (id == QStringLiteral("sync.server")) {
        return new CloudSyncPage(this);
    }

    if (id == QStringLiteral("sync.phone")) {
        return PhoneSyncSettingsPage::build(this);
    }

    if (id == QStringLiteral("sec.roles_access")) {
        return new AccessControlPage(this);
    }

    if (auto* section = SettingsSecuritySection::build(id, this)) {
        return section;
    }


    if (auto* section = SettingsFinanceSection::build(id, this)) {
        return section;
    }

    return buildPlaceholderWidget(QString::fromUtf8("Розділ тимчасово недоступний у поточній збірці."));
}
