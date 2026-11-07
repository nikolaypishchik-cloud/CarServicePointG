#pragma once

#include "core/backup/BackupDtos.h"

#include <QString>
#include <QVector>

class RecoveryDraftStore
{
public:
    static bool saveSessionState(const RecoverySessionState& state, QString* errorText = nullptr);
    static bool loadSessionState(const QString& sessionDir, RecoverySessionState* state, QString* errorText = nullptr);
    static QVector<RecoverySessionState> listSessions();

    static bool saveDraft(DraftPayload* payload, const QString& sessionDir, QString* errorText = nullptr);
    static QVector<DraftPayload> listDrafts(const QString& sessionDir);
    static bool removeDraft(const DraftPayload& payload, QString* errorText = nullptr);
    static bool clearSession(const QString& sessionDir, QString* errorText = nullptr);
};
