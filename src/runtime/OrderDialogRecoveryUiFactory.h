#pragma once

#include "runtime/RecoveryUiFactory.h"
#include <memory>

class OrderDialogRecoveryUiFactory final : public RecoveryUiFactory
{
public:
    std::unique_ptr<RecoveryDialogSession> createSession(QWidget* parent, RecoveryUiTarget target, int orderNumber) const override;
};