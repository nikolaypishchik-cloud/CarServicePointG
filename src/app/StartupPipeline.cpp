#include "app/StartupPipeline.h"

#include "app/App.h"
#include "app/Settings.h"
#include "core/services/startup/StartupDatabaseFacade.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QDebug>
#include <QVariant>

#include <cstdio>
#include <functional>

namespace {

QStringList& pendingStartupLines()
{
    static QStringList lines;
    return lines;
}

bool& loggerReady()
{
    static bool ready = false;
    return ready;
}

StartupPipeline::Phase& activePhase()
{
    static StartupPipeline::Phase phase = StartupPipeline::Phase::None;
    return phase;
}

void emitStartupLine(const QString& line, bool forceStdErr = false)
{
    const QByteArray bytes = (line + QLatin1Char('\n')).toUtf8();
    FILE* stream = forceStdErr ? stderr : stdout;

    if (loggerReady()) {
        qInfo().noquote() << line;
        return;
    }

    pendingStartupLines().push_back(line);
    std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stream);
    std::fflush(stream);
}

void flushPendingStartupLines()
{
    loggerReady() = true;
    const QStringList buffered = pendingStartupLines();
    pendingStartupLines().clear();
    for (const QString& line : buffered) {
        qInfo().noquote() << line;
    }
}

QString timestampUtc()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QString startupPrefix(const QString& phaseKey, const QString& state)
{
    return QStringLiteral("STARTUP [%1] [%2] %3")
        .arg(timestampUtc(), phaseKey, state);
}

bool runPhase(StartupPipeline::Phase phase,
              const std::function<bool(QString*)>& action,
              StartupPipeline::Result* result)
{
    StartupPipeline::beginPhase(phase);
    QString errorText;
    if (!action(&errorText)) {
        if (errorText.trimmed().isEmpty()) {
            errorText = QStringLiteral("Unknown startup failure");
        }
        StartupPipeline::failPhase(phase, errorText);
        if (result) {
            result->ok = false;
            result->failedPhase = phase;
            result->errorText = errorText;
        }
        return false;
    }
    StartupPipeline::succeedPhase(phase);
    return true;
}

} // namespace

