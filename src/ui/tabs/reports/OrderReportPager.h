#pragma once

#include "ui/tabs/reports/ReportsTableSupport.h"
#include "core/util/DateFormats.h"

#include <QCollator>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableView>
#include <algorithm>

class OrderReportPager final : public QWidget {
public:
    OrderReportPager(QTableView* table, ReportsTableSupport::ReportsSnapshotTableModel* model, QWidget* parent)
        : QWidget(parent), m_table(table), m_model(model) {
        auto* bar = new QHBoxLayout(this);
        bar->setContentsMargins(8, 2, 8, 2);
        m_status = new QLabel(this);
        m_status->setStyleSheet("color:#172a3a;");
        bar->addWidget(m_status);
        bar->addStretch();
        m_size = new QComboBox(this);
        m_size->setObjectName("reportOrdersPageSize");
        m_size->setAccessibleName(tr("Нарядів на сторінці"));
        for (int size : {10, 25, 50, 100}) m_size->addItem(QString::number(size), size);
        m_size->setCurrentIndex(2);
        m_previous = new QPushButton(tr("Назад"), this);
        m_next = new QPushButton(tr("Далі"), this);
        m_next->setObjectName("reportOrdersNextPage");
        bar->addWidget(m_size);
        bar->addWidget(m_previous);
        bar->addWidget(m_next);
        setObjectName("orderReportPager");
        setAttribute(Qt::WA_StyledBackground);
        setStyleSheet("#orderReportPager { background:#eef3f7; }");
        m_status->setStyleSheet("background:#eef3f7;color:#172a3a;padding:3px;");
        connect(m_size, &QComboBox::currentIndexChanged, this, [this] { m_page = 0; display(); });
        connect(m_previous, &QPushButton::clicked, this, [this] { --m_page; display(); });
        connect(m_next, &QPushButton::clicked, this, [this] { ++m_page; display(); });
        auto* header = m_table->horizontalHeader();
        header->setSectionsClickable(true);
        connect(header, &QHeaderView::sectionClicked, this, [this, header](int column) {
            m_descending = m_sortColumn == column && !m_descending;
            m_sortColumn = column;
            header->setSortIndicator(column, m_descending ? Qt::DescendingOrder : Qt::AscendingOrder);
            header->setSortIndicatorShown(true);
            sort();
            m_page = 0;
            display();
        });
        m_collator.setNumericMode(true);
        connect(model, &QAbstractItemModel::modelReset, this, [this] {
            if (m_rendering) return;
            m_snapshot = {};
            m_page = 0;
            setEnabled(false);
            m_status->clear();
        });
    }

    void setSnapshot(const OrdersSnapshot& filtered) {
        setEnabled(true);
        m_snapshot = filtered;
        m_page = 0;
        sort();
        display();
    }

private:
    static QString sortText(const OrdersSnapshotRow& row, int column) {
        switch (column) {
        case 0: return DateFormats::parseDate(row.date).toString(Qt::ISODate);
        case 1: return row.orderNumber;
        case 2: return row.car;
        case 3: return row.invoiceNumbers.join(' ');
        case 5: return row.paymentDue;
        default: return row.paymentStatus;
        }
    }

    void sort() {
        if (m_sortColumn < 0) return;
        const auto money = [](QString value) {
            return value.simplified().remove(' ').replace(',', '.').toDouble();
        };
        std::stable_sort(m_snapshot.rows.begin(), m_snapshot.rows.end(), [&](const auto& a, const auto& b) {
            if (m_sortColumn == 4) return m_descending ? money(b.invoiceTotal) < money(a.invoiceTotal)
                                                       : money(a.invoiceTotal) < money(b.invoiceTotal);
            const int cmp = m_collator.compare(sortText(a, m_sortColumn), sortText(b, m_sortColumn));
            return m_descending ? cmp > 0 : cmp < 0;
        });
    }

    void display() {
        const int size = m_size->currentData().toInt();
        const int count = m_snapshot.rows.size();
        const int pages = qMax(1, (count + size - 1) / size);
        m_page = qBound(0, m_page, pages - 1);
        OrdersSnapshot slice;
        slice.rows = m_snapshot.rows.mid(m_page * size, size);
        m_rendering = true;
        ReportsTableSupport::applyOrdersSnapshot(m_table, m_model, slice);
        m_rendering = false;
        m_previous->setEnabled(m_page > 0);
        m_next->setEnabled(m_page + 1 < pages);
        m_status->setText(tr("Показано %1–%2 із %3 · Сторінка %4 із %5")
            .arg(count ? m_page * size + 1 : 0).arg(qMin(count, (m_page + 1) * size)).arg(count).arg(m_page + 1).arg(pages));
    }

    QTableView* m_table;
    ReportsTableSupport::ReportsSnapshotTableModel* m_model;
    QLabel* m_status;
    QComboBox* m_size;
    QPushButton* m_previous;
    QPushButton* m_next;
    OrdersSnapshot m_snapshot;
    QCollator m_collator;
    int m_page = 0;
    int m_sortColumn = -1;
    bool m_descending = false;
    bool m_rendering = false;
};
