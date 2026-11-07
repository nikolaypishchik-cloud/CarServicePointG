#pragma once

#include <QString>
#include <QList>

class QApplication;

class StartupPipeline
{
public:
    enum class Phase {
        None,
        Bootstrap,
        EnvironmentInit,
        SettingsInit,
        LoggerInit,
        DbPreparation,
        SchemaCheck,
        MigrationCheck,
        ArchiveRolloverMaintenance,
        RegistryRoutingRefresh,
        UiInit
    };

    struct PhaseContract {
        Phase phase = Phase::None;
        QString phaseKey;
        QString input;
        QString output;
        QString allowedSideEffects;
        QString loggingOrder;
        QString failureAction;
    };

    struct Result {
        bool ok = false;
        Phase failedPhase = Phase::None;
        QString errorText;
    };

    static Result runCoreStartup(QApplication& app);
    static const QList<PhaseContract>& contracts();

    static void beginPhase(Phase phase);
    static void succeedPhase(Phase phase);
    static void failPhase(Phase phase, const QString& errorText);

    static QString phaseKey(Phase phase);
    static QString currentPhaseKey();
};
