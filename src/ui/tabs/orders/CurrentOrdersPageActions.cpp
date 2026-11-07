#include "ui/tabs/orders/OrderSettlementPresentation.h"
#include "core/services/orders/OrderClosureApplicationService.h"
#include "core/services/orders/OrderDialogReadQueryService.h"
// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

#include "ui/tabs/orders/CurrentOrdersPageModulePrelude.h"
#include "ui/common/TabularSnapshotTableModel.h"
#include "core/services/AccessControlService.h"
#include "ui/common/dialogs/InvoiceDialogSupport.h"
#include "ui/tabs/orders/BayOccupancyPage.h"
#include "core/services/OrderIdentityResolver.h"
#include "core/cloudsync/CloudSyncSettings.h"
#include "core/services/orders/LocalWorkshopBackend.h"

using CurrentOrdersSupport::modelColumnByHeader;
using CurrentOrdersSupport::confirmCurrentOrdersInvoiceColumnMapping;
using CurrentOrdersSupport::publishCurrentOrdersBackupEvent;
using CurrentOrdersSupport::ImportedPartDraft;
using CurrentOrdersSupport::ExcelReadResult;

bool editInvoiceColumnMapping(QWidget* parent, const QList<QStringList>& rows,
    const InvoiceImport::InvoiceColumnMappingPreview& preview, const QString& sourceFile,
    InvoiceImport::InvoiceColumnMappingPreview* confirmed)
{
    return confirmCurrentOrdersInvoiceColumnMapping(parent, rows, preview, sourceFile, confirmed);
}

int CurrentOrdersPage::selectedOrderNumber() const
{
    if (!m_table || !m_model || !m_proxy) return -1;
    const QModelIndex idx = m_table->currentIndex();
    if (!idx.isValid()) return -1;
    const int internalCol = modelColumnByHeader(m_model, QStringLiteral("__internal_number"));
    if (internalCol < 0) return -1;
    const QModelIndex srcIdx = m_proxy->mapToSource(idx.sibling(idx.row(), internalCol));
    if (!srcIdx.isValid()) return -1;
    bool ok = false;
    const int num = m_model->data(srcIdx).toInt(&ok);
    return ok ? num : -1;
}

QRect CurrentOrdersPage::partsCellRectForOrder(int orderNumber) const
{
    if (!m_table || !m_model || !m_proxy) return {};

    const int partsColumn = 8;
    int proxyRow = -1;

    if (orderNumber > 0) {
        const int sourceRow = m_model->rowByColumnValue(QStringLiteral("__internal_number"), orderNumber);
        if (sourceRow >= 0) {
            const QModelIndex proxyIndex = m_proxy->mapFromSource(m_model->index(sourceRow, 0));
            if (proxyIndex.isValid()) proxyRow = proxyIndex.row();
        }
    }

    if (proxyRow < 0 && m_table->currentIndex().isValid()) {
        proxyRow = m_table->currentIndex().row();
    }

    if (proxyRow >= 0) {
        const QRect cellRect = ImportFlightAnimation::tableViewCellRect(const_cast<CurrentOrdersPage*>(this), m_table, proxyRow, partsColumn);
        if (cellRect.isValid()) return cellRect;
    }
    return ImportFlightAnimation::tableViewColumnHintRect(const_cast<CurrentOrdersPage*>(this), m_table, partsColumn);
}

void CurrentOrdersPage::publishSettlement()
{
    const auto idx = m_table->currentIndex();
    if (!idx.isValid() || !m_table->selectionModel()->isRowSelected(idx.row(), {})) {
        emit settlementChanged(tr("Оберіть наряд для розрахунку"));
        return;
    }
    emit settlementChanged(QStringLiteral("№%1     %2").arg(idx.siblingAtColumn(0).data().toString(),
        OrderSettlementPresentation::summary(selectedOrderNumber(), selectedOrderUid())));
}

QString CurrentOrdersPage::selectedOrderUid() const
{
    if (!m_table || !m_model || !m_proxy) return QString();
    const QModelIndex idx = m_table->currentIndex();
    if (!idx.isValid()) return QString();
    const int uidCol = modelColumnByHeader(m_model, QStringLiteral("__order_uid"));
    if (uidCol < 0) return QString();
    const QModelIndex srcIdx = m_proxy->mapToSource(idx.sibling(idx.row(), uidCol));
    if (!srcIdx.isValid()) return QString();
    return m_model->data(srcIdx).toString().trimmed();
}

