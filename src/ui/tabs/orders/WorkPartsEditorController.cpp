// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

#include "ui/tabs/orders/WorkPartsDialogModulePrelude.h"
#include "core/util/OrderClosureMarker.h"
#include "ui/tabs/orders/HistoryPartsDraftRoles.h"
#include "core/services/AccessControlService.h"

#include "ui/tabs/orders/WorkPartsDialog.h"

#include "ui/common/dialogs/OrderDialog.h"
#include "ui/common/AdaptiveUi.h"
#include "app/AppContext.h"
#include "core/backup/CrashRecoveryManager.h"
#include "core/backup/BackupPerformanceGuard.h"
#include "core/services/orders/WorkPartsReadQueryService.h"
#include "core/util/FormatUtils.h"
#include "core/util/PlateUtils.h"

#include <QComboBox>
#include <QDate>
#include <QDialog>
#include <QHideEvent>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPointer>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTimer>

namespace {
void editorFillTableViewportWithRows(QTableWidget* table, bool keepHintRowAtBottom)
{
    if (!table) return;
    // Decorative viewport rows are layout, not user edits. Their itemChanged
    // signals otherwise invalidate the asynchronous initial data snapshot.
    const QSignalBlocker blocker(table);

    removeViewportFillRows(table);

    const int rowHeight = qMax(1, table->verticalHeader() ? table->verticalHeader()->defaultSectionSize() : 40);
    const int viewportHeight = qMax(0, table->viewport() ? table->viewport()->height() : table->height());
    if (viewportHeight <= 0) return;

    const int currentRows = table->rowCount();
    const int rowsNeeded = qMax(currentRows, (viewportHeight + rowHeight - 1) / rowHeight);
    const int rowsToAdd = rowsNeeded - currentRows;
    if (rowsToAdd <= 0) return;

    int insertRow = currentRows;
    if (keepHintRowAtBottom && currentRows > 0) {
        int hintRow = -1;
        for (int row = 0; row < currentRows; ++row) {
            if (rowHasHintMarker(table, row)) {
                hintRow = row;
                break;
            }
        }
        if (hintRow >= 0) insertRow = hintRow;
    }

    // P1 performance: grow the backing model once instead of insertRow() in a
    // loop. The fill rows are lightweight, but insertRow() repeatedly forces
    // model/layout churn during resize/open.
    table->setRowCount(currentRows + rowsToAdd);
    for (int row = currentRows + rowsToAdd - 1; row >= insertRow + rowsToAdd; --row) {
        for (int c = 0; c < table->columnCount(); ++c) {
            auto* existing = table->takeItem(row - rowsToAdd, c);
            table->setItem(row, c, existing ? existing : makeViewportFillCell());
        }
        auto* headerItem = table->takeVerticalHeaderItem(row - rowsToAdd);
        table->setVerticalHeaderItem(row, headerItem ? headerItem : new QTableWidgetItem(QString()));
    }
    for (int row = insertRow; row < insertRow + rowsToAdd; ++row) {
        for (int c = 0; c < table->columnCount(); ++c) table->setItem(row, c, makeViewportFillCell());
        setVerticalHeaderLabel(table, row, QString());
    }
}
}

void WorkPartsDialog::setEmbedded(bool on)
{
    m_embedded = on;
    setWindowFlags(on ? Qt::Widget : Qt::Dialog);
    setModal(!on);
    setWindowModality(on ? Qt::NonModal : Qt::ApplicationModal);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* crash = AppContext::instance().crashRecoveryManager();
    if (on && m_recoveryRegistered && crash) {
        crash->unregisterParticipant(this);
        m_recoveryRegistered = false;
    } else if (!on && !m_recoveryRegistered && crash) {
        crash->registerParticipant(this);
        m_recoveryRegistered = true;
    }
    if (!on) {
        BackupPerformanceGuard::hold(AppContext::instance().backupManager(), 2000);
    }
    if (on) {
        setMinimumSize(0, 0);
        setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        updateGeometry();
    }
}

bool WorkPartsDialog::hasPendingEdits() const
{
    if (!m_userTouchedPendingContent) return false;
    return currentContentSignature() != persistedContentSignature();
}

