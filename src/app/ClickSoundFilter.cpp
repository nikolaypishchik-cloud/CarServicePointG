#include "app/ClickSoundFilter.h"
#include "app/ClickSoundBackend.h"
#include "app/Logger.h"

#include <QByteArray>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDebug>
#include <QEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTabBar>
#include <QTableView>
#include <QTextEdit>
#include <QWidget>
#include <QWindowStateChangeEvent>
#include <QStringList>
#include <QString>
#include <QtGlobal>


static bool deepClickDiagnosticsEnabled()
{
    const QByteArray env = qgetenv("CS_DEEP_DEBUG").trimmed();
    return env == "1" || env.compare("true", Qt::CaseInsensitive) == 0;
}

static QString clickLogTargetName(QObject* watched)
{
    if (!watched) return QStringLiteral("<null>");
    QStringList parts;
    parts << QString::fromLatin1(watched->metaObject()->className());
    if (!watched->objectName().trimmed().isEmpty()) {
        parts << QStringLiteral("object=%1").arg(watched->objectName().trimmed());
    }
    if (auto* widget = qobject_cast<QWidget*>(watched)) {
        if (!widget->windowTitle().trimmed().isEmpty()) {
            parts << QStringLiteral("window=%1").arg(widget->windowTitle().trimmed());
        }
    }
    return parts.join(QStringLiteral(" | "));
}

ClickSoundFilter::ClickSoundFilter(QObject* parent)
    : QObject(parent)
{
    reload();
}

void ClickSoundFilter::reload()
{
    ClickSoundBackend::reloadEffects();
    ClickSoundBackend::preloadStartupSoundAssets();
}

void ClickSoundFilter::playGlobalClick()
{
    static qint64 s_lastPlayMs = 0;
    const qint64 ts = ClickSoundBackend::nowMs();
    if (ts - s_lastPlayMs < 60) {
        return;
    }
    s_lastPlayMs = ts;

    const bool enabled = ClickSoundBackend::clickSoundsEnabled();
    if (!enabled) {
        return;
    }

    const int volumePct = ClickSoundBackend::clickSoundsVolumePct();
    bool played = ClickSoundBackend::playClick(volumePct);


    if (!played) {
        QApplication::beep();
        qWarning() << "All click sound backends failed; fallback beep used";
    }
}

void ClickSoundFilter::playGlobalTableClick()
{
    static qint64 s_lastPlayMs = 0;
    const qint64 ts = ClickSoundBackend::nowMs();
    if (ts - s_lastPlayMs < 45) {
        return;
    }
    s_lastPlayMs = ts;

    const bool enabled = ClickSoundBackend::clickSoundsEnabled();
    if (!enabled) {
        return;
    }

    const int volumePct = ClickSoundBackend::clickSoundsVolumePct();
    bool played = ClickSoundBackend::playTableClick(volumePct);


    if (!played) {
        playGlobalClick();
    }
}

void ClickSoundFilter::playGlobalMinimize()
{
    static qint64 s_lastPlayMs = 0;
    const qint64 ts = ClickSoundBackend::nowMs();
    if (ts - s_lastPlayMs < 180) {
        return;
    }
    s_lastPlayMs = ts;

    if (!ClickSoundBackend::clickSoundsEnabled()) {
        return;
    }

    const int volumePct = ClickSoundBackend::clickSoundsVolumePct();
    bool played = ClickSoundBackend::playMinimize(volumePct);


    if (!played) {
        playGlobalClick();
    }
}

void ClickSoundFilter::playGlobalRestore()
{
    static qint64 s_lastPlayMs = 0;
    const qint64 ts = ClickSoundBackend::nowMs();
    if (ts - s_lastPlayMs < 180) {
        return;
    }
    s_lastPlayMs = ts;

    if (!ClickSoundBackend::clickSoundsEnabled()) {
        return;
    }

    const int volumePct = ClickSoundBackend::clickSoundsVolumePct();
    bool played = ClickSoundBackend::playRestore(volumePct);


    if (!played) {
        playGlobalClick();
    }
}

void ClickSoundFilter::playGlobalStartup()
{
    static qint64 s_lastPlayMs = 0;
    const qint64 ts = ClickSoundBackend::nowMs();
    if (ts - s_lastPlayMs < 500) {
        return;
    }
    s_lastPlayMs = ts;

    if (!ClickSoundBackend::clickSoundsEnabled()) {
        return;
    }

    const int volumePct = ClickSoundBackend::clickSoundsVolumePct();
    bool played = ClickSoundBackend::playStartup(volumePct);


    if (!played) {
        playGlobalClick();
    }
}

void ClickSoundFilter::playGlobalClose()
{
    static qint64 s_lastPlayMs = 0;
    const qint64 ts = ClickSoundBackend::nowMs();
    if (ts - s_lastPlayMs < 500) {
        return;
    }
    s_lastPlayMs = ts;

    if (!ClickSoundBackend::clickSoundsEnabled()) {
        return;
    }

    const int volumePct = ClickSoundBackend::clickSoundsVolumePct();
    bool played = ClickSoundBackend::playClose(volumePct);


    if (!played) {
        playGlobalClick();
    }
}