namespace {
QString foldSearchConfusablesLocal(QString value)
{
    value = value.simplified().trimmed().toLower();
    value.replace(QChar(0x0430), QLatin1Char('a'));
    value.replace(QChar(0x0432), QLatin1Char('b'));
    value.replace(QChar(0x0441), QLatin1Char('c'));
    value.replace(QChar(0x0435), QLatin1Char('e'));
    value.replace(QChar(0x043d), QLatin1Char('h'));
    value.replace(QChar(0x0456), QLatin1Char('i'));
    value.replace(QChar(0x0457), QLatin1Char('i'));
    value.replace(QChar(0x043a), QLatin1Char('k'));
    value.replace(QChar(0x043c), QLatin1Char('m'));
    value.replace(QChar(0x043e), QLatin1Char('o'));
    value.replace(QChar(0x0440), QLatin1Char('p'));
    value.replace(QChar(0x0442), QLatin1Char('t'));
    value.replace(QChar(0x0443), QLatin1Char('y'));
    value.replace(QChar(0x0445), QLatin1Char('x'));
    return value;
}

QString compactSearchLocal(QString value)
{
    value = foldSearchConfusablesLocal(value);
    value.remove(QRegularExpression(QStringLiteral(R"([^\p{L}\p{N}]+)")));
    return value;
}

QString compactSqlExprLocal(const QString& expr)
{
    QString sql = QStringLiteral("LOWER(COALESCE(%1,''))").arg(expr);
    sql = QStringLiteral("REPLACE(%1,'а','a')").arg(sql);
    sql = QStringLiteral("REPLACE(%1,'в','b')").arg(sql);
    sql = QStringLiteral("REPLACE(%1,'с','c')").arg(sql);
    sql = QStringLiteral("REPLACE(%1,'е','e')").arg(sql);
    sql = QStringLiteral("REPLACE(%1,'н','h')").arg(sql);
    sql = QStringLiteral("REPLACE(%1,'і','i')").arg(sql);
    sql = QStringLiteral("REPLACE(%1,'ї','i')").arg(sql);
    sql = QStringLiteral("REPLACE(%1,'к','k')").arg(sql);
    sql = QStringLiteral("REPLACE(%1,'м','m')").arg(sql);
    sql = QStringLiteral("REPLACE(%1,'о','o')").arg(sql);
    sql = QStringLiteral("REPLACE(%1,'р','p')").arg(sql);
    sql = QStringLiteral("REPLACE(%1,'т','t')").arg(sql);
    sql = QStringLiteral("REPLACE(%1,'у','y')").arg(sql);
    sql = QStringLiteral("REPLACE(%1,'х','x')").arg(sql);
    sql = QStringLiteral("REPLACE(%1,' ','')").arg(sql);
    sql = QStringLiteral("REPLACE(%1,'-','')").arg(sql);
    sql = QStringLiteral("REPLACE(%1,'_','')").arg(sql);
    return sql;
}

QString displayDateSqlExprLocal(const QString& expr)
{
    return QStringLiteral(
        "CASE "
        "WHEN TRIM(COALESCE(%1,''))='' THEN '' "
        "WHEN substr(TRIM(%1),3,1)='.' AND substr(TRIM(%1),6,1)='.' AND substr(TRIM(%1),9,1)='/' THEN substr(TRIM(%1),1,8) "
        "WHEN substr(TRIM(%1),3,1)='.' AND substr(TRIM(%1),6,1)='.' THEN substr(TRIM(%1),1,8) "
        "WHEN substr(TRIM(%1),5,1)='-' AND substr(TRIM(%1),8,1)='-' THEN substr(TRIM(%1),9,2) || '.' || substr(TRIM(%1),6,2) || '.' || substr(TRIM(%1),3,2) "
        "ELSE '' END").arg(expr);
}
}