QString WorkPartsDialog::currentContentSignature() const
{
    QStringList services;
    if (m_servicesTable) {
        for (int row = 0; row < m_servicesTable->rowCount(); ++row) {
            auto* titleItem = m_servicesTable->item(row, ServiceColTitle);
            if (!titleItem || isSpacerItem(titleItem) || titleItem->data(RoleIsHint).toBool()) continue;
            if (!rowHasMeaningfulServiceData(m_servicesTable, row)) continue;

            const QString title = canonicalizeServiceTitle(titleItem->text());
            const QString price = m_servicesTable->item(row, ServiceColPrice)
                ? moneyText(qMax(0.0, parseLooseNumber(m_servicesTable->item(row, ServiceColPrice)->text(), nullptr)))
                : moneyText(0.0);
            const QString master = m_servicesTable->item(row, ServiceColMaster)
                ? normalizeSentence(m_servicesTable->item(row, ServiceColMaster)->text())
                : QString();
            services << QStringLiteral("%1|%2|%3")
                            .arg(title)
                            .arg(price)
                            .arg(master);
        }
    }

    QStringList parts;
    if (m_partsTable) {
        for (int row = 0; row < m_partsTable->rowCount(); ++row) {
            auto* nameItem = m_partsTable->item(row, PartColName);
            if (!nameItem || isSpacerItem(nameItem) || nameItem->data(RoleIsHint).toBool()) continue;
            if (!rowHasMeaningfulPartData(m_partsTable, row)) continue;

            const auto normalizeMoneyCell = [this, row](int col) {
                Q_UNUSED(this);
                bool ok = false;
                const double value = m_partsTable->item(row, col) ? parseLooseNumber(m_partsTable->item(row, col)->text(), &ok) : 0.0;
                return moneyText(ok ? qMax(0.0, value) : 0.0);
            };
            const auto normalizeQtyCell = [this, row]() {
                Q_UNUSED(this);
                bool ok = false;
                const double value = m_partsTable->item(row, PartColQty) ? parseLooseNumber(m_partsTable->item(row, PartColQty)->text(), &ok) : 1.0;
                return FormatUtils::smartQty(ok && value > 0.0 ? value : 1.0, 2);
            };

            const QString orderedAt = m_partsTable->item(row, PartColOrderedAt)
                ? storedDateFromDisplay(m_partsTable->item(row, PartColOrderedAt)->text())
                : QDate::currentDate().toString(Qt::ISODate);
            const QString delivered = m_partsTable->item(row, PartColDelivered)
                ? m_partsTable->item(row, PartColDelivered)->data(RoleDeliveryDate).toString().trimmed()
                : QString();
            const QString oem = m_partsTable->item(row, PartColOem)
                ? m_partsTable->item(row, PartColOem)->text().trimmed()
                : QString();
            const QString name = normalizeSentence(nameItem->text());
            const QString qty = normalizeQtyCell();
            const QString price = normalizeMoneyCell(PartColPrice);
            const QString amount = normalizeMoneyCell(PartColAmount);
            const QString store = m_partsTable->item(row, PartColStore)
                ? normalizeSentence(m_partsTable->item(row, PartColStore)->text())
                : QString();

            parts << QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8")
                         .arg(orderedAt)
                         .arg(delivered)
                         .arg(oem)
                         .arg(name)
                         .arg(qty)
                         .arg(price)
                         .arg(amount)
                         .arg(store);
        }
    }

    return services.join(QStringLiteral("\n")) + QStringLiteral("\x1F") + parts.join(QStringLiteral("\n"));
}

QString WorkPartsDialog::persistedContentSignature() const
{
    QStringList services;
    const auto serviceRows = WorkPartsReadQueryService::listServicesByInternalNumber(m_orderNumber);
    for (const auto& service : serviceRows) {
        const QString title = canonicalizeServiceTitle(service.title);
        const QString price = moneyText(qMax(0.0, service.price));
        const QString qty = FormatUtils::smartQty(service.qty > 0.0 ? service.qty : 1.0, 2);
        const QString master = normalizeSentence(service.masterName);
        services << QStringLiteral("%1|%2|%3|%4")
                        .arg(title)
                        .arg(price)
                        .arg(qty)
                        .arg(master);
    }

    QStringList parts;
    const auto partRows = WorkPartsReadQueryService::listPartsByInternalNumber(m_orderNumber);
    for (const auto& part : partRows) {
        parts << QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8")
                     .arg(storedDateFromDisplay(displayDateFromStored(part.orderedAt)))
                     .arg(part.deliveryDate.trimmed())
                     .arg(part.partNumber.trimmed())
                     .arg(normalizeSentence(part.name))
                     .arg(FormatUtils::smartQty(part.qty > 0.0 ? part.qty : 1.0, 2))
                     .arg(moneyText(qMax(0.0, part.clientPrice)))
                     .arg(moneyText(qMax(0.0, part.clientAmount)))
                     .arg(normalizeSentence(part.store));
    }

    return services.join(QStringLiteral("\n")) + QStringLiteral("\x1F") + parts.join(QStringLiteral("\n"));
}

