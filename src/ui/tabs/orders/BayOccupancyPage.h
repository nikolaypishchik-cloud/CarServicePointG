#pragma once
#include <QWidget>
#include <QJsonObject>
#include "core/services/orders/BaySnapshot.h"
class QNetworkAccessManager;
class QTableWidget;
class QLabel;
class QPushButton;
class QDateEdit;
class QComboBox;

class BayOccupancyPage final : public QWidget {
public:
    explicit BayOccupancyPage(QWidget* parent = nullptr);
    static void open(QWidget* parent, const QString& orderUid = {}, int preferredPost = -1);
    static bool closeOrders(QWidget* parent, const QStringList& orderUids, const QString& targetStatus = "closed");
    // Rendering is independent of credentials and databases, including in UI tests.
    bool displaySnapshot(const QJsonObject& snapshot);
private:
    void request(const QString& endpoint, QJsonObject payload = {});
    void refresh();
    void render();
    void changeOrder(const QString& initialBay = {});
    void correctHistory();
    void editCalendar();
    void confirmClosure();
    bool allowClose();
    void command(const QString& endpoint, QJsonObject payload);
    void updateEnabled();
    QNetworkAccessManager* network;
    QTableWidget* resources;
    QTableWidget* report;
    QTableWidget* history;
    QTableWidget* audit;
    QPushButton* correct;
    QPushButton* calendar;
    QLabel* status;
    QDateEdit* from;
    QDateEdit* to;
    QComboBox* bayFilter;
    QPushButton* reload;
    QPushButton* provision;
    QPushButton* change;
    QPushButton* retry;
    BaySnapshot::Data data;
    QJsonObject raw;
    QString pendingEndpoint;
    QJsonObject pendingPayload;
    bool busy = false;
    bool fresh = false;
    QString selectedOrder;
    int preferredPost = -1;
    bool offerOrderChange = false;
    QStringList closingOrders;
    QString closingStatus;
    bool closureConfirmed = false;
};