QList<int> CurrentOrdersPage::selectedOrderNumbers() const
{
    QList<int> out;
    if (!m_table || !m_model || !m_proxy) return out;
    auto* sel = m_table->selectionModel();
    if (!sel) return out;

    const int internalCol = modelColumnByHeader(m_model, QStringLiteral("__internal_number"));
    if (internalCol < 0) return out;
    const QModelIndexList rows = sel->selectedRows(0);
    for (const QModelIndex& idx : rows) {
        QModelIndex srcIdx = m_proxy->mapToSource(idx.sibling(idx.row(), internalCol));
        bool ok = false;
        int num = m_model->data(srcIdx).toInt(&ok);
        if (ok) out.append(num);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

void CurrentOrdersPage::markDirty()
{
    m_dirty = true;
}

void CurrentOrdersPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    // MainWindow already refreshes the active page on tab navigation. Only do an
    // eager load here for the very first show to avoid repeated full reloads on
    // every return to the tab.
    if (!m_loaded) {
        markDirty();
        refresh();
    }
}

void CurrentOrdersPage::refresh()
{
    const QString scopeKey = AccessControlService::currentScopeKey();
    if (m_actorScopeKey != scopeKey) {
        m_actorScopeKey = scopeKey;
        ++m_refreshGeneration;
        m_dirty = true;
        TabularReadModelMapper::apply(m_model, TabularReadModel{});
    }
    const bool showAdminActions = !AccessControlService::currentActorIsMaster();
    if (m_btnPdf) m_btnPdf->setVisible(showAdminActions);
    if (m_btnImportInvoice) m_btnImportInvoice->setVisible(showAdminActions);
    if (m_loaded && !m_dirty && !m_refreshWorker) return;
    if (m_refreshInProgress || m_refreshWorker) {
        AsyncTaskHandle::cancel(m_refreshCancelToken);
        if (m_refreshWorker) m_refreshWorker->requestInterruption();
        m_refreshQueued = true;
        m_dirty = true;
        return;
    }

    m_refreshInProgress = true;
    startAsyncRefresh();
}

void CurrentOrdersPage::startAsyncRefresh()
{
    const int selectedBefore = selectedOrderNumber();
    const int vScrollBefore = (m_table && m_table->verticalScrollBar()) ? m_table->verticalScrollBar()->value() : 0;
    const int hScrollBefore = (m_table && m_table->horizontalScrollBar()) ? m_table->horizontalScrollBar()->value() : 0;
    const QString filter = m_search ? m_search->text().trimmed() : QString();
    const int generation = ++m_refreshGeneration;
    const QString scopeKey = AccessControlService::currentScopeKey();
    const QString refreshCacheKey = QStringLiteral("current_orders|filter=%1|%2").arg(filter.toLower(), scopeKey);
    const auto runtimeTicket = RuntimeJob::RuntimeJobDispatcher::instance().startLatest(
        RuntimeJob::Family::CurrentOrders,
        QStringLiteral("CurrentOrdersPage"),
        15000,
        80,
        refreshCacheKey);
    m_refreshCancelToken = runtimeTicket.cancelToken;
    const auto cancelToken = m_refreshCancelToken;
    const AsyncTaskHandle asyncHandle(this, static_cast<quint64>(generation), cancelToken);

    PerfWatchdog::asyncJobStarted(QStringLiteral("current-orders"), QString::number(generation));

    struct AsyncResult {
        CurrentOrdersQueryResult queryResult;
        TabularReadModel readModel;
    };
    const auto result = std::make_shared<AsyncResult>();

    auto* worker = AsyncThreadScope::startWorker(this,
                                               QStringLiteral("current_orders_refresh_%1").arg(generation),
                                               cancelToken,
                                               [result, filter, runtimeTicket](const std::shared_ptr<std::atomic_bool>& cancelToken) {
        RuntimeJob::RuntimeJobDispatcher::instance().markRunning(runtimeTicket);
        auto cancelled = [&cancelToken]() { return AsyncThreadScope::isCancellationRequested(cancelToken); };
        if (cancelled()) return;

        const CurrentOrdersRefreshJob::Result localResult = CurrentOrdersRefreshJob::run(filter, cancelToken);
        if (!localResult.cancelled && !cancelled()) {
            result->queryResult = localResult.queryResult;
            result->readModel = localResult.readModel;
        }
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
            qWarning().noquote() << QStringLiteral("ASYNC_LIFECYCLE current_orders_refresh_timeout_cancel generation=") << generation
                                 << QStringLiteral(" timeoutMs=") << runtimeTicket.timeoutMs;
        });
    }
    // This mutex-protected counter must finish before deferred worker deletion;
    // queued callbacks targeting the worker can be discarded with that object.
    connect(worker, &QThread::finished, worker, [generation]() {
        PerfWatchdog::asyncJobFinished(QStringLiteral("current-orders"), QString::number(generation));
    }, Qt::DirectConnection);
    connect(worker, &QThread::finished, this, [this, worker, result, asyncHandle, runtimeTicket, selectedBefore, vScrollBefore, hScrollBefore, scopeKey]() {
        if (m_refreshWorker == worker) m_refreshWorker = nullptr;
        const bool canApply = scopeKey == AccessControlService::currentScopeKey()
            && asyncHandle.shouldApply(static_cast<quint64>(m_refreshGeneration))
            && RuntimeJob::RuntimeJobDispatcher::instance().shouldApply(runtimeTicket);
        RuntimeJob::RuntimeJobDispatcher::instance().finish(runtimeTicket, canApply ? RuntimeJob::Status::Succeeded : RuntimeJob::Status::Cancelled);
        if (canApply) {
            applyAsyncSnapshot(result->queryResult, result->readModel, selectedBefore, vScrollBefore, hScrollBefore);
        } else {
            qWarning().noquote() << QStringLiteral("ASYNC_LIFECYCLE current_orders_refresh_result_detached generation=") << asyncHandle.generation();
            m_refreshInProgress = false;
            if (m_refreshQueued) {
                m_refreshQueued = false;
                QPointer<CurrentOrdersPage> self(this);
                QTimer::singleShot(0, this, [self]() { if (self) self->refresh(); });
            }
        }
    });
    worker->start();
}

