#pragma once

#include <QDialog>
#include "core/backup/BackupContracts.h"
#include "core/services/OrderDetailsRepository.h"
#include "core/services/orders/OrderAttachmentService.h"
#include <QJsonObject>
#include <QHash>
#include <QSet>
#include <QVector>
#include <QString>
#include <QRect>
#include <QVariantMap>
#include <QtGlobal>

class QLabel;
class QTableWidget;
class QSplitter;
class QComboBox;
class QPushButton;
class QResizeEvent;
class QHideEvent;

class WorkPartsDialog : public QDialog, public RecoveryDraftParticipant
{
    friend class TableKeyboardNavigator;
    Q_OBJECT
public:
    explicit WorkPartsDialog(QWidget* parent, int orderNumber);

    void setEmbedded(bool on);
    bool isEmbedded() const { return m_embedded; }
    bool commitPendingEdits(QString* errorText = nullptr, bool applyGrouping = false, bool skipUiRefreshAfterCommit = false);
    bool hasPendingEdits() const;


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

    static bool editOutsourceJob(QWidget* parent, int orderNumber, int serviceId);

    bool runEditOutsourceJobFlow(QWidget* parent, int orderNumber, int serviceId);

signals:
    void requestOk();
    void requestCancel();
    void requestPdf();
    void requestCloseOrder();
    void purchasePaymentsChanged();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void buildUi();
    void persistUiLayoutState() const;
    void loadHeader();
    void loadMasterChoice();
    void loadServices();
    void loadPartsForActiveService();
    void applyServicesSnapshot(const QList<WorkOrderServiceRow>& rows,
                               const QHash<int, QString>& outsourceDisplayNames,
                               const QHash<int, OrderAttachmentService::AttachmentInfo>& attachmentsByServiceId,
                               const QString& primaryMaster);
    void applyPartsSnapshot(const QList<WorkOrderPartRow>& rows,
                            const QHash<int, OrderAttachmentService::AttachmentInfo>& attachmentsByPartId);
    void refreshViewportFillRows();
    void refreshAfterMutation(bool reloadServices, bool reloadParts, bool reloadHeader, bool ensureServiceSelection = false);
    void scheduleWorkPartsRefresh(bool reloadServices,
                                  bool reloadParts,
                                  bool reloadHeader,
                                  bool ensureServiceSelection = false,
                                  const QString& reason = QString(),
                                  bool clearUserTouchedAfterApply = false,
                                  bool focusFirstSectionAfterApply = false);
    void refreshServiceAttachmentCell(int row);
    void refreshPartAttachmentCell(int row);
    void refreshPartInvoiceNumberCell(int row);
    void openServiceAttachmentDialog(int row);
    void openPartAttachmentDialog(int row);
    void openPartInvoiceDocument(int row);

    void ensureServiceSelected();
    void setActiveServiceId(int serviceId);
    void syncServiceVisualSelection();
    int serviceRowById(int serviceId) const;
    int partRowById(int partId) const;
    void clearWorkPartsRowIndexes();
    void indexServiceRow(int serviceId, int row);
    void indexPartRow(int partId, int row);
    void focusSection(int section);

    bool insertService(const QString& title, const QString& prefix = QString());
    bool savePrimaryMaster();
    bool saveServiceRow(int row, QString* errorText = nullptr, bool reloadAfterSave = true);
    bool savePartRow(int row, QString* errorText = nullptr, bool reloadAfterSave = true);
    bool commitPendingRowsInSingleTransaction(const QList<int>& serviceRowsToPersist,
                                              const QList<int>& partRowsToPersist,
                                              bool applyGrouping,
                                              QString* errorText = nullptr);
    struct DeferredAttachmentOperation {
        enum class Kind { Service, Part };
        Kind kind = Kind::Part;
        int row = -1;
        int ownerId = 0;
        QString sourcePath;
        QString invoiceNumber;
        QString invoiceDate;
        QString supplierName;
        bool importedInvoiceRow = false;
        double confirmedInvoiceAmount = -1;
        QString importTraceId;
    };
    bool stageServiceAttachmentAfterTransaction(int row, int serviceId, const QString& sourcePath);
    bool stagePartAttachmentAfterTransaction(int row, int partId, const QString& sourcePath,
                                             const QString& invoiceNumber, const QString& invoiceDate,
                                             const QString& supplierName, bool importedInvoiceRow,
                                             const QString& importTraceId, double confirmedInvoiceAmount = -1);
    bool processDeferredAttachmentOperations(QString* errorText = nullptr);
    bool processDeferredAttachmentOperation(const DeferredAttachmentOperation& op, QString* errorText = nullptr);

