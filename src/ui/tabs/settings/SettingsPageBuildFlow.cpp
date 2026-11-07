// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

#include "ui/tabs/settings/SettingsPageModulePrelude.h"

#include <QSizePolicy>

namespace {

static bool settingsBuildFlowIsDarkUi()
{
    return ThemeManager::currentTheme() == QStringLiteral("graphite");
}

static QString settingsBuildTextPrimary()
{
    return settingsBuildFlowIsDarkUi() ? QStringLiteral("#f7fbff") : QStringLiteral("#101418");
}

static QString settingsBuildTextSecondary()
{
    return settingsBuildFlowIsDarkUi() ? QStringLiteral("#e8eef6") : QStringLiteral("#4b5563");
}

static QString cSettingsTextSecondary()
{
    return settingsBuildTextSecondary();
}

static QString settingsLeftQss()
{
    if (settingsBuildFlowIsDarkUi()) {
        return QStringLiteral(
            "QFrame#settings_left{background:transparent;}"
            "QPushButton{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 rgba(104,120,141,0.98),stop:1 rgba(27,34,43,0.99));"
            "border:1px solid rgba(228,236,246,0.26);border-radius:14px;color:#f8fbff;font-weight:700;padding-left:16px;text-align:left;}"
            "QPushButton:checked{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 rgba(215,225,236,0.98),stop:1 rgba(72,85,100,0.99));border:1px solid rgba(220,234,252,0.82);color:#0e151d;}"
        );
    }
    return QStringLiteral(
        "QFrame#settings_left{background:transparent;}"
        "QPushButton{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #ffffff,stop:1 #e6e6e6);border:1px solid #bfc3c7;border-radius:12px;color:#151a20;font-weight:700;padding-left:14px;text-align:left;}"
        "QPushButton:checked{background:#d7ffd7;border:2px solid #3ccf3c;color:#101418;}"
    );
}

static QString settingsRightQss()
{
    const QString text = settingsBuildTextPrimary();
    const QString secondary = settingsBuildTextSecondary();
    if (settingsBuildFlowIsDarkUi()) {
        return QStringLiteral(
            "QFrame#settings_right{background:rgba(31,40,51,0.88);border:1px solid rgba(222,234,247,0.28);border-radius:14px;color:%1;}"
            "QFrame#settings_right QLabel{color:%1;}"
            "QFrame#settings_right QLabel[settingsMutedText=\"true\"]{color:%2;}"
            "QFrame#settings_right QFrame#hdr,QFrame#settings_right QFrame#ftr{background:rgba(22,28,36,0.86);border:none;}"
        ).arg(text, secondary);
    }
    return QStringLiteral(
        "QFrame#settings_right{background:#ffffff;border:1px solid #c7cdd6;border-radius:14px;color:%1;}"
        "QFrame#settings_right QLabel{color:%1;}"
        "QFrame#settings_right QLabel[settingsMutedText=\"true\"]{color:%2;}"
        "QFrame#settings_right QFrame#hdr,QFrame#settings_right QFrame#ftr{background:#f2f4f8;border:none;}"
    ).arg(text, secondary);
}

static QString settingsItemsPageQss()
{
    return settingsLeftQss();
}

static QString settingsNavButtonQss(bool submenu)
{
    const QString pad = submenu ? QStringLiteral("18px") : QStringLiteral("16px");
    if (settingsBuildFlowIsDarkUi()) {
        return QStringLiteral(
            "QPushButton{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 rgba(214,224,236,0.84),stop:0.50 rgba(34,46,61,1.0),stop:1 rgba(5,12,24,1.0));border:1px solid rgba(255,255,255,0.88);border-radius:16px;color:#f8fbff;font-weight:800;padding-left:%1;text-align:left;}"
            "QPushButton:checked{border:2px solid rgba(196,222,255,0.96);color:#ffffff;}"
        ).arg(pad);
    }
    return QStringLiteral(
        "QPushButton{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #ffffff,stop:1 #e6e6e6);border:1px solid #bfc3c7;border-radius:12px;color:#151a20;font-weight:700;padding-left:%1;text-align:left;}"
        "QPushButton:checked{background:#d7ffd7;border:2px solid #3ccf3c;color:#101418;}"
    ).arg(pad);
}

static QString settingsReportButtonQss()
{
    return QStringLiteral(
        "QPushButton{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #ff6b6b,stop:0.48 #d82f42,stop:1 #8f1322);"
        "border:1px solid rgba(255,214,219,0.72);border-radius:16px;color:#ffffff;font-weight:900;padding-left:14px;text-align:left;}"
        "QPushButton:hover{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #ff8585,stop:0.50 #e94356,stop:1 #a7192a);border:1px solid rgba(255,235,238,0.92);}"
        "QPushButton:pressed{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #c91f34,stop:0.52 #981424,stop:1 #5f0a15);border:1px solid rgba(255,185,194,0.74);}"
        "QPushButton:disabled{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #8f4d56,stop:1 #4b1820);border:1px solid rgba(255,255,255,0.20);color:rgba(255,255,255,0.74);}"
    );
}

static void applySettingsNavButtonStyle(QPushButton* button, bool submenu = false)
{
    if (!button) return;
    button->setStyleSheet(settingsNavButtonQss(submenu));
}

static void applySettingsReportButtonStyle(QPushButton* button)
{
    if (!button) return;
    button->setStyleSheet(settingsReportButtonQss());
}

static void applySettingsContrastPalette(QWidget* root)
{
    if (!root) return;
    const QColor text(settingsBuildTextPrimary());
    const QColor secondary(settingsBuildTextSecondary());
    auto applyOne = [&](QWidget* widget) {
        if (!widget) return;
        QPalette pal = widget->palette();
        pal.setColor(QPalette::WindowText, widget->property("settingsMutedText").toBool() ? secondary : text);
        pal.setColor(QPalette::Text, settingsBuildFlowIsDarkUi() ? QColor(QStringLiteral("#101418")) : text);
        pal.setColor(QPalette::ButtonText, text);
        widget->setPalette(pal);
    };
    applyOne(root);
    const auto children = root->findChildren<QWidget*>();
    for (auto* child : children) applyOne(child);
}

static QPushButton* makeNavBtn(const QString& text, QWidget* parent)
{
    auto* button = new QPushButton(text, parent);
    button->setCheckable(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    button->setFixedSize(186, 57);
    button->setFocusPolicy(Qt::NoFocus);
    auto* shadow = new QGraphicsDropShadowEffect(button);
    shadow->setBlurRadius(14);
    shadow->setOffset(0, 2);
    shadow->setColor(QColor(0, 0, 0, 80));
    button->setGraphicsEffect(shadow);
    return button;
}

} // namespace


void SettingsPage::buildUi()
{
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0,0,0,0);
    root->setSpacing(10);

