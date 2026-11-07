#include "ui/tabs/orders/WorkPartsDialog.h"
#include "ui/tabs/orders/WorkPartsDialogAttachmentSupport.h"
#include "ui/tabs/orders/WorkPartsDialogExcelSupport.h"
#include "ui/tabs/orders/PartImportDraftUiSupport.h"
#include "ui/common/dialogs/OrderDialog.h"
#include "ui/theme/ThemeManager.h"
#include "core/services/LegacyAncillaryMutationLogSupport.h"
#include "ui/common/widgets/ImportFlightAnimation.h"
#include "ui/common/AdaptiveUi.h"

#include "core/services/orders/WorkPartsPersistenceRepositoryFacade.h"
#include "core/services/orders/WorkPartsReadQueryService.h"
#include "core/services/orders/WorkPartsQueryService.h"
#include "core/services/OrderIdentityResolver.h"
#include "core/services/orders/OrderStatusService.h"
#include "core/services/orders/ServiceBatchMutationService.h"
#include "core/services/orders/PartMutationService.h"
#include "core/services/orders/OrderMutationFacade.h"
#include "core/services/orders/WorkPartsMutationService.h"
#include "core/services/orders/OutsourceJobService.h"
#include "core/services/MutationExecutor.h"
#include "core/services/AppRuntimeService.h"
#include "core/services/settings/UiStateSettingsService.h"
#include "core/services/settings/UiSettingsService.h"
#include "core/services/settings/FinanceSettingsService.h"
#include "core/services/settings/PersonnelSettingsService.h"
#include "core/services/settings/OutsourceDirectorySettingsService.h"
#include "core/services/orders/PartServiceAssignmentService.h"
#include "core/services/orders/PartImportTextSupport.h"
#include "core/services/OrderWorkspaceService.h"
#include "core/util/FormatUtils.h"
#include "core/util/OilServiceClassifier.h"
#include "core/util/PlateUtils.h"
#include "app/App.h"
#include "core/backup/CrashRecoveryManager.h"
#include "core/backup/BackupCommitPublisher.h"
#include "core/backup/BackupPerformanceGuard.h"
#include "runtime/RuntimeJobDispatcher.h"
#include "runtime/AsyncThreadScope.h"
#include "app/AppContext.h"

#include <QByteArray>
#include <QComboBox>
#include <QCheckBox>
#include <QCompleter>
#include <QCoreApplication>
#include <QCursor>
#include <QDate>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QHash>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMenu>
#include <QMetaObject>
#include <QMouseEvent>
#include <QFocusEvent>
#include <QFontInfo>
#include <QHideEvent>
#include <QPainter>
#include <QPainterPath>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateEdit>
#include <QDoubleSpinBox>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QPixmap>
#include <QScrollArea>
#include <QStyle>
#include <QUrl>
#include <QFormLayout>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressDialog>
#include <QScrollBar>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QUuid>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSet>
#include <QStyledItemDelegate>
#include <QApplication>
#include <QAbstractButton>
#include <QSplitter>
#include <QSqlError>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>
#include <QJsonValue>
#include <QJsonParseError>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QScopedValueRollback>
#include <QScopeGuard>
#if defined(CS_HAS_QT_PDF) && CS_HAS_QT_PDF
#include <QPdfDocument>
#include <QPdfSelection>
#endif
#include <algorithm>
#include <initializer_list>
#include <functional>
#include <memory>

#include "ui/common/filters/ManualInputCorrectionFilter.h"

using namespace WorkPartsDialogExcelSupport;
using namespace WorkPartsDialogAttachmentSupport;