QObject* WorkPartsDialog::draftQObject()
{
    return this;
}

QString WorkPartsDialog::recoveryModule() const
{
    return QStringLiteral("orders");
}

QString WorkPartsDialog::recoveryFormType() const
{
    return QStringLiteral("WorkPartsDialog");
}

QString WorkPartsDialog::recoveryEntityType() const
{
    return QStringLiteral("order_details");
}

QString WorkPartsDialog::recoveryEntityId() const
{
    return QString::number(m_orderNumber);
}

QString WorkPartsDialog::recoveryOrderUid() const
{
    return currentOrderUid();
}

QString WorkPartsDialog::draftEntityKey() const
{
    const QString uid = currentOrderUid();
    return uid.trimmed().isEmpty() ? QStringLiteral("order_%1").arg(m_orderNumber) : uid;
}

QString WorkPartsDialog::committedBaseVersion() const
{
    return persistedContentSignature();
}

bool WorkPartsDialog::isDraftDirty() const
{
    return !m_embedded && hasPendingEdits();
}
QJsonObject WorkPartsDialog::serializeDraftState() const
{
    QJsonArray services;
    if (m_servicesTable) {
        for (int row = 0; row < m_servicesTable->rowCount(); ++row) {
            auto* titleItem = m_servicesTable->item(row, ServiceColTitle);
            if (!titleItem || isSpacerItem(titleItem) || titleItem->data(RoleIsHint).toBool() || !rowHasMeaningfulServiceData(m_servicesTable, row)) continue;
            QJsonObject item;
            item.insert(QStringLiteral("id"), titleItem->data(RoleId).toInt());
            item.insert(QStringLiteral("title"), titleItem->text());
            item.insert(QStringLiteral("historyWorkKey"), titleItem->data(HistoryPartsDraftRoles::WorkKey).toString());
            item.insert(QStringLiteral("historyCheckedTitle"), titleItem->data(HistoryPartsDraftRoles::CheckedTitle).toString());
            item.insert(QStringLiteral("price"), m_servicesTable->item(row, ServiceColPrice) ? m_servicesTable->item(row, ServiceColPrice)->text() : QString());
            item.insert(QStringLiteral("qty"), m_servicesTable->item(row, ServiceColQty) ? m_servicesTable->item(row, ServiceColQty)->text() : QString());
            item.insert(QStringLiteral("master"), m_servicesTable->item(row, ServiceColMaster) ? m_servicesTable->item(row, ServiceColMaster)->text() : QString());
            if (auto* attachmentItem = m_servicesTable->item(row, ServiceColAttachment)) {
                item.insert(QStringLiteral("attachmentPath"), attachmentItem->data(RoleAttachmentPath).toString());
                item.insert(QStringLiteral("attachmentSource"), attachmentItem->data(RoleAttachmentSource).toString());
                item.insert(QStringLiteral("attachmentName"), attachmentItem->data(RoleAttachmentName).toString());
            }
            services.push_back(item);
        }
    }

    QJsonArray parts;
    if (m_partsTable) {
        for (int row = 0; row < m_partsTable->rowCount(); ++row) {
            auto* nameItem = m_partsTable->item(row, PartColName);
            if (!nameItem || isSpacerItem(nameItem) || nameItem->data(RoleIsHint).toBool() || !rowHasMeaningfulPartData(m_partsTable, row)) continue;
            QJsonObject item;
            item.insert(QStringLiteral("id"), nameItem->data(RoleId).toInt());
            item.insert(QStringLiteral("orderedAt"), m_partsTable->item(row, PartColOrderedAt) ? m_partsTable->item(row, PartColOrderedAt)->text() : QString());
            item.insert(QStringLiteral("deliveredDate"), m_partsTable->item(row, PartColDelivered) ? m_partsTable->item(row, PartColDelivered)->data(RoleDeliveryDate).toString() : QString());
            item.insert(QStringLiteral("oem"), m_partsTable->item(row, PartColOem) ? m_partsTable->item(row, PartColOem)->text() : QString());
            item.insert(QStringLiteral("name"), nameItem->text());
            item.insert(QStringLiteral("historyWorkKey"), nameItem->data(HistoryPartsDraftRoles::SourceWorkKey).toString());
            item.insert(QStringLiteral("historyNote"), nameItem->data(HistoryPartsDraftRoles::SourceNote).toString());
            item.insert(QStringLiteral("historyOriginalCells"), QJsonArray::fromStringList(nameItem->data(HistoryPartsDraftRoles::OriginalCells).toStringList()));
            item.insert(QStringLiteral("manufacturer"), partManufacturerText(m_partsTable, row));
            item.insert(QStringLiteral("qty"), m_partsTable->item(row, PartColQty) ? m_partsTable->item(row, PartColQty)->text() : QString());
            item.insert(QStringLiteral("price"), m_partsTable->item(row, PartColPrice) ? m_partsTable->item(row, PartColPrice)->text() : QString());
            item.insert(QStringLiteral("amount"), m_partsTable->item(row, PartColAmount) ? m_partsTable->item(row, PartColAmount)->text() : QString());
            item.insert(QStringLiteral("store"), m_partsTable->item(row, PartColStore) ? m_partsTable->item(row, PartColStore)->text() : QString());
            if (auto* attachmentItem = m_partsTable->item(row, PartColAttachment)) {
                item.insert(QStringLiteral("attachmentPath"), attachmentItem->data(RoleAttachmentPath).toString());
                item.insert(QStringLiteral("attachmentSource"), attachmentItem->data(RoleAttachmentSource).toString());
                item.insert(QStringLiteral("attachmentName"), attachmentItem->data(RoleAttachmentName).toString());
                item.insert(QStringLiteral("invoiceNumber"), attachmentItem->data(RoleAttachmentDocumentNumber).toString());
                item.insert(QStringLiteral("invoiceDate"), attachmentItem->data(RoleAttachmentDocumentDate).toString());
            }
            parts.push_back(item);
        }
    }

    return QJsonObject{{QStringLiteral("orderNumber"), m_orderNumber},
                       {QStringLiteral("activeServiceId"), m_activeServiceId},
                       {QStringLiteral("services"), services},
                       {QStringLiteral("parts"), parts}};
}

