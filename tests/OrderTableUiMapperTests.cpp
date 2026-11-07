#include <QtTest/QtTest>
#include <QHeaderView>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTableView>
#include "ui/common/OrderTableUiMapper.h"

class OrderTableUiMapperTests : public QObject {
    Q_OBJECT
private slots:
    void internalColumns_data() {
        QTest::addColumn<bool>("history");
        QTest::newRow("current") << false;
        QTest::newRow("history") << true;
    }
    void internalColumns() {
        QFETCH(bool, history);
        QStandardItemModel model;
        const QStringList visible = {QString::fromUtf8("№"), QString::fromUtf8("Відкрито"),
            QString::fromUtf8("Статус"), QString::fromUtf8("Номер"), QString::fromUtf8("Авто"),
            QString::fromUtf8("Клієнт"), QString::fromUtf8("Майстер"), QString::fromUtf8("Роботи"),
            QString::fromUtf8("Запчастини")};
        const QStringList internal = {"__rank", "__internal_number", "__order_uid", "__order_year",
            "__parts_sort_rank", "__date_rank", "__order_number_sort", "__effective_datetime_sort",
            "__opened_datetime_sort", "__plate_search", "__future_search_field"};
        model.setHorizontalHeaderLabels(visible + internal);
        model.setRowCount(1);
        model.setData(model.index(0, visible.size()+1), 468);
        QSortFilterProxyModel proxy;
        proxy.setSourceModel(&model);
        QTableView table;
        table.setModel(&proxy);
        const QByteArray legacyState = table.horizontalHeader()->saveState();
        auto apply = [&] {
            if (history) OrderTableUiMapper::applyHistoryModelShape(&table, &model);
            else OrderTableUiMapper::applyCurrentOrdersModelShape(&table, &model);
        };
        for (int refresh=0; refresh<2; ++refresh) {
            QVERIFY(table.horizontalHeader()->restoreState(legacyState));
            apply();
            for (int column=0; column<model.columnCount(); ++column)
                QCOMPARE(table.isColumnHidden(column), column>=visible.size());
            QCOMPARE(proxy.data(proxy.index(0,visible.size()+1)).toInt(),468);
        }
        model.clear();
        model.setHorizontalHeaderLabels(visible + internal + QStringList{"__new_projection"});
        apply();
        for (int column=0; column<model.columnCount(); ++column)
            QCOMPARE(table.isColumnHidden(column), column>=visible.size());
    }
};
QTEST_MAIN(OrderTableUiMapperTests)
#include "OrderTableUiMapperTests.moc"
