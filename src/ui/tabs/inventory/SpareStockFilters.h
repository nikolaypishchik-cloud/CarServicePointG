#pragma once

#include "ui/common/TabularSnapshotTableModel.h"
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QTableView>

class SpareStockFilters final : public QWidget {
public:
    SpareStockFilters(QTableView* view, TabularSnapshotTableModel* source, QWidget* parent)
        : QWidget(parent), table(view), model(source) {
        auto* controls = new QHBoxLayout(this);
        controls->setContentsMargins(0, 0, 0, 0);
        total = new QLabel(this);
        total->setObjectName("spareFilterCount");
        search = new QLineEdit(this);
        search->setObjectName("spareFilterSearch");
        search->setPlaceholderText(tr("Пошук запчастини, OEM або авто"));
        search->setClearButtonEnabled(true);
        overdue = new QComboBox(this);
        overdue->setObjectName("spareFilterOverdue");
        overdue->addItems({tr("Усі позиції"), tr("Протерміновано")});
        master = new QComboBox(this);
        master->setAccessibleName(tr("Майстер"));
        supplier = new QComboBox(this);
        supplier->setAccessibleName(tr("Постачальник"));
        controls->addWidget(total);
        controls->addWidget(search, 1);
        for (auto* box : {overdue, master, supplier}) {
            controls->addWidget(box);
            connect(box, &QComboBox::currentIndexChanged, this, [this] { apply(); });
        }
        connect(search, &QLineEdit::textChanged, this, [this] { apply(); });
        connect(model, &QAbstractItemModel::modelReset, this, [this] { reload(); });
        reload();
    }

    void reset() {
        for (auto* box : {overdue, master, supplier}) {
            QSignalBlocker blocker(box);
            box->setCurrentIndex(0);
        }
        search->clear();
        apply();
    }

private:
    void reload() {
        const auto choices = [this](QComboBox* box, int column, const QString& label) {
            QSignalBlocker blocker(box);
            const auto selected = box->currentData();
            QStringList values;
            for (const auto& row : model->snapshot().rows) values.append(row.value(column).toString());
            values.removeDuplicates();
            values.sort(Qt::CaseInsensitive);
            box->clear();
            box->addItem(label);
            for (const auto& value : values) box->addItem(value.isEmpty() ? tr("Не вказано") : value, value);
            box->setCurrentIndex(selected.isValid() ? qMax(0, box->findData(selected)) : 0);
        };
        choices(master, 3, tr("Усі майстри"));
        choices(supplier, 7, tr("Усі постачальники"));
        apply();
    }

    void apply() {
        const auto& rows = model->snapshot().rows;
        const auto query = search->text().trimmed();
        int visible = 0;
        for (int i = 0; i < rows.size(); ++i) {
            const auto& row = rows.at(i);
            const bool match = (row.value(1).toString().contains(query, Qt::CaseInsensitive)
                                || row.value(2).toString().contains(query, Qt::CaseInsensitive))
                && (!overdue->currentIndex() || row.value(9).toInt() > 0)
                && (master->currentIndex() <= 0 || row.value(3).toString() == master->currentData().toString())
                && (supplier->currentIndex() <= 0 || row.value(7).toString() == supplier->currentData().toString());
            table->setRowHidden(i, !match);
            if (match) ++visible;
        }
        total->setText(tr("Запчастини під авто · %1 із %2").arg(visible).arg(rows.size()));
    }

    QTableView* table;
    TabularSnapshotTableModel* model;
    QLabel* total;
    QLineEdit* search;
    QComboBox *overdue, *master, *supplier;
};
