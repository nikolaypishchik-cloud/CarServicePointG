#pragma once
#include <QWidget>
#include <QHash>
#include <QVariantMap>
class QTableWidget;
class QLineEdit;
class QCheckBox;
class QDateEdit;
class QDateTimeEdit;
class QPlainTextEdit;
class QButtonGroup;
class QRadioButton;
class QGroupBox;
class QPushButton;
class QLabel;
class QComboBox;

class VehicleIntakePage : public QWidget {
    Q_OBJECT
public:
    explicit VehicleIntakePage(QWidget* parent = nullptr);
    void refresh();
signals:
    void requestRefreshAll();
private:
    void buildUi();
    void saveDraft();
    void addPhotoForSelected();
    void pickSignatureForSelected();
    void generatePdfForSelected();
    void printBlankAct();
    void loadSelectedIntake();
    void autofillFromOrder();
    void lookupVehicleByPlate();
    void loadMasters();
    QString selectedIntakeUid() const;
    QString selectedOrderUid() const;
    QVariantMap collectPayload() const;
    void applyChecklistJson(const QString& json);
    void clearForm();
    void setMasterText(const QString& master);
    QString masterText() const;
    void applyVehiclePrefill(const QVariantMap& data, const QString& sourceLabel, bool askBeforeOverwrite);
    bool hasVehiclePrefillConflicts(const QVariantMap& data) const;
    void setVehicleLookupStatus(const QString& text);
    void setSignatureSectionExpanded(bool expanded);
    void setActsTableExpanded(bool expanded);
    QTableWidget* m_table = nullptr;
    QPushButton* m_actsTableToggle = nullptr;
    QLabel* m_actsTableEmptyLabel = nullptr;
    QLineEdit* m_intakeUid = nullptr;
    QLineEdit* m_actNumber = nullptr;
    QDateEdit* m_actDate = nullptr;
    QLineEdit* m_order = nullptr;
    QLineEdit* m_clientUid = nullptr;
    QLineEdit* m_vehicleUid = nullptr;
    QLineEdit* m_client = nullptr;
    QLineEdit* m_phone = nullptr;
    QLineEdit* m_vehicle = nullptr;
    QLineEdit* m_plate = nullptr;
    QLineEdit* m_vin = nullptr;
    QLineEdit* m_year = nullptr;
    QLineEdit* m_engine = nullptr;
    QLineEdit* m_gearbox = nullptr;
    QLineEdit* m_mileage = nullptr;
    QLineEdit* m_fuel = nullptr;
    QDateTimeEdit* m_acceptedAt = nullptr;
    QDateTimeEdit* m_plannedReturnAt = nullptr;
    QComboBox* m_master = nullptr;
    QLabel* m_plateLookupStatus = nullptr;
    QPlainTextEdit* m_complaint = nullptr;
    QPlainTextEdit* m_diagnostics = nullptr;
    QLineEdit* m_signature = nullptr;
    QPlainTextEdit* m_valuables = nullptr;
    QLineEdit* m_contactMethod = nullptr;
    QLineEdit* m_customerName = nullptr;
    QLineEdit* m_receiverName = nullptr;
    QGroupBox* m_signatureTransferBox = nullptr;
    QPushButton* m_signatureTransferToggle = nullptr;
    QHash<QString, QButtonGroup*> m_conditionGroups;
    QHash<QString, QPlainTextEdit*> m_conditionNotes;
    QHash<QString, QCheckBox*> m_equipment;
    QHash<QString, QCheckBox*> m_permissions;
    QString m_lastLookupPlate;
    bool m_vehicleLookupInProgress = false;
    bool m_refreshInProgress = false;
    bool m_refreshPending = false;
    bool m_populatingTable = false;
    bool m_pdfGenerationInProgress = false;
    quint64 m_lookupGeneration = 0;
};
