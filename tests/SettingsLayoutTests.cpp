#include <QMessageBox>
#include "core/crm/CrmPageReadService.h"
#include "ui/tabs/crm/CrmPage.h"
#include "core/crm/CrmPageApplicationService.h"
#include "core/cloudsync/CloudSyncSchemaService.h"
#include "core/services/settings/PersonnelEmploymentHistory.h"
#include "core/services/settings/PersonnelSettingsService.h"
#include "ui/tabs/intake/VehicleIntakePage.h"
#include <QStackedWidget>
#include <QPushButton>
#include "core/crm/VehicleIntakeService.h"
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QLineEdit>
#include <QTableWidget>
#include <QPushButton>
#include <QComboBox>
#include <QHeaderView>
#include <QFontDatabase>
#include <QPdfWriter>
#include <QTextBrowser>
#include <QTabWidget>
#include <QStackedWidget>
#include <QScrollBar>
#include "ui/tabs/statistics/StatsPage.h"
#include "core/services/WorkshopSpareStockService.h"
#include <QFile>
#include "app/App.h"
#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/db/sqlite/DbFacadeSupport.h"
#include "core/services/StaffAccountService.h"
#include "core/services/AccessControlService.h"
#include "core/services/ActivityAuditService.h"
#include "core/crm/CrmClientService.h"
#include "core/crm/CrmTaskService.h"
#include "ui/common/dialogs/OrderDialog.h"
#include "core/services/orders/OrderDialogReadQueryService.h"
#include "core/util/PlateUtils.h"
#include "ui/theme/ThemeManager.h"
#include "ui/tabs/settings/SettingsGeneralSection.h"
#include "ui/tabs/settings/SettingsIntegrationsSection.h"
#include "ui/tabs/settings/SettingsDetailUiSupport.h"
#include "ui/tabs/settings/AccessControlPage.h"
#include "ui/tabs/cloudsync/CloudSyncPage.h"
#include "core/services/PdfWebsiteFooter.h"
#include "core/backup/BackupPolicy.h"
#include "core/backup/BackupPaths.h"
#include "core/backup/BackupManifestService.h"
#include "ui/tabs/settings/StaffSessionDialog.h"
#include "core/util/StoreNameNormalizer.h"
#include "core/services/ReportsDataLoaderLocalSupport.h"

