#pragma once

#include "core/backup/RecoveryUiContracts.h"

#include <memory>

class QString;
class QWidget;
class QJsonObject;

class RecoveryDialogSession
{
public:
    virtual ~RecoveryDialogSession() = default;

    virtual QString committedBaseVersion() const = 0;
    virtual bool restoreOrderDraft(const QJsonObject& payloadData, QString* errorText) = 0;
    virtual bool ensureWorkPartsPage(QString* errorText) = 0;
    virtual bool restoreWorkPartsDraft(const QJsonObject& payloadData, QString* errorText) = 0;
    virtual void openWorkPartsTab() = 0;
    virtual void showAndActivate() = 0;
};

class RecoveryUiFactory
{
public:
    virtual ~RecoveryUiFactory() = default;
    virtual std::unique_ptr<RecoveryDialogSession> createSession(QWidget* parent, RecoveryUiTarget target, int orderNumber) const = 0;
};
