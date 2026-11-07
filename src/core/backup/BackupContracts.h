#pragma once

#include <QJsonObject>
#include <QString>

class QObject;

class RecoveryDraftParticipant
{
public:
    virtual ~RecoveryDraftParticipant() = default;

    virtual QObject* draftQObject() = 0;
    virtual QString recoveryModule() const = 0;
    virtual QString recoveryFormType() const = 0;
    virtual QString recoveryEntityType() const = 0;
    virtual QString recoveryEntityId() const = 0;
    virtual QString recoveryOrderUid() const = 0;
    virtual QString draftEntityKey() const = 0;
    virtual QString committedBaseVersion() const = 0;
    virtual bool isDraftDirty() const = 0;
    virtual QJsonObject serializeDraftState() const = 0;
    virtual bool restoreDraftState(const QJsonObject& payload, QString* errorText = nullptr) = 0;
    virtual void notifyDraftDirtyChanged() = 0;
};

