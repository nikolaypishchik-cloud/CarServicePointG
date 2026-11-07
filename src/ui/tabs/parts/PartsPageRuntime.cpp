#include "core/services/SupplierDebtReadService.h"
#include "core/util/DateFormats.h"
// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

#include "ui/tabs/parts/PartsPageModulePrelude.h"
#include "ui/tabs/parts/PartsSupplierDashboard.h"
#include "ui/common/dialogs/PurchasePaymentDialog.h"


#include "core/services/ActivityAuditService.h"
#include "core/services/AccessControlService.h"
#include "core/services/SupplierDebtReconciliationService.h"
#include "core/services/SupplierInvoiceLedger.h"

#include "core/util/StoreNameNormalizer.h"



#include <QUuid>

namespace {
QString displayStoreNameForPartsUi(const QString& rawStore,
                                  const QString& defaultValue = QString::fromUtf8("Без магазину"));

QString shortPartsRowKey(const PartRowSnapshot& rowData)
{
    const QString name = rowData.name.simplified().left(24);
    return QStringLiteral("%1|%2|%3")
        .arg(rowData.orderNumber)
        .arg(rowData.id)
        .arg(name);
}

bool currentActorIsMaster()
{
    return AccessControlService::currentActorIsMaster();
}
}

void PartsPage::queueRefreshAll(int delayMs)
{
    if (!m_refreshAllDebounce) {
        emit requestRefreshAll();
        return;
    }
    m_refreshAllDebounce->start(qMax(0, delayMs));
}

void PartsPage::restoreTableHeaderState()
{
    if (!m_table || !m_table->horizontalHeader()) return;

    const auto applySavedState = [this]() {
        if (!m_table || !m_table->horizontalHeader()) return;
        auto* header = m_table->horizontalHeader();
        QSettings s;

        const QByteArray state = s.value(QString::fromLatin1(kHeaderStateKey)).toByteArray();
        if (!state.isEmpty()) {
            header->restoreState(state);
        }

        const QVariantList visualOrder = s.value(QString::fromLatin1(kHeaderVisualOrderKey)).toList();
        if (visualOrder.size() == header->count()) {
            QSignalBlocker blocker(header);
            for (int visual = 0; visual < visualOrder.size(); ++visual) {
                bool ok = false;
                const int logical = visualOrder[visual].toInt(&ok);
                if (!ok || logical < 0 || logical >= header->count()) continue;
                const int currentVisual = header->visualIndex(logical);
                if (currentVisual >= 0 && currentVisual != visual) {
                    header->moveSection(currentVisual, visual);
                }
            }
        }

        for (int logical = 0; logical < header->count(); ++logical) {
            const int savedWidth = s.value(QStringLiteral("%1/%2").arg(QString::fromLatin1(kHeaderWidthsPrefix)).arg(logical), -1).toInt();
            if (savedWidth > 0) {
                m_table->setColumnWidth(logical, savedWidth);
            }
        }

        if (m_table->columnWidth(ColPaid) < kPaidColumnMinWidth) {
            m_table->setColumnWidth(ColPaid, kPaidColumnMinWidth);
        }
        syncPaidColumnWidgetsWidth(m_table);
    };

    applySavedState();
    QTimer::singleShot(0, this, [this, applySavedState]() {
        applySavedState();
    });
}

void PartsPage::saveTableHeaderState()
{
    if (!m_table || !m_table->horizontalHeader()) return;
    auto* header = m_table->horizontalHeader();
    QSettings s;
    s.setValue(QString::fromLatin1(kHeaderStateKey), header->saveState());

    QVariantList visualOrder;
    visualOrder.reserve(header->count());
    for (int visual = 0; visual < header->count(); ++visual) {
        visualOrder.push_back(header->logicalIndex(visual));
    }
    s.setValue(QString::fromLatin1(kHeaderVisualOrderKey), visualOrder);

    for (int logical = 0; logical < header->count(); ++logical) {
        s.setValue(QStringLiteral("%1/%2").arg(QString::fromLatin1(kHeaderWidthsPrefix)).arg(logical), m_table->columnWidth(logical));
    }
    s.sync();
}

