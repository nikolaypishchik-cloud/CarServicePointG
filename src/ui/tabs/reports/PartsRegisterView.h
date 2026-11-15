#pragma once

#include "ui/tabs/reports/ReportsTableSupport.h"
#include <optional>
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
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* filters = new QHBoxLayout;
        store = new QComboBox(this);
        payment = new QComboBox(this);
        store->setAccessibleName(tr("Магазин"));
        payment->setAccessibleName(tr("Стан оплати"));
        filters->addWidget(store);
        filters->addWidget(payment);
        filters->addStretch();
        layout->addLayout(filters);
        message = new QLabel(this);
        layout->addWidget(message);
        table = new QTableView(this);
        table->setObjectName("partsRegisterTable");
        model = new ReportsTableSupport::ReportsSnapshotTableModel(table);
        table->setModel(model);
        ReportsTableSupport::setTableBaseStyle(table);
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        layout->addWidget(table, 1);
        connect(store, &QComboBox::currentIndexChanged, this, [this] { filter(); });
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

    void setSearch(const QString& value) {
        search = value.trimmed();
        if (active) filter();
    }

    void setActive(bool value) {
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
        state = tr("Завантаження…");
        model->clearSnapshot();
        filter();
    }

    void setSnapshot(const PartsSnapshot& snapshot) {
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
        int visible = 0;
        for (int r = 0; r < model->rowCount(); ++r) {
            bool matches = search.isEmpty();
            for (int c : {2, 3, 5, 6, 7}) matches |= model->cellText(r, c).contains(search, Qt::CaseInsensitive);
            if (store->currentIndex() > 0) matches &= model->cellText(r, 4) == store->currentData().toString();
            if (payment->currentIndex() > 0) matches &= model->cellText(r, 11) == payment->currentData().toString();
            table->setRowHidden(r, !matches);
            if (matches) ++visible;
        }
        message->setText(state.isEmpty() ? tr("За вибраними умовами позицій немає") : state);
        message->setVisible(visible == 0);
    }

    QComboBox *store, *payment;
    QLabel* message;
    QTableView* table;
    ReportsTableSupport::ReportsSnapshotTableModel* model;
    QString search, state;
    std::optional<PartsSnapshot> pending;
    bool active = false;
};
