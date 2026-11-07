#include "app/App.h"
#include "app/AcceptanceProfile.h"
#include "ui/common/dialogs/OrderClosureFlow.h"
#include "AppVersion.h"
#include "app/AppContext.h"
#include "app/ClickSoundFilter.h"
#include "app/StartupPipeline.h"
#include "core/diagnostics/RuntimeLoggingControl.h"
#include "app/IdleMaintenanceScheduler.h"
#include "app/SingleInstanceCoordinator.h"
#include "infrastructure/db/sqlite/Db.h"
#include "ui/splash/SplashScreen.h"
#include "ui/main/MainWindow.h"
#include "ui/tabs/settings/LockScreenWindow.h"
#include "ui/tabs/settings/IdleLockMonitor.h"
#include "ui/tabs/settings/PasswordManager.h"
#include "ui/tabs/settings/StaffSessionDialog.h"
#include "core/services/StaffAccountService.h"
#include "core/services/AccessControlService.h"
#include "ui/common/filters/WindowsContextMenuFilter.h"
#include "ui/common/filters/ManualInputCorrectionFilter.h"
#include "ui/theme/ThemeManager.h"
#include "ui/common/dialogs/MessageBoxUtils.h"
#include "ui/tabs/settings/RecoveryManagerDialog.h"
#include "core/backup/BackupLogging.h"
#include "core/backup/BackupManager.h"
#include "core/backup/BackupPerformanceGuard.h"
#include "core/backup/CrashRecoveryManager.h"
#include "core/backup/RecoveryCoordinator.h"
#include "core/backup/BackupDtos.h"

#include <QAbstractButton>
#include <QPushButton>

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QIcon>
#include <QMenu>
#include <QPointer>
#include <QSystemTrayIcon>
#include <QGuiApplication>
#include <QTimer>
#include <QScreen>
#include <QMessageBox>
#include <QStatusBar>
#include <QTranslator>
#include <QLocale>

#include <exception>
#include <cstdlib>


class MessageBoxPolishFilter : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (auto* box = qobject_cast<QMessageBox*>(watched)) {
            if (event->type() == QEvent::Show) {
                applyStandardMessageBoxStyle(box);
                playStandardMessageBoxConfirmationShowSound(box);
                installStandardMessageBoxConfirmationYesSoundHook(box);
                installStandardMessageBoxConfirmationNoSoundHook(box);
                installStandardMessageBoxSettingsActionSoundHook(box);
                playStandardMessageBoxAlertSound(box);
            } else if (event->type() == QEvent::Hide || event->type() == QEvent::Close) {
                playStandardMessageBoxCloseSound(box);
            }
        }
        return QObject::eventFilter(watched, event);
    }
};

static void ensureOnScreen(QWidget& w)
{
    // Make sure the window is not off-screen (multi-monitor / DPI edge cases).
    const QRect win = w.frameGeometry();
    QScreen* sc = QGuiApplication::primaryScreen();
    if (!sc) return;
    const QRect screen = sc->availableGeometry();
    if (!screen.intersects(win)) {
        w.resize(1200, 760);
        w.move(screen.center() - QPoint(w.width() / 2, w.height() / 2));
    }
}