const QList<StartupPipeline::PhaseContract>& StartupPipeline::contracts()
{
    static const QList<PhaseContract> kContracts = {
        { Phase::Bootstrap,
          QStringLiteral("bootstrap"),
          QStringLiteral("Створений QApplication, ще немає активного startup state."),
          QStringLiteral("Встановлено app/org name, вимкнено quit-on-last-window-closed до завершення startup."),
          QStringLiteral("Лише базова ініціалізація процесу та application identity."),
          QStringLiteral("BEGIN -> OK/FAIL."),
          QStringLiteral("Негайно зупинити startup; не переходити до environment/db/ui.") },
        { Phase::EnvironmentInit,
          QStringLiteral("environment_init"),
          QStringLiteral("Відомі app paths, process context уже створений."),
          QStringLiteral("Підготовлені data/logs/security/runtime каталоги та best-effort at-rest protection."),
          QStringLiteral("Створення каталогів та безпечної файлової основи, без відкриття БД."),
          QStringLiteral("BEGIN -> OK/FAIL."),
          QStringLiteral("Зупинити startup; не відкривати БД без валідного environment.") },
        { Phase::SettingsInit,
          QStringLiteral("settings_init"),
          QStringLiteral("QSettings уже адресуються через актуальні app/org names."),
          QStringLiteral("Перевірено доступ до ключових settings без важких мутацій."),
          QStringLiteral("Тільки читання/валідація startup-настроювань, без UI та без БД side effects."),
          QStringLiteral("BEGIN -> OK/FAIL."),
          QStringLiteral("Зупинити startup; не переходити до logger/db/ui із невизначеним settings context.") },
        { Phase::LoggerInit,
          QStringLiteral("logger_init"),
          QStringLiteral("Є стабільний filesystem environment та app identity."),
          QStringLiteral("Активний app.log handler, buffered startup events перенесені в лог."),
          QStringLiteral("Лише ініціалізація логування й захисту log paths."),
          QStringLiteral("BEGIN -> OK/FAIL; після OK усі подальші події йдуть у прозорий startup-log."),
          QStringLiteral("Зупинити startup; не продовжувати без діагностованого логу.") },
        { Phase::DbPreparation,
          QStringLiteral("db_preparation"),
          QStringLiteral("Logger активний; filesystem і secure storage уже готові."),
          QStringLiteral("Runtime DB відновлена/підготовлена; відкрите лише plain DB connection без startup-maintenance побічних мутацій."),
          QStringLiteral("Crash-recovery runtime, openPrepared, PRAGMA-конфігурація connection."),
          QStringLiteral("BEGIN -> OK/FAIL."),
          QStringLiteral("Зупинити startup; не переходити до schema/migration без відкритої БД.") },
        { Phase::SchemaCheck,
          QStringLiteral("schema_check"),
          QStringLiteral("Відкрита active DB connection без тяжких startup side effects."),
          QStringLiteral("Базова схема, колонки, індекси, FTS і локальні schema-залежності приведені до очікуваного стану."),
          QStringLiteral("Структурні CREATE/ALTER/INDEX дії без archive rollover і без routing refresh."),
          QStringLiteral("BEGIN -> OK/FAIL."),
          QStringLiteral("Зупинити startup; не виконувати міграції поверх невідомої схеми.") },
        { Phase::MigrationCheck,
          QStringLiteral("migration_check"),
          QStringLiteral("Базова схема вже існує й придатна до роботи."),
          QStringLiteral("Виконані canonical data/schema migrations для legacy-case; після цього schema повторно звірена."),
          QStringLiteral("Лише explicit migration path; без archive rollover і без registry refresh."),
          QStringLiteral("BEGIN -> OK/FAIL."),
          QStringLiteral("Зупинити startup; не переходити до maintenance із partially migrated DB.") },
        { Phase::ArchiveRolloverMaintenance,
          QStringLiteral("archive_rollover_maintenance"),
          QStringLiteral("Схема і міграції вже стабільні."),
          QStringLiteral("Закриті замовлення перенесені в archive shards контрольованим maintenance-кроком."),
          QStringLiteral("Лише explicit archive/rollover maintenance."),
          QStringLiteral("BEGIN -> OK/FAIL."),
          QStringLiteral("Зупинити startup; не оновлювати registry/routing, якщо rollover не завершився коректно.") },
        { Phase::RegistryRoutingRefresh,
          QStringLiteral("registry_routing_refresh"),
          QStringLiteral("Active/archive state уже стабілізований після schema+migration+rollover."),
          QStringLiteral("Перестворені router views і синхронізований order registry."),
          QStringLiteral("Лише rebuild routing views і registry refresh; без прихованих DB opens/migrations."),
          QStringLiteral("BEGIN -> OK/FAIL."),
          QStringLiteral("Зупинити startup; не переходити до UI, якщо routing/registry неузгоджені.") },
        { Phase::UiInit,
          QStringLiteral("ui_init"),
          QStringLiteral("Усі non-UI startup фази завершені успішно."),
          QStringLiteral("Тема, глобальні event filters, splash і main window підготовлені до показу."),
          QStringLiteral("Лише UI construction/apply theme/install filters/show wiring."),
          QStringLiteral("BEGIN -> OK/FAIL."),
          QStringLiteral("Зупинити startup; не входити в event loop із частково піднятим UI.") },
    };
    return kContracts;
}

