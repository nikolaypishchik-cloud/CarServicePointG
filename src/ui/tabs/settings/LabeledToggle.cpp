#include "ui/tabs/settings/LabeledToggle.h"
#include "ui/tabs/settings/ToggleSwitch.h"
#include "ui/theme/ThemeManager.h"

#include <QHBoxLayout>
#include <QLabel>

namespace {

QString labelColor()
{
    return ThemeManager::currentTheme() == QStringLiteral("graphite")
        ? QStringLiteral("#f7fbff")
        : QStringLiteral("#111111");
}

} // namespace

LabeledToggle::LabeledToggle(QWidget* parent)
    : LabeledToggle(QString(), parent)
{
}

LabeledToggle::LabeledToggle(const QString& text, QWidget* parent)
    : QWidget(parent)
{
    buildUi(text);
}

void LabeledToggle::buildUi(const QString& text)
{
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(12);

    m_label = new QLabel(text, this);
    m_label->setWordWrap(true);
    m_label->setStyleSheet(QString("background:transparent; color:%1;").arg(labelColor()));
    h->addWidget(m_label, 1);

    m_toggle = new ToggleSwitch(this);
    h->addWidget(m_toggle, 0, Qt::AlignVCenter);

    // Forward toggle signal so external code can connect to LabeledToggle::toggled
    connect(m_toggle, &QAbstractButton::toggled, this, &LabeledToggle::toggled);
}

bool LabeledToggle::isChecked() const
{
    return m_toggle && m_toggle->isChecked();
}

void LabeledToggle::setChecked(bool checked)
{
    if (m_toggle) m_toggle->setChecked(checked);
}

QString LabeledToggle::text() const
{
    return m_label ? m_label->text() : QString();
}

void LabeledToggle::setText(const QString& text)
{
    if (m_label) m_label->setText(text);
}
