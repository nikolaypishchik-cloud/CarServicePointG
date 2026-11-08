#include "core/util/DateFormats.h"
// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

#include "ui/common/dialogs/OrderDialogModulePrelude.h"
#include "core/services/AccessControlService.h"
#include "core/util/PlateUtils.h"

bool OrderDialog::prefillFromCrmVehicle(const QString& clientUid, const QString& vehicleUid)
{
    if (!m_isNewOrder || clientUid.isEmpty() || vehicleUid.isEmpty()) return false;
    OrderDialogReadQueryService::CrmVehiclePrefill vehicle;
    QString error;
    if(!OrderDialogReadQueryService::loadCrmVehiclePrefill(clientUid,vehicleUid,&vehicle,&error)) {
        if(!error.isEmpty())QMessageBox::warning(this,QString::fromUtf8("Дані автомобіля"),error);
        return false;
    }
    if(vehicle.previousOrder>0)prefillFromExistingOrder(vehicle.previousOrder);
    const auto text=[](QLineEdit* field,const QString& value) {
        if(field && !value.trimmed().isEmpty())field->setText(value.trimmed());
    };
    if(!vehicle.plate.isEmpty())m_plate->setText(PlateUtils::formatPlateDisplay(vehicle.plate));
    text(m_vin,vehicle.vin);
    text(m_car,(vehicle.brand+" "+vehicle.model).simplified());
    if(vehicle.year>0)m_year->setValue(vehicle.year);
    text(m_engineCode,vehicle.engine);
    if(!vehicle.fuelType.isEmpty())setSelectedFuelType(vehicle.fuelType);
    if(vehicle.mileage>0)m_odometer->setValue(vehicle.mileage);
    text(m_color,vehicle.color);text(m_client,vehicle.clientName);text(m_phone,vehicle.phone);
    m_discount->setValue(vehicle.discountPercent);
    m_discountMode->setCurrentIndex(m_discountMode->findData(QStringLiteral("percent")));
    m_userInteractedWithForm = true;
    refreshTotals();
    return true;
}

void OrderDialog::prefillFromSpareStock(const WorkshopSpareOrder& spare, double quantity)
{
    if (!m_isNewOrder || m_orderNumber > 0) return;
    m_pendingSpare = spare; m_pendingSpareQuantity = quantity;
    if (m_tabs && m_tabs->count() == 1) {
        auto* workTab = new QWidget(m_tabs);
        auto* workLayout = new QVBoxLayout(workTab);
        workLayout->setContentsMargins(0, 0, 0, 0);
        workLayout->setSpacing(0);
        m_tabs->addTab(workTab, QString::fromUtf8("Роботи та запчастини"));
    }
    const auto plate=PlateUtils::normalizePlate(spare.plate);
    const auto vin=spare.vin.trimmed().toUpper();
    QList<OrderSnapshotService::OrderSnapshot> snapshots;
    QString historyError;
    bool restored=false;
    if (OrderDialogReadQueryService::loadVehicleHistory(plate,vin,&snapshots,&historyError)) {
        for (const auto& snapshot : snapshots) {
            if(!restored) {
                prefillFromExistingOrder(snapshot.number);
                m_odometer->setValue(snapshot.odometer);
                restored=true;
            } else {
                const auto fill=[](QLineEdit* field,const QString& value){if(field->text().trimmed().isEmpty() && !value.trimmed().isEmpty())field->setText(value);};
                fill(m_vin,snapshot.vin);fill(m_car,normalizeSnapshotCarDisplay(snapshot));
                fill(m_color,snapshot.color);fill(m_engineCode,snapshot.engineType);fill(m_engineVolume,snapshot.engineVolume);
                fill(m_client,normalizeSnapshotClientName(snapshot));fill(m_phone,snapshot.clientPhone);
                if(m_year->value()==m_year->minimum() && snapshot.carYear>m_year->minimum())m_year->setValue(snapshot.carYear);
                if(m_odometer->value()==0 && snapshot.odometer>0)m_odometer->setValue(snapshot.odometer);
                if(selectedFuelType().isEmpty())setSelectedFuelType(snapshot.fuelType);
            }
        }
    }
    if (!historyError.isEmpty())
        QMessageBox::warning(this,QString::fromUtf8("Історія автомобіля"),historyError);
    m_plate->setText(PlateUtils::formatPlateDisplay(plate));
    if(!vin.isEmpty())m_vin->setText(vin);
    if(m_car->text().trimmed().isEmpty())m_car->setText((spare.brand + " " + spare.model).simplified());
    const int master=m_master->findData(spare.masterId);if(master>=0)m_master->setCurrentIndex(master);
    setOpenedAt(QDateTime::currentDateTime());
    m_userInteractedWithForm=true;
    setProperty("spareStockOrder",spare.id);
    m_focusOdometerOnShow=true;
    m_odometer->setFocus(Qt::OtherFocusReason);m_odometer->selectAll();
}

bool OrderDialog::issuePendingSpare(QString* error)
{
    if (!m_pendingSpare.id || m_spareIssued) return true;
    if (!WorkshopSpareStockService::issue(m_pendingSpare,m_pendingSpareQuantity,m_orderNumber,error)) return false;
    m_spareIssued=true;
    return true;
}