StartupPipeline::Result StartupPipeline::runCoreStartup(QApplication& app, bool isolatedAcceptance)
{
    Result result;
    result.ok = false;

    if (!runPhase(Phase::Bootstrap, [&](QString*) {
            if (!isolatedAcceptance) {
                QApplication::setApplicationName(App::appName());
                QApplication::setOrganizationName(App::orgName());
            }
            QApplication::setApplicationDisplayName(QStringLiteral("CarServicePointG_Sergiy&Soul"));
            app.setQuitOnLastWindowClosed(false);
            return true;
        }, &result)) {
        return result;
    }

    if (!runPhase(Phase::EnvironmentInit, [&](QString*) {
            QDir(App::dataDir()).mkpath(QStringLiteral("."));
            QDir(App::logsDir()).mkpath(QStringLiteral("."));
            QDir(App::securityDir()).mkpath(QStringLiteral("."));
            QDir(App::sessionDir()).mkpath(QStringLiteral("."));
            QDir(App::backupsDir()).mkpath(QStringLiteral("."));
            QDir(App::backupTempDir()).mkpath(QStringLiteral("."));
            QDir(App::recoveryDir()).mkpath(QStringLiteral("."));
            QDir(App::recoveryTempDir()).mkpath(QStringLiteral("."));
            App::ensureDataAtRestProtection();
            StartupDatabaseFacade::setSetting(QStringLiteral("startup.recovery.bootstrap_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
            return true;
        }, &result)) {
        return result;
    }

    if (!runPhase(Phase::SettingsInit, [&](QString*) {
            const QVariant theme = Settings::get(Settings::keyTheme(), QStringLiteral("graphite"));
            const QVariant sounds = Settings::get(Settings::keySounds(), true);
            Q_UNUSED(theme);
            Q_UNUSED(sounds);
            return true;
        }, &result)) {
        return result;
    }

    if (!runPhase(Phase::LoggerInit, [&](QString* errorText) {
            App::installLogHandler();
            flushPendingStartupLines();
            if (App::logFilePath().trimmed().isEmpty()) {
                if (errorText) *errorText = QStringLiteral("Logger path is empty");
                return false;
            }
            if (!QFileInfo::exists(App::logFilePath())) {
                if (errorText) *errorText = QStringLiteral("Logger file was not created: %1").arg(App::logFilePath());
                return false;
            }
            return true;
        }, &result)) {
        return result;
    }

    if (!runPhase(Phase::DbPreparation, [&](QString* errorText) {
            return StartupDatabaseFacade::prepareRuntime(errorText) && StartupDatabaseFacade::openPrepared(errorText);
        }, &result)) {
        return result;
    }

    if (!runPhase(Phase::SchemaCheck, [&](QString* errorText) {
            return StartupDatabaseFacade::runStartupSchemaCheck(errorText);
        }, &result)) {
        return result;
    }

    emitStartupLine(startupPrefix(phaseKey(Phase::MigrationCheck), QStringLiteral("DEFERRED")));
    emitStartupLine(startupPrefix(phaseKey(Phase::ArchiveRolloverMaintenance), QStringLiteral("DEFERRED")));
    emitStartupLine(startupPrefix(phaseKey(Phase::RegistryRoutingRefresh), QStringLiteral("DEFERRED")));

    result.ok = true;
    result.failedPhase = Phase::None;
    result.errorText.clear();
    return result;
}

void StartupPipeline::beginPhase(Phase phase)
{
    activePhase() = phase;
    emitStartupLine(startupPrefix(phaseKey(phase), QStringLiteral("BEGIN")));
}

void StartupPipeline::succeedPhase(Phase phase)
{
    emitStartupLine(startupPrefix(phaseKey(phase), QStringLiteral("OK")));
    if (activePhase() == phase) {
        activePhase() = Phase::None;
    }
}

void StartupPipeline::failPhase(Phase phase, const QString& errorText)
{
    emitStartupLine(startupPrefix(phaseKey(phase), QStringLiteral("FAIL"))
                    + QStringLiteral(" :: ") + errorText,
                    false);
    activePhase() = phase;
}

QString StartupPipeline::phaseKey(Phase phase)
{
    for (const PhaseContract& contract : contracts()) {
        if (contract.phase == phase) return contract.phaseKey;
    }
    return QStringLiteral("unknown_phase");
}

QString StartupPipeline::currentPhaseKey()
{
    return phaseKey(activePhase());
}