bool WorkPartsDialog::restoreDraftState(const QJsonObject& payload, QString* errorText)
{
    const QVariant previousSyncReloadFlag = property("csAllowSyncWorkPartsReload");
    setProperty("csAllowSyncWorkPartsReload", true);
    auto restoreSyncReloadFlag = qScopeGuard([this, previousSyncReloadFlag]() {
        setProperty("csAllowSyncWorkPartsReload", previousSyncReloadFlag);
    });

    loadServices();
    ensureServiceSelected();
    loadPartsForActiveService();

    const auto findServiceRowById = [this](int id) {
        if (id <= 0) return -1;
        // P1/pass12: use the snapshot-built id index first; fall back to one
        // guarded scan only for draft rows inserted after the snapshot.
        int row = serviceRowById(id);
        if (row >= 0) return row;
        for (int candidate = 0; candidate < m_servicesTable->rowCount(); ++candidate) {
            auto* item = m_servicesTable->item(candidate, ServiceColTitle);
            if (item && item->data(RoleId).toInt() == id) {
                indexServiceRow(id, candidate);
                return candidate;
            }
        }
        return -1;
    };
    const auto findPartRowById = [this](int id) {
        if (id <= 0) return -1;
        // P1/pass12: use the snapshot-built id index first; fall back to one
        // guarded scan only for draft rows inserted after the snapshot.
        int row = partRowById(id);
        if (row >= 0) return row;
        for (int candidate = 0; candidate < m_partsTable->rowCount(); ++candidate) {
            auto* item = m_partsTable->item(candidate, PartColName);
            if (item && item->data(RoleId).toInt() == id) {
                indexPartRow(id, candidate);
                return candidate;
            }
        }
        return -1;
    };

    const QJsonArray services = payload.value(QStringLiteral("services")).toArray();
    for (const QJsonValue& value : services) {
        const QJsonObject item = value.toObject();
        const int id = item.value(QStringLiteral("id")).toInt();
        int row = findServiceRowById(id);
        if (row < 0) {
            insertEmptyServiceRow(m_servicesTable->rowCount());
            row = m_servicesTable->rowCount() - 1;
        }
        auto* titleItem = m_servicesTable->item(row, ServiceColTitle);
        if (!titleItem) continue;
        clearRowHintState(m_servicesTable, row);
        titleItem->setData(RoleId, id);
        titleItem->setData(HistoryPartsDraftRoles::WorkKey, item.value(QStringLiteral("historyWorkKey")).toString());
        titleItem->setData(HistoryPartsDraftRoles::CheckedTitle, item.value(QStringLiteral("historyCheckedTitle")).toString());
        indexServiceRow(id, row);
        titleItem->setData(RoleIsHint, false);
        titleItem->setText(item.value(QStringLiteral("title")).toString());
        if (auto* priceItem = m_servicesTable->item(row, ServiceColPrice)) { priceItem->setData(RoleIsHint, false); priceItem->setText(item.value(QStringLiteral("price")).toString()); }
        if (auto* qtyItem = m_servicesTable->item(row, ServiceColQty)) { qtyItem->setData(RoleIsHint, false); qtyItem->setText(item.value(QStringLiteral("qty")).toString()); }
        if (auto* masterItem = m_servicesTable->item(row, ServiceColMaster)) { masterItem->setData(RoleIsHint, false); masterItem->setText(item.value(QStringLiteral("master")).toString()); }
        if (auto* attachmentItem = m_servicesTable->item(row, ServiceColAttachment)) {
            attachmentItem->setData(RoleAttachmentPath, item.value(QStringLiteral("attachmentPath")).toString());
            attachmentItem->setData(RoleAttachmentSource, item.value(QStringLiteral("attachmentSource")).toString());
            attachmentItem->setData(RoleAttachmentName, item.value(QStringLiteral("attachmentName")).toString());
        }
        setRowEditingVisual(m_servicesTable, row);
        m_dirtyServiceRows.insert(row);
    }

    const QJsonArray parts = payload.value(QStringLiteral("parts")).toArray();
    for (const QJsonValue& value : parts) {
        const QJsonObject item = value.toObject();
        const int id = item.value(QStringLiteral("id")).toInt();
        int row = findPartRowById(id);
        if (row < 0) {
            insertEmptyPartRow(m_partsTable->rowCount());
            row = m_partsTable->rowCount() - 1;
        }
        auto* nameItem = m_partsTable->item(row, PartColName);
        if (!nameItem) continue;
        clearRowHintState(m_partsTable, row);
        nameItem->setData(RoleId, id);
        nameItem->setData(HistoryPartsDraftRoles::SourceWorkKey, item.value(QStringLiteral("historyWorkKey")).toString());
        nameItem->setData(HistoryPartsDraftRoles::SourceNote, item.value(QStringLiteral("historyNote")).toString());
        QStringList originalCells;
        for (const auto& cell : item.value(QStringLiteral("historyOriginalCells")).toArray()) originalCells.append(cell.toString());
        nameItem->setData(HistoryPartsDraftRoles::OriginalCells, originalCells);
        indexPartRow(id, row);
        nameItem->setData(RoleIsHint, false);
        nameItem->setText(item.value(QStringLiteral("name")).toString());
        nameItem->setData(RolePartManufacturer, item.value(QStringLiteral("manufacturer")).toString());
        if (auto* orderedItem = m_partsTable->item(row, PartColOrderedAt)) { orderedItem->setData(RoleIsHint, false); orderedItem->setText(item.value(QStringLiteral("orderedAt")).toString()); }
        if (auto* deliveredItem = m_partsTable->item(row, PartColDelivered)) {
            deliveredItem->setData(RoleDeliveryDate, item.value(QStringLiteral("deliveredDate")).toString());
            deliveredItem->setCheckState(item.value(QStringLiteral("deliveredDate")).toString().trimmed().isEmpty() ? Qt::Unchecked : Qt::Checked);
        }
        if (auto* oemItem = m_partsTable->item(row, PartColOem)) { oemItem->setData(RoleIsHint, false); oemItem->setText(item.value(QStringLiteral("oem")).toString()); }
        if (auto* qtyItem = m_partsTable->item(row, PartColQty)) { qtyItem->setData(RoleIsHint, false); qtyItem->setText(item.value(QStringLiteral("qty")).toString()); }
        if (auto* priceItem = m_partsTable->item(row, PartColPrice)) { priceItem->setData(RoleIsHint, false); priceItem->setText(item.value(QStringLiteral("price")).toString()); }
        if (auto* amountItem = m_partsTable->item(row, PartColAmount)) { amountItem->setData(RoleIsHint, false); amountItem->setText(item.value(QStringLiteral("amount")).toString()); }
        if (auto* storeItem = m_partsTable->item(row, PartColStore)) { storeItem->setData(RoleIsHint, false); storeItem->setText(item.value(QStringLiteral("store")).toString()); }
        if (auto* brandItem = m_partsTable->item(row, PartColBrand)) { brandItem->setData(RoleIsHint, false); brandItem->setText(item.value(QStringLiteral("manufacturer")).toString()); }
        if (auto* attachmentItem = m_partsTable->item(row, PartColAttachment)) {
            attachmentItem->setData(RoleAttachmentPath, item.value(QStringLiteral("attachmentPath")).toString());
            attachmentItem->setData(RoleAttachmentSource, item.value(QStringLiteral("attachmentSource")).toString());
            attachmentItem->setData(RoleAttachmentName, item.value(QStringLiteral("attachmentName")).toString());
            attachmentItem->setData(RoleAttachmentDocumentNumber, item.value(QStringLiteral("invoiceNumber")).toString());
            attachmentItem->setData(RoleAttachmentDocumentDate, item.value(QStringLiteral("invoiceDate")).toString());
        }
        syncPartManufacturerRoleFromRow(m_partsTable, row);
        setRowEditingVisual(m_partsTable, row);
        refreshPartInvoiceNumberCell(row);
        syncPartDeliveredCellState(m_partsTable, row);
        synchronizePairedRowHeights(row);
        m_dirtyPartRows.insert(row);
    }

    setActiveServiceId(payload.value(QStringLiteral("activeServiceId")).toInt(m_activeServiceId));
    renumberServiceItems();
    renumberPartItems();
    repositionInputRows();
    m_userTouchedPendingContent = true;
    notifyDraftDirtyChanged();
    if (errorText) errorText->clear();
    return true;
}