void PartsPage::restoreDateRangeState()
{
    if (!m_dateFrom || !m_dateTo) return;

    const auto readSavedDate = [](const QVariant& value) -> QDate {
        if (!value.isValid()) return QDate();

        const QDate directDate = value.toDate();
        if (directDate.isValid()) return directDate;

        const QString raw = value.toString().trimmed();
        if (raw.isEmpty()) return QDate();

        QDate parsed = DateFormats::parseDate(raw);
        if (parsed.isValid()) return parsed;

        parsed = QDate::fromString(raw, QStringLiteral("yyyy-MM-dd"));
        if (parsed.isValid()) return parsed;

        parsed = QDate::fromString(raw, QStringLiteral("dd.MM.yy"));
        if (parsed.isValid()) return parsed;

        parsed = QDate::fromString(raw, QStringLiteral("dd.MM.yyyy"));
        return parsed.isValid() ? parsed : QDate();
    };

    QSettings s;
    QDate savedFrom = readSavedDate(s.value(QString::fromLatin1(kDateRangeFromKey)));
    QDate savedTo = readSavedDate(s.value(QString::fromLatin1(kDateRangeToKey)));

    if (!savedFrom.isValid() && !savedTo.isValid()) {
        m_lastAppliedFrom = m_dateFrom->date();
        m_lastAppliedTo = m_dateTo->date();
        m_dateRangeInitialized = true;
        return;
    }

    if (!savedFrom.isValid()) savedFrom = m_dateFrom->date();
    if (!savedTo.isValid()) savedTo = m_dateTo->date();

    const QSignalBlocker b1(m_dateFrom);
    const QSignalBlocker b2(m_dateTo);
    m_dateFrom->setDate(savedFrom);
    m_dateTo->setDate(savedTo);
    m_lastAppliedFrom = savedFrom;
    m_lastAppliedTo = savedTo;
    m_dateRangeInitialized = true;
}

void PartsPage::saveDateRangeState() const
{
    if (!m_dateFrom || !m_dateTo) return;

    const QDate from = m_dateFrom->date();
    const QDate to = m_dateTo->date();
    if (!from.isValid() || !to.isValid()) return;

    QSettings s;
    s.setValue(QString::fromLatin1(kDateRangeFromKey), from.toString(Qt::ISODate));
    s.setValue(QString::fromLatin1(kDateRangeToKey), to.toString(Qt::ISODate));
    s.sync();
}

int PartsPage::selectedPartId() const
{
    const QList<int> ids = selectedPartIds();
    return ids.isEmpty() ? -1 : ids.first();
}

int PartsPage::selectedOrderNumber() const
{
    if (!m_table || !m_table->selectionModel()) return -1;
    const auto rows = m_table->selectionModel()->selectedRows();
    if (rows.isEmpty()) return -1;
    const int row = rows.first().row();
    if (row < 0 || row >= m_rowsSnapshot.size()) return -1;
    return m_rowsSnapshot.at(row).orderNumber;
}

QString PartsPage::selectedOrderUid() const
{
    if (!m_table || !m_table->selectionModel()) return QString();
    const auto rows = m_table->selectionModel()->selectedRows();
    if (rows.isEmpty()) return QString();
    const int row = rows.first().row();
    if (row < 0 || row >= m_rowsSnapshot.size()) return QString();
    return m_rowsSnapshot.at(row).orderUid.trimmed();
}

