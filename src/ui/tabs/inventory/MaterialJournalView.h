#pragma once

#include "ui/common/TabularSnapshotTableModel.h"
#include "core/services/WorkshopInventoryService.h"
#include "core/util/FormatUtils.h"
#include <QCheckBox>
#include <QMessageBox>
#include "ui/tabs/orders/CurrentOrdersCoordinator.h"
#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableView>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <functional>
#include <algorithm>

class MaterialJournalView final : public QWidget {
public:
    MaterialJournalView(QWidget* page, QWidget* flatPage, QTableView* journal, TabularSnapshotTableModel* source)
        : QWidget(page), flat(flatPage), table(journal), model(source) {
        setObjectName("materialJournalView");
        setAttribute(Qt::WA_StyledBackground);
        setStyleSheet("#materialJournalView { background:#eff4f9; color:#192c3a; } "
                      "#materialJournalView QCheckBox { color:#192c3a; } "
                      "#materialJournalGrouped { background:white; alternate-background-color:#edf4fa; color:#192c3a; }");
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* controls = new QHBoxLayout;
        search = new QLineEdit(this);
        search->setObjectName("materialJournalSearch");
        search->setClearButtonEnabled(true);
        search->setPlaceholderText(tr("Матеріал або № наряду"));
        material = new QComboBox(this);
        operation = new QComboBox(this);
        material->setAccessibleName(tr("Матеріал"));
        operation->setAccessibleName(tr("Операція"));
        negative = new QCheckBox(tr("Лише від’ємні"), this);
        negative->setObjectName("materialJournalNegative");
        mode = new QComboBox(this);
        mode->setObjectName("materialJournalMode");
        mode->addItems({tr("Журнал"), tr("За матеріалами")});
        controls->addWidget(search, 1);
        controls->addWidget(material);
        controls->addWidget(operation);
        controls->addWidget(negative);
        controls->addWidget(mode);
        layout->addLayout(controls);
        auto* options = new QHBoxLayout;
        grouping = new QComboBox(this);
        grouping->addItems({tr("Без групування"), tr("За місяцями")});
        sorting = new QComboBox(this);
        sorting->addItems({tr("Нові спочатку"), tr("Старі спочатку")});
        fold = new QPushButton(tr("Згорнути всі"), this);
        options->addWidget(grouping);
        options->addWidget(fold);
        options->addStretch();
        options->addWidget(sorting);
        layout->addLayout(options);
        warning = new QLabel(this);
        warning->setTextFormat(Qt::PlainText);
        warning->setWordWrap(true);
        warning->setStyleSheet("color:#8f2427; background:#fff0e9; padding:5px;");
        warning->hide();
        layout->addWidget(warning);
        layout->addWidget(flat, 1);
        tree = new QTreeWidget(this);
        tree->setObjectName("materialJournalGrouped");
        tree->setAlternatingRowColors(true);
        tree->setUniformRowHeights(true);
        tree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
        layout->addWidget(tree, 1);
        tree->hide();
        connect(tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int column) {
            if (item->data(0, Qt::UserRole).isValid())
                openRow(item->data(0, Qt::UserRole).toInt(), column);
        });
        connect(table, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
            openRow(index.row(), index.column());
        });
        for (auto* combo : {material, operation, mode, grouping, sorting})
            connect(combo, &QComboBox::currentIndexChanged, this, [this] { apply(); });
        connect(search, &QLineEdit::textChanged, this, [this] { apply(); });
        connect(negative, &QCheckBox::toggled, this, [this] { apply(); });
        fold->setCheckable(true);
        connect(fold, &QPushButton::toggled, this, [this](bool collapsed) {
            if (collapsed) tree->collapseAll(); else tree->expandAll();
            fold->setText(collapsed ? tr("Розгорнути всі") : tr("Згорнути всі"));
        });
        connect(model, &QAbstractItemModel::modelReset, this, [this] { reload(); });
        reload();
    }

    std::function<void(int)> showDetails;
    std::function<void()> orderViewed;

    void setRows(const QVector<WorkshopInventoryPeriodRow>& rows) {
        TabularReadModel period;
        period.headers = {tr("Дата"), tr("Операція"), tr("Наряд"), tr("Матеріал"), tr("Од."),
                          tr("Надійшло"), tr("Витрачено"), tr("Залишок"), tr("Ціна"), tr("Сума"),
                          "PurchaseID", "InternalOrder", "OrderUid"};
        for (const auto& row : rows) {
            period.rows.append({row.date.toString("dd.MM.yy"), row.operation,
                row.orderNumber > 0 ? QString::number(row.orderNumber) : QString(), row.materialName, row.unit,
                row.receivedQuantity > 0 ? FormatUtils::smartQty(row.receivedQuantity, 3) : QString(),
                row.consumedQuantity > 0 ? FormatUtils::smartQty(row.consumedQuantity, 3) : QString(),
                FormatUtils::smartQty(row.balanceQuantity, 3),
                row.unitCost > 0 ? FormatUtils::money(row.unitCost, 2) : QString(),
                FormatUtils::money(row.totalCost, 2), row.purchaseId, row.internalOrderNumber, row.orderUid});
        }
        model->setSnapshot(period);
        table->setObjectName("inventoryMovementJournal");
        for (int column : {10, 11, 12}) table->setColumnHidden(column, true);
    }

    void revealPurchase() {
        for (auto* combo : {material, operation, mode, grouping, sorting}) {
            QSignalBlocker blocker(combo);
            combo->setCurrentIndex(0);
        }
        search->clear();
        negative->setChecked(false);
        apply();
    }

