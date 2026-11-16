#pragma once

#include "ui/tabs/reports/ReportsTableSupport.h"
#include "core/services/PartsRegisterPdf.h"
#include <optional>
#include <algorithm>
#include "core/util/FormatUtils.h"
#include <QListWidget>
#include <QSplitter>
#include <QPushButton>
#include "ui/tabs/reports/ReportsInteractionSupport.h"
#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QSignalBlocker>
#include <QSet>
#include <QTableView>
#include <QVBoxLayout>

class PartsRegisterView final : public QWidget {
public:
    explicit PartsRegisterView(QWidget* parent) : QWidget(parent) {
        setObjectName("partsRegisterView");
        setAttribute(Qt::WA_StyledBackground);
        setStyleSheet(QStringLiteral("#partsRegisterView { background: #edf2f6; } #partsRegisterView QLabel { color: #203447; background: transparent; }"));
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* filters = new QHBoxLayout;
        store = new QComboBox(this);
        payment = new QComboBox(this);
        store->setAccessibleName(tr("Магазин"));
        payment->setAccessibleName(tr("Стан оплати"));
        registerTitle = new QLabel(this);
        registerTitle->setObjectName("partsRegisterTitle");
        QFont titleFont = registerTitle->font();
        titleFont.setBold(true);
        registerTitle->setFont(titleFont);
        filters->addWidget(registerTitle);
        filters->addStretch();
        auto* storeLabel = new QLabel(tr("Магазин"), this);
        storeLabel->setBuddy(store);
        filters->addWidget(storeLabel);
        filters->addWidget(store);
        auto* paymentLabel = new QLabel(tr("Оплата"), this);
        paymentLabel->setBuddy(payment);
        filters->addWidget(paymentLabel);
        filters->addWidget(payment);
        layout->addLayout(filters);
        message = new QLabel(this);
        layout->addWidget(message);
        table = new QTableView(this);
        table->setObjectName("partsRegisterTable");
        model = new ReportsTableSupport::ReportsSnapshotTableModel(table);
        table->setModel(model);
        ReportsTableSupport::setTableBaseStyle(table);
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        auto* split = new QSplitter(Qt::Horizontal, this);
        orders = new QListWidget(split);
        orders->setObjectName("partsRegisterOrders");
        orders->setAccessibleName(tr("Наряди з придбаними запчастинами"));
        auto* details = new QWidget(split);
        auto* detailsLayout = new QVBoxLayout(details);
        detailsLayout->setContentsMargins(0, 0, 0, 0);
        orderLink = new QPushButton(details);
        orderLink->setObjectName("partsRegisterOpenOrder");
        detailsLayout->addWidget(orderLink, 0, Qt::AlignLeft);
        orderDates = new QLabel(details);
        orderDates->setObjectName("partsRegisterOrderDates");
        orderDates->setTextFormat(Qt::PlainText);
        orderDates->setWordWrap(true);
        detailsLayout->addWidget(orderDates);
        detailsLayout->addWidget(table, 1);
        split->addWidget(orders);
        split->addWidget(details);
        split->setStretchFactor(1, 1);
        layout->addWidget(split, 1);
        orders->hide();
        orderLink->hide();
        connect(orders, &QListWidget::currentRowChanged, this, [this] { displayOrder(); });
        connect(orderLink, &QPushButton::clicked, this, [this] {
            if (selectedRows.isEmpty()) return;
            ReportsInteractionSupport::openOrderFromReportTable(this, table, 2, selectedRows.first(), orderEdited);
        });
        connect(store, &QComboBox::currentIndexChanged, this, [this] { filter(); if (storeChanged) storeChanged(); });
        connect(payment, &QComboBox::currentIndexChanged, this, [this] { filter(); });
        buildRows({});
        const auto metrics = table->horizontalHeader()->fontMetrics();
        for (int c = 0; c < model->columnCount(); ++c) {
            const auto title = model->headerData(c, Qt::Horizontal).toString();
            table->setColumnWidth(c, qMax(table->columnWidth(c), metrics.horizontalAdvance(title) + 28));
        }
        table->setColumnWidth(5, qMax(160, table->columnWidth(5)));
        table->setColumnWidth(11, qMax(table->columnWidth(11),
            metrics.horizontalAdvance(tr("Немає підтверджених даних")) + 24));
    }