namespace {
constexpr int RoleId = Qt::UserRole + 1;
constexpr int RoleIsHint = Qt::UserRole + 2;
constexpr int RolePurchasePrice = Qt::UserRole + 3;
constexpr int RoleMarkupPercent = Qt::UserRole + 4;
constexpr int RoleSpacer = Qt::UserRole + 5;
constexpr int RoleViewportFill = Qt::UserRole + 6;
constexpr int RoleDeliveryDate = Qt::UserRole + 7;
constexpr int RoleAttachmentPath = Qt::UserRole + 8;
constexpr int RoleAttachmentName = Qt::UserRole + 9;
constexpr int RoleAttachmentSource = Qt::UserRole + 10;
constexpr int RoleAttachmentDocumentNumber = Qt::UserRole + 11;
constexpr int RoleAttachmentDocumentDate = Qt::UserRole + 12;
constexpr int RolePartManufacturer = Qt::UserRole + 13;
constexpr int RoleImportedInvoiceRow = Qt::UserRole + 14;
constexpr int RoleImportedInvoiceTrace = Qt::UserRole + 15;
constexpr int RoleManualMarkupOverride = Qt::UserRole + 16;

constexpr int ServiceColIndex = 0;
constexpr int ServiceColTitle = 1;
constexpr int ServiceColPrice = 2;
constexpr int ServiceColQty = 3;
constexpr int ServiceColMaster = 4;
constexpr int ServiceColAttachment = 5;

constexpr int PartColIndex = 0;
constexpr int PartColDelivered = 1;
constexpr int PartColOrderedAt = 2;
constexpr int PartColOem = 3;
constexpr int PartColName = 4;
constexpr int PartColQty = 5;
constexpr int PartColPrice = 6;
constexpr int PartColAmount = 7;
constexpr int PartColStore = 8;
constexpr int PartColBrand = 9;
constexpr int PartColInvoiceNumber = 10;
constexpr int PartColAttachment = 11;

constexpr int kDefaultTableRowHeight = 40;

constexpr const char* kWorksHeaderStateKey = "work_parts_dialog/works_header_state_v3";
constexpr const char* kPartsHeaderStateKey = "work_parts_dialog/parts_header_state_v5";
constexpr const char* kWorksColumnWidthsKey = "work_parts_dialog/works_column_widths_v1";
constexpr const char* kPartsColumnWidthsKey = "work_parts_dialog/parts_column_widths_v2";
constexpr const char* kWorksRowHeaderWidthKey = "work_parts_dialog/works_row_header_width_v1";
constexpr const char* kSplitterStateKey = "work_parts_dialog/splitter_state";


struct OutsourceCompanyEntry {
    QString id;
    QString company;
    QString address;
    QString contact;
    QString branch;
    QString note;
    double markup = 20.0;
};

struct OutsourceJobDraft {
    QDate sentDate = QDate::currentDate();
    QDate returnDate;
    QString partName;
    double removePrice = 0.0;
    double companyPrice = 0.0;
    double markup = 20.0;
    double clientPrice = 0.0;
    double installPrice = 0.0;
    QString note;
};

} // namespace

bool rowHasHintMarker(QTableWidget* table, int row);
bool rowIsPureViewportFill(QTableWidget* table, int row);