int main(int argc, char *argv[])
{
    try {
        RuntimeLoggingControl::configureEnvironmentForRuntime();
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QLocale::setDefault(QLocale(QLocale::Ukrainian, QLocale::Ukraine));
        QApplication app(argc, argv);
        const bool isolatedAcceptance = app.arguments().contains(QStringLiteral("--isolated-acceptance"));
        const bool checkIsolatedStartup = app.arguments().contains(QStringLiteral("--check-isolated-startup"));
        if (checkIsolatedStartup && !isolatedAcceptance) {
            qCritical() << "Startup check requires --isolated-acceptance";
            return 2;
        }
        QString acceptanceRoot;
        QString acceptanceError;
        if (isolatedAcceptance) {
            acceptanceRoot = AcceptanceProfile::create(&acceptanceError);
            if (acceptanceRoot.isEmpty()) { qCritical().noquote() << acceptanceError; return 2; }
            qInfo().noquote() << "Acceptance profile:" << acceptanceRoot;
        }
        Q_INIT_RESOURCE(resources);
        QTranslator ukrainianDialogs;
        if (ukrainianDialogs.load(QStringLiteral(":/translations/qtbase_uk.qm"))) {
            app.installTranslator(&ukrainianDialogs);
        } else {
            qWarning() << "Ukrainian Qt dialog translation could not be loaded";
        }
        MessageBoxPolishFilter messageBoxPolishFilter;
        app.installEventFilter(&messageBoxPolishFilter);
        CarService::SingleInstanceCoordinator singleInstance;
        QString singleInstanceError;
        switch (singleInstance.acquireOrActivate(&singleInstanceError)) {
        case CarService::SingleInstanceCoordinator::StartResult::SecondaryInstanceActivated:
            return 0;
        case CarService::SingleInstanceCoordinator::StartResult::Unavailable:
            qCritical() << "Single-instance startup failed:" << singleInstanceError;
            QMessageBox::critical(nullptr,
                                  QString::fromUtf8("Не вдалося запустити застосунок"),
                                  QString::fromUtf8("Не вдалося перевірити вже відкритий екземпляр програми.\n\n%1")
                                      .arg(singleInstanceError));
            return 1;
        case CarService::SingleInstanceCoordinator::StartResult::PrimaryInstance:
            break;
        }
        // resources.qrc is built through CarServiceLib; explicit registration
        // keeps qrc assets visible from the executable after static linking.
        app.setWindowIcon(QIcon(QStringLiteral(":/assets/app_icon.png")));
        // Keep the process alive while the splash screen hands off to the hidden
        // MainWindow. QApplication defaults quitOnLastWindowClosed to true; if
        // the splash hides before MainWindow.showMaximized(), the event loop can
        // quit cleanly with no crash log and the app looks like it never opened.
        // It is switched back to true immediately after MainWindow is shown.
        app.setQuitOnLastWindowClosed(false);
        app.setApplicationVersion(CarService::AppVersion::versionString());
        RuntimeLoggingControl::configureForRuntime();

        SplashScreen splash;
        // Paint a visible ring before synchronous startup. Blocked UI time must
        // not consume the animation or allow an early handoff.
        splash.start(6000);
        splash.repaint();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

        const StartupPipeline::Result startupResult = StartupPipeline::runCoreStartup(app, isolatedAcceptance);
        if (!startupResult.ok) {
            qCritical() << "Startup failed at phase" << StartupPipeline::phaseKey(startupResult.failedPhase)
                        << ':' << startupResult.errorText;
            return 1;
        }

        if (isolatedAcceptance && !AcceptanceProfile::configureServices(acceptanceRoot,&acceptanceError)) {
            qCritical().noquote() << acceptanceError; return 2;
        }
        qInfo() << "Startup core completed";
        AppContext::instance().initializeRuntimeServices();
        if (!AppContext::instance().runtimeServicesHealthy()) {
            qCritical() << "Runtime services degraded startup:" << AppContext::instance().runtimeServicesError();
        }

        QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
            StaffAccountService::closeCurrentSession(Database::db());
            try {
                AppContext::instance().shutdownRuntimeServices();
            } catch (...) {
                qCritical() << "aboutToQuit: shutdownRuntimeServices threw";
            }
            ClickSoundFilter::playGlobalClose();
            // Do not force-remove the main SQLite connection here.
            // At this stage Qt widgets/services may still own live SQL handles,
            // and removing the connection early leads to:
            // QSqlDatabasePrivate::removeDatabase: connection 'carservice_main' is still in use
            // which can destabilize shutdown and present as a crash on exit.
            qInfo() << "App aboutToQuit";
        });

        StartupPipeline::beginPhase(StartupPipeline::Phase::UiInit);

        ThemeManager::applySavedTheme();

        ClickSoundFilter clickSoundFilter;
        app.installEventFilter(&clickSoundFilter);

        WindowsContextMenuFilter windowsContextMenuFilter;
        app.installEventFilter(&windowsContextMenuFilter);
        ManualInputCorrectionFilter manualInputCorrection;
        app.installEventFilter(&manualInputCorrection);

        MainWindow w;
        QPointer<StaffSessionDialog> activeStaffSession;
        QSystemTrayIcon trayIcon(QIcon(QStringLiteral(":/assets/app_icon.png")), &app);
        trayIcon.setToolTip(QString::fromUtf8("CarService PointG — застосунок запущено"));
        QMenu trayMenu;
        QAction* showApplicationAction = trayMenu.addAction(QString::fromUtf8("Показати застосунок"));
        QAction* quitApplicationAction = trayMenu.addAction(QString::fromUtf8("Завершити роботу"));
        trayIcon.setContextMenu(&trayMenu);
        if (QSystemTrayIcon::isSystemTrayAvailable()) {
            trayIcon.show();
        }
        const auto raiseFromTray = [&w, &activeStaffSession]() {
            QWidget* target = activeStaffSession
                ? static_cast<QWidget*>(activeStaffSession.data())
                : static_cast<QWidget*>(&w);
            if (!target) return;
            target->setWindowState((target->windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
            target->show();
            target->raise();
            target->activateWindow();
        };
        QObject::connect(showApplicationAction, &QAction::triggered, &app, raiseFromTray);
        QObject::connect(&trayIcon, &QSystemTrayIcon::activated, &app,
                         [&raiseFromTray](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) raiseFromTray();
        });
        QObject::connect(quitApplicationAction, &QAction::triggered, &app, &QCoreApplication::quit);
        QObject::connect(&app, &QCoreApplication::aboutToQuit, &trayIcon, [&trayIcon]() { trayIcon.hide(); });

        if (auto* backup = AppContext::instance().backupManager()) {
            QObject::connect(backup, &BackupManager::userNotificationRequested, &w, [&w](const QString& message, int timeoutMs) {
                if (auto* sb = w.statusBar()) {
                    sb->showMessage(message, qMax(1500, timeoutMs));
                }
            });
        }
        IdleMaintenanceScheduler idleMaintenance(&w);
        ensureOnScreen(w);

        StartupPipeline::succeedPhase(StartupPipeline::Phase::UiInit);

        // Open in the native maximized state so Windows keeps full control over
        // maximize/restore buttons, taskbar bounds and caption repainting.
        auto notifyBackupUi = [&w](const QString& message, int timeoutMs = 5000) {
            bool showBackupNotifications = true;
            try {
                showBackupNotifications = Database::getSetting(QStringLiteral("settings.int.autobackup.show_backup_notifications"), true).toBool();
            } catch (const std::exception& e) {
                qCritical() << "Failed to read backup notification setting:" << e.what();
                showBackupNotifications = false;
            } catch (...) {
                qCritical() << "Failed to read backup notification setting";
                showBackupNotifications = false;
            }
            if (!showBackupNotifications) {
                return;
            }
            if (auto* sb = w.statusBar()) {
                sb->showMessage(message, qMax(1500, timeoutMs));
            }
        };
        bool recoveryPromptShown = false;
        bool unlocked = false;
        bool staffSessionAuthenticated = false;
        bool mainShowInProgress = false;
        IdleLockMonitor idleLock;
        auto showMain = [&]() {
            if (w.isVisible() || mainShowInProgress) return;
            if (splash.isVisible()) {
                splash.raise();
                return;
            }
            mainShowInProgress = true;
            // ── Lock-screen gate ─────────────────────────────────────────────
            // If a startup password is configured, show a full-screen lock
            // window and block the main UI until the user enters the correct
            // password (or resets it after MAX_ATTEMPTS failed attempts).
            if (!unlocked) {
                if (PasswordManager::isPasswordEnabled() && PasswordManager::hasPassword()) {
                    LockScreenWindow lock;
                    if (lock.exec() != QDialog::Accepted) {
                        // User could not unlock and chose to quit — exit cleanly
                        mainShowInProgress = false;
                        QApplication::quit();
                        return;
                    }
                }
                unlocked = true;
            }

            if (!staffSessionAuthenticated) {
                QString sessionSchemaError;
                if (!StaffAccountService::ensureSchema(Database::db(), &sessionSchemaError)) {
                    QMessageBox::critical(nullptr,
                                          QString::fromUtf8("Вхід працівника"),
                                          QString::fromUtf8("Не вдалося підготувати персональні облікові записи.\n\n%1")
                                              .arg(sessionSchemaError));
                    mainShowInProgress = false;
                    QApplication::quit();
                    return;
                }
                if (!AccessControlService::ensureSchema(Database::db(), &sessionSchemaError)) {
                    QMessageBox::critical(nullptr,
                                          QString::fromUtf8("Права доступу"),
                                          QString::fromUtf8("Не вдалося підготувати ролі та права доступу.\n\n%1")
                                              .arg(sessionSchemaError));
                    mainShowInProgress = false;
                    QApplication::quit();
                    return;
                }
                StaffSessionDialog staffSession;
                activeStaffSession = &staffSession;
                trayIcon.setToolTip(QString::fromUtf8("CarService PointG — очікується вхід працівника"));
                const int staffSessionResult = staffSession.exec();
                activeStaffSession.clear();
                if (staffSessionResult != QDialog::Accepted) {
                    mainShowInProgress = false;
                    QApplication::quit();
                    return;
                }
                staffSessionAuthenticated = true;
                trayIcon.setToolTip(QString::fromUtf8("CarService PointG — застосунок запущено"));
                w.refreshAccessControl();
            }

            BackupPerformanceGuard::hold(AppContext::instance().backupManager(), 3500);
            if (!w.isVisible()) {
                qInfo() << "Showing MainWindow";
                w.showMaximized();
                QTimer::singleShot(1500, &w, [&w]() { OrderClosureFlow::recoverPending(&w); });
                ClickSoundFilter::playGlobalStartup();
                QTimer::singleShot(0, &w, [&w]() {
                    w.raise();
                    w.activateWindow();
                });
                QTimer::singleShot(120, &w, [&w]() {
                    if (!w.isMaximized() && !w.isFullScreen()) {
                        w.lockToAvailableScreenGeometry();
                    }
                });
                qApp->setQuitOnLastWindowClosed(true);
                // Start the inactivity-driven lock monitor only after the main
                // window is visible so that input during startup doesn't count
                // and the lock can use MainWindow as parent.
                idleLock.start(&w);
                QTimer::singleShot(2500, &w, [&w]() {
                    w.beginSplashPrewarm();
                });
            }

            if (!recoveryPromptShown) {
                recoveryPromptShown = true;
                QTimer::singleShot(250, &w, [&w, &notifyBackupUi]() {
                    auto& ctx = AppContext::instance();
                    auto* crash = ctx.crashRecoveryManager();
                    auto* recovery = ctx.recoveryCoordinator();
                    if (!crash || !recovery) return;
                    bool showDialog = true;
                    try {
                        showDialog = Database::getSetting(QStringLiteral("settings.int.autobackup.recovery_show_startup_dialog"), true).toBool();
                    } catch (const std::exception& e) {
                        qCritical() << "Failed to read recovery dialog setting:" << e.what();
                        showDialog = false;
                    } catch (...) {
                        qCritical() << "Failed to read recovery dialog setting";
                        showDialog = false;
                    }
                    if (!showDialog || !crash->hasRecoverableSession()) return;

                    BackupLogging::recoveryInfo(QStringLiteral("recovery prompt shown"));
                    notifyBackupUi(QString::fromUtf8("Знайдено незбережені дані попередньої сесії"), 6500);

                    const RecoverySessionState sessionState = crash->recoverableSession();
                    const int draftCount = crash->recoverableDrafts().size();
                    const QString lastAutosaveText = sessionState.lastDraftWrite.isValid()
                            ? sessionState.lastDraftWrite.toString(QStringLiteral("dd.MM.yyyy HH:mm:ss"))
                            : QString::fromUtf8("—");
                    QString promptText = QString::fromUtf8(
                                             "Виявлено аварійно завершену сесію з незбереженими даними.\n\n"
                                             "Сесія: %1\n"
                                             "Чернеток: %2\n"
                                             "Останній autosave: %3")
                                             .arg(sessionState.sessionId)
                                             .arg(QString::number(draftCount))
                                             .arg(lastAutosaveText);

                    QMessageBox box(&w);
                    box.setIcon(QMessageBox::Question);
                    box.setWindowTitle(QString::fromUtf8("Відновлення незбережених даних"));
                    box.setText(promptText);
                    box.setStandardButtons(QMessageBox::NoButton);
                    QPushButton* restoreBtn = box.addButton(QString::fromUtf8("Відновити"), QMessageBox::AcceptRole);
                    QPushButton* detailsBtn = box.addButton(QString::fromUtf8("Відкрити деталі"), QMessageBox::ActionRole);
                    QPushButton* skipBtn = box.addButton(QString::fromUtf8("Пропустити"), QMessageBox::RejectRole);
                    QPushButton* deleteBtn = box.addButton(QString::fromUtf8("Видалити незбережені дані"), QMessageBox::DestructiveRole);
                    Q_UNUSED(skipBtn);
                    applyStandardMessageBoxStyle(&box);
                    box.exec();
                    QAbstractButton* clicked = box.clickedButton();
                    if (clicked == restoreBtn) {
                        BackupLogging::recoveryInfo(QStringLiteral("recovery accepted from startup prompt"));
                        notifyBackupUi(QString::fromUtf8("Почато відновлення незбережених даних"), 5000);
                        const RecoveryRestoreResult result = recovery->restoreRecoverableDrafts(&w);
                        QString text = QString::fromUtf8("Відновлено: %1\nПропущено: %2\nКонфліктів: %3")
                                           .arg(result.restoredCount)
                                           .arg(result.skippedCount)
                                           .arg(result.conflictCount);
                        if (!result.warnings.isEmpty()) {
                            text += QString::fromUtf8("\n\nПопередження:\n- ") + result.warnings.join(QString::fromUtf8("\n- "));
                        }
                        if (!result.errors.isEmpty()) {
                            text += QString::fromUtf8("\n\nПомилки:\n- ") + result.errors.join(QString::fromUtf8("\n- "));
                        }
                        notifyBackupUi(QString::fromUtf8("Recovery: відновлено %1, пропущено %2, конфліктів %3")
                                           .arg(result.restoredCount)
                                           .arg(result.skippedCount)
                                           .arg(result.conflictCount),
                                       7000);
                        if (result.conflictCount > 0 || !result.warnings.isEmpty() || !result.errors.isEmpty() || result.restoredCount > 0 || result.skippedCount > 0) {
                            QMessageBox info(&w);
                            info.setIcon(QMessageBox::Information);
                            info.setWindowTitle(QString::fromUtf8("Recovery"));
                            info.setText(text);
                            info.setStandardButtons(QMessageBox::Ok);
                            applyStandardMessageBoxStyle(&info);
                            info.exec();
                        }
                    } else if (clicked == detailsBtn) {
                        BackupLogging::recoveryInfo(QStringLiteral("recovery details opened from startup prompt"));
                        notifyBackupUi(QString::fromUtf8("Відкрито менеджер recovery-чернеток"), 5000);
                        RecoveryManagerDialog dlg(crash, recovery, &w);
                        dlg.exec();
                    } else if (clicked == deleteBtn) {
                        BackupLogging::recoveryInfo(QStringLiteral("recovery drafts deleted from startup prompt"));
                        QString errorText;
                        recovery->deleteRecoverableSession(&errorText);
                        notifyBackupUi(errorText.trimmed().isEmpty()
                                           ? QString::fromUtf8("Незбережені дані видалено")
                                           : QString::fromUtf8("Помилка видалення recovery-даних"),
                                       6000);
                    } else {
                        BackupLogging::recoveryInfo(QStringLiteral("recovery skipped from startup prompt"));
                        notifyBackupUi(QString::fromUtf8("Recovery відкладено користувачем"), 5000);
                    }
                });
            }
            mainShowInProgress = false;
        };

        QObject::connect(&singleInstance,
                         &CarService::SingleInstanceCoordinator::activationRequested,
                         &w,
                         [&w, &showMain]() {
            if (!w.isVisible()) {
                showMain();
                return;
            }

            const Qt::WindowStates restoredState =
                (w.windowState() & ~Qt::WindowMinimized) | Qt::WindowActive;
            w.setWindowState(restoredState);
            w.show();
            QTimer::singleShot(0, &w, [&w]() {
                w.raise();
                w.activateWindow();
            });
        });

        QObject::connect(&splash, &SplashScreen::finished, &w, showMain);

        // Disabled for now: worker-thread startup DB maintenance is causing
        // thread-affinity warnings and instability in interactive flows.
        Q_UNUSED(idleMaintenance);

        // Arm completion only after its receiver and authentication gate exist.
        // A wall-clock fallback would bypass the remaining visible animation.
        splash.setStartupReady();

        if (checkIsolatedStartup)
            QTimer::singleShot(0, &app, &QCoreApplication::quit);
        return app.exec();
    } catch (const std::exception& e) {
        qCritical() << "Fatal std::exception during phase" << StartupPipeline::currentPhaseKey() << ':' << e.what();
        return 1;
    } catch (...) {
        qCritical() << "Fatal unknown exception during phase" << StartupPipeline::currentPhaseKey();
        return 1;
    }
}
