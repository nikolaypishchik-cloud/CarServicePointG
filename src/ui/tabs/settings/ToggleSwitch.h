#pragma once

#include <QAbstractButton>

// iOS-style toggle switch: rounded pill background + sliding white knob.
// Drop-in replacement for a checkable QPushButton in settings rows.
class ToggleSwitch : public QAbstractButton
{
    Q_OBJECT
public:
    explicit ToggleSwitch(QWidget* parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
};