// Cross-module helper declarations for inline editor controller after P2 split.
double parseLooseNumber(QString text, bool* ok);
QString normalizeSentence(const QString& s);
QString normalizePartSentence(const QString& value);
QString canonicalizeServiceTitle(const QString& title);
QString currentOrderDateDisplay();
QString displayDateFromStored(const QString& raw);
QString storedDateFromDisplay(const QString& raw);
QStringList loadPartStoreNamesFromSettings();
QString resolvedOrderUidForUi(int orderNumber);
bool ensureOrderReadyForEditing(int orderNumber, QString* errorText);
bool isServiceHintValue(const QString& text);
bool isPartHintValue(const QString& text);
bool isSpacerItem(const QTableWidgetItem* item);
bool rowHasMeaningfulServiceData(QTableWidget* table, int row);
bool rowHasMeaningfulPartData(QTableWidget* table, int row);
bool shouldShowPartDeliveredCheckbox(QTableWidget* table, int row);
bool partRowHasImportedInvoiceMarker(QTableWidget* table, int row);
void syncPartDeliveredCellState(QTableWidget* table, int row);
void syncPartManufacturerRoleFromRow(QTableWidget* table, int row);
void updateSingleMainPartsRowHeight(QTableWidget* table, int row);
void restoreHeaderState(QHeaderView* header, const QString& key);
void bindHeaderStatePersistence(QHeaderView* header, const QString& key);
void refreshPartRowClientPricing(QTableWidget* table, int row, int changedColumn = -1);
void markImportedInvoiceRow(QTableWidget* table, int row, const QString& traceId);
void applyImportedCellValueLikeManualInput(QTableWidget* table, int row, int col, const QString& rawValue);
QTableWidgetItem* makePartIndexItem(const QString& text);
QTableWidgetItem* makePartDeliveredItem(bool checked, int partId, bool enabled = true);
QTableWidgetItem* makeAttachmentItem(const QString& path, const QString& displayName = QString());
QTableWidgetItem* makeInvoiceNumberItem(const QString& invoiceNumber = QString(),
                                        const QString& path = QString(),
                                        const QString& invoiceDate = QString());


#include "ui/tabs/orders/WorkPartsInlineEditorController.h"


// P1/pass12 WorkPartsDialog row-index closure: keep id -> row maps for the legacy
// editor widgets so selection/draft restore paths do not scan every table row.
int WorkPartsDialog::serviceRowById(int serviceId) const
{
    if (serviceId <= 0) return -1;
    const auto it = m_serviceRowById.constFind(serviceId);
    if (it == m_serviceRowById.constEnd()) return -1;
    const int row = it.value();
    if (!m_servicesTable || row < 0 || row >= m_servicesTable->rowCount()) return -1;
    return row;
}

int WorkPartsDialog::partRowById(int partId) const
{
    if (partId <= 0) return -1;
    const auto it = m_partRowById.constFind(partId);
    if (it == m_partRowById.constEnd()) return -1;
    const int row = it.value();
    if (!m_partsTable || row < 0 || row >= m_partsTable->rowCount()) return -1;
    return row;
}

void WorkPartsDialog::clearWorkPartsRowIndexes()
{
    m_serviceRowById.clear();
    m_partRowById.clear();
}

void WorkPartsDialog::indexServiceRow(int serviceId, int row)
{
    if (serviceId > 0 && row >= 0) m_serviceRowById.insert(serviceId, row);
}

void WorkPartsDialog::indexPartRow(int partId, int row)
{
    if (partId > 0 && row >= 0) m_partRowById.insert(partId, row);
}

bool WorkPartsDialog::editOutsourceJob(QWidget* parent, int orderNumber, int serviceId)
{
    WorkPartsDialog dlg(parent, orderNumber);
    return dlg.runEditOutsourceJobFlow(parent, orderNumber, serviceId);
}

#include "core/services/AccessControlService.h"

