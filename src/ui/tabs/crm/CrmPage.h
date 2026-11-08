#pragma once

#include <QList>
#include <QWidget>
#include <QString>

class QLabel;
class QLineEdit;
class QPushButton;
class QTabWidget;
class QTableWidget;
class QTableWidgetItem;
class QTimer;
class QVBoxLayout;
class QWidget;

class CrmPage : public QWidget
{
public:
    explicit CrmPage(QWidget* parent = nullptr);
    void refresh();

private:
    enum class SelectionReason {
        UserClick,
        UserSelection,
        RefreshRestore,
        Programmatic
    };

    void refreshClients();
    void refresh(bool showStatus);
    bool runBackfill(QString* statusText, QString* errorText);
    void restoreClientTableColumnWidths();
    void saveClientTableColumnWidth(int logicalIndex, int width);
    QString clientUidForItem(const QTableWidgetItem* item) const;
    QString clientUidForRow(int row) const;
    void loadSelectedClientCard(const QString& reason = QString());
    void loadClientCardForRow(int row, const QString& reason = QString());
    bool selectClientByUid(const QString& clientUid, SelectionReason reason, bool allowFirstRow = false);
    void loadClientDetails(const QString& clientUid);
    void clearVehicleCards();
    void selectVehicleUid(const QString& vehicleUid);
    void setVehiclePrimaryStatus(const QString& vehicleUid, bool primary);
    void updateVehicleStatusButtons();
    QString selectedClientUid() const;
    QString selectedVehicleUid() const;
    QString selectedTaskUid() const;
    void addClientNote();
    void addClientTask();
    void markSelectedTaskCompleted();
    void requestNewOrder();
    void openClientDebt();
    void requestRepairBookingTask();
    QString historyOrderUidForRow(int row) const;
    int historyOrderNumberForRow(int row) const;
    void openHistoryOrderRow(int row);

    QLabel* m_summary = nullptr;
    QLabel* m_detail = nullptr;
    QLabel* m_debt = nullptr;
    QLabel* m_paymentNotice = nullptr;
    QPushButton* m_showOrders = nullptr;
    QWidget* m_clientCard = nullptr;
    QTabWidget* m_sections = nullptr;
    QLineEdit* m_search = nullptr;
    QPushButton* m_createOrder = nullptr;
    QPushButton* m_createAppointment = nullptr;
    QPushButton* m_addNote = nullptr;
    QPushButton* m_addTask = nullptr;
    QPushButton* m_completeTask = nullptr;
    QTableWidget* m_clients = nullptr;
    QVBoxLayout* m_vehicleList = nullptr;
    QLabel* m_vehiclePlaceholder = nullptr;
    QList<QPushButton*> m_vehicleCards;
    QTableWidget* m_history = nullptr;
    QTableWidget* m_events = nullptr;
    QTableWidget* m_tasks = nullptr;
    QString m_currentClientUid;
    QString m_currentVehicleUid;
    QTimer* m_searchDebounce = nullptr;
    bool m_refreshInProgress = false;
    bool m_refreshPending = false;
    bool m_backfillCompletedThisSession = false;
    bool m_populatingClients = false;
    bool m_selectingClient = false;
    quint64 m_refreshGeneration = 0;
};
