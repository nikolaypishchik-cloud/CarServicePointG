#include <QtTest>
#include <QHeaderView>
#include <QLineEdit>
#include <QSettings>
#include <QSpinBox>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QTableView>
#include <QTemporaryDir>
#include <QUuid>
#include "ui/tabs/orders/CurrentOrdersPage.h"
#include "ui/tabs/schedule/PlanningPage.h"
#include "ui/tabs/schedule/PlanningPageSupport.h"
#include "ui/theme/ThemeManager.h"

class PlanningOrdersDisplayTests final : public QObject {
    Q_OBJECT
    QTemporaryDir settings;
    void screenshot(QWidget* widget, const QString& name) {
        const QString dir = qEnvironmentVariable("CS_DISPLAY_SCREENSHOTS");
        if (dir.isEmpty()) return;
        QDir().mkpath(dir);
        widget->grab().save(QDir(dir).filePath(name));
    }
private slots:
    void mobilePlanningPreviewHidesIdentityButKeepsSource() {
        const QString raw = QString::fromUtf8("AA 1212 BA | Audi A6 | 2014 | Client | 0501234567 | Oil | Master | normal | Note | Пробіг: 125000 км | Джерело: мобільний застосунок | Майстер ID: MASTER-Case-1");
        const auto preview = PlanningPageSupport::compactPlanLine(9, raw);
        QVERIFY(preview.startsWith("09:00 AA 1212 BA"));
        QVERIFY(preview.contains(QString::fromUtf8("Джерело: мобільний застосунок")));
        QVERIFY(!preview.contains("MASTER-Case-1"));
        QVERIFY(!preview.contains(QString::fromUtf8("Майстер ID")));
        const auto normalized = PlanningPageSupport::normalizePlanningSlotText(raw);
        QVERIFY(normalized.endsWith(QString::fromUtf8(" | Майстер ID: MASTER-Case-1")));
    }
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("CarServiceTests");
        QCoreApplication::setApplicationName("Display-" + QUuid::createUuid().toString(QUuid::Id128));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        auto db = QSqlDatabase::addDatabase("QSQLITE", "carservice_main");
        db.setDatabaseName(":memory:");
        QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE settings(key TEXT PRIMARY KEY, value TEXT)"));
        QVERIFY(q.exec("CREATE TABLE planning_calendar(day TEXT PRIMARY KEY, note TEXT)"));
        ThemeManager::applySavedTheme();
    }
    void yearFits_data() {
        QTest::addColumn<int>("fontSize");
        QTest::newRow("normal") << 12;
        QTest::newRow("large") << 18;
    }
    void yearFits() {
        QFETCH(int, fontSize);
        PlanningPage page;
        page.resize(1400, 900);
        auto* year = page.findChild<QSpinBox*>("planning_year_sp");
        QVERIFY(year);
        year->setFont(QFont("Times New Roman", fontSize));
        page.show();
        QTest::qWait(100);
        auto* editor = year->findChild<QLineEdit*>();
        QVERIFY(editor);
        for (int value : {2000, 2026, 2100}) {
            year->setValue(value);
            QCoreApplication::processEvents();
            QCOMPARE(editor->text(), QString::number(value));
            const int textWidth = editor->fontMetrics().horizontalAdvance(editor->text());
            QVERIFY2(editor->contentsRect().width() >= textWidth + 4,
                     qPrintable(QString("Editor %1px, text %2px").arg(editor->contentsRect().width()).arg(textWidth)));
            QVERIFY(year->rect().contains(editor->geometry()));
        }
        year->setValue(2026);
        year->stepUp();
        QCOMPARE(year->value(), 2027);
        year->stepDown();
        QCOMPARE(year->value(), 2026);
        screenshot(page.findChild<QWidget*>("planning_topbar"), QString("year-%1.png").arg(fontSize));
    }
    void overdueSelectionVisible() {
        // Keep the page hidden so its asynchronous database refresh cannot replace test rows.
        CurrentOrdersPage page;
        auto* table = page.findChild<QTableView*>("premiumCurrentOrdersTable");
        QVERIFY(table);
        table->setParent(nullptr);
        QScopedPointer<QTableView> owned(table);
        QStandardItemModel model(2, 9);
        model.setHorizontalHeaderLabels({"No", "Opened", "Status", "Plate", "Car", "Client", "Master", "Work", "Parts"});
        const QColor overdue("#ff9f9f"), selected("#dce8fb"), marker("#c62828");
        for (int col = 0; col < 9; ++col) {
            model.setData(model.index(0, col), col == 0 ? "12" : "Volvo");
            model.setData(model.index(0, col), overdue, Qt::BackgroundRole);
            model.setData(model.index(1, col), col == 0 ? "6" : "Audi");
        }
        table->setModel(&model);
        table->resize(1550, 300);
        table->show();
        QTest::qWait(100);
        auto colorAt = [&](int row, int col, bool bottom) {
            const QRect cell = table->visualRect(model.index(row, col));
            const QPoint point(cell.right() - 8, cell.bottom() - (bottom ? 1 : 9));
            const QPixmap captured = table->viewport()->grab();
            const qreal dpr = captured.devicePixelRatio();
            return captured.toImage().pixelColor(QPoint(qRound(point.x() * dpr), qRound(point.y() * dpr)));
        };
        QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier, table->visualRect(model.index(1, 0)).center());
        QCOMPARE(colorAt(1, 0, false), selected);
        QCOMPARE(colorAt(0, 0, false), overdue);
        QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier, table->visualRect(model.index(0, 0)).center());
        for (int col = 0; col < 3; ++col) {
            QCOMPARE(colorAt(0, col, false), selected);
            QCOMPARE(colorAt(0, col, true), marker);
        }
        QCOMPARE(colorAt(1, 0, false), QColor("#ffffff"));
        screenshot(table, "overdue-selected.png");
        table->clearFocus();
        QCOMPARE(colorAt(0, 0, false), selected);
        table->selectionModel()->select(model.index(1, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
        QCOMPARE(colorAt(1, 0, false), selected);
        QCOMPARE(colorAt(0, 0, false), selected);
        table->clearSelection();
        QCOMPARE(colorAt(0, 0, false), overdue);
    }
    void cleanupTestCase() {
        QSqlDatabase::removeDatabase("carservice_main");
    }
};
QTEST_MAIN(PlanningOrdersDisplayTests)
#include "PlanningOrdersDisplayTests.moc"
