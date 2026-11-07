#pragma once

// P2 architecture module prelude.
// Generated during monolith-removal p2 so converted implementation slices
// compile as separate .cpp modules without being text-included by the owner.
// Owner: src/ui/tabs/orders/WorkPartsDialog.cpp

#include "ui/tabs/orders/WorkPartsDialog.h"
#include "ui/tabs/orders/WorkPartsDialogAttachmentSupport.h"
#include "ui/tabs/orders/WorkPartsDialogExcelSupport.h"
#include "ui/tabs/orders/PartImportDraftUiSupport.h"
#include "ui/common/dialogs/OrderDialog.h"
#include "ui/theme/ThemeManager.h"
#include "core/services/LegacyAncillaryMutationLogSupport.h"
#include "core/diagnostics/StructuredDiagnosticsLogger.h"
#include "ui/common/widgets/ImportFlightAnimation.h"
#include "ui/common/AdaptiveUi.h"
#include "core/services/orders/PartRowPayloadBuilderService.h"
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
#include "core/perf/PerfWatchdog.h"
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
#include <QMouseEvent>
#include <QFocusEvent>
#include <QFontInfo>
#include <QHideEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPair>
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

// File-local helpers required by converted slices.
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


}

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

class ScopedOrderSaveBackupDisable final
{
public:
    ScopedOrderSaveBackupDisable()
    {
        if (!qApp) return;
        m_prev = qApp->property("csDisableOrderSaveBackup");
        qApp->setProperty("csDisableOrderSaveBackup", true);
    }

    ~ScopedOrderSaveBackupDisable()
    {
        if (!qApp) return;
        qApp->setProperty("csDisableOrderSaveBackup", m_prev);
    }

private:
    QVariant m_prev;
};

class ClearOnFocusSpinBox final : public QDoubleSpinBox
{
public:
    explicit ClearOnFocusSpinBox(QWidget* parent = nullptr) : QDoubleSpinBox(parent) {}

protected:
    void focusInEvent(QFocusEvent* event) override
    {
        QDoubleSpinBox::focusInEvent(event);
        QPointer<ClearOnFocusSpinBox> self(this);
        QTimer::singleShot(0, this, [self]() {
            if (!self) return;
            if (QLineEdit* edit = self->lineEdit()) {
                const QSignalBlocker blocker(edit);
                edit->clear();
                edit->setCursorPosition(0);
            }
        });
    }

    void focusOutEvent(QFocusEvent* event) override
    {
        if (QLineEdit* edit = lineEdit(); edit && edit->text().trimmed().isEmpty()) {
            QDoubleSpinBox::setValue(0.0);
        }
        QDoubleSpinBox::focusOutEvent(event);
    }
};
// Cross-module helpers exported by WorkPartsDialogSupport/Pricing/Outsource modules.
double parseLooseNumber(QString text, bool* ok);
QString normalizeSentence(const QString& s);
QString normalizePartSentence(const QString& value);
QString canonicalizeServiceTitle(const QString& title);
QString currentOrderDateDisplay();
QString displayDateFromStored(const QString& raw);
QString storedDateFromDisplay(const QString& raw);
QStringList loadPartStoreNamesFromSettings();
QHash<int, QString> loadOutsourceServiceDisplayNames(int orderNumber);
double effectivePartMarkupPercent(const QString& partName, const QString& store);
QString effectiveOilBrand(const QString& partName, const QString& store);
double applyMarkup(double purchasePrice, double pct);
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
void publishWorkPartsBackupEvent(const QString& page,
                                 const QString& scope,
                                 const QString& entityId,
                                 const QString& entityUid,
                                 int orderNumber,
                                 const QString& operationType,
                                 const QString& description,
                                 const QStringList& changedTables);

bool promoteOrderStatusToInProgressIfNeeded(const QString& connectionName, int orderNumber, QString* errorText = nullptr);
void installSentenceCaseAutoCap(QLineEdit* edit);
QString localizedWorkOrderStatusDisplay(const QString& value);
QString mainDialogTextColor();
QString normalizeInvoiceDisplayNumber(QString raw);
QString attachmentToolTipText(const QString& path,
                              const QString& invoiceNumber = QString(),
                              const QString& invoiceDate = QString());
QString partManufacturerText(QTableWidget* table, int row);
QString clippedImportLogText(QString value, int maxLen = 180);
bool rowIsRemovableBlankPartRow(QTableWidget* table, int row);
void setRowEditingVisual(QTableWidget* table, int row);
void clearRowHintState(QTableWidget* table, int row);
void logPartCrashContext(const QString& category,
                         const QString& action,
                         QTableWidget* table,
                         int row,
                         int col,
                         int orderNumber,
                         const QString& orderUid,
                         const QString& note = QString());
QString writePartCrashSnapshot(const QString& stage,
                               QTableWidget* table,
                               int row,
                               int col,
                               int orderNumber,
                               const QString& orderUid,
                               const QString& note = QString());
void enforcePersistentInteractiveColumnWidths(QTableWidget* table, const QString& key);
void restoreHeaderFixedWidth(QHeaderView* header, const QString& key, int defaultWidth);
void restoreSplitterState(QSplitter* split, const QString& key);
void bindTableColumnWidthPersistence(QTableWidget* table, const QString& key);
void bindHeaderFixedWidthPersistence(QHeaderView* header, const QString& key);
void bindSplitterStatePersistence(QSplitter* split, const QString& key);
void persistHeaderState(QHeaderView* header, const QString& key);
void persistTableColumnWidths(QTableWidget* table, const QString& key);
void persistSplitterState(QSplitter* split, const QString& key);
bool ensureWorkPartsDialogDbReady(QWidget* parent, bool refreshRouting, QString* errorText = nullptr);
bool chooseOutsourceCompanyAndFill(QWidget* parent, int orderNumber, OutsourceCompanyEntry& selectedCompany, OutsourceJobDraft& draft);
bool clearOutsourceServiceMaster(int orderNumber, int serviceId, QWidget* parent);
bool saveOutsourceJob(int orderNumber, int serviceId, const OutsourceCompanyEntry& company, const OutsourceJobDraft& draft, QWidget* parent);
#include "ui/tabs/orders/WorkPartsInlineEditorController.h"
#include "ui/tabs/orders/WorkPartsPartEditServices.h"
