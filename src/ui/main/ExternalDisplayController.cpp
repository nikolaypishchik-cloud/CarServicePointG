#include "ui/main/ExternalDisplayController.h"
#include "core/services/AccessControlService.h"
#include "ui/theme/ThemeManager.h"
#include <QApplication>
#include <QScreen>
#include <QWindow>
#include <QCryptographicHash>
#include <QSettings>
#include <QSignalBlocker>
#include <QEvent>
#include <QShortcut>
#include <QComboBox>
#include <QListWidget>
#include <QGridLayout>
#include <QFrame>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QScrollArea>
#include <QTableView>
#include <QHeaderView>
#include <QMessageBox>
#include <QTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QGraphicsView>
#include <QGraphicsProxyWidget>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSpinBox>
#include <QCheckBox>
#include <QLocale>
#include <QDate>

namespace {
int panelCount(int mode) {
    return mode == 0 ? 1 : mode == 3 ? 4 : mode >= 4 && mode <= 6 ? 3 : 2;
}
int panelRow(int mode, int index) {
    return mode == 2 ? index : mode == 3 || mode == 5 ? index / 2 : mode == 6 ? (index == 0 ? 0 : 1) : 0;
}
// Keep a live, interactive page at its usable size and fit it independently
// into its panel. Shrinking QWidget geometry alone clips minimum-size controls.
class FittedPageView final : public QGraphicsView {
public:
    FittedPageView(QWidget* page, QWidget* parent, const QString& settingsKey = {}) : QGraphicsView(parent), m_page(page), m_settingsKey(settingsKey) {
        setObjectName("externalFittedPage");
        setMinimumSize(80, 80);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        setFrameShape(QFrame::NoFrame);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setAlignment(Qt::AlignCenter);
        setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);
        setScene(new QGraphicsScene(this));
        setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
        page->setParent(nullptr);
        page->setProperty("externalFitToPanel", true);
        if (auto* top = page->findChild<QWidget*>("planning_topbar")) {
            for (auto* control : top->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) control->hide();
            while (auto* item = top->layout()->takeAt(0)) delete item;
            m_date = new QLabel(top); m_date->setObjectName("externalCurrentDate");
            m_date->setAlignment(Qt::AlignCenter); m_date->setWordWrap(true);
            top->layout()->addWidget(m_date);
        }
        page->resize(QSize(1200, 800).expandedTo(page->minimumSizeHint()));
        m_proxy = scene()->addWidget(page);
        auto* timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this] { fitPage(); });
        timer->start(250);
        fitPage();
    }
protected:
    void resizeEvent(QResizeEvent* event) override {
        QGraphicsView::resizeEvent(event);
        fitPage();
    }