void WorkPartsDialog::notifyDraftDirtyChanged()
{
    ++m_draftEditRevision;
    if (!m_recoveryRegistered) {
        if (m_embedded) {
            if (auto* parentDialog = qobject_cast<OrderDialog*>(parentWidget())) {
                parentDialog->notifyDraftDirtyChanged();
            }
        }
        return;
    }
    auto* crash = AppContext::instance().crashRecoveryManager();
    if (!crash) return;
    crash->notifyParticipantDirty(this);
}

#include "ui/tabs/orders/OrderSettlementPresentation.h"

void WorkPartsDialog::loadHeader()
{
    const QString orderUid = currentOrderUid();
    m_settlement->clear();
    if (orderUid.isEmpty()) {
        m_lblTitle->setText(QString::fromUtf8("Наряд №%1").arg(m_orderNumber));
        m_lblSub->setText(QString::fromUtf8("Технічна помилка: не визначено канонічний order_uid"));
        return;
    }

    const auto snapshot = WorkPartsQueryService::loadHeaderSnapshot(orderUid);
    if (!snapshot.found) {
        m_lblTitle->setText(QString::fromUtf8("Наряд №%1").arg(m_orderNumber));
        m_lblSub->clear();
        return;
    }

    m_settlement->setText(OrderSettlementPresentation::summary(m_orderNumber, orderUid));
    const QString displayNumber = snapshot.displayNumber;
    const QString displayYear = snapshot.displayYear;
    const QString plate = PlateUtils::formatPlateDisplay(snapshot.plate);
    const QString vin = snapshot.vin;
    const QString car = snapshot.car;
    const QString client = snapshot.client;
    const QString phone = snapshot.phone;
    const QString master = snapshot.master;
    QString status = localizedWorkOrderStatusDisplay(snapshot.status);
    const QString closureMarker = OrderClosureMarker::label(snapshot.status, snapshot.closed, snapshot.mobileState);
    if (!closureMarker.isEmpty()) status += QStringLiteral(" · ") + closureMarker;
    const QString opened = displayDateFromStored(snapshot.opened);
    const double worksSum = snapshot.worksSum;
    const double partsSum = snapshot.partsSum;

    const QString displayOrder = (!displayNumber.trimmed().isEmpty() && !displayYear.trimmed().isEmpty())
        ? QStringLiteral("%1 / %2").arg(displayNumber, displayYear)
        : displayNumber;
    m_lblTitle->setText(QString::fromUtf8("Наряд №%1").arg(displayOrder.isEmpty() ? QString::number(m_orderNumber) : displayOrder));
    const QString subHtml = QString::fromUtf8(
        "%1 • VIN: %2 • %3 • Клієнт: %4 • Тел: %5 • Майстер: %6 • Статус: %7 • Відкрито: %8 • "
        "<span style=\"font-weight:600;\">Роботи: %9 грн</span> • "
        "<span style=\"font-weight:600;\">Запчастини: %10 грн</span>")
        .arg(plate.toHtmlEscaped(),
             vin.toHtmlEscaped(),
             car.toHtmlEscaped(),
             client.toHtmlEscaped(),
             phone.toHtmlEscaped(),
             master.toHtmlEscaped(),
             status.toHtmlEscaped(),
             opened.toHtmlEscaped(),
             moneyText(worksSum).toHtmlEscaped(),
             moneyText(partsSum).toHtmlEscaped());
    m_lblSub->setText(subHtml);
}