    void autoAssignPartsToServices();
    void openAddServicesDialog();
    void openEditServicesDialog();
    void openAddPartsDialog();
    void openRepairAdvisorDialog();
    void openEditPartsDialog();
    void importPartsFromExcel();
    void applySelectedPartMarkupPercent();
    QRect partsImportTargetRect() const;
    bool deleteActiveService();
    bool deleteActivePart();
    bool deleteSelectedServices();
    bool deleteSelectedParts();
    bool setServiceMaster(const QString& masterId, const QString& masterName);
    bool setServicePrice(double price);
    bool clearServiceMaster();
    void insertEmptyServiceRow(int row = -1);
    void insertEmptyPartRow(int row = -1);
    void autofillDraftPartsForService(int row);
    friend class HistoryPartsAutofillTests;
    void renumberServiceItems();
    void renumberPartItems();
    void synchronizePairedRowHeights(int row);
    void synchronizeAllPairedRowHeights();
    void invalidateWorkPartsRowHeightCache(int row = -1);
    void repositionInputRows();
    void showServicesMenu(const QPoint& pos);
    void showPartsMenu(const QPoint& pos);

    int activeServiceId() const;
    QString currentOrderUid(QString* errorText = nullptr) const;
    QString currentContentSignature() const;
    QString persistedContentSignature() const;

    void logWorkPartsDiagnostic(const QString& operation,
                                const QString& entityType = QString(),
                                int row = -1,
                                int entityId = -1,
                                const QString& invoiceNumber = QString(),
                                const QVariantMap& details = QVariantMap()) const;
    quint64 bumpWorkPartsTableRevision(const QString& operation);
    QString workPartsRowUid(QTableWidget* table, int row) const;

    int m_orderNumber = -1;
    int m_activeServiceId = 0;
    bool m_embedded = false;
    bool m_syncingServices = false;
    bool m_loadingParts = false;
    bool m_userTouchedPendingContent = false;
    QSet<int> m_dirtyServiceRows;
    QSet<int> m_dirtyPartRows;
    bool m_recoveryRegistered = false;
    bool m_useExternalSaveTransaction = false;
    bool m_commitPendingInProgress = false;
    bool m_viewportRefreshQueued = false;
    bool m_partsItemChangedGuard = false;
    bool m_partsCurrentCellGuard = false;
    QString m_externalSaveConnectionName;
    QVector<DeferredAttachmentOperation> m_deferredAttachmentOperations;
    mutable quint64 m_tableRevision = 0;
    quint64 m_draftEditRevision = 0;
    mutable QString m_cachedOrderUid;
    mutable QHash<int, quint64> m_pairedRowHeightSignatureByRow;
    mutable QHash<int, int> m_pairedRowHeightByRow;
    QHash<int, int> m_serviceRowById;
    QHash<int, int> m_partRowById;
    quint64 m_workPartsRefreshGeneration = 0;
    bool m_pendingReloadServices = false;
    bool m_pendingReloadParts = false;
    bool m_pendingReloadHeader = false;
    bool m_pendingEnsureServiceSelection = false;
    bool m_pendingClearUserTouchedAfterApply = false;
    bool m_pendingFocusFirstSectionAfterApply = false;

    QLabel* m_settlement = nullptr;
    QLabel* m_lblTitle = nullptr;
    QLabel* m_lblSub = nullptr;
    QLabel* m_lblPartHint = nullptr;

    QComboBox* m_masterCombo = nullptr;
    QPushButton* m_btnSaveMaster = nullptr;
    QPushButton* m_btnAddService = nullptr;
    QPushButton* m_btnEditService = nullptr;
    QPushButton* m_btnAddPart = nullptr;
    QPushButton* m_btnRepairAi = nullptr;
    QPushButton* m_btnCustomerParts = nullptr;
    QPushButton* m_btnEditPart = nullptr;
    QPushButton* m_btnImportExcel = nullptr;
    QPushButton* m_btnOutsource = nullptr;
    QPushButton* m_btnPartPercent = nullptr;
    QPushButton* m_btnPdf = nullptr;
    QPushButton* m_btnCloseOrder = nullptr;
    QPushButton* m_btnDialogOk = nullptr;
    QPushButton* m_btnDialogCancel = nullptr;

    QTableWidget* m_servicesTable = nullptr;
    QTableWidget* m_partsTable = nullptr;
    QSplitter* m_split = nullptr;
};