void CurrentOrdersPage::applyAsyncSnapshot(const CurrentOrdersQueryResult& queryResult,
                                           const TabularReadModel& readModel,
                                           int selectedBefore,
                                           int vScrollBefore,
                                           int hScrollBefore)
{
    QElapsedTimer t; t.start();
    PerfWatchdog::ScopedTimer perfTimer(QStringLiteral("CurrentOrdersPage::applyAsyncSnapshot"), 80);

    if (!queryResult.hasSource) {
        qWarning() << "CurrentOrdersPage refresh aborted:" << queryResult.errorText;
        TabularReadModelMapper::apply(m_model, TabularReadModel{});
        restoreHeaderState();
        m_loaded = true;
        m_dirty = false;
        m_refreshInProgress = false;
        if (m_refreshQueued) {
            m_refreshQueued = false;
            QPointer<CurrentOrdersPage> self(this);
            QTimer::singleShot(0, this, [self]() { if (self) self->refresh(); });
        }
        return;
    }
    if (!queryResult.errorText.trimmed().isEmpty()) {
        qWarning() << "CurrentOrdersPage query error" << queryResult.errorText << "source=" << queryResult.sourceTable;
    }

    if (m_table) m_table->setUpdatesEnabled(false);
    TabularReadModelMapper::apply(m_model, readModel);
    restoreHeaderState();
    OrderTableUiMapper::applyCurrentOrdersModelShape(m_table, m_model);
    if (m_table) m_table->setUpdatesEnabled(true);

    bool restoredSelection = false;
    if (m_table && m_proxy) {
        int targetRow = -1;
        if (selectedBefore > 0) {
            const int sourceRow = m_model->rowByColumnValue(QStringLiteral("__internal_number"), selectedBefore);
            if (sourceRow >= 0) {
                const QModelIndex proxyIndex = m_proxy->mapFromSource(m_model->index(sourceRow, 0));
                if (proxyIndex.isValid()) targetRow = proxyIndex.row();
            }
        }
        if (targetRow >= 0) {
            const QModelIndex current = m_proxy->index(targetRow, 0);
            if (current.isValid()) {
                m_table->setCurrentIndex(current);
                m_table->selectRow(targetRow);
                m_table->scrollTo(current, QAbstractItemView::PositionAtCenter);
                restoredSelection = true;
            }
        }
        if (!restoredSelection) {
            if (auto* vbar = m_table->verticalScrollBar()) vbar->setValue(vScrollBefore);
            if (auto* hbar = m_table->horizontalScrollBar()) hbar->setValue(hScrollBefore);
        }
        if (m_table->viewport()) m_table->viewport()->update();
    }

    m_loaded = true;
    m_dirty = false;
    m_refreshInProgress = false;
    publishSettlement();
    qDebug() << "CurrentOrdersPage applyAsyncSnapshot ms=" << t.elapsed() << "rows=" << (m_model ? m_model->rowCount() : 0);
    if (m_refreshQueued) {
        m_refreshQueued = false;
        QPointer<CurrentOrdersPage> self(this);
        QTimer::singleShot(0, this, [self]() { if (self) self->refresh(); });
    }
}