void WorkPartsDialog::loadMasterChoice()
{
    if (!m_masterCombo) return;
    const QSignalBlocker blocker(m_masterCombo);
    m_masterCombo->clear();
    m_masterCombo->setInsertPolicy(QComboBox::NoInsert);
    m_masterCombo->setEditable(false);
    m_masterCombo->addItem(QString::fromUtf8("Оберіть майстра"), QString());

    const auto masterSelection = WorkPartsQueryService::loadMasterSelection(currentOrderUid());
    QString currentMasterId = masterSelection.currentMasterId;
    QString currentMasterName = masterSelection.currentMasterName;

    const auto allPersonnel = PersonnelSettingsService::loadPersonnel(false);
    for (const auto& m : allPersonnel) {
        const QString role = m.role.trimmed().toLower();
        const bool isMasterRole = role.isEmpty() || role == QStringLiteral("master");
        if (!m.active || !isMasterRole) continue;
        if (AccessControlService::currentActorIsMaster()
            && m.id != AccessControlService::currentMasterId()) continue;
        m_masterCombo->addItem(m.name, m.id);
    }

    int idx = currentMasterId.isEmpty() ? -1 : m_masterCombo->findData(currentMasterId);
    if (idx < 0 && !currentMasterName.isEmpty()) {
        idx = m_masterCombo->findText(currentMasterName, Qt::MatchFixedString);
    }

    if (idx < 0 && (!currentMasterId.isEmpty() || !currentMasterName.isEmpty())) {
        QString fallbackName = currentMasterName;
        if (fallbackName.isEmpty()) {
            for (const auto& m : allPersonnel) {
                if (m.id == currentMasterId) {
                    fallbackName = m.name;
                    break;
                }
            }
        }
        if (fallbackName.isEmpty()) fallbackName = QString::fromUtf8("Майстер");
        const QString archivedLabel = QString::fromUtf8("%1 (архів)").arg(fallbackName);
        m_masterCombo->addItem(archivedLabel, currentMasterId);
        idx = m_masterCombo->count() - 1;
    }

    m_masterCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    if (AccessControlService::currentActorIsMaster()) m_masterCombo->setEnabled(false);
}

