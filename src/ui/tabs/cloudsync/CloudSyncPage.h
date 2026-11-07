#pragma once

#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

class CloudSyncPage : public QWidget
{
    Q_OBJECT
public:
    explicit CloudSyncPage(QWidget* parent = nullptr);
    void refresh();

signals:
    void settingsSaved();

private:
    void loadSettingsToUi();
    bool saveSettingsFromUi(QString* errorText = nullptr);
    void runBackgroundSync(bool onlyTestConnection);
    void refreshConflicts();
    void resolveSelectedConflict(const QString& resolution);

    QLabel* m_status = nullptr;
    QLabel* m_statusBadge = nullptr;
    QLabel* m_conflictsEmpty = nullptr;
    QCheckBox* m_enabled = nullptr;
    QCheckBox* m_offline = nullptr;
    QLineEdit* m_deviceName = nullptr;
    QLineEdit* m_deviceUid = nullptr;
    QLineEdit* m_serverUrl = nullptr;
    QLineEdit* m_ownerEmail = nullptr;
    QLineEdit* m_ownerPassword = nullptr;
    QCheckBox* m_rememberOwnerPassword = nullptr;
    QLineEdit* m_organizationUid = nullptr;
    QLineEdit* m_branchUid = nullptr;
    QLineEdit* m_apiToken = nullptr;
    QSpinBox* m_intervalSeconds = nullptr;
    QPushButton* m_testButton = nullptr;
    QPushButton* m_syncNowButton = nullptr;
    QTableWidget* m_conflictsTable = nullptr;
    bool m_refreshInProgress = false;
    bool m_refreshPending = false;
    bool m_syncInProgress = false;
    quint64 m_refreshGeneration = 0;
};
