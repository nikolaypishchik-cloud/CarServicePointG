#pragma once

#include <QDialog>
#include "core/backup/BackupContracts.h"
#include <QString>
#include <QDate>
#include <QJsonObject>
#include <QTime>
#include "integrations/opendatabot/OpenDataBotClient.h"
#include "core/services/orders/OrderDialogStateSignature.h"
#include "core/services/OrderWorkspaceService.h"
#include "core/services/WorkshopSpareStockService.h"

class QShowEvent;
class QDateTime;

class QSpinBox;
class QDateEdit;
class QTimeEdit;
class QComboBox;
class QLineEdit;
class QDoubleSpinBox;
class QCheckBox;
class QTableWidget;
class QPushButton;
class QLabel;
class QTabWidget;

class WorkPartsDialog;

class OrderDialog : public QDialog, public RecoveryDraftParticipant
{
    Q_OBJECT
public:
    explicit OrderDialog(QWidget* parent=nullptr, int orderNumber=-1);

    void setReadOnly(bool on);
    void openWorkPartsTab();
    void exportPdfPreviewAndOpen(bool partsReport = false);
    bool exportPartsReportToFile(const QString& outFile, QString* errorText = nullptr);
    bool exportCustomerReportToFile(const QString& outFile, const QString& recommendations, QString* errorText = nullptr);
    void prefillFromExistingOrder(int sourceOrderNumber);
    bool prefillFromCrmVehicle(const QString& clientUid, const QString& vehicleUid);
    void prefillFromPlanningSource(int sourceOrderNumber, const QString& planningText, const QDate& plannedDate = QDate(), const QTime& plannedTime = QTime());
    void prefillFromPlanningEntry(const QString& planningText, const QDate& plannedDate = QDate(), const QTime& plannedTime = QTime());
    void prefillFromSpareStock(const WorkshopSpareOrder& spare, double quantity);

    bool ensureWorkPartsPageForRecovery(QString* errorText = nullptr);
    WorkPartsDialog* workPartsPageForRecovery() const;

    QObject* draftQObject() override;
    QString recoveryModule() const override;
    QString recoveryFormType() const override;
    QString recoveryEntityType() const override;
    QString recoveryEntityId() const override;
    QString recoveryOrderUid() const override;
    QString draftEntityKey() const override;
    QString committedBaseVersion() const override;
    bool isDraftDirty() const override;
    QJsonObject serializeDraftState() const override;
    bool restoreDraftState(const QJsonObject& payload, QString* errorText = nullptr) override;
    void notifyDraftDirtyChanged() override;

signals:
    void purchasePaymentsChanged();

protected:
    void accept() override;
    void reject() override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void centerOnScreen();
    void armUserEditTracking(QWidget* widget);
    bool prefillFromCompletedVehicleByPlateIfAvailable();
    void lookupVehicleByPlateIfConfigured();
    void applyVehicleLookupResult(const OpenDataBotClient::VehicleLookupResult& result);

    void buildUi();
    void loadMasters();
    void editServiceMasters(int row);
    void loadOrder(int number);
    bool ensureDraftOrder();
    bool issuePendingSpare(QString* error);
    WorkshopSpareOrder m_pendingSpare;
    double m_pendingSpareQuantity = 0;
    bool m_spareIssued = false;
    OrderDialogStateSnapshot buildStateSnapshot() const;
    OrderWorkspaceInput buildWorkspaceInput(const QString& status, double worksSum, double partsSum, double discount) const;
    QString currentStateSignature() const;
    bool hasUnsavedChanges() const;
    void captureSavedState();
    QString selectedFuelType() const;
    void setSelectedFuelType(const QString& fuelType);
    QDateTime currentOpenedAt() const;
    void setOpenedAt(const QDateTime& dt);
    bool ensureWorkPartsPage(QString* errorText = nullptr);

    double servicesTotal() const;
    double partsTotal() const;

    void updateServiceRow(int row);
    void updatePartRow(int row);
    void refreshTotals();

    bool exportPdfToFile(const QString& outFile, QString* errorText = nullptr, bool partsReport = false);
    void exportPdf(const QString& outFile, bool partsReport = false);

    int m_orderNumber = -1;
    QString m_creationRequestUid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString m_pdfRecommendations;
    bool m_readOnly = false;
    bool m_isNewOrder = false;
    bool m_draftCreated = false;
    bool m_vehicleLookupInProgress = false;
    bool m_vehicleLookupWarned = false;
    bool m_userInteractedWithForm = false;
    bool m_acceptInProgress = false;
    bool m_draftCreationInProgress = false;
    QString m_lastLookupPlate;
    QString m_lastCarEditedText;
    QString m_savedStateSignature;
    bool m_recoveryDirtyNotified = false;
    bool m_focusOdometerOnShow = false;

    QSpinBox* m_num = nullptr;
    QDateEdit* m_openedDate = nullptr;
    QTimeEdit* m_openedTime = nullptr;
    QComboBox* m_status = nullptr;
    QLineEdit* m_plate = nullptr;
    QLineEdit* m_vin = nullptr;
    QLineEdit* m_car = nullptr;
    QLineEdit* m_color = nullptr;
    QSpinBox* m_year = nullptr;
    QSpinBox* m_odometer = nullptr;
    QLineEdit* m_engineCode = nullptr;
    QLineEdit* m_engineVolume = nullptr;
    QCheckBox* m_fuelDiesel = nullptr;
    QCheckBox* m_fuelPetrol = nullptr;
    QCheckBox* m_fuelHybrid = nullptr;
    QCheckBox* m_fuelElectric = nullptr;
    QLineEdit* m_client = nullptr;
    QLineEdit* m_phone = nullptr;
    QComboBox* m_master = nullptr;
    QDoubleSpinBox* m_discount = nullptr;
    QComboBox* m_discountMode = nullptr;

    QTabWidget* m_tabs = nullptr;
    WorkPartsDialog* m_workPartsPage = nullptr;
    QPushButton* m_btnOk = nullptr;
    QPushButton* m_btnCancel = nullptr;

    QPushButton* m_btnPrint = nullptr;
    QPushButton* m_btnCopy = nullptr;
    QLabel* m_totals = nullptr;
};
