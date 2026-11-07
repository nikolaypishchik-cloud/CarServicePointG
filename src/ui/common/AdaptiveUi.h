#pragma once

#include "core/services/settings/UiStateSettingsService.h"

#include <QAbstractItemView>
#include <QAbstractItemModel>
#include <QApplication>
#include <QByteArray>
#include <QEvent>
#include <QGuiApplication>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QScreen>
#include <QScrollBar>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>
#include <QWindow>
#include <QtMath>
#include <climits>
#include <utility>

namespace AdaptiveUi {

inline QScreen* currentScreen(const QWidget* widget)
{
    if (!widget) return QGuiApplication::primaryScreen();
    if (const auto* win = widget->windowHandle(); win && win->screen()) return win->screen();
    if (const auto* wh = widget->window()->windowHandle(); wh && wh->screen()) return wh->screen();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (auto* sc = widget->screen()) return sc;
#endif
    return QGuiApplication::primaryScreen();
}

inline qreal dpiScale(const QWidget* widget)
{
    const QScreen* sc = currentScreen(widget);
    const qreal dpi = sc ? sc->logicalDotsPerInch() : 96.0;
    return qBound<qreal>(1.0, dpi / 96.0, 2.25);
}

inline int px(const QWidget* widget, int basePx)
{
    return qMax(1, qRound(basePx * dpiScale(widget)));
}

inline QRect availableRect(const QWidget* widget)
{
    const QScreen* sc = currentScreen(widget);
    return sc ? sc->availableGeometry() : QRect(0, 0, 1600, 900);
}

inline void fitTopLevelWindow(QWidget* window, const QSize& designSize, const QSize& minimumSize)
{
    if (!window || !window->isWindow()) return;
    if (window->isMaximized() || window->isFullScreen()) return;

    const QRect avail = availableRect(window);
    const qreal scale = dpiScale(window);
    const QSize scaledDesign(qRound(designSize.width() * qMin<qreal>(scale, 1.35)),
                             qRound(designSize.height() * qMin<qreal>(scale, 1.30)));
    const QSize scaledMin(qRound(minimumSize.width() * qMin<qreal>(scale, 1.20)),
                          qRound(minimumSize.height() * qMin<qreal>(scale, 1.20)));

    const int maxW = qMax(scaledMin.width(), qRound(avail.width() * 0.96));
    const int maxH = qMax(scaledMin.height(), qRound(avail.height() * 0.95));

    QSize target = window->size().isValid() ? window->size() : scaledDesign;
    if (target.width() <= 1 || target.height() <= 1) target = scaledDesign;
    target = target.expandedTo(scaledMin).boundedTo(QSize(maxW, maxH));

    if (window->minimumSize().width() < scaledMin.width() || window->minimumSize().height() < scaledMin.height()) {
        window->setMinimumSize(scaledMin);
    }
    if (window->width() != target.width() || window->height() != target.height()) {
        window->resize(target);
    }

    QRect geom = window->frameGeometry();
    if (!avail.contains(geom)) {
        const QPoint centered(avail.center().x() - geom.width() / 2,
                              avail.center().y() - geom.height() / 2);
        geom.moveTopLeft(centered);
        if (geom.left() < avail.left()) geom.moveLeft(avail.left());
        if (geom.top() < avail.top()) geom.moveTop(avail.top());
        if (geom.right() > avail.right()) geom.moveRight(avail.right());
        if (geom.bottom() > avail.bottom()) geom.moveBottom(avail.bottom());
        window->move(geom.topLeft());
    }
}

inline void fillTopLevelWindow(QWidget* window,
                               const QSize& minimumSize,
                               qreal widthRatio = 0.985,
                               qreal heightRatio = 0.975)
{
    if (!window || !window->isWindow()) return;
    if (window->isMaximized() || window->isFullScreen()) return;

    const QRect avail = availableRect(window);
    const qreal scale = dpiScale(window);
    const QSize scaledMin(qRound(minimumSize.width() * qMin<qreal>(scale, 1.20)),
                          qRound(minimumSize.height() * qMin<qreal>(scale, 1.20)));
    if (window->minimumSize().width() < scaledMin.width() || window->minimumSize().height() < scaledMin.height()) {
        window->setMinimumSize(scaledMin);
    }

    const int targetW = qBound(scaledMin.width(), qRound(avail.width() * qBound<qreal>(0.75, widthRatio, 1.0)), avail.width());
    const int targetH = qBound(scaledMin.height(), qRound(avail.height() * qBound<qreal>(0.75, heightRatio, 1.0)), avail.height());
    const QRect targetRect(avail.center().x() - targetW / 2,
                           avail.center().y() - targetH / 2,
                           targetW,
                           targetH);
    if (window->geometry() != targetRect) {
        window->setGeometry(targetRect);
    }
}

struct ColumnSpec {
    int logicalIndex = -1;
    int minimum = 56;
    int preferred = 96;
    int stretch = 0;
    int maximum = -1;
    bool fixed = false;
};

class TableAutoController final : public QObject
{
public:
    static TableAutoController* install(QTableView* table,
                                        QVector<ColumnSpec> specs,
                                        const QString& settingsPrefix)
    {
        if (!table || specs.isEmpty()) return nullptr;
        auto* ctl = new TableAutoController(table, std::move(specs), settingsPrefix);
        ctl->restoreManualState();
        ctl->scheduleApply();
        return ctl;
    }

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (!m_table) return QObject::eventFilter(watched, event);
        switch (event ? event->type() : QEvent::None) {
        case QEvent::Show:
        case QEvent::Resize:
        case QEvent::LayoutRequest:
        case QEvent::PolishRequest:
        case QEvent::ScreenChangeInternal:
            scheduleApply();
            break;
        default:
            break;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    TableAutoController(QTableView* table,
                        QVector<ColumnSpec> specs,
                        const QString& settingsPrefix)
        : QObject(table)
        , m_table(table)
        , m_specs(std::move(specs))
        , m_settingsPrefix(settingsPrefix)
    {
        m_applyDebounce.setParent(this);
        m_applyDebounce.setSingleShot(true);
        m_applyDebounce.setInterval(45);
        connect(&m_applyDebounce, &QTimer::timeout, this, [this]() { applyNow(); });

        if (m_table) {
            m_table->installEventFilter(this);
            if (m_table->viewport()) m_table->viewport()->installEventFilter(this);
            if (auto* header = m_table->horizontalHeader()) {
                header->installEventFilter(this);
                connect(header, &QHeaderView::sectionResized, this, [this](int logical, int, int now) {
                    if (m_applying || logical < 0 || now <= 0) return;
                    m_manualMode = true;
                    persistMode();
                    saveWidths();
                });
                connect(header, &QHeaderView::sectionMoved, this, [this](int, int, int) {
                    if (m_applying) return;
                    m_manualMode = true;
                    persistMode();
                    saveHeaderState();
                });
            }
            if (m_table->model()) {
                connect(m_table->model(), &QAbstractItemModel::modelReset, this, [this]() { scheduleApply(); });
                connect(m_table->model(), &QAbstractItemModel::layoutChanged, this, [this]() { scheduleApply(); });
                connect(m_table->model(), &QAbstractItemModel::columnsInserted, this, [this]() { scheduleApply(); });
            }
        }
    }

    void scheduleApply(int delayMs = 20)
    {
        if (!m_table) return;
        if (m_manualMode) return;
        m_applyDebounce.start(qMax(0, delayMs));
    }

    void restoreManualState()
    {
        const QString mode = UiStateSettingsService::loadValue(m_settingsPrefix + QStringLiteral(".mode"), QStringLiteral("auto")).toString().trimmed().toLower();
        m_manualMode = (mode == QStringLiteral("manual"));
        if (!m_manualMode || !m_table) return;

        const QString rawState = UiStateSettingsService::loadValue(m_settingsPrefix + QStringLiteral(".header_state"), QString()).toString().trimmed();
        if (!rawState.isEmpty()) {
            if (auto* header = m_table->horizontalHeader()) {
                header->restoreState(QByteArray::fromBase64(rawState.toUtf8()));
            }
        }

        const QString rawWidths = UiStateSettingsService::loadValue(m_settingsPrefix + QStringLiteral(".widths"), QStringLiteral("{}")).toString();
        const QJsonDocument doc = QJsonDocument::fromJson(rawWidths.toUtf8());
        if (!doc.isObject()) return;

        const QJsonObject obj = doc.object();
        m_applying = true;
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            bool ok = false;
            const int logical = it.key().toInt(&ok);
            if (!ok || logical < 0 || logical >= m_table->model()->columnCount()) continue;
            const int width = it.value().toInt();
            if (width > 0 && !m_table->isColumnHidden(logical)) m_table->setColumnWidth(logical, width);
        }
        m_applying = false;
    }