bool OrderDialog::prefillFromCompletedVehicleByPlateIfAvailable()
{
    if (!m_isNewOrder || m_readOnly || !m_plate) {
        return false;
    }

    const QString plateNorm = PlateUtils::normalizePlate(m_plate->text()).trimmed().toUpper();
    if (plateNorm.isEmpty()) {
        return false;
    }

    OrderSnapshotService::OrderSnapshot snapshot;
    QString lookupError;
    if (!OrderDialogReadQueryService::loadLatestClosedVehicleSnapshotByPlate(plateNorm, &snapshot, &lookupError)) {
        if (RuntimeLoggingControl::deepDebugEnabled()) {
            qInfo().noquote() << QStringLiteral("OrderDialog local vehicle prefill skipped plate=%1 reason=%2")
                                      .arg(plateNorm, lookupError.trimmed().isEmpty() ? QStringLiteral("not_found") : lookupError);
        }
        return false;
    }

    const QString sourcePlate = normalizeSnapshotPlateDisplay(snapshot);
    if (RuntimeLoggingControl::deepDebugEnabled()) {
        qInfo().noquote() << QStringLiteral("OrderDialog local vehicle prefill applied plate=%1 source_order=%2")
                                  .arg(plateNorm, QString::number(snapshot.visibleNumber > 0 ? snapshot.visibleNumber : snapshot.number));
    }

    if (m_status) {
        const int openIndex = statusComboIndexByValue(m_status, QStringLiteral("open"));
        if (openIndex >= 0) m_status->setCurrentIndex(openIndex);
    }

    if (m_plate && !sourcePlate.isEmpty()) {
        QSignalBlocker plateBlocker(m_plate);
        m_plate->setText(sourcePlate);
    }
    if (m_vin) m_vin->setText(snapshot.vin.toUpper());
    if (m_car) {
        const QString carDisplay = normalizeSnapshotCarDisplay(snapshot);
        m_car->setText(carDisplay);
        m_lastCarEditedText = carDisplay;
    }
    if (m_color) m_color->setText(capitalizeSentence(snapshot.color));
    if (m_year) {
        if (snapshot.carYear > 0) m_year->setValue(snapshot.carYear);
        else {
            m_year->setValue(m_year->minimum());
            m_year->clear();
        }
    }

    // User requirement: when the vehicle is reused from completed orders,
    // odometer and master must stay empty for the new order.
    if (m_odometer) {
        m_odometer->setValue(0);
        m_odometer->clear();
    }
    if (m_master) {
        m_master->setCurrentIndex(-1);
        m_master->setCurrentText(QString());
    }

    if (m_engineCode) m_engineCode->setText(snapshot.engineType);
    if (m_engineVolume) m_engineVolume->setText(snapshot.engineVolume);
    setSelectedFuelType(snapshot.fuelType);
    if (m_client) m_client->setText(normalizeSnapshotClientName(snapshot));
    if (m_phone) m_phone->setText(snapshot.clientPhone);
    if (m_discount) m_discount->setValue(snapshot.discount);
    if (m_discountMode) m_discountMode->setCurrentIndex(m_discountMode->findData(snapshot.discountMode));

    refreshTotals();
    if (m_odometer) {
        m_odometer->setFocus();
        m_odometer->selectAll();
    }
    return true;
}

void OrderDialog::lookupVehicleByPlateIfConfigured()
{
    if (!m_isNewOrder || m_readOnly || m_vehicleLookupInProgress || !m_plate) {
        return;
    }

    const QString plate = PlateUtils::normalizePlate(m_plate->text());
    if (plate.isEmpty()) {
        return;
    }

    if (!OpenDataBotClient::isConfigured()) {
        return;
    }

    if (plate == m_lastLookupPlate && ((m_vin && !m_vin->text().trimmed().isEmpty()) || (m_car && !m_car->text().trimmed().isEmpty()))) {
        return;
    }

    m_vehicleLookupInProgress = true;
    m_lastLookupPlate = plate;

    OpenDataBotClient::lookupByPlate(this, plate, [this, plate](const OpenDataBotClient::VehicleLookupResult& result) {
        m_vehicleLookupInProgress = false;
        if (plate != m_lastLookupPlate) {
            return;
        }

        if (!result.success) {
            if (!m_vehicleLookupWarned) {
                m_vehicleLookupWarned = true;
                QMessageBox::warning(this,
                                     QString::fromUtf8("OpenDataBot"),
                                     result.error.isEmpty()
                                         ? QString::fromUtf8("Не вдалося автоматично отримати дані авто за держномером.")
                                         : result.error);
            }
            return;
        }

        m_vehicleLookupWarned = false;
        applyVehicleLookupResult(result);
    });
}

void OrderDialog::applyVehicleLookupResult(const OpenDataBotClient::VehicleLookupResult& result)
{
    if (m_vin && m_vin->text().trimmed().isEmpty() && !result.vin.isEmpty()) {
        m_vin->setText(result.vin.trimmed().toUpper());
    }
    if (m_car && m_car->text().trimmed().isEmpty() && !result.carDisplay.isEmpty()) {
        m_car->setText(titleCaseWords(VehicleNameUtils::normalizeVehicleFreeText(result.carDisplay)));
    }
    if (m_color && m_color->text().trimmed().isEmpty() && !result.color.isEmpty()) {
        m_color->setText(capitalizeSentence(result.color));
    }
    if (m_year && m_year->value() <= m_year->minimum() && result.year > 0) {
        m_year->setValue(result.year);
    }
    if (m_engineVolume && m_engineVolume->text().trimmed().isEmpty() && !result.engineVolume.isEmpty()) {
        m_engineVolume->setText(result.engineVolume.trimmed());
    }
    if (!result.fuelType.isEmpty()) {
        setSelectedFuelType(result.fuelType);
    }
}


QDateTime OrderDialog::currentOpenedAt() const
{
    const QDate date = m_openedDate ? m_openedDate->date() : QDate::currentDate();
    const QTime time = m_openedTime ? m_openedTime->time() : QTime::currentTime();
    const QDateTime dt(date, time);
    return dt.isValid() ? dt : QDateTime::currentDateTime();
}

void OrderDialog::setOpenedAt(const QDateTime& dt)
{
    const QDateTime safe = dt.isValid() ? dt : QDateTime::currentDateTime();
    if (m_openedDate) m_openedDate->setDate(safe.date());
    if (m_openedTime) m_openedTime->setTime(safe.time());
}

QString OrderDialog::selectedFuelType() const
{
    if (m_fuelDiesel && m_fuelDiesel->isChecked()) return QString::fromUtf8("Дизель");
    if (m_fuelPetrol && m_fuelPetrol->isChecked()) return QString::fromUtf8("Бензин (газ)");
    if (m_fuelHybrid && m_fuelHybrid->isChecked()) return QString::fromUtf8("Гібрид");
    if (m_fuelElectric && m_fuelElectric->isChecked()) return QString::fromUtf8("Електро");
    return QString::fromUtf8("Не вказано");
}

void OrderDialog::setSelectedFuelType(const QString& fuelType)
{
    for (auto* cb : {m_fuelDiesel, m_fuelPetrol, m_fuelHybrid, m_fuelElectric}) {
        if (cb) cb->setChecked(false);
    }
    const QString t = fuelType.trimmed();
    if (m_fuelDiesel && (t.compare(QString::fromUtf8("Дизель"), Qt::CaseInsensitive) == 0 || t.compare(QStringLiteral("diesel"), Qt::CaseInsensitive) == 0)) m_fuelDiesel->setChecked(true);
    else if (m_fuelPetrol && (t.contains(QString::fromUtf8("Бенз"), Qt::CaseInsensitive) || t.compare(QStringLiteral("petrol"), Qt::CaseInsensitive) == 0 || t.compare(QStringLiteral("gas"), Qt::CaseInsensitive) == 0)) m_fuelPetrol->setChecked(true);
    else if (m_fuelHybrid && (t.compare(QString::fromUtf8("Гібрид"), Qt::CaseInsensitive) == 0 || t.compare(QStringLiteral("hybrid"), Qt::CaseInsensitive) == 0)) m_fuelHybrid->setChecked(true);
    else if (m_fuelElectric && (t.compare(QString::fromUtf8("Електро"), Qt::CaseInsensitive) == 0 || t.compare(QStringLiteral("electric"), Qt::CaseInsensitive) == 0)) m_fuelElectric->setChecked(true);
}