    // Left: categories (buttons)
    auto* left = new QFrame(this);
    m_leftPane = left;
    left->setObjectName("settings_left");
    left->setFixedWidth(190);
    m_cachedLeftQss = settingsLeftQss();
    left->setStyleSheet(m_cachedLeftQss);

    buildCategoryButtons(left);

    // Right: framed pane
    auto* right = new QFrame(this);
    m_rightPane = right;
    right->setObjectName("settings_right");
    m_cachedRightQss = settingsRightQss();
    right->setStyleSheet(m_cachedRightQss);

    buildRightPane(right);

    root->addWidget(left);
    root->addWidget(right, 1);

    applySettingsContrastPalette(left);
    applySettingsContrastPalette(right);
}

void SettingsPage::buildCategoryButtons(QWidget* parent)
{
    auto* lay = new QVBoxLayout(parent);
    lay->setContentsMargins(10, 18, 10, 10);
    lay->setSpacing(qRound(parent->logicalDpiY()*1.5/25.4));

    m_catGroup = new QButtonGroup(this);
    m_catGroup->setExclusive(true);

    struct CBtn { Category c; QString t; };
    const QList<CBtn> btns = {
        {Category::General,      QString::fromUtf8("Загальні")},
        {Category::Ui,           QString::fromUtf8("Інтерфейс")},
        {Category::Integrations, QString::fromUtf8("Інтеграції")},
        {Category::Finance,      QString::fromUtf8("Фінанси")},
        {Category::Personnel,    QString::fromUtf8("Персонал")},
        {Category::Outsource,    QString::fromUtf8("Аутсорс")},
        {Category::Security,     QString::fromUtf8("Безпека та шифрування")},
        {Category::Synchronization, QString::fromUtf8("Синхронізація")},
        {Category::ExternalDisplay, QString::fromUtf8("Зовнішній екран")},
    };

    for (const auto& b : btns) {
        if (b.c == Category::Outsource) {
            continue;
        }
        auto* btn = makeNavBtn(b.t, parent);
        btn->setFixedSize(170, qMax(32,57-qRound(parent->logicalDpiY()*4.0/25.4)));
        applySettingsNavButtonStyle(btn, false);
        m_catGroup->addButton(btn, static_cast<int>(b.c));
        lay->addWidget(btn, 0, Qt::AlignHCenter);

        connect(btn, &QPushButton::clicked, this, [this, b]() {
            setCategory(b.c);
            if (b.c == Category::ExternalDisplay) showItem(QStringLiteral("ui.external_display"));
        });
    }

    lay->addStretch(1);

    m_sendReportButton = new QPushButton(QString::fromUtf8("Відправити звіт"), parent);
    m_sendReportButton->setCursor(Qt::PointingHandCursor);
    m_sendReportButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_sendReportButton->setFixedSize(170, qMax(32,57-qRound(parent->logicalDpiY()*4.0/25.4)));
    m_sendReportButton->setFocusPolicy(Qt::NoFocus);
    applySettingsReportButtonStyle(m_sendReportButton);
    auto* shadow = new QGraphicsDropShadowEffect(m_sendReportButton);
    shadow->setBlurRadius(18);
    shadow->setOffset(0, 3);
    shadow->setColor(QColor(80, 0, 10, 135));
    m_sendReportButton->setGraphicsEffect(shadow);
    lay->addWidget(m_sendReportButton, 0, Qt::AlignHCenter);

    connect(m_sendReportButton, &QPushButton::clicked, this, [this]() {
        if (m_hdrTitle) m_hdrTitle->setText(QString::fromUtf8("Звіт та логи"));
        showItem(QStringLiteral("diagnostics.report_logs"));
    });
}

