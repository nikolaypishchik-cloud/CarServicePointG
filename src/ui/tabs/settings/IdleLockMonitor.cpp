#include "ui/tabs/settings/IdleLockMonitor.h"
#include "ui/tabs/settings/LockScreenWindow.h"
#include "ui/tabs/settings/PasswordManager.h"

#include <QApplication>
#include <QEvent>

IdleLockMonitor::IdleLockMonitor(QObject* parent)
    : QObject(parent)
{
    m_tick.setInterval(10000);   // check every 10 seconds — coarse is fine for minutes-scale
    m_tick.setSingleShot(false);
    connect(&m_tick, &QTimer::timeout, this, &IdleLockMonitor::check);
}

void IdleLockMonitor::start(QWidget* parent)
{
    m_lockParent = parent;
    m_lastActivity.start();
    if (qApp) qApp->installEventFilter(this);
    m_tick.start();
}

void IdleLockMonitor::stop()
{
    m_tick.stop();
    if (qApp) qApp->removeEventFilter(this);
}

bool IdleLockMonitor::eventFilter(QObject* /*watched*/, QEvent* event)
{
    // While the lock screen is up, ignore events for activity tracking
    if (m_locking) return false;

    switch (event->type()) {
    case QEvent::MouseMove:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
    case QEvent::Wheel:
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TabletPress:
        m_lastActivity.restart();
        break;
    default:
        break;
    }
    return false;
}

void IdleLockMonitor::check()
{
    if (m_locking) return;
    if (!PasswordManager::isAutoLockEnabled()) return;
    if (!PasswordManager::isPasswordEnabled()) return;
    if (!PasswordManager::hasPassword()) return;

    const int minutes = PasswordManager::autoLockMinutes();
    if (minutes <= 0) return;
    const qint64 idleMs = m_lastActivity.elapsed();
    const qint64 thresholdMs = qint64(minutes) * 60LL * 1000LL;
    if (idleMs < thresholdMs) return;

    m_locking = true;
    {
        LockScreenWindow lock(m_lockParent ? m_lockParent.data() : nullptr);
        lock.exec();
    }
    m_locking = false;
    m_lastActivity.restart();
}