void OrderDialog::loadMasters()
{
    if (!m_master) return;

    m_master->clear();

    // The new-order primary master selector must mirror the current
    // "Персонал" settings page: only strictly active personnel records
    // with the master role are allowed here.
    const auto personnel = PersonnelSettingsService::loadSnapshot().masters;
    for (const auto& m : personnel) {
        if (m.name.trimmed().isEmpty()) continue;
        if (AccessControlService::currentActorIsMaster()
            && m.id != AccessControlService::currentMasterId()) continue;
        m_master->addItem(m.name, m.id);
    }
    if (AccessControlService::currentActorIsMaster()) m_master->setEnabled(false);
}

void OrderDialog::loadOrder(int number)
{
    QElapsedTimer timer;
    timer.start();
    QString prepError;
    if (!ensureOrderDialogDbReady(this, &prepError)) {
        QTimer::singleShot(0, this, &QDialog::reject);
        return;
    }

    const auto loadResult = OrderDialogCoordinator::loadEditableOrder(number);
    if (!loadResult.ok) {
        QMessageBox box(QMessageBox::Critical,
                        QString::fromUtf8("Помилка"),
                        loadResult.error.trimmed().isEmpty()
                            ? QString::fromUtf8("Не вдалося відкрити дані наряду з бази")
                            : QString::fromUtf8("Не вдалося знайти або імпортувати наряд у активну БД.\n%1").arg(loadResult.error),
                        QMessageBox::Ok,
                        this);
        styleMessageBoxReadable(&box);
        box.exec();
        QTimer::singleShot(0, this, &QDialog::reject);
        return;
    }

    const OrderSnapshotService::OrderSnapshot& snapshot = loadResult.snapshot;

    if (m_num) {
        m_num->setValue(snapshot.visibleNumber > 0 ? snapshot.visibleNumber : number);
    }

    const QDateTime openedDt = parseStoredDateTimeFlexible(snapshot.openedAt);
    setOpenedAt(openedDt.isValid() ? openedDt : QDateTime::currentDateTime());

    const int si = statusComboIndexByValue(m_status, snapshot.status);
    if (si >= 0) m_status->setCurrentIndex(si);

    m_plate->setText(normalizeSnapshotPlateDisplay(snapshot));
    m_vin->setText(snapshot.vin.toUpper());
    m_car->setText(normalizeSnapshotCarDisplay(snapshot));
    if (m_color) {
        m_color->setText(capitalizeSentence(snapshot.color));
    }
    if (m_year) m_year->setValue(snapshot.carYear);
    if (m_odometer) m_odometer->setValue(snapshot.odometer);
    if (m_engineCode) m_engineCode->setText(snapshot.engineType);
    if (m_engineVolume) m_engineVolume->setText(snapshot.engineVolume);
    setSelectedFuelType(snapshot.fuelType);
    m_client->setText(normalizeSnapshotClientName(snapshot));
    m_phone->setText(snapshot.clientPhone);

    int mi = m_master->findData(snapshot.masterId);
    if (mi < 0 && !snapshot.masterNameClosed.isEmpty()) {
        mi = m_master->findText(snapshot.masterNameClosed, Qt::MatchFixedString);
    }
    if (mi >= 0) m_master->setCurrentIndex(mi);

    m_discount->setValue(snapshot.discount);
    m_discountMode->setCurrentIndex(m_discountMode->findData(snapshot.discountMode));

    refreshTotals();
    if (RuntimeLoggingControl::deepDebugEnabled()) qInfo() << "OrderDialog loadOrder elapsed_ms" << timer.elapsed() << "order" << number;
}

void OrderDialog::prefillFromExistingOrder(int sourceOrderNumber)
{
    if (sourceOrderNumber <= 0) return;

    OrderSnapshotService::OrderSnapshot snapshot;
    if (!OrderDialogReadQueryService::loadSnapshotByNumber(sourceOrderNumber, &snapshot, nullptr)) {
        return;
    }

    if (m_status) {
        const int openIndex = statusComboIndexByValue(m_status, QStringLiteral("open"));
        if (openIndex >= 0) m_status->setCurrentIndex(openIndex);
    }

    setOpenedAt(QDateTime::currentDateTime());

    if (m_plate) m_plate->setText(normalizeSnapshotPlateDisplay(snapshot));
    if (m_vin) m_vin->setText(snapshot.vin.toUpper());
    if (m_car) m_car->setText(normalizeSnapshotCarDisplay(snapshot));
    if (m_color) m_color->setText(capitalizeSentence(snapshot.color));
    if (m_year) m_year->setValue(snapshot.carYear);
    if (m_odometer) {
        m_odometer->setValue(0);
        m_odometer->clear();
    }
    if (m_engineCode) m_engineCode->setText(snapshot.engineType);
    if (m_engineVolume) m_engineVolume->setText(snapshot.engineVolume);
    setSelectedFuelType(snapshot.fuelType);
    if (m_client) m_client->setText(normalizeSnapshotClientName(snapshot));
    if (m_phone) m_phone->setText(snapshot.clientPhone);

    if (m_master) m_master->setCurrentIndex(-1);

    if (m_discount) m_discount->setValue(snapshot.discount);
    if (m_discountMode) m_discountMode->setCurrentIndex(m_discountMode->findData(snapshot.discountMode));

    refreshTotals();
    m_focusOdometerOnShow = true;
    if (m_plate) m_plate->deselect();
    if (m_vin) m_vin->deselect();
    if (m_car) m_car->deselect();
    if (m_color) m_color->deselect();
    if (m_engineCode) m_engineCode->deselect();
    if (m_engineVolume) m_engineVolume->deselect();
    if (m_client) m_client->deselect();
    if (m_phone) m_phone->deselect();
    if (m_odometer) {
        m_odometer->setFocus(Qt::OtherFocusReason);
        m_odometer->selectAll();
    }
}