void CurrentOrdersPage::restoreHeaderState()
{
    if (!m_table || !m_table->horizontalHeader() || !m_table->model()) return;
    auto* header = m_table->horizontalHeader();
    if (!header) return;

    const QString rawState = UiStateSettingsService::loadString(QStringLiteral("ui.current_orders.header_state.v3")).trimmed();
    if (!rawState.isEmpty()) {
        const bool restored = header->restoreState(QByteArray::fromBase64(rawState.toUtf8()));
        if (!restored) {
            qWarning() << "CurrentOrdersPage ignored invalid saved header state";
        }
    }

    const QString rawWidths = UiStateSettingsService::loadString(QStringLiteral("ui.current_orders.column_widths.v3"), QStringLiteral("{}")).trimmed();
    const QJsonDocument widthsDoc = QJsonDocument::fromJson(rawWidths.toUtf8());
    if (widthsDoc.isObject()) {
        const QJsonObject widthsObj = widthsDoc.object();
        for (auto it = widthsObj.begin(); it != widthsObj.end(); ++it) {
            bool ok = false;
            const int logicalIndex = it.key().toInt(&ok);
            if (!ok) continue;
            if (logicalIndex < 0 || logicalIndex >= m_table->model()->columnCount()) continue;
            const int width = it.value().toInt();
            if (width > 0) {
                m_table->setColumnWidth(logicalIndex, width);
            }
        }
    } else {
        qWarning() << "CurrentOrdersPage ignored invalid saved column widths";
    }

    const QStringList visibleHeaders = {
        QStringLiteral("№"),
        QString::fromUtf8("Пост"),
        QString::fromUtf8("Відкрито"),
        QString::fromUtf8("Статус"),
        QString::fromUtf8("Номер"),
        QString::fromUtf8("Авто"),
        QString::fromUtf8("Клієнт"),
        QString::fromUtf8("Майстер"),
        QString::fromUtf8("Роботи"),
        QString::fromUtf8("Запчастини")
    };
    for (const QString& headerText : visibleHeaders) {
        const int col = modelColumnByHeader(m_model, headerText);
        if (col < 0) continue;
        m_table->setColumnHidden(col, false);
        if (!widthsDoc.object().contains(QString::number(col))) {
            const int defaultWidth = (headerText == QString::fromUtf8("Відкрито")) ? 178
                : (headerText == QString::fromUtf8("Статус")) ? 108
                : (headerText == QString::fromUtf8("Номер")) ? 130
                : (headerText == QString::fromUtf8("Авто")) ? 210
                : (headerText == QString::fromUtf8("Клієнт")) ? 170
                : (headerText == QString::fromUtf8("Майстер")) ? 130
                : (headerText == QString::fromUtf8("Роботи")) ? 110
                : (headerText == QString::fromUtf8("Запчастини")) ? 130
                : 66;
            m_table->setColumnWidth(col, defaultWidth);
        }
    }
    header->setStretchLastSection(false);
    header->setSectionResizeMode(QHeaderView::Interactive);
}

void CurrentOrdersPage::saveHeaderState() const
{
    if (m_refreshInProgress || !m_table || !m_table->horizontalHeader() || !m_table->model() || m_table->model()->columnCount()==0) return;
    auto* header = m_table->horizontalHeader();
    UiStateSettingsService::saveString(QStringLiteral("ui.current_orders.header_state.v3"), QString::fromLatin1(header->saveState().toBase64()));

    QJsonObject widthsObj;
    for (int logicalIndex = 0; logicalIndex < m_table->model()->columnCount(); ++logicalIndex) {
        widthsObj.insert(QString::number(logicalIndex), m_table->columnWidth(logicalIndex));
    }
    UiStateSettingsService::saveString(QStringLiteral("ui.current_orders.column_widths.v3"), QString::fromUtf8(QJsonDocument(widthsObj).toJson(QJsonDocument::Compact)));
}