WorkPartsDialog::WorkPartsDialog(QWidget* parent, int orderNumber)
    : QDialog(parent), m_orderNumber(orderNumber)
{
    if (!AccessControlService::canAccessOrder( orderNumber)) {
        setEnabled(false);
        QTimer::singleShot(0, this, &QDialog::reject);
        return;
    }
    QString routeError;
    if (!ensureOrderReadyForEditing(m_orderNumber, &routeError)) {
        QMessageBox::critical(this,
                              QString::fromUtf8("Помилка"),
                              QString::fromUtf8("Не вдалося підготувати наряд для редагування.\n%1").arg(routeError));
        QTimer::singleShot(0, this, &QDialog::reject);
        return;
    }
    setWindowTitle(QString::fromUtf8("Роботи та запчастини"));
    BackupPerformanceGuard::hold(AppContext::instance().backupManager(), 3000);
    setModal(false);
    setWindowModality(Qt::NonModal);

    m_cachedOrderUid = resolvedOrderUidForUi(m_orderNumber).trimmed();
    buildUi();
    QObject::connect(FinanceSettingsService::markupSettingsBroadcaster(),
                     &FinanceSettingsService::MarkupSettingsBroadcaster::markupSettingsChanged,
                     this,
                     [this]() {
                         if (!m_partsTable) return;
                         for (int row = 0; row < m_partsTable->rowCount(); ++row) {
                             if (!rowHasMeaningfulPartData(m_partsTable, row)) continue;
                             refreshPartRowClientPricing(m_partsTable, row, -1);
                         }
                     },
                     Qt::QueuedConnection);
    loadHeader();
    loadMasterChoice();
    // P0 performance: do not rebuild WorkParts tables synchronously inside the
    // dialog constructor. Showing the shell first removes the visible open-dialog
    // freeze; the actual table population is coalesced and applied on the next
    // UI turn through the same guarded refresh path used after mutations.
    scheduleWorkPartsRefresh(true,
                             true,
                             false,
                             true,
                             QStringLiteral("initial_open"),
                             true,
                             true);

    if (auto* crash = AppContext::instance().crashRecoveryManager()) {
        crash->registerParticipant(this);
        m_recoveryRegistered = true;
    }

    if (parent) {
        const QRect pg = parent->geometry();
        resize(pg.size());
        move(pg.topLeft());
    } else {
        showMaximized();
    }

    QPointer<WorkPartsDialog> self(this);
    QTimer::singleShot(0, this, [self]() {
        if (!self) return;
        if (!self->m_embedded) AdaptiveUi::fitTopLevelWindow(self, QSize(1540, 920), QSize(1120, 680));
    });
}