void OrderDialog::prefillFromPlanningSource(int sourceOrderNumber, const QString& planningText, const QDate& plannedDate, const QTime& plannedTime)
{
    if (sourceOrderNumber <= 0) {
        prefillFromPlanningEntry(planningText, plannedDate, plannedTime);
        return;
    }

    OrderSnapshotService::OrderSnapshot snapshot;
    if (!OrderDialogReadQueryService::loadSnapshotByNumber(sourceOrderNumber, &snapshot, nullptr)) {
        prefillFromPlanningEntry(planningText, plannedDate, plannedTime);
        return;
    }

    PlanningOrderPrefillData planningData = extractPlanningOrderPrefillData(planningText);
    enrichPlanningDataFromStoredOnlineBooking(&planningData, planningText, plannedDate, plannedTime);
    const QString planningPlateDisplay = planningData.plateDisplay;
    const QString sourcePlateDisplay = normalizeSnapshotPlateDisplay(snapshot);
    const QString planningCarDisplay = sanitizePlanningCarDisplayForOrder(planningData.carDisplay);
    const QString sourceCarDisplay = sanitizePlanningCarDisplayForOrder(normalizeSnapshotCarDisplay(snapshot));
    const QString vinText = snapshot.vin.toUpper();
    const QString colorText = capitalizeSentence(snapshot.color);
    const QString yearText = !planningData.carYear.trimmed().isEmpty() ? planningData.carYear.trimmed() : QString::number(snapshot.carYear);
    const QString engineCodeText = snapshot.engineType;
    const QString engineVolumeText = !planningData.engineVolume.trimmed().isEmpty() ? planningData.engineVolume.trimmed() : snapshot.engineVolume;
    const QString fuelTypeText = !planningData.fuelType.trimmed().isEmpty() ? planningData.fuelType.trimmed() : snapshot.fuelType;

    if (m_status) {
        const int openIndex = statusComboIndexByValue(m_status, QStringLiteral("open"));
        if (openIndex >= 0) m_status->setCurrentIndex(openIndex);
    }

    if (plannedDate.isValid() && plannedTime.isValid()) {
        setOpenedAt(QDateTime(plannedDate, plannedTime));
    } else {
        setOpenedAt(QDateTime::currentDateTime());
    }

    const QString plateDisplay = !planningPlateDisplay.isEmpty() ? planningPlateDisplay : sourcePlateDisplay;
    const QString carDisplay = !planningCarDisplay.isEmpty() ? planningCarDisplay : sourceCarDisplay;

    if (m_plate) {
        if (!plateDisplay.isEmpty()) m_plate->setText(plateDisplay);
        else m_plate->clear();
    }
    if (m_vin) {
        if (!vinText.isEmpty()) m_vin->setText(vinText);
        else m_vin->clear();
    }
    if (m_car) {
        if (!carDisplay.isEmpty()) m_car->setText(carDisplay);
        else m_car->clear();
    }
    if (m_color) {
        if (!colorText.isEmpty()) m_color->setText(colorText);
        else m_color->clear();
    }
    if (m_year) {
        bool yearOk = false;
        const int yearValue = yearText.toInt(&yearOk);
        if (yearOk && yearValue >= m_year->minimum()) m_year->setValue(yearValue);
        else m_year->clear();
    }
    if (m_odometer) {
        const int planningOdometer = planningPositiveIntFromText(planningData.odometer);
        if (planningOdometer > 0) m_odometer->setValue(planningOdometer);
        else {
            m_odometer->setValue(0);
            m_odometer->clear();
        }
    }
    if (m_engineCode) {
        if (!engineCodeText.isEmpty()) m_engineCode->setText(engineCodeText);
        else m_engineCode->clear();
    }
    if (m_engineVolume) {
        if (!engineVolumeText.isEmpty()) m_engineVolume->setText(engineVolumeText);
        else m_engineVolume->clear();
    }

    setSelectedFuelType(QString());
    if (!fuelTypeText.isEmpty()) {
        setSelectedFuelType(fuelTypeText);
    }

    if (m_client) {
        if (!planningData.clientName.trimmed().isEmpty()) m_client->setText(titleCaseWords(planningData.clientName.trimmed()));
        else m_client->clear();
    }
    if (m_phone) {
        if (!planningData.clientPhone.trimmed().isEmpty()) m_phone->setText(planningData.clientPhone.trimmed());
        else m_phone->clear();
    }
    if (m_master) m_master->setCurrentIndex(0);
    if (m_discount) m_discount->setValue(0.0);
    if (m_discountMode) m_discountMode->setCurrentIndex(0);

    refreshTotals();
    // Opening an order from a planning/reminder entry must not leave the prefilled
    // plate field selected. showEvent() normally focuses the plate for a blank
    // new order, so reuse the existing deferred odometer-focus guard here.
    m_focusOdometerOnShow = true;
    if (m_odometer) {
        m_odometer->setFocus(Qt::OtherFocusReason);
        m_odometer->selectAll();
    }
}


void OrderDialog::prefillFromPlanningEntry(const QString& planningText, const QDate& plannedDate, const QTime& plannedTime)
{
    PlanningOrderPrefillData planningData = extractPlanningOrderPrefillData(planningText);
    enrichPlanningDataFromStoredOnlineBooking(&planningData, planningText, plannedDate, plannedTime);
    const QString plateDisplay = planningData.plateDisplay;
    const QString carDisplay = sanitizePlanningCarDisplayForOrder(planningData.carDisplay);

    if (m_status) {
        const int openIndex = statusComboIndexByValue(m_status, QStringLiteral("open"));
        if (openIndex >= 0) m_status->setCurrentIndex(openIndex);
    }

    if (plannedDate.isValid() && plannedTime.isValid()) {
        setOpenedAt(QDateTime(plannedDate, plannedTime));
    } else {
        setOpenedAt(QDateTime::currentDateTime());
    }

    if (m_plate) {
        if (!plateDisplay.isEmpty()) m_plate->setText(plateDisplay);
        else m_plate->clear();
    }
    if (m_vin) m_vin->clear();
    if (m_car) {
        if (!carDisplay.isEmpty()) m_car->setText(carDisplay);
        else m_car->clear();
    }
    if (m_color) m_color->clear();
    if (m_year) {
        const int planningYear = planningPositiveIntFromText(planningData.carYear);
        if (planningYear >= m_year->minimum()) m_year->setValue(planningYear);
        else m_year->clear();
    }
    if (m_odometer) {
        const int planningOdometer = planningPositiveIntFromText(planningData.odometer);
        if (planningOdometer > 0) m_odometer->setValue(planningOdometer);
        else {
            m_odometer->setValue(0);
            m_odometer->clear();
        }
    }
    if (m_engineCode) m_engineCode->clear();
    if (m_engineVolume) {
        if (!planningData.engineVolume.trimmed().isEmpty()) m_engineVolume->setText(planningData.engineVolume.trimmed());
        else m_engineVolume->clear();
    }
    setSelectedFuelType(QString());
    if (!planningData.fuelType.trimmed().isEmpty()) setSelectedFuelType(planningData.fuelType.trimmed());
    if (m_client) {
        if (!planningData.clientName.trimmed().isEmpty()) m_client->setText(titleCaseWords(planningData.clientName.trimmed()));
        else m_client->clear();
    }
    if (m_phone) {
        if (!planningData.clientPhone.trimmed().isEmpty()) m_phone->setText(planningData.clientPhone.trimmed());
        else m_phone->clear();
    }
    if (m_master) m_master->setCurrentIndex(0);
    if (m_discount) m_discount->setValue(0.0);
    if (m_discountMode) m_discountMode->setCurrentIndex(0);

    refreshTotals();
    // Same focus contract for planning entries without an existing source order:
    // keep all prefilled vehicle text intact, but direct the first user input to
    // the odometer field after the modal dialog is shown.
    m_focusOdometerOnShow = true;
    if (m_odometer) {
        m_odometer->setFocus(Qt::OtherFocusReason);
        m_odometer->selectAll();
    }
}