void SettingsPage::buildRightPane(QWidget* parent)
{
    auto* outer = new QVBoxLayout(parent);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* box = new QFrame(parent);
    box->setObjectName("box");

    auto* boxL = new QVBoxLayout(box);
    boxL->setContentsMargins(0,0,0,0);
    boxL->setSpacing(0);

    auto* hdr = new QFrame(box);
    hdr->setObjectName("hdr");
    hdr->setFixedHeight(38);
    auto* hdrL = new QHBoxLayout(hdr);
    const int captionInset = qRound(2.0 * logicalDpiX() / 25.4);
    hdrL->setContentsMargins(captionInset,0,captionInset,0);

    m_hdrTitle = new QLabel(QString(), hdr);
    m_hdrTitle->setObjectName("hdrTitle");
    hdrL->addWidget(m_hdrTitle, 1);

    boxL->addWidget(hdr);

    m_contentStack = new QStackedWidget(box);

    m_itemsPage = new QWidget(m_contentStack);
    m_itemsPage->setObjectName(QStringLiteral("settings_items_page"));
    m_cachedItemsQss = settingsItemsPageQss();
    m_itemsPage->setStyleSheet(m_cachedItemsQss);
    auto* itemsPageL = new QVBoxLayout(m_itemsPage);
    itemsPageL->setContentsMargins(18,18,18,18);
    itemsPageL->setSpacing(12);

    auto* menuInfo = new QLabel(QString::fromUtf8("Оберіть пункт праворуч для відкриття детальних налаштувань."), m_itemsPage);
    menuInfo->setWordWrap(true);
    menuInfo->setStyleSheet(QString("color:%1; font-size:11pt;").arg(cSettingsTextSecondary()));
    itemsPageL->addWidget(menuInfo);

    m_items = new QListWidget(m_itemsPage);
    m_items->hide();
    itemsPageL->addWidget(m_items);

    m_itemsCategoryStack = new QStackedWidget(m_itemsPage);
    itemsPageL->addWidget(m_itemsCategoryStack, 1);

    m_detailPage = new QWidget(m_contentStack);
    m_detailPage->setObjectName(QStringLiteral("settings_detail_page"));
    auto* detailPageL = new QVBoxLayout(m_detailPage);
    detailPageL->setContentsMargins(0,0,0,0);
    detailPageL->setSpacing(0);

    m_detail = new QStackedWidget(m_detailPage);
    m_detail->addWidget(buildPlaceholderWidget(QString::fromUtf8("Оберіть пункт праворуч для відкриття детальних налаштувань.")));
    detailPageL->addWidget(m_detail, 1);

    m_contentStack->addWidget(m_itemsPage);
    m_contentStack->addWidget(m_detailPage);
    m_contentStack->setCurrentWidget(m_itemsPage);

    boxL->addWidget(m_contentStack, 1);

    auto* ftr = new QFrame(box);
    ftr->setObjectName("ftr");
    ftr->setFixedHeight(28);
    auto* ftrL = new QHBoxLayout(ftr);
    ftrL->setContentsMargins(captionInset,0,captionInset,0);

    m_footer = new QLabel(QString::fromUtf8("Оберіть пункт праворуч для відкриття детальних налаштувань."), ftr);
    m_footer->setObjectName("footerText");
    ftrL->addWidget(m_footer, 1);

    boxL->addWidget(ftr);

    outer->addWidget(box, 1);
}