void WorkPartsDialog::refreshServiceAttachmentCell(int row)
{
    if (!m_servicesTable || row < 0 || row >= m_servicesTable->rowCount()) return;
    auto* titleItem = m_servicesTable->item(row, ServiceColTitle);
    if (!titleItem || isSpacerItem(titleItem)) return;
    const int serviceId = titleItem->data(RoleId).toInt();
    QString displayName;
    const QString path = WorkPartsReadQueryService::serviceAttachmentPath(serviceId, &displayName);
    auto* item = m_servicesTable->item(row, ServiceColAttachment);
    if (!item) {
        item = makeAttachmentItem(path, displayName);
        m_servicesTable->setItem(row, ServiceColAttachment, item);
    }
    const bool hasAttachment = !path.trimmed().isEmpty();
    const bool showAttachmentShortcut = hasAttachment || rowHasMeaningfulServiceData(m_servicesTable, row);
    const QSignalBlocker blocker(m_servicesTable);
    item->setText(showAttachmentShortcut ? QString::fromUtf8("📎") : QString());
    item->setData(RoleAttachmentPath, path);
    item->setData(RoleAttachmentName, displayName);
    item->setData(RoleAttachmentSource, QString());
    item->setToolTip(hasAttachment ? QString::fromUtf8("Натисніть, щоб переглянути фото")
                                   : QString::fromUtf8("Натисніть, щоб додати фото"));
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    applyLeftAlignmentToServiceRow(m_servicesTable, row);
}

void WorkPartsDialog::refreshPartAttachmentCell(int row)
{
    if (!m_partsTable || row < 0 || row >= m_partsTable->rowCount()) return;
    auto* nameItem = m_partsTable->item(row, PartColName);
    if (!nameItem || isSpacerItem(nameItem)) return;
    const int partId = nameItem->data(RoleId).toInt();
    QString displayName;
    QString invoiceNumber;
    QString invoiceDate;
    const QString path = WorkPartsReadQueryService::partAttachmentPath(partId, &displayName, &invoiceNumber, &invoiceDate);
    auto* item = m_partsTable->item(row, PartColAttachment);
    if (!item) {
        item = makeAttachmentItem(path, displayName);
        m_partsTable->setItem(row, PartColAttachment, item);
    }
    const bool hasAttachment = !path.trimmed().isEmpty() || !invoiceNumber.trimmed().isEmpty();
    item->setText(hasAttachment ? QString::fromUtf8("📎") : QString());
    item->setData(RoleAttachmentPath, path);
    item->setData(RoleAttachmentName, displayName);
    item->setData(RoleAttachmentSource, QString());
    item->setData(RoleAttachmentDocumentNumber, invoiceNumber);
    item->setData(RoleAttachmentDocumentDate, invoiceDate);
    item->setToolTip(attachmentToolTipText(path, invoiceNumber, invoiceDate));
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    applyLeftAlignmentToServiceRow(m_servicesTable, row);
    refreshPartInvoiceNumberCell(row);
}