void OrderDialog::setReadOnly(bool on)
{
    m_readOnly = on;

    const QList<QWidget*> fields = {m_openedDate, m_openedTime, m_status, m_plate, m_vin, m_car, m_color, m_year, m_odometer, m_engineCode, m_engineVolume, m_fuelDiesel, m_fuelPetrol, m_fuelHybrid, m_fuelElectric, m_client, m_phone, m_master, m_discount, m_discountMode};
    for (QWidget* w : fields) {
        if (!w) continue;
        w->setEnabled(!on);
    }
    if (m_num) m_num->setEnabled(false);
    if (m_workPartsPage) m_workPartsPage->setEnabled(!on);
}


void OrderDialog::openWorkPartsTab()
{
    if (m_tabs) {
        if (ensureDraftOrder()) {
            QString error;
            if (!ensureWorkPartsPage(&error)) {
                QMessageBox::warning(this, QString::fromUtf8("Не вдалося відкрити запчастини"), error);
                return;
            }
            m_tabs->setCurrentIndex(1);
        }
    }
}

bool OrderDialog::ensureWorkPartsPage(QString* errorText)
{
    if (m_orderNumber > 0 && !issuePendingSpare(errorText)) return false;
    if (m_workPartsPage) {
        if (m_readOnly) m_workPartsPage->setEnabled(false);
        return true;
    }
    if (!m_tabs || m_tabs->count() < 2) return true;

    QString dbReadyError;
    if (!ensureOrderDialogDbReady(this, &dbReadyError)) {
        if (errorText) *errorText = dbReadyError;
        return false;
    }

    auto* tab = m_tabs->widget(1);
    auto* layout = tab ? qobject_cast<QVBoxLayout*>(tab->layout()) : nullptr;
    if (!tab || !layout) {
        if (errorText) *errorText = QString::fromUtf8("Не знайдено вкладку робіт та запчастин.");
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    BackupPerformanceGuard::hold(AppContext::instance().backupManager(), 2200);
    m_workPartsPage = new WorkPartsDialog(tab, m_orderNumber);
    m_workPartsPage->setEmbedded(true);
    m_workPartsPage->setMinimumSize(0, 0);
    m_workPartsPage->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    layout->addWidget(m_workPartsPage, 1);
    connect(m_workPartsPage, &WorkPartsDialog::requestOk,
            this,
            [this]() {
                if (RuntimeLoggingControl::deepDebugEnabled()) qInfo() << "OrderDialog requestOk queued order" << m_orderNumber;
                QMetaObject::invokeMethod(this, "accept", Qt::QueuedConnection);
            },
            Qt::QueuedConnection);
    connect(m_workPartsPage, &WorkPartsDialog::requestCancel, this, &OrderDialog::reject);
    connect(m_workPartsPage, &WorkPartsDialog::purchasePaymentsChanged, this, &OrderDialog::purchasePaymentsChanged);
    connect(m_workPartsPage, &WorkPartsDialog::requestPdf, this, [this]() {
        exportPdfPreviewAndOpen(false);
    });
    connect(m_workPartsPage, &WorkPartsDialog::requestCloseOrder,
            this,
            [this]() {
                const int idx = statusComboIndexByValue(m_status, QStringLiteral("closed"));
                if (idx >= 0) m_status->setCurrentIndex(idx);
                if (RuntimeLoggingControl::deepDebugEnabled()) qInfo() << "OrderDialog requestCloseOrder queued order" << m_orderNumber;
                QMetaObject::invokeMethod(this, "accept", Qt::QueuedConnection);
            },
            Qt::QueuedConnection);
    if (m_readOnly) m_workPartsPage->setEnabled(false);
    if (RuntimeLoggingControl::deepDebugEnabled()) qInfo() << "OrderDialog ensureWorkPartsPage elapsed_ms" << timer.elapsed() << "order" << m_orderNumber;
    return true;
}

bool OrderDialog::ensureDraftOrder()
{
    if (m_orderNumber > 0) {
        return true;
    }
    if (m_acceptInProgress) {
        qWarning() << "OrderDialog ensureDraftOrder blocked during accept for order" << m_orderNumber;
        return false;
    }
    if (m_draftCreationInProgress) {
        qWarning() << "OrderDialog ensureDraftOrder reentry blocked for order" << m_orderNumber;
        return false;
    }
    QScopedValueRollback<bool> draftGuard(m_draftCreationInProgress, true);

    const int visibleNumber = m_num ? m_num->value() : 0;
    OrderWorkspaceInput input = buildWorkspaceInput(currentStatusValue(m_status), 0.0, 0.0, m_discount ? m_discount->value() : 0.0);
    const auto validation = OrderDialogCoordinator::validateDraftInput(input);
    if (!validation.ok
        && OrderDialogFlowController::showValidationErrorAndFocus(
            this,
            validation,
            m_tabs,
            m_plate,
            m_vin,
            m_odometer,
            m_openedDate,
            QString::fromUtf8("Оберіть майстра перед додаванням робіт або запчастин"))) {
        return false;
    }
    input.number = m_orderNumber;

    const auto result = OrderDialogCoordinator::createDraftEditableOrder(input, visibleNumber);
    if (!result.ok) {
        QMessageBox::critical(this,
                              QString::fromUtf8("Помилка БД"),
                              QString::fromUtf8("Наряд не вдалося підготувати для редагування робіт та запчастин.\n%1").arg(result.error));
        return false;
    }

    m_orderNumber = result.orderNumber;
    m_draftCreated = true;
    OrderDialogFlowController::applyResolvedDisplayNumber(m_num, result.displayNumber, visibleNumber);

    const OrderWorkspaceInput normalized = buildWorkspaceInput(currentStatusValue(m_status), 0.0, 0.0, m_discount ? m_discount->value() : 0.0);
    OrderDialogFlowController::applyNormalizedDraftFields(normalized, m_plate, m_vin, m_car, m_color, m_client);

    QString workPartsReinitWarning;
    if (!OrderDialogSubmitSupport::reinitializeWorkPartsPageAfterDraftCreation(
            m_workPartsPage,
            [this](QString* ensureError) { return ensureWorkPartsPage(ensureError); },
            &workPartsReinitWarning)) {
        qWarning() << "OrderDialog failed to reinitialize workparts page:" << workPartsReinitWarning;
    }

    refreshTotals();
    captureSavedState();
    return true;
}

OrderDialogStateSnapshot OrderDialog::buildStateSnapshot() const
{
    OrderDialogStateSnapshot snapshot;
    snapshot.visibleNumber = m_num ? m_num->value() : m_orderNumber;
    snapshot.openedDate = m_openedDate ? m_openedDate->date() : QDate();
    snapshot.openedTime = m_openedTime ? m_openedTime->time() : QTime();
    snapshot.status = currentStatusValue(m_status);
    snapshot.plate = m_plate ? m_plate->text() : QString();
    snapshot.vin = m_vin ? m_vin->text() : QString();
    snapshot.car = m_car ? m_car->text() : QString();
    snapshot.color = m_color ? m_color->text() : QString();
    snapshot.carYear = m_year ? m_year->value() : 0;
    snapshot.odometer = m_odometer ? m_odometer->value() : 0;
    snapshot.engineCode = m_engineCode ? m_engineCode->text() : QString();
    snapshot.engineVolume = m_engineVolume ? m_engineVolume->text() : QString();
    snapshot.fuelType = selectedFuelType();
    snapshot.client = m_client ? m_client->text() : QString();
    snapshot.phone = m_phone ? m_phone->text() : QString();
    snapshot.masterId = m_master ? m_master->currentData().toString() : QString();
    snapshot.discount = m_discount ? m_discount->value() : 0.0;
    snapshot.discountMode = m_discountMode->currentData().toString();
    return snapshot;
}

OrderWorkspaceInput OrderDialog::buildWorkspaceInput(const QString& status, double worksSum, double partsSum, double discount) const
{
    OrderWorkspaceInput input;
    const int visibleNumber = m_num ? m_num->value() : 0;
    const QString plateRaw = m_plate ? m_plate->text().trimmed() : QString();
    const QString plateNorm = PlateUtils::normalizePlate(plateRaw);

    input.number = m_orderNumber;
    input.creationRequestUid = m_creationRequestUid;
    input.visibleNumber = visibleNumber;
    input.openedAt = currentOpenedAt();
    input.status = nonNullDialogText(status);
    input.plate = nonNullDialogText(PlateUtils::formatPlateDisplay(plateNorm));
    input.plateNorm = nonNullDialogText(plateNorm);
    input.vin = nonNullDialogText(m_vin ? m_vin->text().trimmed().toUpper() : QString());
    input.car = nonNullDialogText(titleCaseWords(m_car ? m_car->text().trimmed() : QString()));
    input.color = nonNullDialogText(capitalizeSentence(m_color ? m_color->text().trimmed() : QString()));
    input.carYear = m_year ? m_year->value() : 0;
    input.odometer = m_odometer ? m_odometer->value() : 0;
    input.engineType = nonNullDialogText(m_engineCode ? m_engineCode->text().trimmed() : QString());
    input.engineVolume = nonNullDialogText(m_engineVolume ? m_engineVolume->text().trimmed() : QString());
    input.fuelType = nonNullDialogText(selectedFuelType());
    input.client = nonNullDialogText(titleCaseWords(m_client ? m_client->text().trimmed() : QString()));
    input.clientPhone = nonNullDialogText(m_phone ? m_phone->text().trimmed() : QString());
    input.masterId = nonNullDialogText(m_master ? m_master->currentData().toString() : QString());
    input.worksSum = worksSum;
    input.partsSum = partsSum;
    input.discount = discount;
    input.discountMode = m_discountMode->currentData().toString();
    return input;
}

QString OrderDialog::currentStateSignature() const
{
    const auto signature = OrderDialogStateSignature::build(buildStateSnapshot());
    return m_newOrderPost ? signature + QStringLiteral("|post=") + m_newOrderPost->currentData().toString() : signature;
}

bool OrderDialog::hasUnsavedChanges() const
{
    if (m_userInteractedWithForm && currentStateSignature() != m_savedStateSignature) return true;
    if (m_workPartsPage && m_workPartsPage->hasPendingEdits()) return true;
    return false;
}

void OrderDialog::captureSavedState()
{
    m_savedStateSignature = currentStateSignature();
    m_userInteractedWithForm = false;
    m_recoveryDirtyNotified = false;
    notifyDraftDirtyChanged();
}

QObject* OrderDialog::draftQObject()
{
    return this;
}

QString OrderDialog::recoveryModule() const
{
    return QStringLiteral("orders");
}

QString OrderDialog::recoveryFormType() const
{
    return QStringLiteral("OrderDialog");
}

QString OrderDialog::recoveryEntityType() const
{
    return QStringLiteral("work_order");
}

QString OrderDialog::recoveryEntityId() const
{
    return m_orderNumber > 0 ? QString::number(m_orderNumber) : QStringLiteral("new");
}

QString OrderDialog::recoveryOrderUid() const
{
    return m_orderNumber > 0 ? OrderDialogReadQueryService::canonicalOrderUidForInternalNumber(m_orderNumber) : QString();
}

QString OrderDialog::draftEntityKey() const
{
    const QString orderUid = recoveryOrderUid().trimmed();
    if (!orderUid.isEmpty()) return orderUid;
    return QStringLiteral("order_%1").arg(m_orderNumber > 0 ? m_orderNumber : (m_num ? m_num->value() : -1));
}

QString OrderDialog::committedBaseVersion() const
{
    return m_savedStateSignature;
}

bool OrderDialog::isDraftDirty() const
{
    if (m_readOnly) return false;
    return hasUnsavedChanges();
}

QJsonObject OrderDialog::serializeDraftState() const
{
    QJsonObject payload{
        {QStringLiteral("orderNumber"), m_orderNumber},
        {QStringLiteral("creationRequestUid"), m_creationRequestUid},
        {QStringLiteral("visibleOrderNumber"), m_num ? m_num->value() : m_orderNumber},
        {QStringLiteral("openedDate"), m_openedDate ? m_openedDate->date().toString(Qt::ISODate) : QString()},
        {QStringLiteral("openedTime"), m_openedTime ? m_openedTime->time().toString(QStringLiteral("HH:mm:ss")) : QString()},
        {QStringLiteral("status"), currentStatusValue(m_status)},
        {QStringLiteral("plate"), m_plate ? m_plate->text() : QString()},
        {QStringLiteral("vin"), m_vin ? m_vin->text() : QString()},
        {QStringLiteral("car"), m_car ? m_car->text() : QString()},
        {QStringLiteral("color"), m_color ? m_color->text() : QString()},
        {QStringLiteral("year"), m_year ? m_year->value() : 0},
        {QStringLiteral("odometer"), m_odometer ? m_odometer->value() : 0},
        {QStringLiteral("engineCode"), m_engineCode ? m_engineCode->text() : QString()},
        {QStringLiteral("engineVolume"), m_engineVolume ? m_engineVolume->text() : QString()},
        {QStringLiteral("fuelType"), selectedFuelType()},
        {QStringLiteral("client"), m_client ? m_client->text() : QString()},
        {QStringLiteral("phone"), m_phone ? m_phone->text() : QString()},
        {QStringLiteral("masterId"), m_master ? m_master->currentData().toString() : QString()},
        {QStringLiteral("discount"), m_discount ? m_discount->value() : 0.0},
        {QStringLiteral("discount_mode"), m_discountMode->currentData().toString()}
    };
    if (m_newOrderPost) payload.insert(QStringLiteral("preferredPost"), m_newOrderPost->currentData().toInt());
    if (m_workPartsPage && m_workPartsPage->hasPendingEdits()) {
        payload.insert(QStringLiteral("workParts"), m_workPartsPage->serializeDraftState());
    }
    return payload;
}

bool OrderDialog::restoreDraftState(const QJsonObject& payload, QString* errorText)
{
    const auto requestUid = payload.value(QStringLiteral("creationRequestUid")).toString().trimmed();
    if (!requestUid.isEmpty()) m_creationRequestUid = requestUid;
    if (m_newOrderPost) m_newOrderPost->setCurrentIndex(qMax(0,m_newOrderPost->findData(payload.value("preferredPost").toInt())));
    if (m_num) m_num->setValue(payload.value(QStringLiteral("visibleOrderNumber")).toInt(m_num->value()));
    if (m_openedDate) {
        const QDate d = DateFormats::parseDate(payload.value(QStringLiteral("openedDate")).toString());
        if (d.isValid()) m_openedDate->setDate(d);
    }
    if (m_openedTime) {
        const QTime t = QTime::fromString(payload.value(QStringLiteral("openedTime")).toString(), QStringLiteral("HH:mm:ss"));
        if (t.isValid()) m_openedTime->setTime(t);
    }
    if (m_status) {
        const int idx = statusComboIndexByValue(m_status, payload.value(QStringLiteral("status")).toString());
        if (idx >= 0) m_status->setCurrentIndex(idx);
    }
    if (m_plate) m_plate->setText(payload.value(QStringLiteral("plate")).toString());
    if (m_vin) m_vin->setText(payload.value(QStringLiteral("vin")).toString());
    if (m_car) m_car->setText(payload.value(QStringLiteral("car")).toString());
    if (m_color) m_color->setText(payload.value(QStringLiteral("color")).toString());
    if (m_year) m_year->setValue(payload.value(QStringLiteral("year")).toInt(m_year->value()));
    if (m_odometer) m_odometer->setValue(payload.value(QStringLiteral("odometer")).toInt(m_odometer->value()));
    if (m_engineCode) m_engineCode->setText(payload.value(QStringLiteral("engineCode")).toString());
    if (m_engineVolume) m_engineVolume->setText(payload.value(QStringLiteral("engineVolume")).toString());
    setSelectedFuelType(payload.value(QStringLiteral("fuelType")).toString());
    if (m_client) m_client->setText(payload.value(QStringLiteral("client")).toString());
    if (m_phone) m_phone->setText(payload.value(QStringLiteral("phone")).toString());
    if (m_master) {
        const QString masterId = payload.value(QStringLiteral("masterId")).toString();
        for (int i = 0; i < m_master->count(); ++i) {
            if (m_master->itemData(i).toString() == masterId) {
                m_master->setCurrentIndex(i);
                break;
            }
        }
    }
    if (m_discount) m_discount->setValue(payload.value(QStringLiteral("discount")).toDouble(m_discount->value()));
    m_discountMode->setCurrentIndex(m_discountMode->findData(payload.value(QStringLiteral("discount_mode")).toString(QStringLiteral("legacy"))));

    const QJsonObject workParts = payload.value(QStringLiteral("workParts")).toObject();
    if (!workParts.isEmpty()) {
        QString workPartsError;
        if (!ensureWorkPartsPage(&workPartsError) || !m_workPartsPage || !m_workPartsPage->restoreDraftState(workParts, &workPartsError)) {
            if (errorText) *errorText = workPartsError.trimmed().isEmpty() ? QStringLiteral("Не вдалося відновити роботи/запчастини") : workPartsError;
            return false;
        }
    }

    m_userInteractedWithForm = true;
    m_recoveryDirtyNotified = true;
    notifyDraftDirtyChanged();
    return true;
}

void OrderDialog::notifyDraftDirtyChanged()
{
    auto* crash = AppContext::instance().crashRecoveryManager();
    if (!crash) return;
    const bool dirty = isDraftDirty();
    if (dirty == m_recoveryDirtyNotified) return;
    m_recoveryDirtyNotified = dirty;
    crash->notifyParticipantDirty(this);
}

bool OrderDialog::ensureWorkPartsPageForRecovery(QString* errorText)
{
    return ensureWorkPartsPage(errorText);
}

WorkPartsDialog* OrderDialog::workPartsPageForRecovery() const
{
    return m_workPartsPage;
}


void OrderDialog::showEvent(QShowEvent* event)
{
    BackupPerformanceGuard::hold(AppContext::instance().backupManager(), 1500);
    QDialog::showEvent(event);
    if (!isMaximized() && !isFullScreen()) {
        centerOnScreen();
    }
    if (m_focusOdometerOnShow && m_odometer) {
        QPointer<OrderDialog> self(this);
        QPointer<QSpinBox> odometer = m_odometer;
        QPointer<QLineEdit> plate = m_plate;
        QPointer<QLineEdit> vin = m_vin;
        QPointer<QLineEdit> car = m_car;
        QPointer<QLineEdit> color = m_color;
        QPointer<QLineEdit> engineCode = m_engineCode;
        QPointer<QLineEdit> engineVolume = m_engineVolume;
        QPointer<QLineEdit> client = m_client;
        QPointer<QLineEdit> phone = m_phone;
        QTimer::singleShot(0, this, [self, odometer, plate, vin, car, color, engineCode, engineVolume, client, phone]() {
            if (!self || !odometer) return;
            if (plate) plate->deselect();
            if (vin) vin->deselect();
            if (car) car->deselect();
            if (color) color->deselect();
            if (engineCode) engineCode->deselect();
            if (engineVolume) engineVolume->deselect();
            if (client) client->deselect();
            if (phone) phone->deselect();
            odometer->setFocus(Qt::OtherFocusReason);
            odometer->selectAll();
            self->m_focusOdometerOnShow = false;
        });
        return;
    }

    if (m_plate) {
        QPointer<OrderDialog> self(this);
        QPointer<QLineEdit> plate = m_plate;
        QTimer::singleShot(0, m_plate, [self, plate]() {
            if (!self || !plate) return;
            plate->setFocus(Qt::OtherFocusReason);
            plate->selectAll();
        });
    }
}

bool OrderDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (watched && watched->property("tracks_order_user_edit").toBool() && event) {
        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick:
        case QEvent::KeyPress:
        case QEvent::Wheel:
        case QEvent::InputMethod:
        case QEvent::Drop:
        case QEvent::ContextMenu:
        case QEvent::ShortcutOverride:
            m_userInteractedWithForm = true;
            notifyDraftDirtyChanged();
            break;
        default:
            break;
        }
    }

    if (watched == m_discount && m_discount) {
        if (event->type() == QEvent::FocusIn || event->type() == QEvent::MouseButtonPress) {
            if (qFuzzyIsNull(m_discount->value())) {
                m_discount->clear();
                if (auto* le = m_discount->findChild<QLineEdit*>()) {
                    QTimer::singleShot(0, le, [le]() {
                        le->setCursorPosition(0);
                        le->deselect();
                    });
                }
            }
        }
    }
    return QDialog::eventFilter(watched, event);
}