    void setTabOrderAfter(QWidget* previous) {
        QWidget::setTabOrder(previous, store);
        QWidget::setTabOrder(store, payment);
        QWidget::setTabOrder(payment, orders);
        QWidget::setTabOrder(orders, orderLink);
        QWidget::setTabOrder(orderLink, table);
    }

    std::function<void()> orderEdited, storeChanged;
    QMap<QString,QString> supplierCardAmounts() const {
        QMap<QString,QString> result;
        if (!debtState.isEmpty()) return result;
        const auto selected = store->currentData().toString();
        const auto add = [&](const QString& name, qint64 cents) {
            result.insert(name, tr("%1 грн").arg(FormatUtils::money(cents / 100.0, 2)));
        };
        if (!selected.isEmpty()) add(selected, documentDebts.value(selected));
        else for (auto it = documentDebts.cbegin(); it != documentDebts.cend(); ++it)
            add(it.key(), it.value());
        return result;
    }

    QStringList debtTileBodies() const {
        if (!debtState.isEmpty()) return {debtState.toHtmlEscaped(), debtState.toHtmlEscaped()};
        const auto supplier = store->currentData().toString();
        qint64 amount = documentDebts.value(supplier);
        if (supplier.isEmpty()) for (auto cents : documentDebts) amount += cents;
        QString reconciliation = supplier.isEmpty() ? tr("Оберіть магазин") : tr("Звірку не зафіксовано");
        const auto fact = reconciliations.constFind(supplier);
        if (fact != reconciliations.cend())
            reconciliation = tr("%1 грн · %2").arg(FormatUtils::money(fact.value().first / 100.0, 2),
                                                    fact.value().second.toHtmlEscaped());
        return {tr("%1 грн").arg(FormatUtils::money(amount / 100.0, 2)), reconciliation};
    }
    bool hasExportSelection() const { return !byOrder || !selectedRows.isEmpty(); }

    bool isActive() const { return active; }

    ReportsPdfRenderer::PartsRegisterSelection exportSelection() const {
        ReportsPdfRenderer::PartsRegisterSelection selection;
        selection.headers = model->headers();
        if (byOrder) selection.orderCaption = orderLink->text();
        selection.store = store->currentText();
        selection.payment = payment->currentText();
        selection.search = search;
        for (int r = 0; r < model->rowCount(); ++r) {
            if (table->isRowHidden(r)) continue;
            QStringList cells;
            for (int c = 0; c < model->columnCount(); ++c) cells.append(model->cellText(r, c));
            selection.rows.append(cells);
        }
        return selection;
    }

    void setSearch(const QString& value) {
        search = value.trimmed();
        if (active) filter();
    }

    void setActive(bool value, bool groupByOrder = false) {
        byOrder = groupByOrder;
        orders->setVisible(byOrder);
        active = value;
        if (active && pending) {
            buildRows(*pending);
            pending.reset();
        } else if (active) {
            filter();
        }
        setVisible(active);
    }

    void showLoading() {
        pending.reset();
        debtState = tr("Завантаження…");
        state = debtState;
        model->clearSnapshot();
        filter();
    }