QList<int> PartsPage::selectedPartIds() const
{
    QList<int> out;
    if (!m_table || !m_table->selectionModel()) return out;
    const QModelIndexList rows = m_table->selectionModel()->selectedRows();
    out.reserve(rows.size());
    for (const QModelIndex& idx : rows) {
        const int row = idx.row();
        if (row < 0 || row >= m_rowsSnapshot.size()) continue;
        const int id = m_rowsSnapshot.at(row).id;
        if (id > 0) out.append(id);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

int PartsPage::visibleRowForPartId(int partId) const
{
    if (!m_partsModel || partId <= 0) return -1;
    // P1/pass9 parts snapshot migration closure: the QAbstractTableModel owns visible-row indexes.
    return m_partsModel->rowByPartId(partId);
}

int PartsPage::firstVisibleRowForOrderUid(const QString& orderUid) const
{
    if (!m_partsModel) return -1;
    // P1/pass9 parts snapshot migration closure: no QTableView scan for order selection restore.
    return m_partsModel->firstRowByOrderUid(orderUid);
}

int PartsPage::firstVisibleRowForOrderNumber(int orderNumber) const
{
    if (!m_partsModel) return -1;
    // P1/pass9 parts snapshot migration closure: no QTableView scan for order selection restore.
    return m_partsModel->firstRowByOrderNumber(orderNumber);
}

void PartsPage::markDirty()
{
    m_dirty = true;
}

void PartsPage::refresh()
{
    const QString scopeKey = AccessControlService::currentScopeKey();
    if (m_actorScopeKey != scopeKey) {
        m_actorScopeKey = scopeKey;
        m_dirty = true;
        ++m_refreshGeneration;
        populateRows({});
        if (m_supplierDashboard) m_supplierDashboard->setBalances({});
        // The loaded snapshot schedules banner refresh; avoid a second full
        // synchronous parts query before starting the table's worker.
    }
    if (m_loaded && !m_dirty && !m_refreshWorker) return;
    if (m_refreshInProgress || m_refreshWorker) {
        AsyncTaskHandle::cancel(m_refreshCancelToken);
        if (m_refreshWorker) m_refreshWorker->requestInterruption();
        RuntimeJob::RuntimeJobDispatcher::instance().cancelFamily(RuntimeJob::Family::Parts);
        m_refreshQueued = true;
        m_dirty = true;
        return;
    }

    m_refreshInProgress = true;
    startAsyncRefresh();
}

void PartsPage::loadRows()
{
    markDirty();
    refresh();
}

void PartsPage::populateRows(const QVector<PartRowSnapshot>& rows)
{
    if (!m_table || !m_partsModel) return;

    PartsPageTableModelAdapter::ViewPolicy viewPolicy;
    viewPolicy.textFilter = m_search ? m_search->text().trimmed() : QString();
    viewPolicy.from = m_dateFrom ? m_dateFrom->date() : QDate();
    viewPolicy.to = m_dateTo ? m_dateTo->date() : QDate();
    viewPolicy.today = QDate::currentDate();
    viewPolicy.paymentActionsEnabled = m_paymentActionsEnabled;
    const PartsPageTableModelAdapter::AdaptedRows adaptedRows =
        PartsPageTableModelAdapter::build(rows, viewPolicy);

    m_rowsSnapshot = adaptedRows.snapshots;
    m_rowsSnapshotById.clear();
    m_rowsSnapshotById.reserve(m_rowsSnapshot.size());
    for (int row = 0; row < m_rowsSnapshot.size(); ++row) {
        const PartRowSnapshot& rowData = m_rowsSnapshot.at(row);
        if (rowData.id > 0) {
            m_rowsSnapshotById.insert(rowData.id, rowData);
        }
    }
    m_rowHeightCache.clear();

    const bool wasUpdatesEnabled = m_table->updatesEnabled();
    // A frozen parent already suppresses painting. Setting false on the table
    // in that state would give it a local lock that survives the parent's thaw.
    if (wasUpdatesEnabled) m_table->setUpdatesEnabled(false);
    const QSignalBlocker tableBlocker(m_table);
    const std::unique_ptr<QSignalBlocker> selectionBlocker(
        m_table->selectionModel() ? new QSignalBlocker(m_table->selectionModel()) : nullptr);
    m_table->setSortingEnabled(false);
    m_partsModel->setRows(adaptedRows.rows);
    if (wasUpdatesEnabled) m_table->setUpdatesEnabled(true);

    if (auto* hh = m_table->horizontalHeader()) hh->setSortIndicatorShown(false);
    syncPaidColumnWidgetsWidth(m_table);
    if (m_table->viewport()) m_table->viewport()->update();
}

void PartsPage::populateTableRow(int row, const PartsPageTableModelAdapter::TableRow& rowData)
{
    if (!m_partsModel || row < 0) return;
    m_partsModel->replaceRow(row, rowData);
}

void PartsPage::startAsyncRefresh()
{
    const int selectedPartBefore = selectedPartId();
    const int selectedOrderBefore = selectedOrderNumber();
    const QString selectedOrderUidBefore = selectedOrderUid();
    const int vScrollBefore = (m_table && m_table->verticalScrollBar()) ? m_table->verticalScrollBar()->value() : 0;
    const int hScrollBefore = (m_table && m_table->horizontalScrollBar()) ? m_table->horizontalScrollBar()->value() : 0;
    const QString filter = m_search ? m_search->text().trimmed() : QString();
    const QDate from = m_dateFrom ? m_dateFrom->date() : QDate();
    const QDate to = m_dateTo ? m_dateTo->date() : QDate();
    const bool masterOnlyView = currentActorIsMaster();
    const QString scopeKey = AccessControlService::currentScopeKey();
    const QString masterAccountId = masterOnlyView ? ActivityAuditService::currentActor().accountId.trimmed() : QString();
    m_paymentActionsEnabled = !masterOnlyView;
    const int generation = ++m_refreshGeneration;
    // RuntimeJobDispatcher::startLatest replaces AsyncTaskHandle::renewCancellationToken(m_refreshCancelToken)
    // and also cancels the previous Parts family job atomically.
    const auto runtimeTicket = RuntimeJob::RuntimeJobDispatcher::instance().startLatest(
        RuntimeJob::Family::Parts,
        QStringLiteral("PartsPage"),
        20000,
        60);
    m_refreshCancelToken = runtimeTicket.cancelToken;
    const auto cancelToken = m_refreshCancelToken;
    const AsyncTaskHandle asyncHandle(this, static_cast<quint64>(generation), cancelToken);

    PerfWatchdog::asyncJobStarted(QStringLiteral("parts-page"), QString::number(generation));

    struct AsyncResult {
        QVector<PartRowSnapshot> rows;
        PartsQueryService::PartArchiveDateBounds bounds;
        QString errorText;
    };
    const auto result = std::make_shared<AsyncResult>();
    auto* worker = AsyncThreadScope::startWorker(this,
                                               QStringLiteral("parts_page_refresh_%1").arg(generation),
                                               cancelToken,
                                               [result, filter, from, to, masterOnlyView, masterAccountId, runtimeTicket](const std::shared_ptr<std::atomic_bool>& cancelToken) {
        RuntimeJob::RuntimeJobDispatcher::instance().markRunning(runtimeTicket);
        RuntimeJob::RuntimeJobDispatcher::warnIfUiThreadBlockingCall(QStringLiteral("PartsQueryService::loadRows"));
        auto cancelled = [&cancelToken]() { return AsyncThreadScope::isCancellationRequested(cancelToken); };
        if (cancelled()) {
            AppRuntimeService::closeThreadConnection();
            return;
        }

        AsyncResult localResult;
        QString rowsError;
        localResult.rows = PartsQueryService::loadScopedRows(filter,from,to,masterOnlyView,masterAccountId,&rowsError);
        if (cancelled()) {
            AppRuntimeService::closeThreadConnection();
            return;
        }

        if (!rowsError.trimmed().isEmpty()) localResult.errorText = rowsError;
        if (!cancelled()) {
            *result = std::move(localResult);
        }
        AppRuntimeService::closeThreadConnection();
    });
    m_refreshWorker = worker;
    if (runtimeTicket.timeoutMs > 0) {
        const QPointer<QThread> workerGuard(worker);
        QTimer::singleShot(qMax(1000, runtimeTicket.timeoutMs), this, [workerGuard, cancelToken, runtimeTicket, generation]() {
            if (!workerGuard) return;
            if (!RuntimeJob::RuntimeJobDispatcher::instance().shouldApply(runtimeTicket)) return;
            if (cancelToken) cancelToken->store(true);
            workerGuard->requestInterruption();
            RuntimeJob::RuntimeJobDispatcher::instance().cancelFamily(runtimeTicket.family);
            qWarning().noquote() << QStringLiteral("ASYNC_LIFECYCLE parts_page_refresh_timeout_cancel generation=") << generation
                                 << QStringLiteral(" timeoutMs=") << runtimeTicket.timeoutMs;
        });
    }
    // This mutex-protected counter must finish before deferred worker deletion;
    // queued callbacks targeting the worker can be discarded with that object.
    connect(worker, &QThread::finished, worker, [generation]() {
        PerfWatchdog::asyncJobFinished(QStringLiteral("parts-page"), QString::number(generation));
    }, Qt::DirectConnection);
    connect(worker, &QThread::finished, this, [this, worker, result, asyncHandle, runtimeTicket, selectedPartBefore, selectedOrderBefore, selectedOrderUidBefore, vScrollBefore, hScrollBefore, scopeKey]() {
        if (m_refreshWorker == worker) m_refreshWorker = nullptr;
        const bool canApply = scopeKey == AccessControlService::currentScopeKey()
            && asyncHandle.shouldApply(static_cast<quint64>(m_refreshGeneration))
            && RuntimeJob::RuntimeJobDispatcher::instance().shouldApply(runtimeTicket);
        RuntimeJob::RuntimeJobDispatcher::instance().finish(runtimeTicket, canApply ? RuntimeJob::Status::Succeeded : RuntimeJob::Status::Cancelled);
        if (canApply) {
            applyAsyncSnapshot(result->rows, result->bounds, result->errorText,
                               selectedPartBefore, selectedOrderBefore, selectedOrderUidBefore,
                               vScrollBefore, hScrollBefore);
        } else {
            qWarning().noquote() << QStringLiteral("ASYNC_LIFECYCLE parts_page_refresh_result_detached generation=") << asyncHandle.generation();
            m_refreshInProgress = false;
            if (m_refreshQueued) {
                m_refreshQueued = false;
                QPointer<PartsPage> self(this);
                QTimer::singleShot(0, this, [self]() { if (self) self->refresh(); });
            }
        }
    });
    worker->start();
}

void PartsPage::applyAsyncSnapshot(const QVector<PartRowSnapshot>& rows,
                                   const PartsQueryService::PartArchiveDateBounds& bounds,
                                   const QString& errorText,
                                   int selectedPartBefore,
                                   int selectedOrderBefore,
                                   const QString& selectedOrderUidBefore,
                                   int vScrollBefore,
                                   int hScrollBefore)
{
    if (!errorText.trimmed().isEmpty()) {
        m_refreshInProgress = false;
        QMessageBox::warning(this, QString::fromUtf8("Помилка"), errorText);
        if (m_refreshQueued) {
            m_refreshQueued = false;
            QPointer<PartsPage> self(this);
            QTimer::singleShot(0, this, [self]() { if (self) self->refresh(); });
        }
        return;
    }

    if (m_dateFrom && m_dateTo) {
        Q_UNUSED(bounds);
        const QDate absoluteMin(1900, 1, 1);
        const QDate absoluteMax(9999, 12, 31);
        QDate wantedFrom = m_lastAppliedFrom.isValid() ? m_lastAppliedFrom : QDate::currentDate().addMonths(-1);
        QDate wantedTo = m_lastAppliedTo.isValid() ? m_lastAppliedTo : QDate::currentDate();
        if (!wantedFrom.isValid()) wantedFrom = QDate::currentDate().addMonths(-1);
        if (!wantedTo.isValid()) wantedTo = QDate::currentDate();
        const QSignalBlocker b1(m_dateFrom);
        const QSignalBlocker b2(m_dateTo);
        m_dateFrom->setDateRange(absoluteMin, absoluteMax);
        m_dateTo->setDateRange(absoluteMin, absoluteMax);
        m_dateFrom->setDate(wantedFrom);
        m_dateTo->setDate(wantedTo);
        m_lastAppliedFrom = wantedFrom;
        m_lastAppliedTo = wantedTo;
        m_dateRangeInitialized = true;
    }

    qInfo().noquote() << QStringLiteral("PARTS_PAY_RELOAD_PRESERVE scrollV=%1 scrollH=%2 row=%3")
                             .arg(vScrollBefore)
                             .arg(hScrollBefore)
                             .arg(m_table ? m_table->currentIndex().row() : -1);

    populateRows(rows);
    applyFilter();

    if (m_table) {
        const bool wasUpdatesEnabled = m_table->updatesEnabled();
        if (wasUpdatesEnabled) m_table->setUpdatesEnabled(false);
        const QSignalBlocker tableBlocker(m_table);
        const std::unique_ptr<QSignalBlocker> selectionBlocker(
            m_table->selectionModel() ? new QSignalBlocker(m_table->selectionModel()) : nullptr);
        int targetRow = -1;
        if (selectedPartBefore > 0) {
            targetRow = visibleRowForPartId(selectedPartBefore);
            if (targetRow >= 0 && m_table->isRowHidden(targetRow)) targetRow = -1;
        }
        if (targetRow < 0 && !selectedOrderUidBefore.trimmed().isEmpty()) {
            targetRow = firstVisibleRowForOrderUid(selectedOrderUidBefore);
        }
        if (targetRow < 0 && selectedOrderBefore > 0) {
            targetRow = firstVisibleRowForOrderNumber(selectedOrderBefore);
        }
        if (targetRow >= 0) {
            const QModelIndex targetIndex = m_table->model() ? m_table->model()->index(targetRow, ColName) : QModelIndex();
            if (targetIndex.isValid()) {
                m_table->setCurrentIndex(targetIndex);
                m_table->selectRow(targetRow);
            }
        }
        if (auto* hbar = m_table->horizontalScrollBar()) hbar->setValue(hScrollBefore);
        if (auto* vbar = m_table->verticalScrollBar()) vbar->setValue(vScrollBefore);
        if (wasUpdatesEnabled) m_table->setUpdatesEnabled(true);
        if (m_table->viewport()) m_table->viewport()->update();
    }

    m_loaded = true;
    m_dirty = false;
    m_refreshInProgress = false;
    scheduleDebtBannerUpdate(0);
    if (m_refreshQueued) {
        m_refreshQueued = false;
        QPointer<PartsPage> self(this);
        QTimer::singleShot(0, this, [self]() { if (self) self->refresh(); });
    }
}

void PartsPage::applyFilter()
{
    if (!m_table) return;
    // P1 PartsPage p4: text/date filtering is applied in PartsPageTableModelAdapter::ViewPolicy
    // before rows reach PartsArchiveTableModel, so the view no longer hides rows manually.
    scheduleRowHeightsUpdate(0);
    scheduleDebtBannerUpdate(120);
}

void PartsPage::syncDateRangeFromStorage()
{
    if (!m_dateFrom || !m_dateTo) return;

    const QDate absoluteMin(1900, 1, 1);
    const QDate absoluteMax(9999, 12, 31);
    QDate wantedFrom = m_lastAppliedFrom.isValid() ? m_lastAppliedFrom : QDate::currentDate().addMonths(-1);
    QDate wantedTo = m_lastAppliedTo.isValid() ? m_lastAppliedTo : QDate::currentDate();

    if (!wantedFrom.isValid()) wantedFrom = QDate::currentDate().addMonths(-1);
    if (!wantedTo.isValid()) wantedTo = QDate::currentDate();

    const QSignalBlocker b1(m_dateFrom);
    const QSignalBlocker b2(m_dateTo);
    m_dateFrom->setDateRange(absoluteMin, absoluteMax);
    m_dateTo->setDateRange(absoluteMin, absoluteMax);
    m_dateFrom->setDate(wantedFrom);
    m_dateTo->setDate(wantedTo);
    m_lastAppliedFrom = wantedFrom;
    m_lastAppliedTo = wantedTo;
    m_dateRangeInitialized = true;
}

void PartsPage::scheduleRowHeightsUpdate(int delayMs)
{
    if (!m_rowHeightsDebounce) {
        updateRowHeights();
        return;
    }
    if (delayMs <= 0) {
        m_rowHeightsDebounce->stop();
        m_rowHeightsDebounce->start(0);
        return;
    }
    if (!m_rowHeightsDebounce->isActive() || m_rowHeightsDebounce->remainingTime() > delayMs) {
        m_rowHeightsDebounce->start(delayMs);
    }
}

void PartsPage::updateRowHeights()
{
    if (!m_table) return;

    const int minRowHeight = 37;
    const int nameWidth = qMax(80, m_table->columnWidth(ColName) - 12);
    const QFontMetrics fm(m_table->font());

    int firstRow = 0;
    const int totalRows = m_partsModel ? m_partsModel->rowCount() : 0;
    int lastRow = totalRows - 1;
    if (totalRows > 200) {
        const QRect viewportRect = m_table->viewport()->rect();
        const QModelIndex topIndex = m_table->indexAt(viewportRect.topLeft());
        const QModelIndex bottomIndex = m_table->indexAt(viewportRect.bottomLeft());
        firstRow = qMax(0, topIndex.isValid() ? topIndex.row() - 20 : 0);
        lastRow = qMin(totalRows - 1, (bottomIndex.isValid() ? bottomIndex.row() + 24 : firstRow + 80));
    }

    for (int r = firstRow; r <= lastRow; ++r) {
        int rowHeight = m_rowHeightCache.value(r, -1);
        if (rowHeight <= 0) {
            rowHeight = minRowHeight;
            const QString nameText = (r >= 0 && r < m_rowsSnapshot.size()) ? m_rowsSnapshot.at(r).name : QString();
            if (!nameText.isEmpty()) {
                rowHeight = qMax(rowHeight, rowHeightForText(fm, nameText, nameWidth, minRowHeight));
            }
            m_rowHeightCache.insert(r, rowHeight);
        }
        if (m_table->rowHeight(r) != rowHeight) m_table->setRowHeight(r, rowHeight);
    }
}


bool PartsPage::applyIncrementalPaidToggle(int partId, bool newPaidState)
{
    if (!m_table || partId <= 0) return false;
    const int row = visibleRowForPartId(partId);
    if (row < 0) return false;

    auto snapshotIt = (row >= 0 && row < m_rowsSnapshot.size() && m_rowsSnapshot.at(row).id == partId)
        ? (m_rowsSnapshot.begin() + row)
        : std::find_if(m_rowsSnapshot.begin(), m_rowsSnapshot.end(), [partId](const PartRowSnapshot& rowData) {
              return rowData.id == partId;
          });
    if (snapshotIt == m_rowsSnapshot.end()) return false;

    const QString rowKey = shortPartsRowKey(*snapshotIt);
    Q_UNUSED(newPaidState);
    if (!PurchasePaymentService::refreshPayment(*snapshotIt)) return false;
    PartsQueryService::refreshDerivedFields(&(*snapshotIt));
    if (snapshotIt->id > 0) m_rowsSnapshotById.insert(snapshotIt->id, *snapshotIt);
    const PartsPageTableModelAdapter::TableRow adaptedRow =
        PartsPageTableModelAdapter::adaptRow(*snapshotIt, QDate::currentDate(), m_paymentActionsEnabled);

    const int vScrollBefore = m_table->verticalScrollBar() ? m_table->verticalScrollBar()->value() : 0;
    const int hScrollBefore = m_table->horizontalScrollBar() ? m_table->horizontalScrollBar()->value() : 0;
    const bool wasUpdatesEnabled = m_table->updatesEnabled();
    if (wasUpdatesEnabled) m_table->setUpdatesEnabled(false);
    const QSignalBlocker tableBlocker(m_table);
    const std::unique_ptr<QSignalBlocker> selectionBlocker(
        m_table->selectionModel() ? new QSignalBlocker(m_table->selectionModel()) : nullptr);
    populateTableRow(row, adaptedRow);
    if (wasUpdatesEnabled) m_table->setUpdatesEnabled(true);

    if (m_table->model()) m_table->setCurrentIndex(m_table->model()->index(row, ColPaid));
    m_table->selectRow(row);
    if (auto* hbar = m_table->horizontalScrollBar()) hbar->setValue(hScrollBefore);
    if (auto* vbar = m_table->verticalScrollBar()) vbar->setValue(vScrollBefore);
    if (m_table->viewport()) {
        m_table->viewport()->update(m_table->visualRect(m_table->model()->index(row, ColPaid)));
        m_table->viewport()->update(m_table->visualRect(m_table->model()->index(row, ColAmount)));
    }

    qInfo().noquote() << QStringLiteral("PARTS_PAY_UPDATE_IN_PLACE row=%1 key=%2")
                             .arg(row)
                             .arg(rowKey);
    return true;
}

void PartsPage::resortRowsAfterPaidToggle(int focusPartId)
{
    if (!m_table || !m_partsModel) return;
    const int vertical = m_table->verticalScrollBar()->value();
    const int horizontal = m_table->horizontalScrollBar()->value();
    // Reapply the same policy as a full refresh without rereading the database.
    const auto rows = m_rowsSnapshot;
    populateRows(rows);
    const int targetRow = visibleRowForPartId(focusPartId);
    if (targetRow >= 0) m_table->selectRow(targetRow);
    m_table->verticalScrollBar()->setValue(vertical);
    m_table->horizontalScrollBar()->setValue(horizontal);
    scheduleRowHeightsUpdate(0);
    scheduleDebtBannerUpdate(0);
}

void PartsPage::togglePaidForPart(int partId, bool newPaidState)
{
    if (partId <= 0 || !m_paymentActionsEnabled) return;

    if (m_refreshInProgress || m_refreshWorker) {
        qWarning().noquote() << QStringLiteral("ASYNC_LIFECYCLE parts_page_toggle_paid_cancel_refresh partId=") << partId
                             << QStringLiteral(" generation=") << m_refreshGeneration;
        AsyncTaskHandle::cancel(m_refreshCancelToken);
        if (m_refreshWorker) {
            m_refreshWorker->requestInterruption();
        }
        m_refreshQueued = false;
    }

    if (!newPaidState) {
        if (!confirmUnpayPart(this)) {
            return;
        }
    }

    const int row = visibleRowForPartId(partId);
    const QString rowKey = (row >= 0 && row < m_rowsSnapshot.size())
        ? shortPartsRowKey(m_rowsSnapshot.at(row))
        : QString::number(partId);
    qInfo().noquote() << QStringLiteral("PARTS_PAY_BEGIN row=%1 key=%2")
                             .arg(row)
                             .arg(rowKey);
    const auto paymentRow = (row >= 0 && row < m_rowsSnapshot.size())
        ? m_rowsSnapshot.at(row) : PartsQueryService::loadRowById(partId);
    if (newPaidState && !openPurchasePayment(this, QStringLiteral("part"), partId, paymentRow.orderUid)) return;
    if (!newPaidState && !PurchasePaymentService::resetPart(partId)) {
        QMessageBox::warning(this, QString::fromUtf8("Помилка"), QString::fromUtf8("Не вдалося змінити стан оплати запчастини."));
        return;
    }

    if (!applyIncrementalPaidToggle(partId, newPaidState)) {
        qWarning().noquote() << QStringLiteral("PARTS_PAGE_INCREMENTAL_REFRESH_FALLBACK partId=") << partId;
        markDirty();
        refresh();
        return;
    }

    PartsQueryService::invalidateCache();
    emit requestUnpaidBannerRefresh();
    resortRowsAfterPaidToggle(partId);
    m_dirty = false;

    // Keep this page stable after the modal payment flow. Other pages will refresh
    // from storage on their own navigation/open paths; this page has already been
    // patched in-place and must not trigger MainWindow::refreshAll().
}


namespace {
QString displayStoreNameForPartsUi(const QString& rawStore,
                                  const QString& defaultValue)
{
    const QString normalized = StoreNameNormalizer::canonicalDisplayNameOrDefault(rawStore, defaultValue);
    return StoreNameNormalizer::canonKey(normalized) == QStringLiteral("avtonova_d")
        ? QString::fromUtf8("Автонова-D")
        : normalized;
}

QString normalizeDebtBannerStoreName(const QString& rawStore)
{
    return displayStoreNameForPartsUi(rawStore, QString::fromUtf8("Без магазину"));
}

}

void PartsPage::scheduleDebtBannerUpdate(int delayMs)
{
    setProperty("debtDataLoading",true);
    if (!m_debtBannerDebounce) {
        updateDebtBanner();
        return;
    }
    if (delayMs <= 0) {
        m_debtBannerDebounce->stop();
        m_debtBannerDebounce->start(0);
        return;
    }
    if (!m_debtBannerDebounce->isActive() || m_debtBannerDebounce->remainingTime() > delayMs) {
        m_debtBannerDebounce->start(delayMs);
    }
}

void PartsPage::updateDebtBanner()
{
    if (!m_debtsBanner || !m_table) return;
    setProperty("debtDataLoading",true);
    setProperty("debtLoadError",QString());
    if(m_debtWorker) {
        AsyncTaskHandle::cancel(m_debtCancel);
        m_debtRefreshQueued=true;
        return;
    }
    const auto readTask=SupplierDebtReadService::prepareBanner();
    const auto scopeKey=AccessControlService::currentScopeKey();
    struct Result { QMap<QString,qint64> debts; QString error; };
    auto result=std::make_shared<Result>();
    auto cancel=std::make_shared<std::atomic_bool>(false); m_debtCancel=cancel;
    auto* worker=AsyncThreadScope::startWorker(this,"parts_debt_banner",cancel,[result,cancel,readTask] {
        result->debts=readTask(*cancel,&result->error);
    });
    m_debtWorker=worker;
    connect(worker,&QThread::finished,this,[this,result,cancel,scopeKey] {
        m_debtWorker=nullptr;
        if(m_debtRefreshQueued) { m_debtRefreshQueued=false; updateDebtBanner(); return; }
        setProperty("debtDataLoading",false);
        if(scopeKey!=AccessControlService::currentScopeKey()) return;
        if(cancel->load()) result->error=QString::fromUtf8("Завантаження боргів скасовано.");
        setProperty("debtLoadError",result->error);
        if(!result->error.isEmpty()) m_supplierDashboard->showError(result->error);
        else m_supplierDashboard->setBalances(result->debts);
    });
    worker->start();
}

void PartsPage::clearSearchStateForReturn()
{
    const bool hadLocalSearch = m_search && !m_search->text().isEmpty();

    if (hadLocalSearch && m_search) {
        const QSignalBlocker blocker(m_search);
        m_search->clear();
    }
    if (hadLocalSearch) {
        markDirty();
        refresh();
    }
}

void PartsPage::openSelectedOrder()
{
    QString routingError;
    int orderNumber = selectedOrderNumber();
    const QString orderUid = selectedOrderUid();
    if (!orderUid.trimmed().isEmpty()) {
        if (!ActiveArchiveRoutingLayer::prepareOrderForOpen(orderUid, orderNumber, &orderNumber, &routingError)) {
            QMessageBox::critical(this, QString::fromUtf8("Помилка"), routingError);
            return;
        }
    }
    if (orderNumber <= 0) {
        QMessageBox::information(this, QString::fromUtf8("Наряд"), QString::fromUtf8("Спочатку виберіть рядок із запчастиною."));
        return;
    }
    clearSearchStateForReturn();
    BackupPerformanceGuard::hold(AppContext::instance().backupManager(), 2500);
    OrderDialog dlg(this, orderNumber);
    dlg.openWorkPartsTab();
    if (dlg.exec() == QDialog::Accepted) {
        queueRefreshAll(0);
    }
}

void PartsPage::exportSelectedOrderPdf()
{
    QString routingError;
    int orderNumber = selectedOrderNumber();
    const QString orderUid = selectedOrderUid();
    if (!orderUid.trimmed().isEmpty()) {
        if (!ActiveArchiveRoutingLayer::prepareOrderForOpen(orderUid, orderNumber, &orderNumber, &routingError)) {
            QMessageBox::critical(this, QString::fromUtf8("Помилка"), routingError);
            return;
        }
    }
    if (orderNumber <= 0) {
        QMessageBox::information(this, QString::fromUtf8("PDF"), QString::fromUtf8("Спочатку виберіть рядок із запчастиною."));
        return;
    }
    BackupPerformanceGuard::hold(AppContext::instance().backupManager(), 2500);
    OrderDialog dlg(this, orderNumber);
    dlg.openWorkPartsTab();
    dlg.exportPdfPreviewAndOpen();
}