void OrderDialog::centerOnScreen()
{
    QScreen* screen = nullptr;
    if (parentWidget() && parentWidget()->windowHandle() && parentWidget()->windowHandle()->screen()) {
        screen = parentWidget()->windowHandle()->screen();
    }
    if (!screen && windowHandle() && windowHandle()->screen()) {
        screen = windowHandle()->screen();
    }
    if (!screen) {
        screen = QGuiApplication::screenAt(QCursor::pos());
    }
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) return;

    const QRect available = screen->availableGeometry();
    const QRect frame = frameGeometry();
    move(available.left() + (available.width() - frame.width()) / 2,
         available.top() + (available.height() - frame.height()) / 2);
}


void OrderDialog::armUserEditTracking(QWidget* widget)
{
    if (!widget) return;
    widget->setProperty("tracks_order_user_edit", true);
    widget->installEventFilter(this);
    if (auto* lineEdit = widget->findChild<QLineEdit*>()) {
        lineEdit->setProperty("tracks_order_user_edit", true);
        lineEdit->installEventFilter(this);
    }
}

void OrderDialog::reject()
{
    if (!m_readOnly && hasUnsavedChanges()) {
        const auto answer = QMessageBox::question(
            this,
            QString::fromUtf8("Незбережені зміни"),
            QString::fromUtf8("У наряді є незбережені зміни. Зберегти їх перед закриттям?"),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
            QMessageBox::Yes);
        if (answer == QMessageBox::Yes) {
            accept();
            return;
        }
        if (answer == QMessageBox::Cancel) {
            return;
        }
    }

    if (m_isNewOrder && m_draftCreated && m_orderNumber > 0) {
        QString stockError;
        if (m_spareIssued && !WorkshopSpareStockService::cancelDraftIssue(m_pendingSpare.id,m_orderNumber,&stockError)) {
            QMessageBox::warning(this,QString::fromUtf8("Склад"),stockError);return;
        }
        m_spareIssued=false;
        QString deleteError;
        if (!AppRuntimeService::hardDeleteOrder(m_orderNumber, &deleteError) && !deleteError.trimmed().isEmpty()) {
            qWarning() << "Draft order cleanup failed:" << deleteError;
        }
    }
    QDialog::reject();
}