QString CurrentOrdersPage::selectedOrderStatus() const
{
    if (!m_table || !m_model || !m_proxy) return QString();
    QModelIndex idx = m_table->currentIndex();
    if (!idx.isValid()) return QString();
    QModelIndex srcIdx = m_proxy->mapToSource(idx.sibling(idx.row(), modelColumnByHeader(m_model,QString::fromUtf8("Статус"))));
    return srcIdx.isValid() ? m_model->data(srcIdx).toString() : QString();
}

void CurrentOrdersPage::clearSearchStateForReturn()
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

void CurrentOrdersPage::openSelectedOrder()
{
    const auto result = CurrentOrdersCoordinator::openExistingOrderEditor(
        this,
        selectedOrderNumber(),
        selectedOrderUid(),
        [this]() { clearSearchStateForReturn(); });
    if (!result.errorText.trimmed().isEmpty()) {
        QMessageBox::critical(this, QString::fromUtf8("Помилка"), result.errorText);
        return;
    }
    if (result.accepted) queueRefreshAll(0);
}

void CurrentOrdersPage::openSelectedVehicleAsNewOrder()
{
    const auto result = CurrentOrdersCoordinator::openVehicleAsNewOrder(this, selectedOrderNumber());
    if (!result.errorText.trimmed().isEmpty()) {
        QMessageBox::critical(this, QString::fromUtf8("Помилка"), result.errorText);
        return;
    }
    if (result.accepted) queueRefreshAll(0);
}

void CurrentOrdersPage::exportSelectedOrderPdf()
{
    if (AccessControlService::currentActorIsMaster()) return;
    if (selectedOrderNumber() <= 0) {
        QMessageBox::information(this,
                                 QString::fromUtf8("PDF"),
                                 QString::fromUtf8("Спочатку виберіть наряд у таблиці."));
        return;
    }

    const auto result = CurrentOrdersCoordinator::exportOrderPdf(this, selectedOrderNumber(), selectedOrderUid());
    if (!result.errorText.trimmed().isEmpty()) {
        QMessageBox::critical(this, QString::fromUtf8("Помилка"), result.errorText);
    }
}

void CurrentOrdersPage::importInvoiceFromExcel()
{
    if (AccessControlService::currentActorIsMaster()) return;
    const QString filter = QString::fromUtf8("Накладні/таблиці (*.pdf *.xlsx *.xls *.xlsm *.csv *.txt)");
    const QString filePath = InvoiceDialogSupport::selectFile(
        this,
        QString::fromUtf8("Виберіть файл накладної"),
        filter);
    if (filePath.trimmed().isEmpty()) return;

    const QPointer<CurrentOrdersPage> guard(this);
    WorkPartsDialogExcelSupport::runExcelReadAsync(this, QString(), filePath,
        [guard,filePath](const ExcelReadResult& data,const QString& error) {
            if(!guard || !error.isEmpty()) return;
            auto review=[guard,filePath,data](const QList<ImportedPartDraft>& drafts,const QString& parseError) {
                if(!guard)return;
                if(!parseError.isEmpty()||drafts.isEmpty()){QMessageBox::warning(guard,QString::fromUtf8("Накладна"),parseError.isEmpty()?QString::fromUtf8("Не знайдено позицій."):parseError);return;}
                if(CurrentOrdersSupport::reviewInvoice(filePath,data,drafts,guard) && guard)guard->queueRefreshAll(0);
            };
            if(!data.parsedPartDrafts.isEmpty()){review(data.parsedPartDrafts,QString());return;}
            InvoiceImport::InvoiceColumnMappingPreview mapping;
            if(!confirmCurrentOrdersInvoiceColumnMapping(guard,data.rows,data.columnMappingPreview,filePath,&mapping))return;
            WorkPartsDialogExcelSupport::runConfirmedVatMappingParseAsync(guard,data.rows,mapping,data.documentDate,
                [](const QString& s){return PartImportTextSupport::normalizePartName(s);},
                [](QString s,bool* ok){return PartImportTextSupport::parseLooseNumber(s,ok);},
                [](double value){return FormatUtils::money(value,2);},review);
        });
}