class SettingsLayoutTests : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("CarServiceTests");
        QCoreApplication::setApplicationName("Safety-Settings-" + QUuid::createUuid().toString(QUuid::Id128));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());
        const QString fontPath=qEnvironmentVariable("WINDIR")+"/Fonts/times.ttf";
        if(QFile::exists(fontPath)) {
            const int fontId=QFontDatabase::addApplicationFont(fontPath);
            QVERIFY(fontId>=0);
            QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(fontId).first(),11));
        }
        QString runtimeError;
        QVERIFY2(Database::prepareRuntime(&runtimeError), qPrintable(runtimeError));
        QVERIFY2(Database::openPrepared(&runtimeError), qPrintable(runtimeError));
        auto db=Database::db();
        QVERIFY(Database::ensureSchema());
        QVERIFY(rebuildRouterViews(db));
        QString error;
        QVERIFY(StaffAccountService::ensureSchema(db,&error));
        QVERIFY(AccessControlService::ensureSchema(db,&error));
        QSqlQuery query(db);
        QVERIFY(query.exec("INSERT INTO staff_accounts(id,display_name,role,active) VALUES('qa-owner','Owner','owner',1)"));
        QVERIFY(query.exec("INSERT INTO staff_accounts(id,display_name,role,active) VALUES('qa-admin','Administrator','admin',1)"));
        ThemeManager::setTheme("graphite");
    }
    void staffPinFocusOnShowAndAccountSelection_data() {
        QTest::addColumn<bool>("configured");
        QTest::newRow("first-login") << false;
        QTest::newRow("existing-pin") << true;
    }
    void staffPinFocusOnShowAndAccountSelection() {
        QFETCH(bool, configured);
        if (configured) {
            QString error;
            QVERIFY2(StaffAccountService::setPin( "qa-owner", "1234", &error), qPrintable(error));
            QVERIFY2(StaffAccountService::setPin( "qa-admin", "1234", &error), qPrintable(error));
        }
        StaffSessionDialog dialog;
        dialog.show();
        dialog.activateWindow();
        QLineEdit* pin = nullptr;
        for (auto* field : dialog.findChildren<QLineEdit*>())
            if (field->placeholderText() == QString::fromUtf8("Персональний PIN")) pin = field;
        QVERIFY(pin);
        QTRY_VERIFY(pin->hasFocus());
        QTest::keyClicks(QApplication::focusWidget(), "1234");
        QCOMPARE(pin->text(), QString("1234"));
        auto* account = dialog.findChild<QComboBox*>(); QVERIFY(account);
        QVERIFY(account->count() >= 2);
        account->setFocus();
        QTest::keyClick(account, Qt::Key_Down);
        QTRY_VERIFY(pin->hasFocus());
        QVERIFY(pin->text().isEmpty());
        dialog.hide(); QApplication::processEvents();
        dialog.show();
        // Offscreen has no window manager to reactivate a hidden native window.
        // Simulate activation only; the dialog must still choose the PIN itself.
        QApplication::setActiveWindow(&dialog);
        QTRY_VERIFY(pin->hasFocus());
        const auto output = qEnvironmentVariable("CS_TEST_SCREENSHOTS");
        if (!output.isEmpty()) QVERIFY(dialog.grab().save(output + "/pin-focus.png"));
        if (configured) {
            QString error;ActivityAuditActor actor;
            QVERIFY(!StaffAccountService::signIn("qa-owner","0000",&actor,&error));
            QVERIFY2(StaffAccountService::signIn("qa-owner","1234",&actor,&error),qPrintable(error));
            QCOMPARE(actor.role,QString("owner"));
        }

    }
    void intakeReadModelsPreserveDataOnFailure() {
        ActivityAuditService::setCurrentActor({"qa-owner","Owner","owner","intake-test"});
        const auto db=Database::db();
        QString uid,error;
        QVERIFY2(CloudSyncSchemaService::ensureSchema(db,&error),qPrintable(error));
        const QString checklist=R"({"act_number":"TEST-ACT","client_name":"Test client","vehicle_label":"Audi A6"})";
        QVERIFY2(VehicleIntakeService::saveIntake({{"mileage",123456},{"fuel_level","half"},
            {"checklist_json",checklist}}, &uid,&error),qPrintable(error));
        VehicleIntakeService::IntakeRecord record;
        QVERIFY2(VehicleIntakeService::loadIntake(uid,&record,&error),qPrintable(error));
        QCOMPARE(record.uid,uid); QCOMPARE(record.mileage,QString("123456"));
        QCOMPARE(record.fuelLevel,QString("half")); QCOMPARE(record.checklistJson,checklist);
        QVERIFY(record.pdfPath.isEmpty()); QVERIFY(record.signaturePath.isEmpty());
        QVector<VehicleIntakeService::IntakeRecord> rows;
        QVERIFY2(VehicleIntakeService::listIntakes(&rows,&error),qPrintable(error));
        QCOMPARE(rows.size(),1); QCOMPARE(rows[0].uid,uid);
        QVERIFY(!VehicleIntakeService::loadIntake("missing-intake",&record,&error));
        QVERIFY(!error.isEmpty()); QVERIFY(record.uid.isEmpty());
        VehicleIntakePage page;
        auto* table=page.findChild<QTableWidget*>(); QVERIFY(table);
        page.refresh(); QCOMPARE(table->rowCount(),1);
        QCOMPARE(table->item(0,1)->text(),QString("TEST-ACT"));
        const auto edits=page.findChildren<QLineEdit*>(); QVERIFY(!edits.isEmpty());
        edits.first()->setText("unsaved-input");
        QSqlQuery q(db);
        QVERIFY(q.exec("ALTER TABLE vehicle_intake RENAME COLUMN pdf_path TO unavailable_pdf_path"));
        QVERIFY(!VehicleIntakeService::listIntakes(&rows,&error));
        QVERIFY(rows.isEmpty()); QVERIFY(!error.isEmpty());
        QVERIFY(!VehicleIntakeService::loadIntake(uid,&record,&error));
        QVERIFY(record.uid.isEmpty()); QVERIFY(!error.isEmpty());
        bool warningShown=false;
        QTimer::singleShot(0,&page,[&]{
            for(auto* widget:QApplication::topLevelWidgets()) if(auto* box=qobject_cast<QMessageBox*>(widget)) {
                warningShown=true; box->accept();
            }
        });
        page.refresh(); QVERIFY(warningShown);
        QCOMPARE(table->rowCount(),1); QCOMPARE(table->item(0,1)->text(),QString("TEST-ACT"));
        QCOMPARE(edits.first()->text(),QString("unsaved-input"));
        QVERIFY(q.exec("ALTER TABLE vehicle_intake RENAME COLUMN unavailable_pdf_path TO pdf_path"));
        page.refresh(); QCOMPARE(table->rowCount(),1);
        QVERIFY(VehicleIntakeService::loadIntake(uid,&record,&error));
        QCOMPARE(record.mileage,QString("123456"));
        q.prepare("UPDATE vehicle_intake SET pdf_path=? WHERE intake_uid=?");
        q.addBindValue("test-act.pdf");q.addBindValue(uid);QVERIFY(q.exec());
        page.resize(1320,850); page.show(); table->selectRow(0);
        auto* steps=page.findChild<QStackedWidget*>("intakeSteps"); QVERIFY(steps);
        page.findChild<QLineEdit*>("intakeOrder")->setText("TEST-ORDER");
        page.findChild<QLineEdit*>("intakePhone")->setText("0501234567");
        page.findChild<QComboBox*>("intakeMaster")->setEditText("Test master");
        const auto output=qEnvironmentVariable("CS_INTAKE_SCREENSHOTS");
        if(!output.isEmpty()) QVERIFY(page.grab().save(output+"/intake-1.png"));
        page.findChild<QPushButton*>("intakeContinue")->click(); QCOMPARE(steps->currentIndex(),1);
        if(!output.isEmpty()) QVERIFY(page.grab().save(output+"/intake-2.png"));
        page.refresh(); QCOMPARE(steps->currentIndex(),1);
        page.findChild<QPushButton*>("intakeBack")->click(); QCOMPARE(steps->currentIndex(),0);
        QCOMPARE(page.findChild<QLineEdit*>("intakePhone")->text(),QString("0501234567"));
        QCOMPARE(table->rowCount(),1);
        QVERIFY2(VehicleIntakeService::loadIntake(uid,&record,&error),qPrintable(error));
        QCOMPARE(record.pdfPath,QString("test-act.pdf"));
    }
    void personnelApplicationBoundaryPreservesHistory() {
        ActivityAuditService::setCurrentActor({"qa-owner","Owner","owner","personnel-test"});
        QString error;
        const auto today=QDate::currentDate();
        QVERIFY2(PersonnelSettingsService::hireEmployee("Boundary employee","master",today.addDays(-30).toString(Qt::ISODate),&error),qPrintable(error));
        MasterRow employee;
        for(const auto& row:PersonnelSettingsService::loadPersonnel(true))
            if(row.name=="Boundary employee")employee=row;
        QVERIFY(!employee.id.isEmpty());
        QVERIFY2(PersonnelEmploymentHistory::edit(employee,"Renamed employee","master",&error),qPrintable(error));
        QVERIFY(!PersonnelEmploymentHistory::edit(employee,"Stale edit","master",&error));
        QVERIFY2(PersonnelEmploymentHistory::setActive(employee.id,false,today.addDays(-10).toString(Qt::ISODate),&error),qPrintable(error));
        const auto before=PersonnelEmploymentHistory::load(employee.id,&error);
        QVERIFY(!before.isEmpty()); QVERIFY(error.isEmpty());
        QVERIFY(!PersonnelEmploymentHistory::setActive(employee.id,true,today.addDays(-11).toString(Qt::ISODate),&error));
        QCOMPARE(PersonnelEmploymentHistory::load(employee.id),before);
        QVERIFY2(PersonnelEmploymentHistory::setActive(employee.id,true,today.addDays(-5).toString(Qt::ISODate),&error),qPrintable(error));
        QCOMPARE(PersonnelEmploymentHistory::load(employee.id,&error)["periods"].toArray().size(),2);
        QVERIFY(error.isEmpty());
    }
    void supplierDisplayUsesOneName() {
        const auto expected = QString::fromUtf8("Автонова-D");
        for (const auto& alias : {QString::fromUtf8("Автонова-Д"), QString::fromUtf8("Автонова"),
                                 QString("Avtonova-D"), QString("Autonova-D"), expected}) {
            QCOMPARE(StoreNameNormalizer::canonicalDisplayName(alias), expected);
            QCOMPARE(StoreNameNormalizer::canonKey(alias), QString("avtonova_d"));
            QCOMPARE(ReportsDataLoader::LocalSupport::resolveStoreForReports(alias, {}), expected);
        }
        QCOMPARE(ReportsDataLoader::LocalSupport::resolveStoreForReports({}, "A-12345"), expected);
    }
    void compactFields() {
        std::unique_ptr<QWidget> page(SettingsDetailUiSupport::wrapDetailWithBack(
            SettingsIntegrationsSection::build("int.apikeys",nullptr,{},{},{})));
        page->resize(1000,700);page->show();QTest::qWait(100);
        auto* pages=page->findChild<QStackedWidget*>("apiSettingsPages");
        QVERIFY(pages);
        int checkedFields=0;
        for(int index=1;index<pages->count();++index) {
            pages->setCurrentIndex(index);
            QTest::qWait(50);
            QList<QWidget*> fields;
            for(auto* field:pages->currentWidget()->findChildren<QWidget*>()) if(field->property("settingsCompactInput").toBool()) {
                QVERIFY(field->isVisible());
                fields.append(field);
                ++checkedFields;
                QVERIFY(field->height()<=qRound(field->logicalDpiY()*5.0/25.4));
                QVERIFY(field->height()>=QFontMetrics(field->font()).height());
            }
            QVERIFY(!fields.isEmpty());
            for(int i=1;i<fields.size();++i) {
                const int gap=fields[i]->mapTo(page.get(),QPoint()).y()
                    -fields[i-1]->mapTo(page.get(),QPoint(0,fields[i-1]->height())).y();
                QVERIFY2(gap>=1,qPrintable(QString::number(gap)));
            }
        }
        QVERIFY(checkedFields>=12);
        const QString out=qEnvironmentVariable("CS_SETTINGS_QA_DIR");
        if(!out.isEmpty()) QVERIFY(page->grab().save(out+"/api.png"));
    }
    void rolesAndCloud() {
        std::unique_ptr<QWidget> page(SettingsDetailUiSupport::wrapDetailWithBack(new AccessControlPage));
        page->resize(1100,750);page->show();QTest::qWait(150);
        auto* table=page->findChild<QTableWidget*>("accessAccountsTable");QVERIFY(table);
        QVERIFY(table->rowCount()>=2);
        const QString rolesOutput=qEnvironmentVariable("CS_SETTINGS_QA_DIR");
        if(!rolesOutput.isEmpty()) QVERIFY(page->grab().save(rolesOutput+"/roles.png"));
        for(int row=0;row<table->rowCount();++row) for(int col:{1,4}) {
            auto* cell=table->cellWidget(row,col);QVERIFY(cell);
            for(auto* control:cell->findChildren<QWidget*>(QString(),Qt::FindDirectChildrenOnly)) {
                qInfo() << row << col << cell->rect() << control->geometry() << cell->sizeHint() << table->rowHeight(row);
                QVERIFY(cell->rect().contains(control->geometry()));
            }
        }
        const QString out=qEnvironmentVariable("CS_SETTINGS_QA_DIR");
        if(!out.isEmpty()) QVERIFY(page->grab().save(out+"/roles.png"));
        std::unique_ptr<QWidget> cloud(SettingsDetailUiSupport::wrapDetailWithBack(new CloudSyncPage));
        cloud->resize(1100,750);cloud->show();QTest::qWait(100);
        QVERIFY(cloud->findChildren<QLineEdit*>().size()>2);
        if(!out.isEmpty()) QVERIFY(cloud->grab().save(out+"/cloud.png"));
    }
    void accountReloadKeepsLayout() {
        ActivityAuditService::setCurrentActor({"qa-owner","Owner","owner","layout-test"});
        std::unique_ptr<QWidget> page(SettingsDetailUiSupport::wrapDetailWithBack(new AccessControlPage));
        page->resize(1500,900);page->show();QTest::qWait(150);
        auto* table=page->findChild<QTableWidget*>("accessAccountsTable");QVERIFY(table);
        auto* combo=table->cellWidget(table->rowCount()-1,1)->findChild<QComboBox*>();QVERIFY(combo);
        QVERIFY(combo->isEnabled());QVERIFY(combo->count()>=6);QVERIFY(!combo->currentText().isEmpty());
        combo->showPopup();QApplication::processEvents();QVERIFY(combo->view()->isVisible());
        const QString roleOutput=qEnvironmentVariable("CS_SETTINGS_QA_DIR");
        if(!roleOutput.isEmpty()) { QVERIFY(page->grab().save(roleOutput+"/roles-enabled.png"));QVERIFY(combo->view()->window()->grab().save(roleOutput+"/roles-popup.png")); }
        combo->hidePopup();
        QPushButton* save=nullptr;
        for(auto* button:page->findChildren<QPushButton*>())
            if(button->text()==QString::fromUtf8("Зберегти працівників")) save=button;
        QVERIFY(save);QVERIFY(save->isEnabled());
        const auto widths=[table] {
            QList<int> result;
            for(int col=0;col<table->columnCount();++col) result.append(table->columnWidth(col));
            return result;
        };
        const auto initial=widths();
        const int chosen=combo->findData("manager");QVERIFY(chosen>=0);
        const QString accountId=table->item(table->rowCount()-1,0)->data(Qt::UserRole).toString();
        combo->setFocus();combo->setCurrentIndex(chosen-1);QTest::keyClick(combo,Qt::Key_Down);
        QCOMPARE(combo->currentData().toString(),QString("manager"));
        save->click();QTest::qWait(100);
        QSqlQuery roleQuery(Database::db());roleQuery.prepare("SELECT role FROM staff_accounts WHERE id=?");roleQuery.addBindValue(accountId);
        QVERIFY(roleQuery.exec());QVERIFY(roleQuery.next());QCOMPARE(roleQuery.value(0).toString(),QString("manager"));
        auto* reset=table->cellWidget(0,4)->findChild<QPushButton*>();
        QVERIFY(reset);
        const QString style=reset->styleSheet();
        const int height=table->rowHeight(0);
        for(int iteration=0;iteration<3;++iteration) {
            save->click();QTest::qWait(100);
            QCOMPARE(table->cellWidget(0,4)->findChild<QPushButton*>()->styleSheet(),style);
            QCOMPARE(widths(),initial);
            QCOMPARE(table->rowHeight(0),height);
            QVERIFY(qAbs(table->horizontalHeader()->length()-table->viewport()->width())<=2);
        }
    }
    void spareStockHistoryPrefill() {
        ActivityAuditService::setCurrentActor({"qa-owner","Owner","owner","stock-prefill"});
        auto db=Database::db();QSqlQuery q(db);
        QVERIFY(q.exec("INSERT INTO masters(id,name) VALUES('stock-history-master','History Master')"));
        QVERIFY2(q.exec("INSERT INTO work_orders(number,order_number,order_uid,plate,plate_norm,vin,car,client,client_phone,color,car_year,odometer,engine_type,engine_volume,fuel_type,status,opened_at,master_id) "
                       "VALUES(201,201,'stock-history','AA 1234 BA','AA1234BA','SAMEVIN','AUDI Q7','Client','0980113311','Black',2016,200000,'CRTC','3.0','diesel','closed','2026-09-01','stock-history-master')"),qPrintable(q.lastError().text()));
        QVERIFY2(q.exec("INSERT INTO work_orders(number,order_number,order_uid,plate,plate_norm,vin,car,client,client_phone,car_year,odometer,status,opened_at,master_id) "
                       "VALUES(202,202,'stock-recent','AA1234BA','AA1234BA','SAMEVIN','AUDI Q7','Client','0980113311',2016,234654,'closed','2026-10-01','stock-history-master')"),qPrintable(q.lastError().text()));
        QList<OrderSnapshotService::OrderSnapshot> history;
        QString historyError;
        QVERIFY2(OrderDialogReadQueryService::loadVehicleHistory("aa 1234 ba","samevin",&history,&historyError),qPrintable(historyError));
        QCOMPARE(history.size(),2);QCOMPARE(history.first().number,202);
        QVERIFY(OrderDialogReadQueryService::loadVehicleHistory("AA1234BA","DIFFERENTVIN",&history,&historyError));
        QVERIFY(history.isEmpty());
        QVERIFY(OrderDialogReadQueryService::loadVehicleHistory("AA1234BA","",&history,&historyError));
        QCOMPARE(history.size(),2);
        QVERIFY(q.exec("DROP VIEW router_work_orders"));
        QVERIFY(!OrderDialogReadQueryService::loadVehicleHistory("AA1234BA","SAMEVIN",&history,&historyError));
        QVERIFY(history.isEmpty());QVERIFY(!historyError.isEmpty());
        QVERIFY(rebuildRouterViews(db));
        WorkshopSpareOrder spare;spare.id=987;spare.brand="AUDI";spare.model="Q7";spare.plate="aa1234ba";spare.vin="SAMEVIN";
        OrderDialog dialog;dialog.prefillFromSpareStock(spare,1);dialog.resize(1100,850);dialog.show();QTest::qWait(150);
        const auto state=dialog.serializeDraftState();
        QCOMPARE(state.value("phone").toString(),QString("0980113311"));
        QCOMPARE(state.value("client").toString(),QString("Client"));
        QCOMPARE(state.value("year").toInt(),2016);QCOMPARE(state.value("odometer").toInt(),234654);
        QCOMPARE(state.value("engineVolume").toString(),QString("3.0"));
        QCOMPARE(state.value("color").toString().toLower(),QString("black"));
        auto* focused=QApplication::focusWidget();
        auto* focus=qobject_cast<QLineEdit*>(focused);
        if(!focus && focused)focus=focused->findChild<QLineEdit*>();
        QVERIFY(focus);QVERIFY(!focus->selectedText().isEmpty());
        spare.vin="DIFFERENTVIN";
        OrderDialog other;other.prefillFromSpareStock(spare,1);
        QVERIFY(other.serializeDraftState().value("client").toString().isEmpty());
        QVERIFY(q.exec("DELETE FROM work_orders WHERE number IN (201,202)"));
    }
    void crmVehiclePrefillAndOptionalTaskOrder() {
        auto db=QSqlDatabase::database("carservice_main");
        QString error;
        QVERIFY2(CrmClientService::ensureLocalSchema(db,&error),qPrintable(error));
        QSqlQuery q(db);
        QVERIFY(q.exec("INSERT INTO crm_clients(client_uid,full_name,phone) VALUES('crm-test','Test Client','0980113311')"));
        QVERIFY(q.exec("INSERT INTO crm_vehicles(vehicle_uid,client_uid,plate,brand,model,vin,year,mileage) "
                       "VALUES('crm-car','crm-test','AA1234BA','OPEL','VECTRA','VIN-TEST',2008,234654)"));
        QVERIFY(q.exec("INSERT INTO crm_vehicles(vehicle_uid,client_uid,plate,brand,model,is_primary) "
                       "VALUES('crm-primary','crm-test','AA9999BA','AUDI','Q7',1)"));
        ActivityAuditService::setCurrentActor({"qa-owner","Owner","owner","crm-test"});
        OrderDialog dialog;
        QVERIFY(dialog.prefillFromCrmVehicle("crm-test","crm-car"));
        const auto state=dialog.serializeDraftState();
        QCOMPARE(PlateUtils::normalizePlate(state.value("plate").toString()),QString("AA1234BA"));
        QCOMPARE(state.value("car").toString(),QString("OPEL VECTRA"));
        QCOMPARE(state.value("vin").toString(),QString("VIN-TEST"));
        QCOMPARE(state.value("client").toString(),QString("Test Client"));
        QCOMPARE(state.value("phone").toString(),QString("0980113311"));
        QCOMPARE(state.value("odometer").toInt(),234654);
        QCOMPARE(state.value("year").toInt(),2008);
        QVERIFY(!dialog.prefillFromCrmVehicle("another-client","crm-car"));
        QVERIFY2(CrmTaskService::createTaskDirect(db,"task-test","crm-test",QString(),QString(),
                                                "Call client",QString(),"normal",QString(),&error),qPrintable(error));
        QVERIFY(q.exec("SELECT order_uid,vehicle_uid,due_at,assigned_to FROM crm_client_tasks WHERE task_uid='task-test'"));
        QVERIFY(q.next());
        for(int col=0;col<4;++col) {
            QVERIFY(!q.value(col).isNull());
            QCOMPARE(q.value(col).toString(),QString(""));
        }
        QVERIFY(q.exec("SELECT COUNT(*) FROM work_orders"));
        QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),0);
        QVERIFY(q.exec("INSERT INTO masters(id,name) VALUES('crm-master','Test Master')"));
        QVERIFY2(q.exec("INSERT INTO work_orders(number,order_number,order_uid,client_uid,vehicle_uid,plate,plate_norm,car,client,master_id,engine_volume,status,opened_at) "
                        "VALUES(101,101,'crm-history','crm-test','old-vehicle-uid','AA 1234 BA','AA1234BA','OPEL VECTRA','Test Client','crm-master','2.0','closed','2026-10-01')"),
                 qPrintable(q.lastError().text()));
        OrderDialog fromHistory;
        const int newOrderIdentity=fromHistory.serializeDraftState().value("orderNumber").toInt();
        QVERIFY(fromHistory.prefillFromCrmVehicle("crm-test","crm-car"));
        QCOMPARE(fromHistory.serializeDraftState().value("engineVolume").toString(),QString("2.0"));
        QCOMPARE(fromHistory.serializeDraftState().value("orderNumber").toInt(),newOrderIdentity);
        OrderDialogReadQueryService::CrmVehiclePrefill prefill;
        QVERIFY(OrderDialogReadQueryService::loadCrmVehiclePrefill("crm-test","crm-car",&prefill,&error));
        QCOMPARE(prefill.previousOrder,101);
        QVERIFY(q.exec("ALTER TABLE crm_vehicles RENAME COLUMN brand TO invalid_brand"));
        QVERIFY(!OrderDialogReadQueryService::loadCrmVehiclePrefill("crm-test","crm-car",&prefill,&error));
        QVERIFY(!error.isEmpty());QVERIFY(prefill.plate.isEmpty());QCOMPARE(prefill.previousOrder,0);
        QVERIFY(q.exec("ALTER TABLE crm_vehicles RENAME COLUMN invalid_brand TO brand"));
        QVERIFY(OrderDialogReadQueryService::loadCrmVehiclePrefill("crm-test","crm-car",&prefill,&error));
        QVERIFY(error.isEmpty());QCOMPARE(prefill.previousOrder,101);
        QVERIFY(q.exec("DELETE FROM work_orders WHERE number=101 AND order_uid='crm-history'"));

    }
    void crmReadBoundaryPreservesSnapshotOnQueryFailure() {
        ActivityAuditService::setCurrentActor({"qa-owner","Owner","owner","crm-read"});
        auto db=Database::db();QString error;
        QVERIFY2(CrmClientService::ensureLocalSchema(db,&error),qPrintable(error));
        QSqlQuery q(db);
        QVERIFY(q.exec("INSERT INTO crm_clients(client_uid,full_name,phone) VALUES('boundary-client','Boundary Client','0980112244')"));
        QVERIFY(q.exec("INSERT INTO crm_vehicles(vehicle_uid,client_uid,plate,brand,model,is_primary) VALUES('boundary-car','boundary-client','AA4567BA','Audi','A6',1)"));
        auto list=CrmPageReadService::clients("AA4567BA");
        QVERIFY2(list.error.isEmpty(),qPrintable(list.error));QCOMPARE(list.clients.size(),1);
        QCOMPARE(list.clients.first().value(0).toString(),QString("boundary-client"));
        QCOMPARE(list.clients.first().value(4).toString(),QString("Audi A6"));
        QVERIFY(CrmPageReadService::clients("not-found-client").clients.isEmpty());
        QVERIFY2(CrmPageApplicationService::addNote("boundary-client","boundary-car","Boundary note",&error),qPrintable(error));
        auto details=CrmPageReadService::details("boundary-client");
        QVERIFY2(details.error.isEmpty(),qPrintable(details.error));
        QCOMPARE(details.client.value(0).toString(),QString("Boundary Client"));
        QCOMPARE(details.vehicles.size(),1);QCOMPARE(details.events.size(),1);
        QCOMPARE(details.events.first().value(2).toString(),QString("Boundary note"));
        const auto missing=CrmPageReadService::details("absent-client");
        QVERIFY(!missing.error.isEmpty());QVERIFY(missing.client.isEmpty());
        QVERIFY(q.exec("ALTER TABLE crm_client_tasks RENAME COLUMN title TO unavailable_title"));
        const auto failed=CrmPageReadService::details("boundary-client");
        QVERIFY(!failed.error.isEmpty());QVERIFY(failed.client.isEmpty());
        QVERIFY(failed.vehicles.isEmpty());QVERIFY(failed.events.isEmpty());
        QVERIFY(q.exec("ALTER TABLE crm_client_tasks RENAME COLUMN unavailable_title TO title"));
        details=CrmPageReadService::details("boundary-client");
        QVERIFY2(details.error.isEmpty(),qPrintable(details.error));QCOMPARE(details.events.size(),1);
        QVERIFY(q.exec("ALTER TABLE crm_clients RENAME COLUMN full_name TO unavailable_name"));
        list=CrmPageReadService::clients({});QVERIFY(!list.error.isEmpty());QVERIFY(list.clients.isEmpty());
        QVERIFY(q.exec("ALTER TABLE crm_clients RENAME COLUMN unavailable_name TO full_name"));
        QVERIFY2(CrmPageApplicationService::addNote("boundary-client",{},"Note without vehicle",&error),qPrintable(error));
        details=CrmPageReadService::details("boundary-client");
        QVERIFY2(details.error.isEmpty(),qPrintable(details.error));QCOMPARE(details.events.size(),2);
    }
    void crmFailedSelectionClearsPreviousClientActions() {
        ActivityAuditService::setCurrentActor({"qa-owner","Owner","owner","crm-failed-selection"});
        auto db = Database::db();
        QString error;
        QVERIFY2(CrmClientService::ensureLocalSchema(db, &error), qPrintable(error));
        QSqlQuery q(db);
        QVERIFY(q.exec("INSERT INTO crm_clients(client_uid,full_name) VALUES"
                       "('selection-a','Selection Alpha'),('selection-b','Selection Beta')"));
        CrmPage page;
        auto* clients = page.findChild<QTableWidget*>("crmClientCardsTable");
        QVERIFY(clients);
        const auto rowFor = [clients](const QString& uid) {
            for (int row = 0; row < clients->rowCount(); ++row)
                if (clients->item(row, 0) && clients->item(row, 0)->data(Qt::UserRole).toString() == uid)
                    return row;
            return -1;
        };
        QTRY_VERIFY_WITH_TIMEOUT(rowFor("selection-a") >= 0 && rowFor("selection-b") >= 0, 10000);
        const int alpha = rowFor("selection-a"), beta = rowFor("selection-b");
        QVERIFY(alpha >= 0 && beta >= 0);
        QPushButton* addNote = nullptr;
        for (auto* button : page.findChildren<QPushButton*>())
            if (button->text() == QString::fromUtf8("Додати нотатку")) addNote = button;
        QVERIFY(addNote);
        clients->setCurrentCell(alpha, 0);
        QVERIFY(addNote->isEnabled());
        // Simulate a missing client after the list was loaded, without touching
        // production paths or weakening the application's read checks.
        QVERIFY(q.exec("DELETE FROM crm_clients WHERE client_uid='selection-b'"));
        clients->setCurrentCell(beta, 0);
        QVERIFY(!addNote->isEnabled());
        QCOMPARE(clients->currentRow(), -1);
        QVERIFY(clients->selectedItems().isEmpty());
        clients->setCurrentCell(alpha, 0);
        QVERIFY(addNote->isEnabled());
        QVERIFY(q.exec("DELETE FROM crm_clients WHERE client_uid='selection-a'"));
    }
    void statisticsPerformanceViews() {
        ActivityAuditService::setCurrentActor({"qa-owner","Owner","owner","qa-session"});
        auto db=Database::db();
        QSqlQuery q(db);
        QVERIFY(q.exec("INSERT INTO masters(id,name,role,active,active_from,active_to) VALUES"
            "('stats-a','Andrii','master',1,'2020-01-01',''),"
            "('stats-b','Ihor','master',0,'2020-01-01','2020-12-31'),"
            "('stats-admin','Admin','admin',1,'2020-01-01','')"));
        q.prepare("INSERT INTO work_orders(number,order_uid,order_number,opened_at,closed_at,status,plate,plate_norm,car,client,master_id) "
            "VALUES(90001,'stats-order',90001,?,?,'closed','AA 1234 AA','AA1234AA','OPEL VECTRA','Test','stats-a')");
        q.addBindValue(QDate::currentDate().addMonths(-2).toString(Qt::ISODate));
        q.addBindValue(QDate::currentDate().toString(Qt::ISODate));QVERIFY(q.exec());
        QString stockError;
        QVERIFY2(WorkshopSpareStockService::ensureSchema(db,&stockError),qPrintable(stockError));
        q.prepare("INSERT INTO workshop_spare_orders(id,name,brand,model,plate,master_id,master_name,supplier,ordered_on,quantity,unit_cost) "
            "VALUES(90001,'Brake pads','OPEL','VECTRA','AA 1234 AA','stats-a','Andrii','Shop',?,2,500)");
        q.addBindValue(QDate::currentDate().addDays(-12).toString(Qt::ISODate));QVERIFY(q.exec());
        q.prepare("INSERT INTO workshop_spare_movements(spare_id,operation,quantity,moved_on) VALUES(90001,'receipt',1,?)");
        q.addBindValue(QDate::currentDate().addDays(-9).toString(Qt::ISODate));QVERIFY(q.exec());
        StatsPage page;
        page.resize(1360,900);page.show();page.refresh();QTest::qWait(100);
        QVERIFY2(page.property("performanceError").toString().isEmpty(),qPrintable(page.property("performanceError").toString()));
        QTRY_VERIFY_WITH_TIMEOUT(page.property("currentMasterCount").isValid(),10000);
        QVERIFY2(page.property("statisticsError").toString().isEmpty(),qPrintable(page.property("statisticsError").toString()));
        QStringList staffIds;
        for(const auto& staff:page.property("performanceStaff").toList()) staffIds.append(staff.toMap().value("id").toString());
        QVERIFY(staffIds.contains("stats-a"));
        QVERIFY(!staffIds.contains("stats-b"));
        QVERIFY(!staffIds.contains("stats-admin"));
        QCOMPARE(page.property("currentMasterCount").toInt(),staffIds.size());
        auto* overview=page.findChild<QTextBrowser*>("statsOverview");
        auto* detail=page.findChild<QTextBrowser*>("statsMasterReport");
        auto* master=page.findChild<QComboBox*>("statsMaster");
        QVERIFY(overview && detail && master);
        master->setCurrentIndex(master->findData("stats-a"));
        QVERIFY(detail->toPlainText().contains("90001"));
        QVERIFY(detail->toPlainText().contains("Brake pads"));
        QCOMPARE(page.property("performanceStock").toList().first().toMap().value("days").toInt(),9);
        for(auto* report:{overview,detail}) {
            QVERIFY(report->toPlainText().contains(QString::fromUtf8("Майстрів у табелі")));
            QVERIFY(!report->toPlainText().contains(QString::fromUtf8("Виручка")));
            QVERIFY(report->toPlainText().contains(QString::fromUtf8("Немає обліку")));
        }
        auto* tabs=page.findChild<QTabWidget*>();
        for(int index=0;index<tabs->count();++index)
            QCOMPARE(tabs->isTabVisible(index),index<2 || index==5);
        const QString out=qEnvironmentVariable("CS_SETTINGS_QA_DIR");
        for(int index:{0,1}) {
            tabs->setCurrentIndex(index);QTest::qWait(80);
            auto* report=index==0?overview:detail;
            QCOMPARE(report->horizontalScrollBar()->maximum(),0);
            if(!out.isEmpty()) QVERIFY(page.grab().save(out+QString("/statistics-%1.png").arg(index)));
        }
        master->setCurrentIndex(master->findData("stats-b"));
        QVERIFY(!detail->toPlainText().contains("90001"));
        QVERIFY(!detail->toPlainText().contains("Brake pads"));
        QVERIFY(q.exec("DELETE FROM workshop_spare_movements WHERE spare_id=90001"));
        QVERIFY(q.exec("DELETE FROM workshop_spare_orders WHERE id=90001"));
        QVERIFY(q.exec("DELETE FROM work_orders WHERE order_uid='stats-order'"));
        QVERIFY(q.exec("DELETE FROM masters WHERE id IN ('stats-a','stats-b','stats-admin')"));
    }
    void workLogInGeneralSettings() {
        ActivityAuditService::setCurrentActor({"qa-owner","Owner","owner","qa-session"});
        std::unique_ptr<QWidget> section(SettingsGeneralSection::build("general.work_log",nullptr,{}));
        auto* log=qobject_cast<StatsPage*>(section.get());
        QVERIFY(log);
        QVERIFY(log->property("settingsWorkLog").toBool());
        log->resize(1500,900);log->show();log->refresh();QTest::qWait(80);
        auto* tabs=log->findChild<QTabWidget*>();QVERIFY(tabs);
        QCOMPARE(tabs->currentIndex(),3);
        for(int index=0;index<tabs->count();++index)
            QCOMPARE(tabs->isTabVisible(index),index>=2 && index<=4);
        QVERIFY(!log->property("performanceOrders").isValid());
        const auto out=qEnvironmentVariable("CS_SETTINGS_QA_DIR");
        if(!out.isEmpty()) QVERIFY(log->grab().save(out+"/work-log.png"));
    }
    void websiteQr() {
        QVERIFY(Database::setSetting("settings.reports.pdf.footer_website","carservicepointg.com.ua"));
        QCOMPARE(PdfWebsiteFooter::url(),QString("https://carservicepointg.com.ua"));
        const auto image=PdfWebsiteFooter::qr(PdfWebsiteFooter::url());QVERIFY(!image.isNull());
        QCOMPARE(image.pixelColor(0,0),QColor(Qt::white));
        std::unique_ptr<QWidget> page(SettingsGeneralSection::build("general.pdf",nullptr,{}));
        SettingsDetailUiSupport::normalizeDetailButtons(page.get());page->resize(1100,750);page->show();QTest::qWait(100);
        QVERIFY(page->findChild<QLineEdit*>("pdfFooterWebsite"));
        const QString out=qEnvironmentVariable("CS_SETTINGS_QA_DIR");
        if(!out.isEmpty()) {
            QVERIFY(page->grab().save(out+"/pdf-settings.png"));
            QVERIFY(image.scaled(400,400,Qt::KeepAspectRatio,Qt::FastTransformation).save(out+"/qr.png"));
            QPdfWriter pdf(out+"/website-footer.pdf");pdf.setResolution(144);
            QPainter painter(&pdf);PdfWebsiteFooter::draw(painter);painter.end();
        }
        Database::setSetting("settings.reports.pdf.footer_website","file:///secret");
        QVERIFY(PdfWebsiteFooter::url().isEmpty());
    }
    void weeklyRetention() {
        const QString root=dir.filePath("backups");QDir().mkpath(root);
        Database::setSetting("settings.int.autobackup.dir",root);BackupPaths::refreshFromRuntime();
        QVector<SnapshotMetadata> entries;
        for(int days:{0,6,7,8,30}) {
            SnapshotMetadata meta;meta.snapshotId=QString::number(days);
            meta.snapshotPath=root+"/"+meta.snapshotId+".db";
            meta.createdAt=QDateTime::currentDateTime().addDays(-days);meta.validationStatus="valid";
            meta.source="emergency";
            QFile f(meta.snapshotPath);QVERIFY(f.open(QIODevice::WriteOnly));f.write("test backup");f.close();
            entries.append(meta);
        }
        QString error;QVERIFY(BackupManifestService::replaceSnapshotIndex(entries,&error));
        BackupPolicy::cleanupSnapshots(&error);QVERIFY2(error.isEmpty(),qPrintable(error));
        QCOMPARE(BackupManifestService::listSnapshots().size(),2);
        QVERIFY(QFile::exists(root+"/0.db"));QVERIFY(QFile::exists(root+"/6.db"));
        QVERIFY(!QFile::exists(root+"/7.db"));QVERIFY(!QFile::exists(root+"/30.db"));
    }
};
QTEST_MAIN(SettingsLayoutTests)
#include "SettingsLayoutTests.moc"
