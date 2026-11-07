#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>

class QWidget;

// Application-wide idle monitor. Installs an event filter on QApplication,
// tracks user activity (mouse, keyboard, wheel, touch) and shows the
// LockScreenWindow when:
//   • password protection is enabled (PasswordManager::isPasswordEnabled())
//   • a password is set (PasswordManager::hasPassword())
//   • auto-lock is enabled (PasswordManager::isAutoLockEnabled())
//   • inactivity duration exceeds PasswordManager::autoLockMinutes()
//
// Recommended lifetime: created once in main(), kept alive for the whole
// application lifetime, with start() called after the main window is shown.
class IdleLockMonitor : public QObject
{
    Q_OBJECT
public:
    explicit IdleLockMonitor(QObject* parent = nullptr);

    // Install the event filter on qApp, mark "now" as last activity, start the
    // periodic idle check.
    void start(QWidget* parent = nullptr);
    void stop();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void check();

private:
    QTimer        m_tick;
    QElapsedTimer m_lastActivity;
    bool          m_locking = false;
    QPointer<QWidget> m_lockParent;
};