void CurrentOrdersPage::queueRefreshAll(int delayMs)
{
    if (!m_refreshAllDebounce) {
        emit requestRefreshAll();
        return;
    }
    m_refreshAllDebounce->start(qMax(0, delayMs));
}

bool CurrentOrdersPage::setSelectedOrdersClosed(bool closed)
{
    const QList<int> nums = selectedOrderNumbers();
    if (nums.isEmpty()) return false;

    QString list;
    for (int n : nums) list += QString::number(n) + ", ";
    if (list.endsWith(", ")) list.chop(2);

    const QString actionText = closed ? QString::fromUtf8("Закрити") : QString::fromUtf8("Повторно відкрити");
    if (QMessageBox::question(this, QString::fromUtf8("Підтвердження"),
                              QString::fromUtf8("%1 %2 нарядів?\n№: %3").arg(actionText).arg(nums.size()).arg(list),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes) {
        return false;
    }

    if(closed) {
        QStringList uids;
        for(int n:nums)uids.append(OrderDialogReadQueryService::canonicalOrderUidForInternalNumber(n));
        const bool completed=BayOccupancyPage::closeOrders(this,uids);
        refresh(); // Earlier orders may have committed before a later confirmation was cancelled.
        return completed;
    }
    const auto result = CurrentOrdersCoordinator::setOrdersClosed(nums, closed);
    if (!result.success) {
        QMessageBox::critical(this, QString::fromUtf8("Помилка"), result.errorText);
        return false;
    }

    for (int num : result.affectedOrderNumbers) {
        publishCurrentOrdersBackupEvent(closed ? QStringLiteral("order_status_closed") : QStringLiteral("order_status_reopened"),
                                        num,
                                        QStringLiteral("order_status"),
                                        QString::number(num),
                                        closed
                                            ? QString::fromUtf8("Закрито наряд №%1").arg(num)
                                            : QString::fromUtf8("Повторно відкрито наряд №%1").arg(num),
                                        {QStringLiteral("work_orders"), QStringLiteral("service_masters")});
    }
    return true;
}

bool CurrentOrdersPage::deleteSelectedOrders()
{
    const QList<int> nums = selectedOrderNumbers();
    if (nums.isEmpty()) return false;

    QString list;
    for (int n : nums) list += QString::number(n) + ", ";
    if (list.endsWith(", ")) list.chop(2);

    if (QMessageBox::question(this, QString::fromUtf8("Підтвердження"),
                              QString::fromUtf8("Видалити %1 нарядів?\n№: %2").arg(nums.size()).arg(list),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes) {
        return false;
    }

    if(CloudSyncSettings::localBackendEnabled()) {
        for(const int n:nums) {
            const auto uid=OrderDialogReadQueryService::canonicalOrderUidForInternalNumber(n);
            if(!OrderClosureApplicationService::deletionBlockReason(uid).isEmpty()) {
                BayOccupancyPage::open(this,uid);
                const auto blocked=OrderClosureApplicationService::deletionBlockReason(uid);
                if(!blocked.isEmpty()){QMessageBox::warning(this,QString::fromUtf8("Видалення наряду"),blocked);return false;}
            }
        }
    }
    const auto result = CurrentOrdersCoordinator::deleteOrders(nums);
    if (!result.success) {
        QMessageBox::critical(this, QString::fromUtf8("Помилка"), result.errorText);
        return false;
    }

    for (int num : result.affectedOrderNumbers) {
        BackupCommitPublisher::publish(
            BackupCommitPublisher::makeEvent(QStringLiteral("CurrentOrdersPage"),
                                             QStringLiteral("current_orders"),
                                             QStringLiteral("order"),
                                             QString::number(num),
                                             result.orderUidsByNumber.value(num),
                                             num,
                                             QStringLiteral("order_delete"),
                                             QString::fromUtf8("Видалено наряд №%1").arg(num),
                                             {QStringLiteral("work_orders"), QStringLiteral("work_order_services"), QStringLiteral("work_order_parts")}),
            nullptr);
    }

    return true;
}