    void persistMode() const
    {
        UiStateSettingsService::saveValue(m_settingsPrefix + QStringLiteral(".mode"), m_manualMode ? QStringLiteral("manual") : QStringLiteral("auto"));
    }

    void saveHeaderState() const
    {
        if (!m_table || !m_table->horizontalHeader()) return;
        UiStateSettingsService::saveValue(m_settingsPrefix + QStringLiteral(".header_state"),
                                   QString::fromLatin1(m_table->horizontalHeader()->saveState().toBase64()));
    }

    void saveWidths() const
    {
        if (!m_table || !m_table->model()) return;
        QJsonObject obj;
        for (int logical = 0; logical < m_table->model()->columnCount(); ++logical) {
            if (m_table->isColumnHidden(logical)) continue;
            obj.insert(QString::number(logical), m_table->columnWidth(logical));
        }
        UiStateSettingsService::saveValue(m_settingsPrefix + QStringLiteral(".widths"),
                                   QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
        saveHeaderState();
    }

    void applyNow()
    {
        if (!m_table || !m_table->model() || m_manualMode) return;
        auto* header = m_table->horizontalHeader();
        if (!header) return;

        const int viewportWidth = m_table->viewport() ? m_table->viewport()->width() : m_table->width();
        if (viewportWidth <= 100) return;

        QVector<ColumnSpec> active;
        active.reserve(m_specs.size());
        for (const ColumnSpec& spec : m_specs) {
            if (spec.logicalIndex < 0 || spec.logicalIndex >= m_table->model()->columnCount()) continue;
            if (m_table->isColumnHidden(spec.logicalIndex)) continue;
            active.push_back(spec);
        }
        if (active.isEmpty()) return;

        QVector<int> minWidths;
        QVector<int> prefWidths;
        minWidths.reserve(active.size());
        prefWidths.reserve(active.size());

        int totalMin = 0;
        int totalPref = 0;
        int stretchSum = 0;
        for (const ColumnSpec& spec : active) {
            const int minW = px(m_table, spec.minimum);
            int prefW = qMax(minW, px(m_table, spec.preferred));
            if (spec.fixed) prefW = minW;
            if (spec.maximum > 0) prefW = qMin(prefW, px(m_table, spec.maximum));
            minWidths.push_back(minW);
            prefWidths.push_back(prefW);
            totalMin += minW;
            totalPref += prefW;
            if (!spec.fixed) stretchSum += qMax(0, spec.stretch);
        }

        QVector<int> widths = prefWidths;
        if (viewportWidth <= totalMin) {
            widths = minWidths;
        } else if (viewportWidth < totalPref) {
            const int gain = viewportWidth - totalMin;
            const int span = qMax(1, totalPref - totalMin);
            const qreal ratio = qBound<qreal>(0.0, qreal(gain) / qreal(span), 1.0);
            for (int i = 0; i < active.size(); ++i) {
                widths[i] = minWidths[i] + qRound((prefWidths[i] - minWidths[i]) * ratio);
            }
        } else {
            int extra = viewportWidth - totalPref;
            if (stretchSum <= 0) stretchSum = active.size();
            for (int i = 0; i < active.size(); ++i) {
                if (active[i].fixed) continue;
                const int weight = qMax(1, active[i].stretch);
                int delta = qRound(extra * (qreal(weight) / qreal(stretchSum)));
                int maxW = active[i].maximum > 0 ? px(m_table, active[i].maximum) : INT_MAX;
                widths[i] = qMin(maxW, widths[i] + delta);
            }
            int used = 0;
            for (int w : widths) used += w;
            int leftover = viewportWidth - used;
            for (int i = 0; leftover > 0 && i < active.size(); ++i) {
                if (active[i].fixed) continue;
                const int maxW = active[i].maximum > 0 ? px(m_table, active[i].maximum) : INT_MAX;
                if (widths[i] >= maxW) continue;
                ++widths[i];
                --leftover;
                if (i + 1 == active.size() && leftover > 0) i = -1;
            }
        }

        m_applying = true;
        header->setMinimumSectionSize(px(m_table, 36));
        for (int i = 0; i < active.size(); ++i) {
            const int logical = active[i].logicalIndex;
            const int width = qMax(minWidths[i], widths[i]);
            if (m_table->columnWidth(logical) != width) m_table->setColumnWidth(logical, width);
        }
        m_applying = false;
    }

    QPointer<QTableView> m_table;
    QVector<ColumnSpec> m_specs;
    QString m_settingsPrefix;
    QTimer m_applyDebounce;
    bool m_applying = false;
    bool m_manualMode = false;
};

inline TableAutoController* installAdaptiveTable(QTableView* table,
                                                 QVector<ColumnSpec> specs,
                                                 const QString& settingsPrefix)
{
    return TableAutoController::install(table, std::move(specs), settingsPrefix);
}

inline void polishToolbarButton(QWidget* button, int baseHeight = 34, int baseMinWidth = 0)
{
    if (!button) return;
    button->setMinimumHeight(px(button, baseHeight));
    if (baseMinWidth > 0) button->setMinimumWidth(px(button, baseMinWidth));
}

inline void relaxLineEditWidth(QWidget* editor, int minBase, int maxBase = -1)
{
    if (!editor) return;
    editor->setMinimumWidth(px(editor, minBase));
    if (maxBase > 0) editor->setMaximumWidth(px(editor, maxBase));
}

} // namespace AdaptiveUi