private:
    void fitPage() {
        if (!m_proxy || !m_page) return;
        QSettings settings;
        const int fontSize = qBound(10, settings.value(m_settingsKey + "/fontSize", 24).toInt(), 48);
        const bool contrast = settings.value(m_settingsKey + "/contrast", true).toBool();
        const bool bold = settings.value(m_settingsKey + "/bold", true).toBool();
        if (m_date) {
            QString date = QLocale(QLocale::Ukrainian, QLocale::Ukraine).toString(QDate::currentDate(), "dddd, d MMMM yyyy");
            date[0] = date[0].toUpper();
            m_date->setText(date);
            m_date->setStyleSheet(QString("color:white;background:#202830;font-size:%1px;font-weight:bold;padding:6px;").arg(qMax(24, fontSize + 6)));
        }
        // Avoid nested offscreen effects inside the proxy: they leave stale tiles on TVs.
        for (auto* child : m_page->findChildren<QWidget*>()) {
            if (child->graphicsEffect()) child->setGraphicsEffect(nullptr);
            const QString name = child->objectName();
            if (name == "planning_daycell_body" || name == "planning_daycell_header" || qobject_cast<QTableView*>(child)) {
                const QString style = QString("font-size:%1px;font-weight:%2;color:%3;")
                    .arg(fontSize).arg(bold ? "bold" : "normal").arg(contrast ? "#000000" : "#27333c");
                if (child->property("externalTextStyle").toString() != style) {
                    if (!child->property("externalBaseStyle").isValid()) child->setProperty("externalBaseStyle", child->styleSheet());
                    child->setProperty("externalTextStyle", style);
                    child->setStyleSheet(child->property("externalBaseStyle").toString() +
                        (qobject_cast<QTableView*>(child) ? QString("\nQTableView { %1 }").arg(style) : QString("\n#%1 { %2 }").arg(name, style)));
                }
            }
        }
        auto* daily = m_page->findChild<QTableView*>("planningDailyTable");
        const bool dailyVisible = daily && !daily->isHidden();
        QSize size = (dailyVisible ? QSize(640, 600) : QSize(1200, 800))
            .expandedTo(m_page->minimumSizeHint()).expandedTo(m_page->minimumSize());
        if (m_page->layout()) m_page->layout()->activate();
        // Planning is finite: show the entire calendar or all working hours.
        if (m_page->objectName() == "planning_page") {
            if (auto* scroll = m_page->findChild<QScrollArea*>("planning_scroll"); scroll && !scroll->isHidden()) {
                const QSize required = scroll->widget()->minimumSizeHint();
                size = size.expandedTo(required + m_page->size() - scroll->viewport()->size());
            }
            if (auto* table = m_page->findChild<QTableView*>("planningDailyTable"); table && !table->isHidden()) {
                const int required = qMax(352, table->property("dailyMinimumRowsHeight").toInt());
                size.setHeight(qMax(size.height(), required + m_page->height() - table->viewport()->height() + 40));
            }
        }
        const QSize available = viewport()->size() - QSize(4, 4);
        if (available.width() <= 0 || available.height() <= 0) return;
        const qreal scale = qMin(qreal(available.width()) / size.width(), qreal(available.height()) / size.height());
        size = QSize(qCeil(available.width() / scale), qCeil(available.height() / scale));
        if (m_page->size() != size) m_page->resize(size);
        if (m_page->layout()) m_page->layout()->activate();
        setSceneRect(m_proxy->boundingRect());
        fitInView(sceneRect(), Qt::KeepAspectRatio);
    }
    QPointer<QWidget> m_page;
    QGraphicsProxyWidget* m_proxy = nullptr;
    QPointer<QLabel> m_date;
    QString m_settingsKey;
};

void styleCaption(QLabel* label) {
    label->setStyleSheet(ThemeManager::currentTheme() == "graphite"
        ? "color: #f7fbff; padding: 4px;" : "color: #101418; padding: 4px;");
}
}