void WorkPartsDialog::scheduleWorkPartsRefresh(bool reloadServices,
                                               bool reloadParts,
                                               bool reloadHeader,
                                               bool ensureServiceSelection,
                                               const QString& reason,
                                               bool clearUserTouchedAfterApply,
                                               bool focusFirstSectionAfterApply)
{
    // P0 continuation: multiple save/import/error paths used to call loadServices()
    // and loadPartsForActiveService() synchronously and repeatedly. Coalesce those
    // requests into the latest generation so only one full table refresh is applied.
    m_pendingReloadServices = m_pendingReloadServices || reloadServices;
    m_pendingReloadParts = m_pendingReloadParts || reloadParts;
    m_pendingReloadHeader = m_pendingReloadHeader || reloadHeader;
    m_pendingEnsureServiceSelection = m_pendingEnsureServiceSelection || ensureServiceSelection;
    m_pendingClearUserTouchedAfterApply = m_pendingClearUserTouchedAfterApply || clearUserTouchedAfterApply;
    m_pendingFocusFirstSectionAfterApply = m_pendingFocusFirstSectionAfterApply || focusFirstSectionAfterApply;

    const quint64 generation = ++m_workPartsRefreshGeneration;
    const quint64 draftRevision = m_draftEditRevision;
    const QString owner = QStringLiteral("WorkPartsDialog:%1:%2").arg(m_orderNumber).arg(reason.trimmed().isEmpty() ? QStringLiteral("refresh") : reason.trimmed());
    const auto ticket = RuntimeJob::RuntimeJobDispatcher::instance().startLatest(
        RuntimeJob::Family::WorkPartsDialog,
        owner,
        30000,
        10);

    struct WorkPartsRefreshPayload {
        bool reloadServices = false;
        bool reloadParts = false;
        QList<WorkOrderServiceRow> serviceRows;
        QList<WorkOrderPartRow> partRows;
        QHash<int, QString> outsourceDisplayNames;
        QHash<int, OrderAttachmentService::AttachmentInfo> serviceAttachments;
        QHash<int, OrderAttachmentService::AttachmentInfo> partAttachments;
        QString primaryMaster;
        QString errorText;
    };

    const bool requestedReloadServices = m_pendingReloadServices;
    const bool requestedReloadParts = m_pendingReloadParts;
    const bool requestedReloadHeader = m_pendingReloadHeader;
    const bool requestedEnsureServiceSelection = m_pendingEnsureServiceSelection;
    const bool requestedClearUserTouchedAfterApply = m_pendingClearUserTouchedAfterApply;
    const bool requestedFocusFirstSectionAfterApply = m_pendingFocusFirstSectionAfterApply;
    const int orderNumber = m_orderNumber;
    const QString orderUid = currentOrderUid();
    const QString primaryMaster = currentPrimaryMasterName(m_masterCombo);

    m_pendingReloadServices = false;
    m_pendingReloadParts = false;
    m_pendingReloadHeader = false;
    m_pendingEnsureServiceSelection = false;
    m_pendingClearUserTouchedAfterApply = false;
    m_pendingFocusFirstSectionAfterApply = false;

    QPointer<WorkPartsDialog> self(this);
    auto* worker = AsyncThreadScope::startWorker(this,
                                                 QStringLiteral("workparts_dialog_refresh_%1_%2").arg(orderNumber).arg(generation),
                                                 ticket.cancelToken,
                                                 [self,
                                                  ticket,
                                                  generation,
                                                  draftRevision,
                                                  orderNumber,
                                                  orderUid,
                                                  primaryMaster,
                                                  requestedReloadServices,
                                                  requestedReloadParts,
                                                  requestedReloadHeader,
                                                  requestedEnsureServiceSelection,
                                                  requestedClearUserTouchedAfterApply,
                                                  requestedFocusFirstSectionAfterApply](const std::shared_ptr<std::atomic_bool>& cancelToken) mutable {
        if (!RuntimeJob::RuntimeJobDispatcher::instance().shouldApply(ticket) || AsyncThreadScope::isCancellationRequested(cancelToken)) {
            RuntimeJob::RuntimeJobDispatcher::instance().finish(ticket, RuntimeJob::Status::Cancelled);
            return;
        }

        RuntimeJob::RuntimeJobDispatcher::instance().markRunning(ticket);
        WorkPartsRefreshPayload payload;
        payload.reloadServices = requestedReloadServices;
        payload.reloadParts = requestedReloadParts;
        payload.primaryMaster = primaryMaster;

        if (requestedReloadServices && !AsyncThreadScope::isCancellationRequested(cancelToken)) {
            payload.outsourceDisplayNames = WorkPartsReadQueryService::loadOutsourceServiceDisplayNames(orderUid);
            payload.serviceRows = WorkPartsReadQueryService::listServicesByInternalNumber(orderNumber, &payload.errorText);
            std::stable_sort(payload.serviceRows.begin(), payload.serviceRows.end(), [&payload](const WorkOrderServiceRow& a, const WorkOrderServiceRow& b) {
                const bool aOutsource = payload.outsourceDisplayNames.contains(a.id);
                const bool bOutsource = payload.outsourceDisplayNames.contains(b.id);
                if (aOutsource != bOutsource) return !aOutsource && bOutsource;
                return a.id < b.id;
            });
            QList<int> serviceIds;
            serviceIds.reserve(payload.serviceRows.size());
            for (const auto& service : payload.serviceRows) {
                if (service.id > 0) serviceIds.push_back(service.id);
            }
            payload.serviceAttachments = WorkPartsReadQueryService::serviceAttachmentMap(serviceIds);
        }

        if (requestedReloadParts && !AsyncThreadScope::isCancellationRequested(cancelToken)) {
            payload.partRows = WorkPartsReadQueryService::listPartsByInternalNumber(orderNumber, &payload.errorText);
            QList<int> partIds;
            partIds.reserve(payload.partRows.size());
            for (const auto& part : payload.partRows) {
                if (part.id > 0) partIds.push_back(part.id);
            }
            payload.partAttachments = WorkPartsReadQueryService::partAttachmentMap(partIds);
        }

        if (AsyncThreadScope::isCancellationRequested(cancelToken) || !RuntimeJob::RuntimeJobDispatcher::instance().shouldApply(ticket)) {
            RuntimeJob::RuntimeJobDispatcher::instance().finish(ticket, RuntimeJob::Status::Cancelled);
            return;
        }
        if (self.isNull()) {
            RuntimeJob::RuntimeJobDispatcher::instance().finish(ticket, RuntimeJob::Status::Cancelled);
            return;
        }

        QMetaObject::invokeMethod(self.data(), [self,
                                                ticket,
                                                generation,
                                                draftRevision,
                                                requestedReloadHeader,
                                                requestedEnsureServiceSelection,
                                                requestedClearUserTouchedAfterApply,
                                                requestedFocusFirstSectionAfterApply,
                                                payload = std::move(payload)]() mutable {
            if (!self) {
                RuntimeJob::RuntimeJobDispatcher::instance().finish(ticket, RuntimeJob::Status::Cancelled);
                return;
            }
            if (generation != self->m_workPartsRefreshGeneration
                || draftRevision != self->m_draftEditRevision
                || RuntimeJob::RuntimeJobDispatcher::instance().isShutdownRequested()
                || !RuntimeJob::RuntimeJobDispatcher::instance().shouldApply(ticket)) {
                RuntimeJob::RuntimeJobDispatcher::instance().finish(ticket, RuntimeJob::Status::Cancelled);
                return;
            }

            if (requestedReloadHeader) {
                self->loadHeader();
            }
            if (payload.reloadServices) {
                self->applyServicesSnapshot(payload.serviceRows,
                                            payload.outsourceDisplayNames,
                                            payload.serviceAttachments,
                                            payload.primaryMaster);
                if (requestedEnsureServiceSelection) {
                    self->ensureServiceSelected();
                }
            } else if (requestedEnsureServiceSelection) {
                self->ensureServiceSelected();
            }
            if (payload.reloadParts) {
                self->applyPartsSnapshot(payload.partRows, payload.partAttachments);
            }
            if (requestedClearUserTouchedAfterApply) {
                self->m_userTouchedPendingContent = false;
            }
            if (requestedFocusFirstSectionAfterApply) {
                self->focusSection(0);
            }

            RuntimeJob::RuntimeJobDispatcher::instance().finish(
                ticket,
                RuntimeJob::RuntimeJobDispatcher::instance().shouldApply(ticket) ? RuntimeJob::Status::Succeeded : RuntimeJob::Status::Cancelled);
        }, Qt::QueuedConnection);
    });

    QPointer<QThread> workerGuard(worker);
    if (ticket.timeoutMs > 0) {
        QTimer::singleShot(qMax(1000, ticket.timeoutMs), this, [workerGuard, ticket, generation]() {
            if (!workerGuard || workerGuard->isFinished()) return;
            if (!RuntimeJob::RuntimeJobDispatcher::instance().shouldApply(ticket)) return;
            if (ticket.cancelToken) ticket.cancelToken->store(true);
            workerGuard->requestInterruption();
            qWarning().noquote() << QStringLiteral("ASYNC_LIFECYCLE workparts_dialog_refresh_timeout_cancel generation=") << generation
                                 << QStringLiteral(" timeoutMs=") << ticket.timeoutMs;
        });
    }
    worker->start();
}


QString WorkPartsDialog::currentOrderUid(QString* errorText) const
{
    // P0 performance: this method is used by hot diagnostic and UI paths.
    // Resolve the canonical uid once and reuse it instead of hitting SQLite on
    // every log/event/cell edit.
    if (m_cachedOrderUid.trimmed().isEmpty()) {
        m_cachedOrderUid = resolvedOrderUidForUi(m_orderNumber).trimmed();
    }
    if (m_cachedOrderUid.isEmpty() && errorText) {
        *errorText = QStringLiteral("Canonical order uid not resolved for order %1").arg(m_orderNumber);
    }
    return m_cachedOrderUid;
}

int WorkPartsDialog::activeServiceId() const
{
    return m_activeServiceId;
}

// Decomposed WorkPartsDialog components