    void setSnapshot(const PartsSnapshot& snapshot) {
        documentDebts = snapshot.documentDebtCents;
        reconciliations = snapshot.supplierReconciliations;
        debtState = snapshot.error.isEmpty() ? snapshot.supplierDebtError : snapshot.error;
        if (active) buildRows(snapshot);
        else pending = snapshot;
    }

private:
    void buildRows(const PartsSnapshot& snapshot) {
        QVector<ReportsTableSupport::ReportsSnapshotTableModel::Row> rows;
        rows.reserve(snapshot.rows.size());
        QStringList stores, payments;
        QSet<QString> seenStores, seenPayments;
        const auto known = [](const QString& text) { return text.isEmpty() ? QStringLiteral("—") : text; };
        for (const auto& part : snapshot.rows) {
            if (part.isStoreHeader || !snapshot.error.isEmpty()) continue;
            const QStringList values = {part.date, known(part.installedAt), part.orderNumber,
                (part.car + " " + part.plate).trimmed(), known(part.store), part.partName,
                known(part.oem), known(part.manufacturer), part.qty, known(part.clientUnitPrice),
                known(part.clientAmount), known(part.supplierPayment)};
            ReportsTableSupport::ReportsSnapshotTableModel::Row row;
            for (const auto& value : values) {
                ReportsTableSupport::ReportsSnapshotTableModel::Cell cell;
                cell.text = known(value);
                row.cells.append(cell);
            }
            row.cells[2].userValue = part.internalOrderNumber;
            row.cells[2].extraRoles[ReportsTableSupport::OrderUidRole] = part.orderUid;
            if (part.supplierPayment == tr("Сплачено")) row.cells.last().foreground = QColor("#218746");
            if (part.supplierPayment == tr("Не сплачено")) row.cells.last().foreground = QColor("#c63737");
            rows.append(row);
            if (!seenStores.contains(values[4])) { seenStores.insert(values[4]); stores.append(values[4]); }
            if (!seenPayments.contains(values.last())) { seenPayments.insert(values.last()); payments.append(values.last()); }
        }
        model->setSnapshot({tr("Дата"), tr("Встановлено"), tr("Наряд"), tr("Авто"), tr("Магазин"),
            tr("Запчастина"), tr("OEM"), tr("Виробник"), tr("Кількість"), tr("Ціна за одиницю, грн"),
            tr("Сума клієнту, грн"), tr("Оплата постачальнику")}, rows);
        for (const auto& supplier : documentDebts.keys() + reconciliations.keys())
            if (!stores.contains(supplier)) stores.append(supplier);
        resetChoices(store, stores, tr("Усі магазини"));
        resetChoices(payment, payments, tr("Усі стани оплати"));
        state = snapshot.error;
        filter();
    }

    static void resetChoices(QComboBox* box, QStringList choices, const QString& all) {
        const QSignalBlocker blocker(box);
        const QString previous = box->currentData().toString();
        box->clear();
        box->addItem(all);
        choices.sort(Qt::CaseInsensitive);
        for (const auto& choice : choices) box->addItem(choice, choice);
        box->setCurrentIndex(qMax(0, box->findData(previous)));
    }