void WorkPartsDialog::refreshPartInvoiceNumberCell(int row)
{
    if (!m_partsTable || row < 0 || row >= m_partsTable->rowCount()) return;
    auto* invoiceItem = m_partsTable->item(row, PartColInvoiceNumber);
    if (!invoiceItem) {
        invoiceItem = makeInvoiceNumberItem();
        m_partsTable->setItem(row, PartColInvoiceNumber, invoiceItem);
    }
    auto* attachmentItem = m_partsTable->item(row, PartColAttachment);
    const QString path = attachmentItem ? attachmentItem->data(RoleAttachmentPath).toString().trimmed() : QString();
    const QString sourcePath = attachmentItem ? attachmentItem->data(RoleAttachmentSource).toString().trimmed() : QString();
    const QString effectivePath = path.isEmpty() ? sourcePath : path;
    const QString rawInvoiceNumber = attachmentItem ? attachmentItem->data(RoleAttachmentDocumentNumber).toString().trimmed() : QString();
    const QString invoiceNumber = normalizeInvoiceDisplayNumber(rawInvoiceNumber);
    const QString invoiceDate = attachmentItem ? attachmentItem->data(RoleAttachmentDocumentDate).toString().trimmed() : QString();

    if (attachmentItem) {
        attachmentItem->setData(RoleAttachmentDocumentNumber, invoiceNumber);
        attachmentItem->setToolTip(attachmentToolTipText(effectivePath, invoiceNumber, invoiceDate));
    }
    invoiceItem->setText(invoiceNumber);
    invoiceItem->setData(RoleAttachmentPath, effectivePath);
    invoiceItem->setData(RoleAttachmentDocumentNumber, invoiceNumber);
    invoiceItem->setData(RoleAttachmentDocumentDate, invoiceDate);
    invoiceItem->setToolTip(attachmentToolTipText(effectivePath, invoiceNumber, invoiceDate));
    invoiceItem->setTextAlignment(Qt::AlignCenter);
    invoiceItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
    if (!invoiceNumber.isEmpty()) {
        int partId = 0;
        if (auto* nameItem = m_partsTable->item(row, PartColName)) partId = nameItem->data(RoleId).toInt();
        QString traceId = importedPartTraceId(m_partsTable, row);
        if (traceId.isEmpty()) {
            traceId = QStringLiteral("stored-invoice:%1:%2:%3")
                .arg(m_orderNumber)
                .arg(partId)
                .arg(invoiceNumber);
        }
        markImportedInvoiceRow(m_partsTable, row, traceId);
        qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("IMPORT_PARTS invoice_row_marker order=") << m_orderNumber
                          << QStringLiteral(" row=") << row
                          << QStringLiteral(" partId=") << partId
                          << QStringLiteral(" trace=") << traceId
                          << QStringLiteral(" invoice=") << clippedImportLogText(invoiceNumber)
                          << QStringLiteral(" invoiceDate=") << clippedImportLogText(invoiceDate);
    }

    QFont f = invoiceItem->font();
    if (!invoiceNumber.isEmpty()) {
        f.setUnderline(true);
        invoiceItem->setForeground(QColor(QStringLiteral("#1d4ed8")));
    } else {
        f.setUnderline(false);
        invoiceItem->setForeground(QColor(QStringLiteral("#111111")));
    }
    invoiceItem->setFont(f);
}

void WorkPartsDialog::refreshViewportFillRows()
{
    if (!m_servicesTable || !m_partsTable) return;
    editorFillTableViewportWithRows(m_servicesTable, true);
    editorFillTableViewportWithRows(m_partsTable, true);
}

void WorkPartsDialog::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    if (m_viewportRefreshQueued) {
        return;
    }
    m_viewportRefreshQueued = true;
    QPointer<WorkPartsDialog> self(this);
    QTimer::singleShot(0, this, [self]() {
        if (!self) {
            return;
        }
        self->m_viewportRefreshQueued = false;
        if (!self->isVisible()) {
            return;
        }
        self->refreshViewportFillRows();
    });
}

void WorkPartsDialog::hideEvent(QHideEvent* event)
{
    persistUiLayoutState();
    m_viewportRefreshQueued = false;
    QDialog::hideEvent(event);
}

