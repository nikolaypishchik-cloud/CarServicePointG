#include "runtime/OrderDialogRecoveryUiFactory.h"

#include "ui/common/dialogs/OrderDialog.h"
#include "ui/tabs/orders/WorkPartsDialog.h"

#include <QWidget>
#include <memory>

namespace {

class OrderDialogRecoverySession final : public RecoveryDialogSession
{
public:
    explicit OrderDialogRecoverySession(OrderDialog* dialog)
        : m_dialog(dialog)
    {
    }

    ~OrderDialogRecoverySession() override = default;

    QString committedBaseVersion() const override
    {
        return m_dialog ? m_dialog->committedBaseVersion() : QString();
    }

    bool restoreOrderDraft(const QJsonObject& payloadData, QString* errorText) override
    {
        return m_dialog && m_dialog->restoreDraftState(payloadData, errorText);
    }

    bool ensureWorkPartsPage(QString* errorText) override
    {
        return m_dialog && m_dialog->ensureWorkPartsPageForRecovery(errorText) && m_dialog->workPartsPageForRecovery();
    }

    bool restoreWorkPartsDraft(const QJsonObject& payloadData, QString* errorText) override
    {
        return m_dialog && m_dialog->workPartsPageForRecovery()
            && m_dialog->workPartsPageForRecovery()->restoreDraftState(payloadData, errorText);
    }

    void openWorkPartsTab() override
    {
        if (m_dialog) {
            m_dialog->openWorkPartsTab();
        }
    }

    void showAndActivate() override
    {
        if (!m_dialog) return;
        m_dialog->show();
        m_dialog->raise();
        m_dialog->activateWindow();
    }

private:
    OrderDialog* m_dialog = nullptr;
};

} // namespace

std::unique_ptr<RecoveryDialogSession> OrderDialogRecoveryUiFactory::createSession(QWidget* parent,
                                                                                     RecoveryUiTarget target,
                                                                                     int orderNumber) const
{
    switch (target) {
    case RecoveryUiTarget::OrderEditor:
    case RecoveryUiTarget::WorkItemsEditor:
        break;
    case RecoveryUiTarget::None:
    default:
        return nullptr;
    }

    auto* dialog = new OrderDialog(parent, orderNumber > 0 ? orderNumber : -1);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    return std::make_unique<OrderDialogRecoverySession>(dialog);
}
