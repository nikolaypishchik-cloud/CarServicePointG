#include "ui/common/dialogs/UpdateDialog.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

namespace {
QString cleanedNotes(QString notes)
{
    notes = notes.trimmed();
    if (notes.isEmpty()) return QString();
    if (notes.size() > 3000) {
        notes = notes.left(3000).trimmed() + QString::fromUtf8("\n…");
    }
    return notes;
}
}

UpdateDialog::UpdateDialog(QWidget* parent)
    : QDialog(parent)
{
    buildUi();
    applyBaseStyle();
    setMode(Mode::Prompt);
}

void UpdateDialog::buildUi()
{
    setModal(true);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setMinimumSize(620, 360);
    resize(720, 460);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(18, 18, 18, 18);

    m_shell = new QFrame(this);
    auto* shellLayout = new QVBoxLayout(m_shell);
    shellLayout->setContentsMargins(24, 24, 24, 20);
    shellLayout->setSpacing(14);

    auto* headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(14);
    m_badge = new QLabel(QString::fromUtf8("↻"), m_shell);
    m_badge->setAlignment(Qt::AlignCenter);
    m_badge->setFixedSize(52, 52);

    auto* titleWrap = new QVBoxLayout();
    titleWrap->setSpacing(4);
    m_titleLabel = new QLabel(m_shell);
    m_titleLabel->setWordWrap(true);
    m_subtitleLabel = new QLabel(m_shell);
    m_subtitleLabel->setWordWrap(true);
    titleWrap->addWidget(m_titleLabel);
    titleWrap->addWidget(m_subtitleLabel);
    headerLayout->addWidget(m_badge, 0, Qt::AlignTop);
    headerLayout->addLayout(titleWrap, 1);

    m_versionLabel = new QLabel(m_shell);
    m_versionLabel->setWordWrap(true);
    m_statusLabel = new QLabel(m_shell);
    m_statusLabel->setWordWrap(true);

    m_notesEdit = new QTextEdit(m_shell);
    m_notesEdit->setReadOnly(true);
    m_notesEdit->setMinimumHeight(150);

    m_progressBar = new QProgressBar(m_shell);
    m_progressBar->setTextVisible(true);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);

    auto* buttonsLayout = new QHBoxLayout();
    buttonsLayout->addStretch(1);
    m_detailsButton = new QPushButton(m_shell);
    m_secondaryButton = new QPushButton(m_shell);
    m_primaryButton = new QPushButton(m_shell);
    buttonsLayout->addWidget(m_detailsButton);
    buttonsLayout->addWidget(m_secondaryButton);
    buttonsLayout->addWidget(m_primaryButton);

    shellLayout->addLayout(headerLayout);
    shellLayout->addWidget(m_versionLabel);
    shellLayout->addWidget(m_statusLabel);
    shellLayout->addWidget(m_notesEdit, 1);
    shellLayout->addWidget(m_progressBar);
    shellLayout->addLayout(buttonsLayout);
    rootLayout->addWidget(m_shell);

    connect(m_primaryButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_secondaryButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_detailsButton, &QPushButton::clicked, this, [this]() {
        m_detailsExpanded = !m_detailsExpanded;
        updateNotesVisibility();
    });
}