double OrderDialog::servicesTotal() const
{
    return m_orderNumber<=0 ? 0.0 : OrderDialogReadQueryService::servicesTotal(m_orderNumber);
}


double OrderDialog::partsTotal() const
{
    return m_orderNumber<=0 ? 0.0 : OrderDialogReadQueryService::partsTotal(m_orderNumber);
}


void OrderDialog::refreshTotals()
{
    if (!m_totals) return;
    const auto totals = m_orderNumber>0 ? OrderDialogReadQueryService::financialTotals(m_orderNumber)
        : OrderFinancialService::TotalsResult{OrderFinancialService::TotalsStatus::Ok,0,0,{}};
    if (!totals.ok()) {
        m_totals->setText(QStringLiteral("Фінансові дані недоступні: %1").arg(totals.error));
        return;
    }
    const double w = totals.worksCents / 100.0;
    const double p = totals.partsCents / 100.0;
    const double disc = m_discount ? FormatUtils::discountAmount(m_discount->value(), m_discountMode->currentData().toString(), w) : 0.0;
    const double total = qMax(0.0, FormatUtils::roundMoneyValue(w + p - disc));

    m_totals->setText(
        QString::fromUtf8("Роботи: %1 грн   |   Запчастини: %2 грн   |   Знижка: %3 грн   |   Разом: %4 грн")
            .arg(FormatUtils::money(w, 2),
                 FormatUtils::money(p, 2),
                 FormatUtils::money(disc, 2),
                 FormatUtils::money(total, 2))
    );
}