QList<ExternalDisplayController::View> ExternalDisplayController::views() {
    return {{0, QString::fromUtf8("Тижневе планування")}, {100, QString::fromUtf8("Поточне планування")}, {1, QString::fromUtf8("Поточні наряди")},
        {3, QString::fromUtf8("Завершені наряди")}, {4, QString::fromUtf8("Замовлення запчастин")},
        {5, QString::fromUtf8("Звіти")}, {6, QString::fromUtf8("Статистика")},
        {13, QString::fromUtf8("Склад СТО")}};
}
QString ExternalDisplayController::screenKey(QScreen* screen) {
    if (!screen) return {};
    const auto identity = QJsonDocument(QJsonArray{screen->name(), screen->manufacturer(), screen->model(), screen->serialNumber()}).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
}
ExternalDisplayController::ExternalDisplayController(QWidget* owner, Factory factory, std::function<bool(int)> allowed)
    : QObject(owner), m_owner(owner), m_factory(std::move(factory)), m_allowed(std::move(allowed)) {
    owner->installEventFilter(this);
    connect(qApp, &QGuiApplication::screenRemoved, this, [this](QScreen* screen) { stop(screen); });
}
void ExternalDisplayController::stop(QScreen* screen) {
    m_pages.remove(screenKey(screen));
    auto window = m_windows.take(screenKey(screen));
    if (window) { window->hide(); window->deleteLater(); }
}
void ExternalDisplayController::stopAll() {
    m_pages.clear();
    const auto windows = m_windows; m_windows.clear();
    for (auto window : windows) if (window) { window->hide(); window->deleteLater(); }
}
bool ExternalDisplayController::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_owner && event->type() == QEvent::Close) stopAll();
    return QObject::eventFilter(watched, event);
}
void ExternalDisplayController::refresh() {
    for (const auto& pages : m_pages) for (const auto& page : pages) if (page.refresh) page.refresh();
}
bool ExternalDisplayController::display(QScreen* screen, const QList<int>& ids, int layoutMode, QString* error) {
    return openDisplay(screen, ids, layoutMode, error, false);
}
bool ExternalDisplayController::openDisplay(QScreen* screen, const QList<int>& ids, int layoutMode, QString* error, bool editor) {
    if (error) error->clear();
    const int capacity = panelCount(layoutMode);
    if (!screen || !qApp->screens().contains(screen) || layoutMode < 0 || layoutMode > 6 || ids.isEmpty() || ids.size() > capacity) {
        if (error) *error = QString::fromUtf8("Виберіть підключений екран і розділи відповідно до схеми розміщення."); return false;
    }
    bool hasContent = false;
    for (int id : ids) {
        if (id == -1) continue;
        bool known = false; for (const auto& view : views()) known |= view.id == id;
        if (!known || !m_allowed(id)) {
            if (error) *error = QString::fromUtf8("Вибраний розділ недоступний."); return false;
        }
        hasContent = true;
    }
    if (!hasContent) { if (error) *error = QString::fromUtf8("Виберіть хоча б один розділ."); return false; }
    if (!editor) stop(screen);
    const QString key = screenKey(screen);
    const QString appearanceKey = "externalDisplays/" + key + "/appearance";
    auto* window = new QWidget(m_owner, Qt::Window);
    window->setObjectName(editor ? "externalDisplayEditor" : "externalDisplayWindow");
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->setWindowTitle(QString::fromUtf8("CarServicePointG — Зовнішній екран"));
    auto* root = new QVBoxLayout(window); root->setContentsMargins(8, 8, 8, 8);
    auto* toolbar = new QHBoxLayout;
    auto* brand = new QLabel(QString::fromUtf8("CarServicePointG | %1").arg(screen->name()), window);
    styleCaption(brand); toolbar->addWidget(brand, 1);
    auto* close = new QPushButton(QString::fromUtf8("Закрити екран"), window);
    toolbar->addWidget(close); root->addLayout(toolbar);
    connect(close, &QPushButton::clicked, window, &QWidget::close);
    if (editor) {
        window->setWindowTitle(QString::fromUtf8("Налаштувати показ · %1 × %2").arg(screen->size().width()).arg(screen->size().height()));
        close->setText(QString::fromUtf8("Готово"));
        auto* controls = new QHBoxLayout;
        auto* fontLabel = new QLabel(QString::fromUtf8("Розмір шрифту"), window); styleCaption(fontLabel); controls->addWidget(fontLabel);
        auto* font = new QSpinBox(window); font->setObjectName("externalFontSize"); font->setRange(10, 48);
        font->setValue(QSettings().value(appearanceKey + "/fontSize", 24).toInt()); controls->addWidget(font);
        auto* contrast = new QCheckBox(QString::fromUtf8("Високий контраст"), window); contrast->setObjectName("externalContrast");
        contrast->setChecked(QSettings().value(appearanceKey + "/contrast", true).toBool()); controls->addWidget(contrast);
        auto* bold = new QCheckBox(QString::fromUtf8("Чіткий жирний текст"), window); bold->setObjectName("externalBoldText");
        bold->setChecked(QSettings().value(appearanceKey + "/bold", true).toBool()); controls->addWidget(bold);
        for (auto* check : {contrast, bold}) check->setStyleSheet("QCheckBox{color:#ffffff;spacing:8px;} QCheckBox::indicator{width:20px;height:20px;border:2px solid #c8d4df;background:#ffffff;} QCheckBox::indicator:checked{background:#168343;}");
        controls->addStretch(); root->addLayout(controls);
        auto* hint = new QLabel(QString::fromUtf8("Перетягніть межу між панелями. Зміни зберігаються автоматично та застосовуються до показу."), window);
        hint->setWordWrap(true); styleCaption(hint); root->addWidget(hint);
        connect(font, &QSpinBox::valueChanged, window, [appearanceKey](int value) { QSettings().setValue(appearanceKey + "/fontSize", value); });
        connect(contrast, &QCheckBox::toggled, window, [appearanceKey](bool value) { QSettings().setValue(appearanceKey + "/contrast", value); });
        connect(bold, &QCheckBox::toggled, window, [appearanceKey](bool value) { QSettings().setValue(appearanceKey + "/bold", value); });
    }
    auto* outer = new QSplitter(layoutMode == 2 || layoutMode == 3 || layoutMode >= 5 ? Qt::Vertical : Qt::Horizontal, window);
    outer->setObjectName("externalOuterSplitter");
    outer->setHandleWidth(10);
    outer->setChildrenCollapsible(false); root->addWidget(outer, 1);
    QList<Page> pages;
    QSplitter* row = nullptr;
    for (int index = 0; index < ids.size(); ++index) {
        QWidget* container = outer;
        if (layoutMode == 3 || layoutMode >= 5) {
            const int rowIndex = panelRow(layoutMode, index);
            if (!row || panelRow(layoutMode, index - 1) != rowIndex) { row = new QSplitter(Qt::Horizontal, outer); row->setObjectName("externalRow" + QString::number(rowIndex)); row->setChildrenCollapsible(false); }
            container = row;
        }
        auto* panel = new QWidget(container);
        auto* content = new QVBoxLayout(panel); content->setContentsMargins(4, 4, 4, 4);
        if (ids[index] == -1) continue;
        QString title; for (const auto& view : views()) if (view.id == ids[index]) title = view.title;
        auto* caption = new QLabel(title, panel); styleCaption(caption); content->addWidget(caption);
        auto page = m_factory(ids[index], panel);
        if (!page.widget) { delete window; if (error) *error = QString::fromUtf8("Не вдалося відкрити розділ."); return false; }
        for (auto* table : page.widget->findChildren<QTableView*>()) {
            table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
            table->horizontalHeader()->setResizeContentsPrecision(64);
            table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
            table->horizontalHeader()->setStretchLastSection(true);
        }
        content->addWidget(new FittedPageView(page.widget, panel, appearanceKey), 1); pages.append(page);
    }
    if (editor) {
        const QRect available = m_owner->screen()->availableGeometry().adjusted(30, 40, -30, -40);
        QSize size = screen->size(); size.scale(available.size(), Qt::KeepAspectRatio);
        window->resize(size); window->move(available.center() - window->rect().center()); window->show();
    } else {
    // QScreen geometry is in logical pixels; Qt handles each screen's DPI.
    window->winId(); window->windowHandle()->setScreen(screen);
    window->setGeometry(screen->geometry());
    window->setAttribute(Qt::WA_ShowWithoutActivating);
    window->show();
    // Map the native window to the selected display before entering fullscreen.
    // Windows can otherwise use the owner's monitor during first presentation.
    window->windowHandle()->setScreen(screen);
    window->setGeometry(screen->geometry());
    window->showFullScreen();
    QTimer::singleShot(0, window, [window, target = QPointer<QScreen>(screen)] {
        if (!target) return;
        window->windowHandle()->setScreen(target);
        window->setGeometry(target->geometry());
    });
    m_windows.insert(key, window);
    m_pages.insert(key, pages);
    connect(window, &QObject::destroyed, this, [this, key] { if (m_windows.value(key).isNull()) { m_windows.remove(key); m_pages.remove(key); } });
    }
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), window);
    connect(escape, &QShortcut::activated, window, &QWidget::close);
    QStringList viewIds; for (int id : ids) viewIds << QString::number(id);
    const QString sizeGroup = "externalDisplays/" + key + "/sizes/" + QString::number(layoutMode) + "-" + viewIds.join("-") + "/";
    for (auto* splitter : window->findChildren<QSplitter*>()) {
        QSettings settings;
        const auto savedState = settings.value(sizeGroup + splitter->objectName()).toByteArray();
        if (!savedState.isEmpty()) splitter->restoreState(savedState);
        else splitter->setSizes(QList<int>(splitter->count(), 1000));
        connect(splitter, &QSplitter::splitterMoved, window, [this, splitter, sizeGroup, key, editor] {
            QSettings settings; settings.setValue(sizeGroup + splitter->objectName(), splitter->saveState());
            if (editor) if (auto live = m_windows.value(key); live && live->property("sizeGroup").toString() == sizeGroup)
                if (auto* other = live->findChild<QSplitter*>(splitter->objectName())) other->restoreState(splitter->saveState());
        });
    }
    window->setProperty("sizeGroup", sizeGroup);
    if (!editor) connect(screen, &QScreen::geometryChanged, window, [window, screen] { window->setGeometry(screen->geometry()); });
    connect(qApp, &QGuiApplication::screenRemoved, window, [window, screen](QScreen* removed) { if (removed == screen) window->close(); });
    const QString scope = AccessControlService::currentScopeKey();
    auto* timer = new QTimer(window); timer->setInterval(1000);
    connect(timer, &QTimer::timeout, window, [this, window, ids, pages, scope, tick = 0]() mutable {
        if (scope != AccessControlService::currentScopeKey()) { window->close(); return; }
        for (int id : ids) if (id != -1 && !m_allowed(id)) { window->close(); return; }
        if (++tick % 30 == 0) for (const auto& page : pages) if (page.refresh) page.refresh();
    });
    timer->start();
    for (const auto& page : pages) if (page.refresh) page.refresh();
    return true;
}
QWidget* ExternalDisplayController::settingsWidget(QWidget* parent) {
    auto* widget = new QWidget(parent); widget->setObjectName("externalDisplaySettings");
    auto* root = new QVBoxLayout(widget);
    auto* actions = new QHBoxLayout;
    auto* back = new QPushButton(QString::fromUtf8("← Назад"), widget); back->setObjectName("externalBack");
    actions->addWidget(back); actions->addStretch();
    auto* show = new QPushButton(QString::fromUtf8("Відобразити"), widget); show->setObjectName("showExternalDisplay");
    auto* stopButton = new QPushButton(QString::fromUtf8("Вимкнути показ"), widget); stopButton->setObjectName("stopExternalDisplay");
    actions->addWidget(show); actions->addWidget(stopButton); root->addLayout(actions);
    auto* scroll = new QScrollArea(widget); scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* body = new QWidget(scroll);
    auto* form = new QVBoxLayout(body); form->setSpacing(12);
    scroll->setWidget(body); root->addWidget(scroll, 1);
    auto caption = [body, form](const QString& text) {
        auto* label = new QLabel(text, body); label->setWordWrap(true);
        styleCaption(label); form->addWidget(label); return label;
    };
    caption(QString::fromUtf8("Монітор або телевізор"));
    auto* screens = new QComboBox(body); screens->setObjectName("externalScreenSelector"); form->addWidget(screens);
    auto* info = caption({});
    caption(QString::fromUtf8("Розташування панелей"));
    auto* layout = new QComboBox(body); layout->setObjectName("externalLayoutSelector");
    layout->addItems({QString::fromUtf8("Один розділ"), QString::fromUtf8("Два поруч"),
        QString::fromUtf8("Два один над одним"), QString::fromUtf8("Сітка 2 × 2"), QString::fromUtf8("Три поруч"),
        QString::fromUtf8("Дві зверху, одна знизу"), QString::fromUtf8("Одна зверху, дві знизу")}); form->addWidget(layout);
    caption(QString::fromUtf8("Прев’ю екрана"));
    auto* preview = new QWidget(body); preview->setObjectName("externalLayoutPreview");
    auto* grid = new QGridLayout(preview); grid->setSpacing(12); form->addWidget(preview);
    QList<QFrame*> panels; QList<QComboBox*> selectors; QList<QLabel*> images; QList<QLabel*> positions;
    for (int i = 0; i < 4; ++i) {
        auto* panel = new QFrame(preview); panel->setFrameShape(QFrame::StyledPanel);
        panel->setStyleSheet("QFrame{background:#e9eef3;border:1px solid #a8b5c1;border-radius:6px;} QLabel{color:#172630;border:none;}");
        auto* content = new QVBoxLayout(panel);
        auto* position = new QLabel(panel); position->setWordWrap(true); content->addWidget(position); positions << position;
        auto* selector = new QComboBox(panel); selector->setObjectName("externalPanel" + QString::number(i));
        selector->setMinimumContentsLength(10); selector->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        selector->addItem(QString::fromUtf8("Не показувати"), -1);
        for (const auto& view : views()) if (m_allowed(view.id)) selector->addItem(view.title, view.id);
        selector->setStyleSheet("QComboBox{background:white;color:#101418;padding:6px;border:1px solid #a8b5c1;}");
        content->addWidget(selector); selectors << selector;
        auto* image = new QLabel(panel); image->setAlignment(Qt::AlignCenter);
        image->setMinimumWidth(0); image->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        image->setFixedHeight(90); image->setWordWrap(true);
        content->addWidget(image, 1); images << image; panels << panel;
        connect(selector, &QComboBox::currentIndexChanged, widget, [selector, image] {
            image->clear(); image->setText(selector->currentData().toInt() == -1 ? QString::fromUtf8("Порожня панель") : selector->currentText());
            selector->setToolTip(selector->currentText());
        });
    }
    auto arrange = [layout, grid, panels, positions] {
        while (auto* item = grid->takeAt(0)) delete item;
        const int mode = layout->currentIndex(), count = panelCount(mode);
        for (int i = 0; i < panels.size(); ++i) {
            panels[i]->setVisible(i < count);
            if (i >= count) continue;
            const int row = panelRow(mode, i);
            const int column = mode == 1 || mode == 4 ? i : mode == 3 || mode == 5 ? i % 2 : mode == 6 && i > 0 ? i - 1 : 0;
            const int span = (mode == 5 && i == 2) || (mode == 6 && i == 0) ? 2 : 1;
            positions[i]->setText(mode == 4 ? QString::fromUtf8("Панель %1").arg(i + 1) :
                span == 2 ? (row == 0 ? QString::fromUtf8("Вгорі") : QString::fromUtf8("Внизу")) : mode == 0 ? QString::fromUtf8("Увесь екран") :
                mode == 1 ? (i == 0 ? QString::fromUtf8("Ліворуч") : QString::fromUtf8("Праворуч")) :
                mode == 2 ? (i == 0 ? QString::fromUtf8("Вгорі") : QString::fromUtf8("Внизу")) :
                QString::fromUtf8("%1 · %2").arg(row == 0 ? QString::fromUtf8("Вгорі") : QString::fromUtf8("Внизу"),
                    column == 0 ? QString::fromUtf8("ліворуч") : QString::fromUtf8("праворуч")));
            grid->addWidget(panels[i], row, column, 1, span);
        }
    };
    connect(layout, &QComboBox::currentIndexChanged, widget, [arrange] { arrange(); });
    auto* refreshPreview = new QPushButton(QString::fromUtf8("Оновити прев’ю"), body);
    refreshPreview->setObjectName("refreshExternalPreview");
    auto* previewActions = new QHBoxLayout; previewActions->addWidget(refreshPreview);
    auto* configure = new QPushButton(QString::fromUtf8("Налаштувати показ"), body); configure->setObjectName("configureExternalDisplay");
    previewActions->addWidget(configure); form->addLayout(previewActions);
    connect(configure, &QPushButton::clicked, widget, [this, widget, screens, selectors, layout] {
        QScreen* target = nullptr;
        for (auto* screen : qApp->screens()) if (screenKey(screen) == screens->currentData().toString()) target = screen;
        QList<int> ids;
        const int count = panelCount(layout->currentIndex());
        for (int i = 0; i < count; ++i) ids << selectors[i]->currentData().toInt();
        for (auto* old : m_owner->findChildren<QWidget*>("externalDisplayEditor")) old->close();
        QString error;
        if (!openDisplay(target, ids, layout->currentIndex(), &error, true)) QMessageBox::warning(widget, QString::fromUtf8("Налаштувати показ"), error);
    });
    connect(refreshPreview, &QPushButton::clicked, widget, [this, widget, selectors, images, layout, refreshPreview, screens] {
        refreshPreview->setEnabled(false);
        const int count = panelCount(layout->currentIndex());
        for (int i = 0; i < count; ++i) {
            const int id = selectors[i]->currentData().toInt(); if (id == -1 || !m_allowed(id)) continue;
            auto* host = new QWidget(widget, Qt::Tool);
            host->setAttribute(Qt::WA_DontShowOnScreen); host->resize(1000, 650);
            auto* hostLayout = new QVBoxLayout(host);
            auto page = m_factory(id, host);
            if (!page.widget) { host->deleteLater(); continue; }
            hostLayout->addWidget(new FittedPageView(page.widget, host, "externalDisplays/" + screens->currentData().toString() + "/appearance")); host->show();
            if (page.refresh) page.refresh();
            QPointer<QWidget> guard(host);
            QTimer::singleShot(1500, widget, [guard, image = images[i], selector = selectors[i], id] {
                if (!guard) return;
                if (selector->currentData().toInt() == id) {
                    auto thumbnail = guard->grab().scaled(qMax(80, image->width()), 90, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    thumbnail.setDevicePixelRatio(1.0);
                    image->setText({}); image->setPixmap(thumbnail);
                }
                guard->hide(); guard->deleteLater();
            });
        }
        QTimer::singleShot(1600, refreshPreview, [refreshPreview] { refreshPreview->setEnabled(true); });
    });
    form->addStretch();
    auto load = [this, screens, layout, selectors, info, arrange] {
        QScreen* target = nullptr;
        for (auto* screen : qApp->screens()) if (screenKey(screen) == screens->currentData().toString()) target = screen;
        if (!target) { info->setText(QString::fromUtf8("Немає підключеного екрана.")); return; }
        info->setText(QString::fromUtf8("%1 × %2 · масштаб %3%. Телевізор Wi-Fi підключіть у Windows як розширений екран.")
            .arg(qRound(target->size().width() * target->devicePixelRatio())).arg(qRound(target->size().height() * target->devicePixelRatio())).arg(qRound(target->devicePixelRatio() * 100)));
        QSettings settings; settings.beginGroup("externalDisplays/" + screenKey(target));
        layout->setCurrentIndex(qBound(0, settings.value("layout", 0).toInt(), 6));
        const auto saved = settings.value("views", QStringList{"0"}).toStringList();
        for (int i = 0; i < selectors.size(); ++i) {
            const int id = i < saved.size() ? saved[i].toInt() : -1;
            const int index = selectors[i]->findData(id);
            selectors[i]->setCurrentIndex(index < 0 ? 0 : index);
            selectors[i]->setToolTip(selectors[i]->currentText());
        }
        arrange();
    };
    auto refill = [this, screens, load] {
        const auto previous = screens->currentData().toString(); QSignalBlocker blocker(screens); screens->clear();
        for (auto* screen : qApp->screens()) screens->addItem(screen->name() + (screen == qApp->primaryScreen() ? QString::fromUtf8(" (основний)") : QString()), screenKey(screen));
        int index = screens->findData(previous);
        if (index < 0) {
            const auto preferred = QSettings().value("externalDisplays/target").toString();
            index = screens->findData(preferred);
            if (index < 0) for (auto* screen : qApp->screens()) {
                if (screen != m_owner->screen()) { index = screens->findData(screenKey(screen)); break; }
            }
        }
        screens->setCurrentIndex(index >= 0 ? index : 0); load();
    };
    connect(screens, &QComboBox::currentIndexChanged, widget, [load] { load(); });
    connect(qApp, &QGuiApplication::screenAdded, widget, [refill] { refill(); });
    connect(qApp, &QGuiApplication::screenRemoved, widget, [refill] { refill(); });
    connect(show, &QPushButton::clicked, widget, [this, widget, screens, selectors, layout] {
        QScreen* target = nullptr; for (auto* screen : qApp->screens()) if (screenKey(screen) == screens->currentData().toString()) target = screen;
        QList<int> ids; QStringList saved;
        const int count = panelCount(layout->currentIndex());
        for (int i = 0; i < count; ++i) { ids << selectors[i]->currentData().toInt(); saved << QString::number(ids.last()); }
        QString error;
        if (!display(target, ids, layout->currentIndex(), &error)) { QMessageBox::warning(widget, QString::fromUtf8("Зовнішній екран"), error); return; }
        QSettings settings; settings.setValue("externalDisplays/target", screenKey(target));
        settings.beginGroup("externalDisplays/" + screenKey(target)); settings.setValue("layout", layout->currentIndex()); settings.setValue("views", saved); settings.sync();
        if (settings.status() != QSettings::NoError) QMessageBox::warning(widget, QString::fromUtf8("Зовнішній екран"), QString::fromUtf8("Показ увімкнено, але налаштування не вдалося зберегти."));
    });
    connect(stopButton, &QPushButton::clicked, widget, [this, screens] {
        for (auto* screen : qApp->screens()) if (screenKey(screen) == screens->currentData().toString()) stop(screen);
    });
    refill();
    return widget;
}