QWidget* SettingsPage::ensureCategoryItemsPage(Category c)
{
    if (m_categoryItemsPages.contains(c)) return m_categoryItemsPages.value(c);

    auto* page = new QWidget(m_itemsCategoryStack);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0,0,0,0);
    layout->setSpacing(qRound(page->logicalDpiY()*1.5/25.4));

    auto* group = new QButtonGroup(page);
    group->setExclusive(true);
    m_categoryButtonGroups[c] = group;

    const auto items = m_itemsByCategory.value(c);
    int idx = 0;
    for (const auto& it : items) {
        auto* btn = makeNavBtn(it.title, page);
        btn->setFixedSize(186, qMax(32,57-qRound(page->logicalDpiY()*4.0/25.4)));
        applySettingsNavButtonStyle(btn, true);
        group->addButton(btn, idx++);
        layout->addWidget(btn, 0, Qt::AlignLeft);
        m_itemButtonById[it.id] = btn;
        connect(btn, &QPushButton::clicked, this, [this, it]() { showItem(it.id); });
    }
    layout->addStretch(1);

    m_itemsCategoryStack->addWidget(page);
    m_categoryItemsPages[c] = page;
    return page;
}

void SettingsPage::setPageFrozen(bool frozen)
{
    if (m_pageFrozen == frozen) return;
    m_pageFrozen = frozen;
    if (frozen) {
        if (m_refreshAllDebounce) m_refreshAllDebounce->stop();
        return;
    }
    if (m_themeRefreshPending) {
        m_themeRefreshPending = false;
        refreshThemeStyles();
    }
}


void SettingsPage::refreshThemeStyles()
{
    const auto fontSettings = UiSettingsService::loadFontSettings();
    const QStringList signatureParts = {
        ThemeManager::currentTheme(),
        fontSettings.fontFamily,
        QString::number(fontSettings.fontSize),
        QString::number(fontSettings.tableFontSize),
        fontSettings.fontColor
    };
    const QString signature = signatureParts.join(QStringLiteral("|"));
    if (signature == m_lastThemeRefreshSignature) return;
    if (m_pageFrozen) {
        m_lastThemeRefreshSignature = signature;
        m_themeRefreshPending = true;
        return;
    }
    m_lastThemeRefreshSignature = signature;

    setUpdatesEnabled(false);
    const QString leftQss = settingsLeftQss();
    const QString rightQss = settingsRightQss();
    const QString itemsQss = settingsItemsPageQss();
    if (m_leftPane) {
        if (m_cachedLeftQss != leftQss) {
            m_leftPane->setStyleSheet(leftQss);
            m_cachedLeftQss = leftQss;
        }
        applySettingsContrastPalette(m_leftPane);
    }
    if (m_rightPane) {
        if (m_cachedRightQss != rightQss) {
            m_rightPane->setStyleSheet(rightQss);
            m_cachedRightQss = rightQss;
        }
        applySettingsContrastPalette(m_rightPane);
    }
    if (m_itemsPage && m_cachedItemsQss != itemsQss) {
        m_itemsPage->setStyleSheet(itemsQss);
        m_cachedItemsQss = itemsQss;
    }

    if (m_catGroup) {
        const auto categoryButtons = m_catGroup->buttons();
        for (auto* abstractButton : categoryButtons) {
            if (auto* button = qobject_cast<QPushButton*>(abstractButton)) {
                applySettingsNavButtonStyle(button, false);
            }
        }
    }

    for (auto it = m_itemButtonById.cbegin(); it != m_itemButtonById.cend(); ++it) {
        if (auto* button = qobject_cast<QPushButton*>(it.value())) {
            applySettingsNavButtonStyle(button, true);
        }
    }
    applySettingsReportButtonStyle(m_sendReportButton);

    setUpdatesEnabled(true);
}


