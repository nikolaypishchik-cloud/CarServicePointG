#pragma once

#include <QWidget>

class QLabel;
class ToggleSwitch;

// Drop-in replacement for QCheckBox using the project's toggle-switch visual.
// Layout: [text label] | [stretch] | [ToggleSwitch].
//
// API mirrors QCheckBox where it matters in the settings tabs:
//   - constructor (text, parent)
//   - isChecked() / setChecked(bool)
//   - text() / setText(QString)
//   - toggled(bool) signal
//
// Use this anywhere a QCheckBox was previously used in Settings.
class LabeledToggle : public QWidget
{
    Q_OBJECT
public:
    explicit LabeledToggle(QWidget* parent = nullptr);
    explicit LabeledToggle(const QString& text, QWidget* parent = nullptr);

    bool isChecked() const;
    void setChecked(bool checked);

    QString text() const;
    void setText(const QString& text);

    // Direct access for advanced styling/use cases
    QLabel* label() const { return m_label; }
    ToggleSwitch* toggle() const { return m_toggle; }

signals:
    void toggled(bool checked);

private:
    void buildUi(const QString& text);

    QLabel*       m_label  = nullptr;
    ToggleSwitch* m_toggle = nullptr;
};