private:
    void openRow(int index, int column) {
        const auto& rows = model->snapshot().rows;
        if (index < 0 || index >= rows.size()) return;
        const int internal = rows[index].value(11).toInt();
        if (column == 2 && internal > 0) {
            const auto result = CurrentOrdersCoordinator::openExistingOrderEditor(this, internal, rows[index].value(12).toString());
            if (!result.launched) QMessageBox::warning(this, tr("Наряд"), result.errorText);
            else if (orderViewed) orderViewed();
        } else if (showDetails) {
            showDetails(index);
        }
    }

    void reload() {
        QStringList materials, operations;
        for (const auto& row : model->snapshot().rows) {
            materials.append(row.value(3).toString());
            operations.append(row.value(1).toString());
        }
        auto reset = [](QComboBox* box, QStringList values, const QString& all) {
            const auto previous = box->currentData();
            QSignalBlocker blocker(box);
            values.removeDuplicates();
            values.sort(Qt::CaseInsensitive);
            box->clear();
            box->addItem(all);
            for (const auto& value : values) box->addItem(value, value);
            box->setCurrentIndex(qMax(0, box->findData(previous)));
        };
        reset(material, materials, tr("Усі матеріали"));
        reset(operation, operations, tr("Усі операції"));
        apply();
    }

    void apply() {
        const bool byMaterial = mode->currentIndex() == 1;
        const bool grouped = byMaterial || grouping->currentIndex() == 1;
        const bool reverse = sorting->currentIndex() == 1;
        grouping->setEnabled(!byMaterial);
        fold->setEnabled(grouped);
        flat->setVisible(!grouped && !reverse);
        tree->setVisible(grouped || reverse);
        tree->clear();
        const auto& snapshot = model->snapshot();
        tree->setHeaderLabels(snapshot.headers.mid(0,10));
        QHash<QString, QTreeWidgetItem*> groups;
        QStringList negatives;
        const auto query = search->text().trimmed();
        for (int i = 0; i < snapshot.rows.size(); ++i) {
            const int index = reverse ? snapshot.rows.size()-1-i : i;
            const auto& row = snapshot.rows[index];
            const auto name = row.value(3).toString();
            const auto balance = row.value(7).toString();
            const bool deficit = balance.startsWith('-');
            const bool match = (material->currentIndex() <= 0 || name == material->currentData().toString())
                && (operation->currentIndex() <= 0 || row.value(1).toString() == operation->currentData().toString())
                && (!negative->isChecked() || deficit)
                && (name.contains(query, Qt::CaseInsensitive) || row.value(2).toString().contains(query));
            table->setRowHidden(index, !match);
            if (!match) continue;
            if (deficit) negatives << tr("%1: %2 %3 (%4)").arg(name, balance, row.value(4).toString(), row.value(0).toString());
            if (!grouped && !reverse) continue;
            auto* parent = tree->invisibleRootItem();
            if (grouped) {
                const auto key = byMaterial ? name + " · " + row.value(4).toString() : row.value(0).toString().mid(3);
                if (!groups.contains(key)) {
                    auto* heading = new QTreeWidgetItem(parent, {key});
                    heading->setFirstColumnSpanned(true);
                    heading->setForeground(0, QColor("#223949"));
                    heading->setBackground(0, QColor("#d4e2ec"));
                    groups.insert(key, heading);
                }
                parent = groups.value(key);
            }
            QStringList cells;
            for (int column = 0; column < 10; ++column) cells << row.value(column).toString();
            auto* item = new QTreeWidgetItem(parent, cells);
            item->setData(0, Qt::UserRole, index);
            if (deficit) item->setForeground(7, QColor("#a5272c"));
            if (!cells.at(5).isEmpty()) item->setForeground(1, QColor("#176040"));
            if (row.value(11).toInt() > 0) {
                item->setForeground(2, QColor("#174e83"));
                QFont linkFont = item->font(2);
                linkFont.setUnderline(true);
                item->setFont(2, linkFont);
            }
        }
        if (!fold->isChecked()) tree->expandAll();
        negatives.removeDuplicates();
        warning->setText(negatives.isEmpty() ? QString() : tr("Від’ємний залишок: %1").arg(negatives.first()));
        warning->setToolTip(negatives.join("\n"));
        warning->setVisible(!negatives.isEmpty());
    }

    QWidget* flat;
    QTableView* table;
    TabularSnapshotTableModel* model;
    QTreeWidget* tree;
    QLineEdit* search;
    QComboBox *material, *operation, *mode, *grouping, *sorting;
    QCheckBox* negative;
    QPushButton* fold;
    QLabel* warning;
};