void SettingsPage::executeLegacyImport(const QString& picked, QWidget* sectionWidget)
{
    QPointer<QWidget> sectionGuard(sectionWidget);
    QPointer<SettingsPage> ownerGuard(this);
    if (sectionGuard) sectionGuard->setEnabled(false);

    auto* progress = new QProgressDialog(QString::fromUtf8("Імпорт старої БД..."), QString(), 0, 100, this);
    progress->setWindowTitle(QString::fromUtf8("Імпорт старої БД"));
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->setCancelButton(nullptr);
    progress->show();
    progress->repaint();

    struct ExecuteState {
        bool ok = false;
        LegacyMigrationResult result;
        QString errorText;
    };

    const auto cancelToken = AsyncTaskHandle::renewCancellationToken(m_legacyImportCancelToken);
    const AsyncTaskHandle asyncHandle(this, 0, cancelToken);
    auto state = std::make_shared<ExecuteState>();
    QPointer<QProgressDialog> progressGuard(progress);

    auto* worker = AsyncThreadScope::startWorker(this,
                                                 QStringLiteral("legacy_import_execute_worker"),
                                                 cancelToken,
                                                 [picked, state, progressGuard](const std::shared_ptr<std::atomic_bool>& cancelToken) {
        const auto progressCb = [progressGuard, cancelToken](const LegacyImportProgressState& st) {
            if (cancelToken && cancelToken->load()) return;
            if (!progressGuard) return;
            QMetaObject::invokeMethod(progressGuard.data(), [progressGuard, st]() {
                if (!progressGuard) return;
                if (!st.status.trimmed().isEmpty()) progressGuard->setLabelText(st.status.trimmed());
                const int value = st.mainPercent >= 0 ? qBound(0, st.mainPercent, 100) : 0;
                progressGuard->setRange(0, 100);
                progressGuard->setValue(st.finished ? 100 : value);
            }, Qt::QueuedConnection);
        };
        if (cancelToken && cancelToken->load()) return;
        state->ok = LegacyMigrationService::executeLegacyImport(picked, &state->result, &state->errorText, progressCb);
    });

    connect(worker, &QThread::finished, this, [this, sectionGuard, ownerGuard, progress, state, asyncHandle]() {
        if (progress) {
            progress->setValue(100);
            progress->close();
            progress->deleteLater();
        }
        if (sectionGuard) sectionGuard->setEnabled(true);
        if (!ownerGuard || !asyncHandle.shouldApply()) return;

        if (!state->ok) {
            QMessageBox box(QMessageBox::Warning,
                            QString::fromUtf8("Імпорт старої БД"),
                            QString::fromUtf8("Не вдалося виконати імпорт legacy БД.\n%1").arg(state->errorText),
                            QMessageBox::Ok,
                            this);
            applyStandardMessageBoxStyle(&box);
            box.exec();
            return;
        }

        QMessageBox box(QMessageBox::Information,
                        QString::fromUtf8("Імпорт старої БД"),
                        QString::fromUtf8("Імпорт успішно завершено.\nНарядів: %1\nРобіт: %2\nЗапчастин: %3")
                            .arg(state->result.importedOrderCount)
                            .arg(state->result.importedServiceCount)
                            .arg(state->result.importedPartCount),
                        QMessageBox::Ok,
                        this);
        applyStandardMessageBoxStyle(&box);
        box.exec();
        queueRefreshAll();
    });
    worker->start();
}
