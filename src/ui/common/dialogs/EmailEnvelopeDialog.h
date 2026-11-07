#pragma once

#include <QDialog>
#include <QString>

class QPushButton;
class SealedEnvelopeWidget;

class EmailEnvelopeDialog final : public QDialog
{
public:
    explicit EmailEnvelopeDialog(QWidget* parent = nullptr);

    void setAccountLabel(const QString& account);

private:
    void playOpenAnimation();

    SealedEnvelopeWidget* m_envelope = nullptr;
    QPushButton* m_viewButton = nullptr;
    QPushButton* m_postponeButton = nullptr;
};