void UpdateDialog::applyBaseStyle()
{
    setStyleSheet(QString::fromUtf8(R"(
        UpdateDialog {
            background: #eef3fb;
        }
        QFrame {
            background: #ffffff;
            border: 1px solid #d9e1f2;
            border-radius: 16px;
        }
        QLabel {
            color: #263247;
            background: transparent;
            border: none;
        }
        QTextEdit {
            background: #f7faff;
            color: #2a3447;
            border: 1px solid #dbe6f7;
            border-radius: 12px;
            padding: 8px;
            selection-background-color: #2d7ef7;
        }
        QProgressBar {
            min-height: 18px;
            border-radius: 9px;
            border: 1px solid #dbe5f5;
            background: #eef3fb;
            text-align: center;
            color: #24406f;
            font-weight: 600;
        }
        QProgressBar::chunk {
            border-radius: 8px;
            background: #2d7ef7;
        }
        QPushButton {
            min-width: 126px;
            min-height: 38px;
            border-radius: 10px;
            border: 1px solid #cad6ea;
            background: #f2f5fb;
            color: #2a3447;
            font-weight: 600;
            padding: 0 14px;
        }
        QPushButton:hover {
            background: #e8eef9;
        }
    )"));

    m_badge->setStyleSheet(QString::fromUtf8("background:#2d7ef7;color:#ffffff;border-radius:26px;font-size:28px;font-weight:700;"));
    m_titleLabel->setStyleSheet(QString::fromUtf8("font-size:24px;font-weight:700;color:#1d2b44;"));
    m_subtitleLabel->setStyleSheet(QString::fromUtf8("font-size:14px;color:#5c6b82;"));
    m_versionLabel->setStyleSheet(QString::fromUtf8("font-size:15px;font-weight:600;color:#30415f;"));
    m_statusLabel->setStyleSheet(QString::fromUtf8("font-size:15px;color:#4a5c78;"));
    m_primaryButton->setStyleSheet(QString::fromUtf8(
        "QPushButton{background:#2d7ef7;color:#ffffff;border:1px solid #2d7ef7;font-weight:700;border-radius:10px;}"
        "QPushButton:hover{background:#226fe2;border-color:#226fe2;}"));
}

void UpdateDialog::setMode(Mode mode)
{
    m_mode = mode;
    const bool promptMode = mode == Mode::Prompt;
    const bool progressMode = mode == Mode::Progress;
    const bool infoMode = mode == Mode::Info;

    m_versionLabel->setVisible(promptMode || infoMode);
    m_notesEdit->setVisible(promptMode && m_detailsExpanded && !m_notesEdit->toPlainText().trimmed().isEmpty());
    m_progressBar->setVisible(progressMode);
    m_detailsButton->setVisible(promptMode && !m_notesEdit->toPlainText().trimmed().isEmpty());
    m_secondaryButton->setVisible(promptMode || progressMode);
    if (infoMode) {
        m_progressBar->setVisible(false);
        m_detailsButton->setVisible(false);
        m_secondaryButton->setVisible(false);
    }
}

void UpdateDialog::updateNotesVisibility()
{
    const bool hasNotes = !m_notesEdit->toPlainText().trimmed().isEmpty();
    m_notesEdit->setVisible(m_mode == Mode::Prompt && hasNotes && m_detailsExpanded);
    m_detailsButton->setVisible(m_mode == Mode::Prompt && hasNotes);
    if (hasNotes) {
        m_detailsButton->setText(m_detailsExpanded ? QString::fromUtf8("Сховати деталі")
                                                   : QString::fromUtf8("Деталі"));
    }
}

void UpdateDialog::setPromptMode(const QString& latestVersion,
                                 const QString& currentVersion,
                                 const QString& notes,
                                 const QString& primaryButtonText,
                                 const QString& secondaryButtonText,
                                 const QString& detailsButtonText)
{
    setWindowTitle(QString::fromUtf8("Оновлення застосунку"));
    m_badge->setText(QString::fromUtf8("↻"));
    m_badge->setStyleSheet(QString::fromUtf8("background:#2d7ef7;color:#ffffff;border-radius:26px;font-size:28px;font-weight:700;"));
    m_titleLabel->setText(QString::fromUtf8("Доступне оновлення"));
    m_subtitleLabel->setText(QString::fromUtf8("Безпечне оновлення з перевіркою релізу та пакета."));
    m_versionLabel->setText(QString::fromUtf8("Нова версія: %1    •    Поточна версія: %2").arg(latestVersion, currentVersion));
    m_statusLabel->setText(QString::fromUtf8("Будуть завантажені тільки перевірені файли релізу. Після підготовки оновлення застосунок коректно завершить роботу."));
    m_notesEdit->setPlainText(cleanedNotes(notes));
    m_primaryButton->setText(primaryButtonText);
    m_secondaryButton->setText(secondaryButtonText);
    if (!detailsButtonText.trimmed().isEmpty()) m_detailsButton->setText(detailsButtonText);
    m_detailsExpanded = false;
    setMode(Mode::Prompt);
    updateNotesVisibility();
}

void UpdateDialog::setProgressMode(const QString& title,
                                   const QString& statusText,
                                   const QString& primaryButtonText,
                                   const QString& secondaryButtonText)
{
    setWindowTitle(QString::fromUtf8("Оновлення застосунку"));
    m_badge->setText(QString::fromUtf8("↓"));
    m_badge->setStyleSheet(QString::fromUtf8("background:#2d7ef7;color:#ffffff;border-radius:26px;font-size:28px;font-weight:700;"));
    m_titleLabel->setText(title);
    m_subtitleLabel->setText(QString::fromUtf8("Не закривайте застосунок під час завантаження або підготовки handoff."));
    m_versionLabel->clear();
    m_statusLabel->setText(statusText);
    m_notesEdit->clear();
    m_primaryButton->setVisible(!primaryButtonText.trimmed().isEmpty());
    if (!primaryButtonText.trimmed().isEmpty()) m_primaryButton->setText(primaryButtonText);
    m_secondaryButton->setText(secondaryButtonText);
    m_detailsExpanded = false;
    setMode(Mode::Progress);
    updateNotesVisibility();
}

void UpdateDialog::setInfoMode(const QString& title,
                               const QString& message,
                               const QString& primaryButtonText,
                               bool successState)
{
    setWindowTitle(QString::fromUtf8("Оновлення застосунку"));
    m_badge->setText(successState ? QString::fromUtf8("✓") : QString::fromUtf8("i"));
    m_badge->setStyleSheet(QString::fromUtf8(successState
        ? "background:#2ecc71;color:#ffffff;border-radius:26px;font-size:28px;font-weight:700;"
        : "background:#2d7ef7;color:#ffffff;border-radius:26px;font-size:28px;font-weight:700;"));
    m_titleLabel->setText(title);
    m_subtitleLabel->setText(successState
        ? QString::fromUtf8("Оновлення підготовлено без порушення поточного workflow.")
        : QString::fromUtf8("Інформаційне повідомлення щодо стану оновлення."));
    m_versionLabel->clear();
    m_statusLabel->setText(message);
    m_notesEdit->clear();
    m_primaryButton->setVisible(true);
    m_primaryButton->setText(primaryButtonText);
    m_detailsExpanded = false;
    setMode(Mode::Info);
    updateNotesVisibility();
}

void UpdateDialog::setStatusText(const QString& text)
{
    m_statusLabel->setText(text);
}

void UpdateDialog::setProgressValue(int value)
{
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(qBound(0, value, 100));
}

void UpdateDialog::setProgressIndeterminate(bool on)
{
    if (on) {
        m_progressBar->setRange(0, 0);
    } else if (m_progressBar->maximum() == 0) {
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(0);
    }
}

QPushButton* UpdateDialog::primaryButton() const
{
    return m_primaryButton;
}

QPushButton* UpdateDialog::secondaryButton() const
{
    return m_secondaryButton;
}

QPushButton* UpdateDialog::detailsButton() const
{
    return m_detailsButton;
}
