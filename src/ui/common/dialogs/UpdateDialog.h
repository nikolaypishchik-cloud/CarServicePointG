#pragma once

#include <QDialog>

class QLabel;
class QPushButton;
class QTextEdit;
class QProgressBar;
class QFrame;

class UpdateDialog final : public QDialog
{
    Q_OBJECT
public:
    enum class Mode {
        Prompt,
        Progress,
        Info
    };

    explicit UpdateDialog(QWidget* parent = nullptr);

    void setPromptMode(const QString& latestVersion,
                       const QString& currentVersion,
                       const QString& notes,
                       const QString& primaryButtonText,
                       const QString& secondaryButtonText,
                       const QString& detailsButtonText = QString());

    void setProgressMode(const QString& title,
                         const QString& statusText,
                         const QString& primaryButtonText = QString(),
                         const QString& secondaryButtonText = QString());

    void setInfoMode(const QString& title,
                     const QString& message,
                     const QString& primaryButtonText,
                     bool successState = false);

    void setStatusText(const QString& text);
    void setProgressValue(int value);
    void setProgressIndeterminate(bool on);

    QPushButton* primaryButton() const;
    QPushButton* secondaryButton() const;
    QPushButton* detailsButton() const;

private:
    void buildUi();
    void applyBaseStyle();
    void updateNotesVisibility();
    void setMode(Mode mode);

    Mode m_mode = Mode::Prompt;
    bool m_detailsExpanded = false;

    QFrame* m_shell = nullptr;
    QLabel* m_badge = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_subtitleLabel = nullptr;
    QLabel* m_versionLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QTextEdit* m_notesEdit = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QPushButton* m_primaryButton = nullptr;
    QPushButton* m_secondaryButton = nullptr;
    QPushButton* m_detailsButton = nullptr;
};