bool ClickSoundFilter::shouldPlayFor(QObject* watched, QEvent* event) const
{
    auto* widget = qobject_cast<QWidget*>(watched);
    if (!widget || !widget->isEnabled() || !widget->isVisible()) {
        return false;
    }

    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
        break;
    default:
        return false;
    }

    const auto* mouseEvent = static_cast<QMouseEvent*>(event);
    const QPoint globalPos = mouseEvent ? mouseEvent->globalPosition().toPoint()
                                        : widget->mapToGlobal(widget->rect().center());

    for (QWidget* current = widget; current; current = current->parentWidget()) {
        if (!current->isEnabled() || !current->isVisible()) {
            continue;
        }

        if (qobject_cast<QAbstractButton*>(current)) {
            return true;
        }

        if (auto* tabBar = qobject_cast<QTabBar*>(current)) {
            const QPoint localPos = tabBar->mapFromGlobal(globalPos);
            return tabBar->tabAt(localPos) >= 0;
        }

        if (qobject_cast<QComboBox*>(current) ||
            qobject_cast<QAbstractSpinBox*>(current) ||
            qobject_cast<QAbstractSlider*>(current) ||
            qobject_cast<QScrollBar*>(current) ||
            qobject_cast<QLineEdit*>(current) ||
            qobject_cast<QTextEdit*>(current) ||
            qobject_cast<QPlainTextEdit*>(current) ||
            qobject_cast<QMenu*>(current)) {
            return true;
        }

        if (auto* itemView = qobject_cast<QAbstractItemView*>(current)) {
            if (qobject_cast<QTableView*>(current)) {
                return false;
            }
            QWidget* viewport = itemView->viewport();
            const QPoint localPos = viewport ? viewport->mapFromGlobal(globalPos)
                                             : itemView->mapFromGlobal(globalPos);
            return itemView->indexAt(localPos).isValid();
        }

        if (current->property("cs_clickable").toBool() ||
            current->property("clickable").toBool() ||
            current->cursor().shape() == Qt::PointingHandCursor) {
            return true;
        }
    }

    return false;
}

bool ClickSoundFilter::shouldPlayTableSoundFor(QObject* watched, QEvent* event) const
{
    auto* widget = qobject_cast<QWidget*>(watched);
    if (!widget || !widget->isEnabled() || !widget->isVisible()) {
        return false;
    }

    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
        break;
    default:
        return false;
    }

    return ClickSoundBackend::isInsideTableView(widget);
}

bool ClickSoundFilter::shouldPlayMinimizeSoundFor(QObject* watched, QEvent* event) const
{
    auto* widget = qobject_cast<QWidget*>(watched);
    if (!widget || !widget->isWindow()) {
        return false;
    }

    if (event->type() != QEvent::WindowStateChange) {
        return false;
    }

    auto* ws = static_cast<QWindowStateChangeEvent*>(event);
    const Qt::WindowStates oldState = ws->oldState();
    const Qt::WindowStates newState = widget->windowState();
    const bool nowMinimized = newState.testFlag(Qt::WindowMinimized);
    const bool wasMinimized = oldState.testFlag(Qt::WindowMinimized);
    return nowMinimized && !wasMinimized;
}

bool ClickSoundFilter::shouldPlayRestoreSoundFor(QObject* watched, QEvent* event) const
{
    auto* widget = qobject_cast<QWidget*>(watched);
    if (!widget || !widget->isWindow()) {
        return false;
    }

    if (event->type() != QEvent::WindowStateChange) {
        return false;
    }

    auto* ws = static_cast<QWindowStateChangeEvent*>(event);
    const Qt::WindowStates oldState = ws->oldState();
    const Qt::WindowStates newState = widget->windowState();
    const bool wasMinimized = oldState.testFlag(Qt::WindowMinimized);
    const bool nowMinimized = newState.testFlag(Qt::WindowMinimized);
    return wasMinimized && !nowMinimized;
}

bool ClickSoundFilter::eventFilter(QObject* watched, QEvent* event)
{
    if (deepClickDiagnosticsEnabled()
        && event
        && (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick)) {
        const auto* mouseEvent = static_cast<QMouseEvent*>(event);
        const QPoint globalPos = mouseEvent->globalPosition().toPoint();
        QString localPosText = QStringLiteral("(-1,-1)");
        if (auto* widget = qobject_cast<QWidget*>(watched)) {
            const QPoint localPos = widget->mapFromGlobal(globalPos);
            localPosText = QStringLiteral("(%1,%2)").arg(localPos.x()).arg(localPos.y());
        }
        Logger::info(QStringLiteral("MOUSE_CLICK [global] type=%1 button=%2 buttons=%3 global=(%4,%5) local=%6 target=%7")
                         .arg(int(event->type()))
                         .arg(int(mouseEvent->button()))
                         .arg(int(mouseEvent->buttons()))
                         .arg(globalPos.x())
                         .arg(globalPos.y())
                         .arg(localPosText)
                         .arg(clickLogTargetName(watched)));
    }

    if (shouldPlayMinimizeSoundFor(watched, event)) {
        playGlobalMinimize();
    } else if (shouldPlayRestoreSoundFor(watched, event)) {
        playGlobalRestore();
    } else if (shouldPlayTableSoundFor(watched, event)) {
        playGlobalTableClick();
    } else if (shouldPlayFor(watched, event)) {
        playGlobalClick();
    }
    return QObject::eventFilter(watched, event);
}