    void filter() {
        const QString previous = orders->currentItem() ? orders->currentItem()->data(Qt::UserRole).toString() : QString();
        const QSignalBlocker blocker(orders);
        orders->clear();
        orderRows.clear();
        selectedRows.clear();
        QStringList keys;
        int visible = 0;
        for (int r = 0; r < model->rowCount(); ++r) {
            bool matches = search.isEmpty();
            for (int c : {2, 3, 5, 6, 7}) matches |= model->cellText(r, c).contains(search, Qt::CaseInsensitive);
            if (store->currentIndex() > 0) matches &= model->cellText(r, 4) == store->currentData().toString();
            if (payment->currentIndex() > 0) matches &= model->cellText(r, 11) == payment->currentData().toString();
            table->setRowHidden(r, !matches);
            if (!matches) continue;
            ++visible;
            if (byOrder) {
                const auto index = model->index(r, 2);
                const QString uid = index.data(ReportsTableSupport::OrderUidRole).toString();
                const QString key = uid.isEmpty() ? "number:" + index.data(Qt::UserRole).toString() : "uid:" + uid;
                if (!orderRows.contains(key)) keys.append(key);
                orderRows[key].append(r);
            }
        }
        registerTitle->setText(byOrder ? tr("За нарядами") : tr("Усі позиції"));
        if (state.isEmpty())
            registerTitle->setText(tr("%1 · Показано %2 із %3").arg(registerTitle->text()).arg(visible).arg(model->rowCount()));
        if (byOrder) {
            for (const auto& key : keys) {
                const auto& rows = orderRows.constFind(key).value();
                const int first = rows.first();
                QString status = model->cellText(first, 11);
                QString date = model->cellText(first, 0);
                for (const int row : rows) {
                    if (model->cellText(row, 11) != status) status = tr("Різні стани оплати");
                    if (model->cellText(row, 0) != date) date = tr("Різні дати");
                }
                const QString label = tr("№%1 · %2\n%3 · Позицій: %4 · %5")
                    .arg(model->cellText(first, 2), model->cellText(first, 3), date).arg(rows.size()).arg(status);
                auto* item = new QListWidgetItem(label, orders);
                item->setData(Qt::UserRole, key);
            }
            orders->setCurrentRow(previous.isEmpty() && !keys.isEmpty() ? 0 : keys.indexOf(previous));
            displayOrder();
            return;
        }
        orderLink->hide();
        orderDates->hide();
        for (int column = 0; column < 4; ++column) table->showColumn(column);
        message->setText(state.isEmpty() ? tr("За вибраними умовами позицій немає") : state);
        message->setVisible(visible == 0);
    }

    void displayOrder() {
        const auto* item = orders->currentItem();
        selectedRows = item ? orderRows.value(item->data(Qt::UserRole).toString()) : QList<int>{};
        // The list stores row indices in the same ascending order as the source model.
        int cursor = 0;
        for (int r = 0; r < model->rowCount(); ++r) {
            const bool selected = cursor < selectedRows.size() && selectedRows[cursor] == r;
            table->setRowHidden(r, !selected);
            if (selected) ++cursor;
        }
        if (!selectedRows.isEmpty()) {
            const int r = selectedRows.first();
            orderLink->setText(tr("Наряд №%1 · %2").arg(model->cellText(r, 2), model->cellText(r, 3)));
            QStringList dates;
            for (int column : {0, 1, 2, 3}) {
                const QString first = model->cellText(r, column);
                const bool shared = std::all_of(selectedRows.cbegin(), selectedRows.cend(), [&](int row) {
                    return model->cellText(row, column) == first;
                });
                table->setColumnHidden(column, shared);
                if (column < 2)
                    dates.append(tr("%1: %2").arg(model->headers().at(column), shared ? first : tr("див. позиції")));
            }
            orderDates->setText(dates.join(tr("   ·   ")));
        } else {
            orderLink->setText({});
            orderDates->clear();
        }
        orderDates->setVisible(!selectedRows.isEmpty());
        orderLink->setVisible(!selectedRows.isEmpty());
        message->setText(!state.isEmpty() ? state : (orders->count() ? tr("Виберіть наряд") : tr("За вибраними умовами нарядів немає")));
        message->setVisible(selectedRows.isEmpty());
    }

    QListWidget* orders;
    QPushButton* orderLink;
    QLabel* orderDates;
    QLabel* registerTitle;
    QHash<QString, QList<int>> orderRows;
    QList<int> selectedRows;
    bool byOrder = false;
    QComboBox *store, *payment;
    QLabel* message;
    QTableView* table;
    ReportsTableSupport::ReportsSnapshotTableModel* model;
    QString search, state, debtState = tr("Завантаження…");
    QMap<QString,qint64> documentDebts;
    QMap<QString,QPair<qint64,QString>> reconciliations;
    std::optional<PartsSnapshot> pending;
    bool active = false;
};
