#include "core/util/DateFormats.h"
#include "ui/tabs/intake/VehicleIntakePage.h"
#include "core/crm/VehicleIntakeService.h"
#include "core/services/settings/PersonnelSettingsService.h"
#include "core/util/PlateUtils.h"
#include "core/util/VehicleNameUtils.h"
#include "integrations/opendatabot/OpenDataBotClient.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDateTimeEdit>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPainter>
#include <QStyle>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QIntValidator>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QTableWidget>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>

namespace {

struct IntakeTableGuard {
    explicit IntakeTableGuard(QTableWidget* table)
        : table(table)
        , blocker(table)
        , sorting(table ? table->isSortingEnabled() : false)
        , selectedUid(table && table->currentRow() >= 0 && table->item(table->currentRow(), 0)
              ? table->item(table->currentRow(), 0)->text()
              : QString())
        , scroll(table && table->verticalScrollBar() ? table->verticalScrollBar()->value() : 0)
    {
        if (!table) return;
        table->setUpdatesEnabled(false);
        table->setSortingEnabled(false);
    }

    ~IntakeTableGuard()
    {
        if (!table) return;
        if (!selectedUid.isEmpty()) {
            for (int row = 0; row < table->rowCount(); ++row) {
                if (table->item(row, 0) && table->item(row, 0)->text() == selectedUid) {
                    table->selectRow(row);
                    break;
                }
            }
        }
        table->setSortingEnabled(sorting);
        if (table->verticalScrollBar()) table->verticalScrollBar()->setValue(scroll);
        table->setUpdatesEnabled(true);
    }

    QTableWidget* table = nullptr;
    QSignalBlocker blocker;
    bool sorting = false;
    QString selectedUid;
    int scroll = 0;
};

QIcon intakeIcon(ushort symbol)
{
    QPixmap image(48,48);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::TextAntialiasing);
    QFont font("Segoe MDL2 Assets");
    font.setPixelSize(36);
    painter.setFont(font);
    painter.setPen(QColor("#76dce5"));
    painter.drawText(image.rect(),Qt::AlignCenter,QString(QChar(symbol)));
    return QIcon(image);
}

void applyIntakeActionButtonStyle(QPushButton* button)
{
    if (!button) return;
    button->setCursor(Qt::PointingHandCursor);
    button->setMinimumHeight(36);
    button->setStyleSheet(QStringLiteral(
        "QPushButton{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "stop:0 rgba(255,255,255,0.18),stop:0.10 rgba(255,255,255,0.08),"
        "stop:0.11 rgba(63,72,82,0.90),stop:0.55 rgba(42,48,56,0.94),"
        "stop:1 rgba(26,31,37,0.96));color:#f8f9fb;"
        "border:1px solid rgba(255,255,255,0.16);border-radius:8px;"
        "padding:7px 14px;font-weight:650;}"
        "QPushButton:hover{border:1px solid rgba(220,236,255,0.95);}"
        "QPushButton:pressed{background:#151a20;}"));
}

void applyIntakeInputStyle(QWidget* widget)
{
    if (!widget) return;
    widget->setStyleSheet(QStringLiteral(
        "QLineEdit,QDateEdit,QDateTimeEdit,QPlainTextEdit,QComboBox{background:#ffffff;color:#05070a;"
        "placeholder-text-color:#1f2937;border:1px solid #aab4c0;border-radius:6px;"
        "padding:6px 8px;selection-background-color:#2b5d95;selection-color:#ffffff;}"
        "QLineEdit:focus,QDateEdit:focus,QDateTimeEdit:focus,QPlainTextEdit:focus,QComboBox:focus{border:2px solid #58c5d5;padding:5px 7px;}"
        "QComboBox QAbstractItemView{background:#ffffff;color:#05070a;selection-background-color:#d8e6ff;selection-color:#05070a;}"
        "QComboBox QLineEdit{background:#ffffff;color:#05070a;border:none;padding:0px;}"));
}

void applyIntakeCheckBoxStyle(QCheckBox* checkbox)
{
    if (!checkbox) return;
    checkbox->setStyleSheet(QStringLiteral(
        "QCheckBox{color:#ffffff;background:transparent;spacing:8px;font-weight:650;min-height:26px;}"
        "QCheckBox::indicator{width:18px;height:18px;border:2px solid #ffffff;border-radius:4px;background:#ffffff;}"
        "QCheckBox::indicator:checked{image:url(:/assets/check_white.png);background:#176bff;}"));
}

QString tableStyleSheet()
{
    return QStringLiteral(
        "QTableWidget{background:#ffffff;color:#111111;alternate-background-color:#f6f8fb;"
        "gridline-color:#d5dbe3;border:1px solid rgba(255,255,255,0.10);outline:none;}"
        "QTableWidget::item{background:#ffffff;color:#111111;padding:4px 6px;"
        "border-right:1px solid #e7ebf0;border-bottom:1px solid #edf1f5;}"
        "QTableWidget::item:selected{background:#d8e6ff;color:#111111;}"
        "QHeaderView::section{background:#f5f7fa;color:#111111;border:none;"
        "border-right:1px solid #dde3ea;border-bottom:1px solid #d7dde4;font-weight:800;padding:7px 8px;}");
}

QString pageStyleSheet()
{
    return QStringLiteral(
        "VehicleIntakePage,QScrollArea,QWidget#intakeForm{background:#182331;color:#f3f6fb;}"
        "QGroupBox{color:#f3f6fb;background:#1c2938;border:1px solid #586b7c;border-radius:9px;"
        "margin-top:9px;padding:8px 8px 6px 8px;font-weight:700;}"
        "QGroupBox::title{subcontrol-origin:margin;left:12px;padding:0 6px;}"
        "QLabel{color:#f3f6fb;}QRadioButton{color:#ffffff;spacing:8px;min-height:24px;}"
        "QRadioButton::indicator{width:16px;height:16px;background:#ffffff;border:2px solid #ffffff;border-radius:8px;}"
        "QRadioButton::indicator:checked{background:#176bff;border:2px solid #ffffff;}");
}

QLineEdit* editableChoice(QGridLayout* grid, int row, int column, const QString& label,
                          const QStringList& choices)
{
    auto* combo = new QComboBox;
    combo->setEditable(true);
    combo->setInsertPolicy(QComboBox::NoInsert);
    combo->addItems(choices);
    combo->setCurrentIndex(-1);
    applyIntakeInputStyle(combo);
    grid->addWidget(new QLabel(label),row,column);
    grid->addWidget(combo,row,column+1);
    return combo->lineEdit();
}

QJsonObject objectValue(const QJsonObject& parent, const QString& key)
{
    const QJsonValue value = parent.value(key);
    return value.isObject() ? value.toObject() : QJsonObject{};
}

QString strValue(const QJsonObject& object, const QString& key)
{
    return object.value(key).toString();
}

bool boolValue(const QJsonObject& object, const QString& key, bool defaultValue = false)
{
    return object.contains(key) ? object.value(key).toBool() : defaultValue;
}

QString statusText(int id)
{
    return id == 1 ? QStringLiteral("issues") : QStringLiteral("ok");
}

int statusId(const QString& status)
{
    return status == QStringLiteral("issues") ? 1 : 0;
}

} // namespace

VehicleIntakePage::VehicleIntakePage(QWidget* parent) : QWidget(parent)
{
    buildUi();
    refresh();
}

void VehicleIntakePage::buildUi()
{
    setStyleSheet(pageStyleSheet());

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    auto* heading = new QHBoxLayout;
    auto* title = new QLabel(this);
    title->setStyleSheet("font:700 23px 'Segoe UI';color:#f5f8fc;");
    auto* stepLabel = new QLabel(this);
    stepLabel->setStyleSheet("color:white;border:1px solid #67d6df;border-radius:12px;padding:3px 13px;font-weight:700;");
    auto* badge = new QLabel(this);
    badge->setPixmap(intakeIcon(0xe8fd).pixmap(28,28));
    heading->addWidget(badge);
    heading->addWidget(title,1);
    heading->addWidget(stepLabel);
    root->addLayout(heading);
    auto* divider = new QFrame(this);
    divider->setFixedHeight(2);
    divider->setStyleSheet("background:#67d6df;");
    root->addWidget(divider);
    m_steps = new QStackedWidget(this);
    root->addWidget(m_steps,1);
    auto addPage = [&] {
        auto* scroll = new QScrollArea(m_steps);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto* host = new QWidget;
        host->setObjectName("intakeForm");
        scroll->setWidget(host);
        m_steps->addWidget(scroll);
        return host;
    };
    auto* formHost = addPage();
    auto* main = new QVBoxLayout(formHost);
    main->setContentsMargins(2,0,2,4);
    main->setSpacing(6);
    auto* header = new QGroupBox(QString::fromUtf8("Дані акту"), formHost);
    auto* headerGrid = new QGridLayout(header);
    m_intakeUid = new QLineEdit(header);
    m_intakeUid->setVisible(false);
    m_actNumber = new QLineEdit(header);
    m_actDate = new QDateEdit(QDate::currentDate(), header);
    m_actDate->setCalendarPopup(true);
    m_order = new QLineEdit(header);
    m_order->setPlaceholderText(QString::fromUtf8("номер наряду або order_uid"));
    auto* autofill = new QPushButton(QString::fromUtf8("Підставити з наряду"), header);
    applyIntakeActionButtonStyle(autofill);
    connect(autofill, &QPushButton::clicked, this, &VehicleIntakePage::autofillFromOrder);
    for (QWidget* w : {static_cast<QWidget*>(m_actNumber), static_cast<QWidget*>(m_actDate), static_cast<QWidget*>(m_order)}) {
        applyIntakeInputStyle(w);
    }
    headerGrid->addWidget(new QLabel(QString::fromUtf8("Номер акту *"), header), 0, 0);
    headerGrid->addWidget(m_actNumber, 0, 1);
    headerGrid->addWidget(new QLabel(QString::fromUtf8("Дата акту *"), header), 0, 2);
    headerGrid->addWidget(m_actDate, 0, 3);
    headerGrid->addWidget(new QLabel(QString::fromUtf8("№ замовлення-наряду *"), header), 1, 0);
    headerGrid->addWidget(m_order, 1, 1, 1, 2);
    headerGrid->addWidget(autofill, 1, 3);
    main->addWidget(header);

    auto* clientBox = new QGroupBox(QString::fromUtf8("Дані клієнта"), formHost);
    auto* clientForm = new QGridLayout(clientBox);
    m_client = new QLineEdit(clientBox);
    m_phone = new QLineEdit(clientBox);
    m_clientUid = new QLineEdit(clientBox);
    m_clientUid->hide();
    for (auto* edit : {m_client,m_phone}) applyIntakeInputStyle(edit);
    clientForm->addWidget(new QLabel(tr("Клієнт *")),0,0);
    clientForm->addWidget(m_client,0,1);
    clientForm->addWidget(new QLabel(tr("Телефон *")),0,2);
    clientForm->addWidget(m_phone,0,3);
    clientForm->setColumnStretch(1,2);
    clientForm->setColumnStretch(3,1);
    main->addWidget(clientBox);

    auto* carBox = new QGroupBox(QString::fromUtf8("Дані авто"), formHost);
    auto* carGrid = new QGridLayout(carBox);
    auto addGridLine = [&](const QString& label, int row, int col) {
        auto* edit = new QLineEdit(carBox);
        applyIntakeInputStyle(edit);
        carGrid->addWidget(new QLabel(label, carBox), row, col * 2);
        carGrid->addWidget(edit, row, col * 2 + 1);
        return edit;
    };
    m_vehicle = addGridLine(QString::fromUtf8("Марка / модель *"), 0, 0);
    m_plate = addGridLine(QString::fromUtf8("Державний номер"), 0, 1);
    m_vin = addGridLine(QStringLiteral("VIN"), 1, 0);
    QStringList years;
    for(int year=QDate::currentDate().year()+1;year>=1950;--year) years.append(QString::number(year));
    m_year = editableChoice(carGrid,1,2,tr("Рік випуску"),years);
    m_engine = editableChoice(carGrid,2,0,tr("Тип двигуна"),{tr("Бензин"),tr("Дизель"),tr("Гібрид"),tr("Електро")});
    m_gearbox = editableChoice(carGrid,2,2,tr("КПП"),{tr("Механічна"),tr("Автоматична"),tr("Робот"),tr("Варіатор")});
    m_mileage = addGridLine(tr("Пробіг, км"),3,0);
    m_mileage->setValidator(new QIntValidator(0,2147483647,m_mileage));
    m_fuel = editableChoice(carGrid,3,2,tr("Рівень пального"),{"0","¼","½","¾",tr("Повний")});
    carGrid->setColumnStretch(1,1);
    carGrid->setColumnStretch(3,1);
    m_vehicleUid = new QLineEdit(carBox);
    m_vehicleUid->setVisible(false);
    m_master = new QComboBox(carBox);
    m_master->setEditable(true);
    m_master->setInsertPolicy(QComboBox::NoInsert);
    applyIntakeInputStyle(m_master);

    m_plateLookupStatus = new QLabel(carBox);
    m_plateLookupStatus->setStyleSheet(QStringLiteral("QLabel{color:#d8e6ff;font-size:11px;font-weight:600;}"));
    carGrid->addWidget(m_plateLookupStatus, 4, 2, 1, 2);
    connect(m_plate, &QLineEdit::editingFinished, this, &VehicleIntakePage::lookupVehicleByPlate, Qt::UniqueConnection);
    m_acceptedAt = new QDateTimeEdit(QDateTime::currentDateTime(), carBox);
    m_plannedReturnAt = new QDateTimeEdit(QDateTime::currentDateTime().addDays(1), carBox);
    for (auto* dt : {m_acceptedAt, m_plannedReturnAt}) {
        dt->setCalendarPopup(true);
        dt->setDisplayFormat(QStringLiteral("dd.MM.yy HH:mm"));
        applyIntakeInputStyle(dt);
    }
    main->addWidget(carBox);
    auto* schedule = new QGroupBox(formHost);
    auto* timeRow = new QHBoxLayout(schedule);
    timeRow->addWidget(new QLabel(tr("Майстер *"))); timeRow->addWidget(m_master,1);
    timeRow->addWidget(new QLabel(tr("Дата та час приймання *"))); timeRow->addWidget(m_acceptedAt,1);
    timeRow->addWidget(new QLabel(tr("Орієнтована дата видачі *"))); timeRow->addWidget(m_plannedReturnAt,1);
    main->addWidget(schedule);
    auto* notes = new QHBoxLayout;
    auto noteField = [&](const QString& caption) {
        auto* box = new QGroupBox(caption,formHost);
        auto* layout = new QVBoxLayout(box);
        auto* edit = new QPlainTextEdit(box);
        edit->setMinimumHeight(58);
        applyIntakeInputStyle(edit);
        layout->addWidget(edit);
        notes->addWidget(box,1);
        return edit;
    };
    m_complaint = noteField(tr("Заявлена несправність / побажання клієнта"));
    m_diagnostics = noteField(tr("Погоджені роботи"));
    main->addLayout(notes,1);

    formHost = addPage();
    main = new QVBoxLayout(formHost);
    main->setContentsMargins(2,0,2,4);
    main->setSpacing(6);
    auto* conditionBox = new QGroupBox(QString::fromUtf8("Зовнішній стан авто на момент приймання"), formHost);
    auto* conditionGrid = new QGridLayout(conditionBox);
    const QList<QPair<QString, QString>> zones = {
        {QStringLiteral("body"), QString::fromUtf8("Кузов / ЛФП")},
        {QStringLiteral("glass"), QString::fromUtf8("Скло / оптика")},
        {QStringLiteral("interior"), QString::fromUtf8("Салон / багажник")},
        {QStringLiteral("tires"), QString::fromUtf8("Шини / диски")}
    };
    int zoneRow = 0;
    for (const auto& zone : zones) {
        conditionGrid->addWidget(new QLabel(zone.second, conditionBox), zoneRow, 0);
        auto* ok = new QRadioButton(QString::fromUtf8("без зауважень"), conditionBox);
        auto* issues = new QRadioButton(QString::fromUtf8("є зауваження"), conditionBox);
        ok->setChecked(true);
        auto* group = new QButtonGroup(conditionBox);
        group->addButton(ok, 0);
        group->addButton(issues, 1);
        m_conditionGroups.insert(zone.first, group);
        auto* note = new QPlainTextEdit(conditionBox);
        note->setFixedHeight(37);
        note->setPlaceholderText(QString::fromUtf8("Примітка про пошкодження"));
        applyIntakeInputStyle(note);
        m_conditionNotes.insert(zone.first, note);
        conditionGrid->addWidget(ok, zoneRow, 1);
        conditionGrid->addWidget(issues, zoneRow, 2);
        conditionGrid->addWidget(note, zoneRow, 3);
        ++zoneRow;
    }
    main->addWidget(conditionBox);

    auto* equipmentBox = new QGroupBox(QString::fromUtf8("Комплектація та майно в авто"), formHost);
    auto* equipmentGrid = new QGridLayout(equipmentBox);
    const QList<QPair<QString, QString>> equipment = {
        {QStringLiteral("main_key"), QString::fromUtf8("ключ основний")},
        {QStringLiteral("service_book"), QString::fromUtf8("сервісна книжка")},
        {QStringLiteral("spare_wheel"), QString::fromUtf8("запасне колесо")},
        {QStringLiteral("warning_triangle"), QString::fromUtf8("знак аварійної зупинки")},
        {QStringLiteral("charger"), QString::fromUtf8("зарядний пристрій")},
        {QStringLiteral("spare_key"), QString::fromUtf8("ключ запасний")},
        {QStringLiteral("jack"), QString::fromUtf8("домкрат")},
        {QStringLiteral("first_aid"), QString::fromUtf8("аптечка")},
        {QStringLiteral("mats"), QString::fromUtf8("килимки")},
        {QStringLiteral("docs_receipts"), QString::fromUtf8("документи / чеки")},
        {QStringLiteral("registration"), QString::fromUtf8("техпаспорт")},
        {QStringLiteral("wheel_wrench"), QString::fromUtf8("балонний ключ")},
        {QStringLiteral("extinguisher"), QString::fromUtf8("вогнегасник")},
        {QStringLiteral("media_screen"), QString::fromUtf8("магнітола / екран")},
        {QStringLiteral("other"), QString::fromUtf8("інше")}
    };
    int item = 0;
    for (const auto& def : equipment) {
        auto* cb = new QCheckBox(def.second, equipmentBox);
        applyIntakeCheckBoxStyle(cb);
        m_equipment.insert(def.first, cb);
        equipmentGrid->addWidget(cb, item / 3, item % 3);
        ++item;
    }
    m_valuables = new QPlainTextEdit(equipmentBox);
    m_valuables->setMinimumHeight(64);
    m_valuables->setMaximumHeight(108);
    applyIntakeInputStyle(m_valuables);
    equipmentGrid->addWidget(new QLabel(QString::fromUtf8("Цінні / особисті речі"), equipmentBox), 0, 3);
    equipmentGrid->addWidget(m_valuables, 1, 3, 4, 1);
    for(int i=0;i<3;++i) equipmentGrid->setColumnStretch(i,1);
    equipmentGrid->setColumnStretch(3,2);
    main->addWidget(equipmentBox);

    auto* permissionsBox = new QGroupBox(QString::fromUtf8("Дозволи та умови приймання"), formHost);
    auto* permissionsGrid = new QGridLayout(permissionsBox);
    const QList<QPair<QString, QString>> permissions = {
        {QStringLiteral("initial_diagnostics"), QString::fromUtf8("первинна діагностика дозволена")},
        {QStringLiteral("test_drive"), QString::fromUtf8("тест-драйв дозволено")},
        {QStringLiteral("photo"), QString::fromUtf8("фотофіксація дозволена")},
        {QStringLiteral("extra_approval"), QString::fromUtf8("додаткові роботи - тільки після погодження")},
        {QStringLiteral("cost_approval"), QString::fromUtf8("попереднє погодження вартості")}
    };
    int permIndex = 0;
    for (const auto& def : permissions) {
        auto* cb = new QCheckBox(def.second, permissionsBox);
        cb->setChecked(true);
        applyIntakeCheckBoxStyle(cb);
        m_permissions.insert(def.first, cb);
        permissionsGrid->addWidget(cb, permIndex / 2, permIndex % 2);
        ++permIndex;
    }
    m_contactMethod = editableChoice(permissionsGrid,1,2,tr("Спосіб зв’язку"),{tr("Телефон"),"Viber","Telegram",tr("Електронна пошта")});
    main->addWidget(permissionsBox);

    m_signatureTransferToggle = new QPushButton(formHost);
    m_signatureTransferToggle->setCheckable(true);
    applyIntakeActionButtonStyle(m_signatureTransferToggle);
    connect(m_signatureTransferToggle, &QPushButton::toggled, this, &VehicleIntakePage::setSignatureSectionExpanded);
    main->addWidget(m_signatureTransferToggle);

    m_signatureTransferBox = new QGroupBox(QString::fromUtf8("Підписи / приймання-передача"), formHost);
    auto* transferGrid = new QGridLayout(m_signatureTransferBox);
    m_customerName = new QLineEdit(m_signatureTransferBox);
    m_receiverName = new QLineEdit(m_signatureTransferBox);
    m_signature = new QLineEdit(m_signatureTransferBox);
    for (auto* w : {m_customerName, m_receiverName, m_signature}) applyIntakeInputStyle(w);
    transferGrid->addWidget(new QLabel(QString::fromUtf8("Автомобіль передав / замовник, ПІБ"), m_signatureTransferBox), 0, 0);
    transferGrid->addWidget(m_customerName, 0, 1);
    transferGrid->addWidget(new QLabel(QString::fromUtf8("Автомобіль прийняв / майстер-приймальник, ПІБ"), m_signatureTransferBox), 1, 0);
    transferGrid->addWidget(m_receiverName, 1, 1);
    transferGrid->addWidget(new QLabel(QString::fromUtf8("Файл підпису клієнта"), m_signatureTransferBox), 2, 0);
    transferGrid->addWidget(m_signature, 2, 1);
    main->addWidget(m_signatureTransferBox);
    setSignatureSectionExpanded(false);

    auto addButton = [this](QLayout* layout,const QString& label) {
        auto* button = new QPushButton(label,this);
        applyIntakeActionButtonStyle(button);
        layout->addWidget(button);
        return button;
    };
    auto* media = new QHBoxLayout;
    main->insertLayout(main->indexOf(m_signatureTransferToggle),media);
    auto mediaButton = [&](const QString& heading,const QString& action,ushort icon) {
        auto* box = new QGroupBox(heading,formHost);
        auto* layout = new QHBoxLayout(box);
        auto* button = addButton(layout,action);
        button->setIcon(intakeIcon(icon));
        media->addWidget(box,1);
        return button;
    };
    auto* photo = mediaButton(tr("Фото"),tr("Додати фото"),0xe722);
    auto* signature = mediaButton(tr("Підпис"),tr("Додати підпис"),0xe70f);
    connect(photo,&QPushButton::clicked,this,&VehicleIntakePage::addPhotoForSelected);
    connect(signature,&QPushButton::clicked,this,&VehicleIntakePage::pickSignatureForSelected);
    main->addStretch(1);

    auto* footer = new QStackedWidget(this);
    footer->setStyleSheet("QStackedWidget,QWidget#intakeFooter {background:#182331;border:0;}");
    root->addWidget(footer);
    auto footerRow = [&] {
        auto* host = new QWidget(footer);
        host->setObjectName("intakeFooter");
        footer->addWidget(host);
        auto* row = new QHBoxLayout(host);
        row->setContentsMargins(0,3,0,3);
        return row;
    };
    auto* first = footerRow();
    first->addStretch();
    auto* cancel = addButton(first,tr("Скасувати"));
    auto* next = addButton(first,tr("Прийняти та продовжити"));
    first->addStretch();
    auto* second = footerRow();
    auto* back = addButton(second,tr("Назад"));
    auto* save = addButton(second,tr("Зберегти"));
    auto* pdf = addButton(second,tr("Зберегти в PDF"));
    auto* blank = addButton(second,tr("Друк порожнього акту"));
    auto* finish = addButton(second,tr("Завершити приймання"));
    cancel->setIcon(intakeIcon(0xe711));
    next->setIcon(style()->standardIcon(QStyle::SP_ArrowRight));
    back->setIcon(intakeIcon(0xe72b));
    save->setIcon(intakeIcon(0xe74e));
    pdf->setIcon(intakeIcon(0xea90));
    blank->setIcon(intakeIcon(0xe749));
    finish->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
    autofill->setIcon(intakeIcon(0xe71b));
    for(auto* primary:{next,finish})
        primary->setStyleSheet("QPushButton{color:#08251c;background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #beffda,stop:1 #65dd9b);border:1px solid #8effbf;border-radius:8px;padding:7px 16px;font-weight:700;}");
    connect(cancel,&QPushButton::clicked,this,[this]{clearForm();m_table->setCurrentCell(-1,-1);m_steps->setCurrentIndex(0);});
    m_order->setObjectName("intakeOrder");
    m_phone->setObjectName("intakePhone");
    m_master->setObjectName("intakeMaster");
    m_steps->setObjectName("intakeSteps");
    next->setObjectName("intakeContinue");
    back->setObjectName("intakeBack");
    connect(next,&QPushButton::clicked,this,[this] {
        for(auto* field : {m_actNumber,m_order,m_client,m_phone,m_vehicle,m_master->lineEdit()}) {
            if(field->text().trimmed().isEmpty()) {
                QMessageBox::information(this,tr("Приймання авто"),tr("Заповніть поле, позначене зірочкою."));
                field->setFocus();
                return;
            }
        }
        if(saveDraft()) m_steps->setCurrentIndex(1);
    });
    connect(back,&QPushButton::clicked,this,[this]{m_steps->setCurrentIndex(0);});
    connect(save,&QPushButton::clicked,this,&VehicleIntakePage::saveDraft);
    connect(pdf,&QPushButton::clicked,this,&VehicleIntakePage::generatePdfForSelected);
    connect(blank,&QPushButton::clicked,this,&VehicleIntakePage::printBlankAct);
    connect(finish,&QPushButton::clicked,this,[this]{if(saveDraft()){clearForm();m_table->setCurrentCell(-1,-1);m_steps->setCurrentIndex(0);}});
    auto displayStep = [title,stepLabel,footer](int index) {
        title->setText(index ? tr("Приймання авто — огляд та передача") : tr("Приймання авто — дані акту"));
        stepLabel->setText(tr("Крок %1 із 2").arg(index+1));
        footer->setCurrentIndex(index);
    };
    connect(m_steps,&QStackedWidget::currentChanged,this,displayStep);
    displayStep(0);
    m_actsTableToggle = new QPushButton(this);
    m_actsTableToggle->setCheckable(true);
    applyIntakeActionButtonStyle(m_actsTableToggle);
    connect(m_actsTableToggle, &QPushButton::toggled, this, &VehicleIntakePage::setActsTableExpanded);
    auto* records = new QHBoxLayout;
    auto* newAct = addButton(records,tr("Новий акт"));
    connect(newAct,&QPushButton::clicked,cancel,&QPushButton::click);
    records->addWidget(m_actsTableToggle);
    records->addStretch(1);
    root->addLayout(records);

    m_actsTableEmptyLabel = new QLabel(QString::fromUtf8("Актів ще немає"), this);
    m_actsTableEmptyLabel->setAlignment(Qt::AlignCenter);
    m_actsTableEmptyLabel->setStyleSheet(QStringLiteral(
        "QLabel{color:#f3f6fb;background:rgba(255,255,255,0.06);"
        "border:1px solid rgba(255,255,255,0.14);border-radius:8px;padding:12px;}"));
    root->addWidget(m_actsTableEmptyLabel);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(8);
    m_table->setHorizontalHeaderLabels({QStringLiteral("intake_uid"), QString::fromUtf8("Акт"), QString::fromUtf8("Наряд"),
                                        QString::fromUtf8("Клієнт"), QString::fromUtf8("Авто"), QString::fromUtf8("Пробіг"),
                                        QString::fromUtf8("Пальне"), QStringLiteral("pdf")});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->setStyleSheet(tableStyleSheet());
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &VehicleIntakePage::loadSelectedIntake, Qt::UniqueConnection);
    root->addWidget(m_table, 1);
    setActsTableExpanded(false);

    clearForm();
    loadMasters();
}

void VehicleIntakePage::setSignatureSectionExpanded(bool expanded)
{
    if (m_signatureTransferBox) {
        m_signatureTransferBox->setVisible(expanded);
    }
    if (m_signatureTransferToggle) {
        m_signatureTransferToggle->setChecked(expanded);
        m_signatureTransferToggle->setText(expanded
            ? QString::fromUtf8("▲ Сховати підписи / приймання-передачу")
            : QString::fromUtf8("▼ Показати підписи / приймання-передачу"));
    }
}

void VehicleIntakePage::setActsTableExpanded(bool expanded)
{
    const bool hasRows = m_table && m_table->rowCount() > 0;
    if (m_table) {
        m_table->setVisible(expanded && hasRows);
    }
    if (m_actsTableEmptyLabel) {
        m_actsTableEmptyLabel->setVisible(expanded && !hasRows);
    }
    if (m_actsTableToggle) {
        m_actsTableToggle->setChecked(expanded);
        m_actsTableToggle->setText(expanded
            ? QString::fromUtf8("▲ Сховати список актів")
            : QString::fromUtf8("▼ Показати список актів"));
    }
}

QString VehicleIntakePage::selectedIntakeUid() const
{
    if (!m_table || !m_table->currentItem()) return {};
    auto* item = m_table->item(m_table->currentRow(), 0);
    return item ? item->text() : QString();
}

QString VehicleIntakePage::selectedOrderUid() const
{
    if (!m_table || !m_table->currentItem()) return {};
    auto* item = m_table->item(m_table->currentRow(), 2);
    return item ? item->text() : QString();
}

QVariantMap VehicleIntakePage::collectPayload() const
{
    QJsonObject act;
    act[QStringLiteral("act_number")] = m_actNumber->text().trimmed();
    act[QStringLiteral("act_date")] = m_actDate->date().toString(Qt::ISODate);
    act[QStringLiteral("order_number")] = m_order->text().trimmed();
    act[QStringLiteral("client_name")] = m_client->text().trimmed();
    act[QStringLiteral("client_phone")] = m_phone->text().trimmed();
    act[QStringLiteral("vehicle_label")] = m_vehicle->text().trimmed();
    act[QStringLiteral("plate")] = m_plate->text().trimmed();
    act[QStringLiteral("vin")] = m_vin->text().trimmed();
    act[QStringLiteral("year")] = m_year->text().trimmed();
    act[QStringLiteral("engine_type")] = m_engine->text().trimmed();
    act[QStringLiteral("gearbox")] = m_gearbox->text().trimmed();
    act[QStringLiteral("mileage")] = m_mileage->text().trimmed();
    act[QStringLiteral("fuel_level")] = m_fuel->text().trimmed();
    act[QStringLiteral("accepted_at")] = m_acceptedAt->dateTime().toString(Qt::ISODate);
    act[QStringLiteral("planned_return_at")] = m_plannedReturnAt->dateTime().toString(Qt::ISODate);
    act[QStringLiteral("master")] = masterText();
    act[QStringLiteral("complaint")] = m_complaint->toPlainText().trimmed();
    act[QStringLiteral("diagnostics")] = m_diagnostics->toPlainText().trimmed();
    act[QStringLiteral("valuables")] = m_valuables->toPlainText().trimmed();
    act[QStringLiteral("contact_method")] = m_contactMethod->text().trimmed();
    act[QStringLiteral("customer_name")] = m_customerName->text().trimmed();
    act[QStringLiteral("receiver_name")] = m_receiverName->text().trimmed();

    QJsonObject conditions;
    for (auto it = m_conditionGroups.constBegin(); it != m_conditionGroups.constEnd(); ++it) {
        QJsonObject zone;
        zone[QStringLiteral("status")] = statusText(it.value()->checkedId());
        auto* note = m_conditionNotes.value(it.key());
        zone[QStringLiteral("note")] = note ? note->toPlainText().trimmed() : QString();
        conditions[it.key()] = zone;
    }
    act[QStringLiteral("conditions")] = conditions;

    QJsonObject equipment;
    for (auto it = m_equipment.constBegin(); it != m_equipment.constEnd(); ++it) {
        equipment[it.key()] = it.value() && it.value()->isChecked();
    }
    act[QStringLiteral("equipment")] = equipment;

    QJsonObject permissions;
    for (auto it = m_permissions.constBegin(); it != m_permissions.constEnd(); ++it) {
        permissions[it.key()] = it.value() && it.value()->isChecked();
    }
    act[QStringLiteral("permissions")] = permissions;

    QVariantMap p;
    p[QStringLiteral("intake_uid")] = m_intakeUid->text().trimmed();
    p[QStringLiteral("order_uid")] = m_order->text().trimmed();
    p[QStringLiteral("client_uid")] = m_clientUid->text().trimmed();
    p[QStringLiteral("vehicle_uid")] = m_vehicleUid->text().trimmed();
    p[QStringLiteral("mileage")] = m_mileage->text().toInt();
    p[QStringLiteral("fuel_level")] = m_fuel->text().trimmed();
    p[QStringLiteral("exterior_state")] = QString::fromUtf8("Кузов: %1; Скло: %2; Шини: %3")
        .arg(statusText(m_conditionGroups.value(QStringLiteral("body"))->checkedId()),
             statusText(m_conditionGroups.value(QStringLiteral("glass"))->checkedId()),
             statusText(m_conditionGroups.value(QStringLiteral("tires"))->checkedId()));
    p[QStringLiteral("interior_state")] = QString::fromUtf8("Салон: %1")
        .arg(statusText(m_conditionGroups.value(QStringLiteral("interior"))->checkedId()));
    p[QStringLiteral("customer_signature_path")] = m_signature->text().trimmed();
    p[QStringLiteral("checklist_json")] = QString::fromUtf8(QJsonDocument(act).toJson(QJsonDocument::Compact));
    return p;
}

void VehicleIntakePage::applyChecklistJson(const QString& json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    const QJsonObject act = doc.isObject() ? doc.object() : QJsonObject{};
    if (act.isEmpty()) return;

    m_actNumber->setText(strValue(act, QStringLiteral("act_number")));
    const QDate actDate = DateFormats::parseDate(strValue(act, QStringLiteral("act_date")));
    m_actDate->setDate(actDate.isValid() ? actDate : QDate::currentDate());
    m_client->setText(strValue(act, QStringLiteral("client_name")));
    m_phone->setText(strValue(act, QStringLiteral("client_phone")));
    m_vehicle->setText(strValue(act, QStringLiteral("vehicle_label")));
    m_plate->setText(strValue(act, QStringLiteral("plate")));
    m_vin->setText(strValue(act, QStringLiteral("vin")));
    m_year->setText(strValue(act, QStringLiteral("year")));
    m_engine->setText(strValue(act, QStringLiteral("engine_type")));
    m_gearbox->setText(strValue(act, QStringLiteral("gearbox")));
    if (!strValue(act, QStringLiteral("mileage")).isEmpty()) m_mileage->setText(strValue(act, QStringLiteral("mileage")));
    if (!strValue(act, QStringLiteral("fuel_level")).isEmpty()) m_fuel->setText(strValue(act, QStringLiteral("fuel_level")));
    const QDateTime accepted = DateFormats::parseDateTime(strValue(act, QStringLiteral("accepted_at")));
    const QDateTime planned = DateFormats::parseDateTime(strValue(act, QStringLiteral("planned_return_at")));
    m_acceptedAt->setDateTime(accepted.isValid() ? accepted : QDateTime::currentDateTime());
    m_plannedReturnAt->setDateTime(planned.isValid() ? planned : QDateTime::currentDateTime().addDays(1));
    setMasterText(strValue(act, QStringLiteral("master")));
    m_complaint->setPlainText(strValue(act, QStringLiteral("complaint")));
    m_diagnostics->setPlainText(strValue(act, QStringLiteral("diagnostics")));
    m_valuables->setPlainText(strValue(act, QStringLiteral("valuables")));
    m_contactMethod->setText(strValue(act, QStringLiteral("contact_method")));
    m_customerName->setText(strValue(act, QStringLiteral("customer_name")));
    m_receiverName->setText(strValue(act, QStringLiteral("receiver_name")));

    const QJsonObject conditions = objectValue(act, QStringLiteral("conditions"));
    for (auto it = m_conditionGroups.begin(); it != m_conditionGroups.end(); ++it) {
        const QJsonObject zone = objectValue(conditions, it.key());
        it.value()->button(statusId(strValue(zone, QStringLiteral("status"))))->setChecked(true);
        if (auto* note = m_conditionNotes.value(it.key())) {
            note->setPlainText(strValue(zone, QStringLiteral("note")));
        }
    }

    const QJsonObject equipment = objectValue(act, QStringLiteral("equipment"));
    for (auto it = m_equipment.begin(); it != m_equipment.end(); ++it) {
        it.value()->setChecked(boolValue(equipment, it.key()));
    }
    const QJsonObject permissions = objectValue(act, QStringLiteral("permissions"));
    for (auto it = m_permissions.begin(); it != m_permissions.end(); ++it) {
        it.value()->setChecked(boolValue(permissions, it.key(), true));
    }
}

void VehicleIntakePage::clearForm()
{
    if (m_intakeUid) m_intakeUid->clear();
    for (auto* edit : {m_actNumber, m_order, m_clientUid, m_vehicleUid, m_client, m_phone, m_vehicle, m_plate,
                       m_vin, m_year, m_engine, m_gearbox, m_mileage, m_fuel, m_signature,
                       m_contactMethod, m_customerName, m_receiverName}) {
        if (edit) edit->clear();
    }
    setMasterText(QString());
    setVehicleLookupStatus(QString());
    if (m_actDate) m_actDate->setDate(QDate::currentDate());
    if (m_acceptedAt) m_acceptedAt->setDateTime(QDateTime::currentDateTime());
    if (m_plannedReturnAt) m_plannedReturnAt->setDateTime(QDateTime::currentDateTime().addDays(1));
    for (auto* text : {m_complaint, m_diagnostics, m_valuables}) if (text) text->clear();
    for (auto* group : m_conditionGroups) if (group && group->button(0)) group->button(0)->setChecked(true);
    for (auto* note : m_conditionNotes) if (note) note->clear();
    for (auto* cb : m_equipment) if (cb) cb->setChecked(false);
    for (auto* cb : m_permissions) if (cb) cb->setChecked(true);
    setSignatureSectionExpanded(false);
}

void VehicleIntakePage::refresh()
{
    if (m_refreshInProgress) {
        m_refreshPending = true;
        return;
    }
    m_refreshInProgress = true;
    loadMasters();
    QVector<VehicleIntakeService::IntakeRecord> records;
    QString readError;
    if (!VehicleIntakeService::listIntakes(&records, &readError)) {
        m_refreshInProgress = false;
        m_refreshPending = false;
        QMessageBox::warning(this, tr("Приймання авто"), tr("Не вдалося оновити список: %1").arg(readError));
        return;
    }
    QString selectedAfterRefresh;
    if (m_table) {
        m_populatingTable = true;
        {
            IntakeTableGuard guard(m_table);
            m_table->setRowCount(0);
            for (const auto& record : records) {
                const QJsonDocument doc = QJsonDocument::fromJson(record.checklistJson.toUtf8());
                const QJsonObject act = doc.isObject() ? doc.object() : QJsonObject{};
                const int r = m_table->rowCount();
                m_table->insertRow(r);
                m_table->setItem(r, 0, new QTableWidgetItem(record.uid));
                m_table->setItem(r, 1, new QTableWidgetItem(act.value(QStringLiteral("act_number")).toString()));
                m_table->setItem(r, 2, new QTableWidgetItem(record.orderUid));
                m_table->setItem(r, 3, new QTableWidgetItem(act.value(QStringLiteral("client_name")).toString()));
                m_table->setItem(r, 4, new QTableWidgetItem(act.value(QStringLiteral("vehicle_label")).toString()));
                m_table->setItem(r, 5, new QTableWidgetItem(record.mileage));
                m_table->setItem(r, 6, new QTableWidgetItem(record.fuelLevel));
                m_table->setItem(r, 7, new QTableWidgetItem(record.pdfPath));
            }
            selectedAfterRefresh = selectedIntakeUid();
        }
        m_populatingTable = false;
    }
    setActsTableExpanded(m_actsTableToggle && m_actsTableToggle->isChecked());
    m_refreshInProgress = false;
    if (!selectedAfterRefresh.isEmpty() && selectedAfterRefresh != m_intakeUid->text()) loadSelectedIntake();
    if (m_refreshPending) {
        m_refreshPending = false;
        refresh();
    }
}

void VehicleIntakePage::loadSelectedIntake()
{
    if (m_populatingTable) return;
    const QString uid = selectedIntakeUid();
    if (uid.isEmpty()) return;
    VehicleIntakeService::IntakeRecord record;
    QString readError;
    if (!VehicleIntakeService::loadIntake(uid, &record, &readError)) {
        QMessageBox::warning(this, tr("Приймання авто"), readError);
        return;
    }
    clearForm();
    m_intakeUid->setText(record.uid);
    m_order->setText(record.orderUid);
    m_clientUid->setText(record.clientUid);
    m_vehicleUid->setText(record.vehicleUid);
    m_mileage->setText(record.mileage);
    m_fuel->setText(record.fuelLevel);
    m_signature->setText(record.signaturePath);
    applyChecklistJson(record.checklistJson);
}

void VehicleIntakePage::loadMasters()
{
    if (!m_master) return;
    const QString current = masterText();
    m_master->clear();
    const auto personnel = PersonnelSettingsService::loadSnapshot().masters;
    for (const auto& master : personnel) {
        if (master.name.trimmed().isEmpty()) continue;
        m_master->addItem(master.name.trimmed(), master.id.trimmed());
    }
    if (m_master->count() == 0) {
        m_master->lineEdit()->setPlaceholderText(QString::fromUtf8("Майстри не налаштовані"));
    } else {
        m_master->lineEdit()->setPlaceholderText(QString::fromUtf8("Оберіть майстра"));
    }
    setMasterText(current);
}

QString VehicleIntakePage::masterText() const
{
    return m_master ? m_master->currentText().trimmed() : QString();
}

void VehicleIntakePage::setMasterText(const QString& master)
{
    if (!m_master) return;
    const QString value = master.trimmed();
    if (value.isEmpty()) {
        m_master->setCurrentIndex(-1);
        m_master->setCurrentText(QString());
        return;
    }
    const int idx = m_master->findText(value, Qt::MatchFixedString);
    if (idx >= 0) {
        m_master->setCurrentIndex(idx);
    } else {
        m_master->setCurrentIndex(-1);
        m_master->setCurrentText(value);
    }
}

void VehicleIntakePage::setVehicleLookupStatus(const QString& text)
{
    if (m_plateLookupStatus) m_plateLookupStatus->setText(text.trimmed());
}

bool VehicleIntakePage::hasVehiclePrefillConflicts(const QVariantMap& data) const
{
    const QList<QPair<QLineEdit*, QString>> fields = {
        {m_client, QStringLiteral("client_name")},
        {m_phone, QStringLiteral("client_phone")},
        {m_vehicle, QStringLiteral("vehicle_label")},
        {m_plate, QStringLiteral("plate")},
        {m_vin, QStringLiteral("vin")},
        {m_year, QStringLiteral("year")},
        {m_engine, QStringLiteral("engine_type")},
        {m_gearbox, QStringLiteral("gearbox")},
        {m_mileage, QStringLiteral("mileage")}
    };
    for (const auto& field : fields) {
        const QString incoming = data.value(field.second).toString().trimmed();
        const QString current = field.first ? field.first->text().trimmed() : QString();
        if (!incoming.isEmpty() && !current.isEmpty() && current.compare(incoming, Qt::CaseInsensitive) != 0) {
            return true;
        }
    }
    const QString incomingMaster = data.value(QStringLiteral("master_name")).toString().trimmed();
    if (!incomingMaster.isEmpty() && !masterText().isEmpty() && masterText().compare(incomingMaster, Qt::CaseInsensitive) != 0) {
        return true;
    }
    return false;
}

void VehicleIntakePage::applyVehiclePrefill(const QVariantMap& data, const QString& sourceLabel, bool askBeforeOverwrite)
{
    if (!data.value(QStringLiteral("found"), true).toBool()) return;
    const bool conflicts = hasVehiclePrefillConflicts(data);
    bool overwrite = !askBeforeOverwrite || !conflicts;
    if (askBeforeOverwrite && conflicts) {
        overwrite = QMessageBox::question(
            this,
            QString::fromUtf8("Приймання авто"),
            QString::fromUtf8("Знайдено дані авто. Заповнити дані авто знайденими значеннями?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes) == QMessageBox::Yes;
    }

    auto putLine = [overwrite](QLineEdit* edit, const QString& value) {
        if (!edit || value.trimmed().isEmpty()) return;
        if (overwrite || edit->text().trimmed().isEmpty()) edit->setText(value.trimmed());
    };

    putLine(m_order, data.value(QStringLiteral("order_uid")).toString());
    putLine(m_clientUid, data.value(QStringLiteral("client_uid")).toString());
    putLine(m_vehicleUid, data.value(QStringLiteral("vehicle_uid")).toString());
    putLine(m_client, data.value(QStringLiteral("client_name")).toString());
    putLine(m_phone, data.value(QStringLiteral("client_phone")).toString());
    putLine(m_vehicle, VehicleNameUtils::normalizeVehicleFreeText(data.value(QStringLiteral("vehicle_label")).toString()));
    putLine(m_plate, PlateUtils::formatPlateDisplay(PlateUtils::normalizePlate(data.value(QStringLiteral("plate")).toString())));
    putLine(m_vin, data.value(QStringLiteral("vin")).toString().trimmed().toUpper());
    putLine(m_year, data.value(QStringLiteral("year")).toString());
    QString engine = data.value(QStringLiteral("engine_type")).toString().trimmed();
    if (engine.isEmpty()) engine = data.value(QStringLiteral("fuel_type")).toString().trimmed();
    putLine(m_engine, engine);
    putLine(m_gearbox, data.value(QStringLiteral("gearbox")).toString());
    putLine(m_mileage, data.value(QStringLiteral("mileage")).toString());

    const QString master = data.value(QStringLiteral("master_name")).toString().trimmed();
    if (!master.isEmpty() && (overwrite || masterText().isEmpty())) setMasterText(master);
    if (m_customerName->text().trimmed().isEmpty()) m_customerName->setText(m_client->text().trimmed());
    if (m_receiverName->text().trimmed().isEmpty()) m_receiverName->setText(masterText());

    setVehicleLookupStatus(sourceLabel.trimmed().isEmpty()
        ? QString::fromUtf8("Дані авто знайдено")
        : QString::fromUtf8("Дані авто знайдено: %1").arg(sourceLabel));
}

void VehicleIntakePage::lookupVehicleByPlate()
{
    if (!m_plate || m_vehicleLookupInProgress) return;
    const QString plateNorm = PlateUtils::normalizePlate(m_plate->text()).trimmed().toUpper();
    if (plateNorm.isEmpty()) {
        setVehicleLookupStatus(QString());
        return;
    }
    if (plateNorm == m_lastLookupPlate && (!m_vehicle->text().trimmed().isEmpty() || !m_vin->text().trimmed().isEmpty())) {
        return;
    }
    m_lastLookupPlate = plateNorm;

    m_vehicleLookupInProgress = true;
    setVehicleLookupStatus(QString::fromUtf8("Шукаю авто в історії..."));
    const quint64 generation = ++m_lookupGeneration;
    QPointer<VehicleIntakePage> self(this);
    auto* thread = QThread::create([self, plateNorm, generation]() {
        VehicleIntakeService::VehiclePrefill prefill;
        QString err;
        const bool found = VehicleIntakeService::lookupVehicleByPlate(plateNorm, &prefill, &err);
        const QVariantMap data = found ? VehicleIntakeService::vehiclePrefillToMap(prefill) : QVariantMap();
        QMetaObject::invokeMethod(qApp, [self, plateNorm, generation, found, data]() {
            if (!self || generation != self->m_lookupGeneration || plateNorm != self->m_lastLookupPlate) return;
            if (found) {
                self->m_vehicleLookupInProgress = false;
                self->applyVehiclePrefill(data, QString::fromUtf8("історія"), true);
                return;
            }

            if (!OpenDataBotClient::isConfigured()) {
                self->m_vehicleLookupInProgress = false;
                self->setVehicleLookupStatus(QString::fromUtf8("Не знайдено, заповніть вручну"));
                return;
            }

            self->setVehicleLookupStatus(QString::fromUtf8("Локально не знайдено, перевіряю OpenDataBot..."));
            OpenDataBotClient::lookupByPlate(self, plateNorm, [self, plateNorm, generation](const OpenDataBotClient::VehicleLookupResult& result) {
                if (!self) return;
                self->m_vehicleLookupInProgress = false;
                if (generation != self->m_lookupGeneration || plateNorm != self->m_lastLookupPlate) return;
                if (!result.success) {
                    self->setVehicleLookupStatus(QString::fromUtf8("Не знайдено, заповніть вручну"));
                    return;
                }
                QVariantMap data;
                data.insert(QStringLiteral("found"), true);
                data.insert(QStringLiteral("source"), QStringLiteral("opendatabot"));
                data.insert(QStringLiteral("plate"), result.plate.isEmpty() ? plateNorm : result.plate);
                data.insert(QStringLiteral("vin"), result.vin);
                data.insert(QStringLiteral("vehicle_label"), result.carDisplay);
                data.insert(QStringLiteral("year"), result.year > 0 ? QString::number(result.year) : QString());
                data.insert(QStringLiteral("fuel_type"), result.fuelType);
                self->applyVehiclePrefill(data, result.demo ? QString::fromUtf8("OpenDataBot demo") : QStringLiteral("OpenDataBot"), true);
            });
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void VehicleIntakePage::autofillFromOrder()
{
    const QString key = m_order->text().trimmed();
    if (key.isEmpty()) return;
    VehicleIntakeService::VehiclePrefill prefill;
    QString err;
    if (!VehicleIntakeService::lookupOrderForIntake(key, &prefill, &err)) {
        QMessageBox::information(this, QString::fromUtf8("Приймання авто"), QString::fromUtf8("Наряд не знайдено."));
        return;
    }
    applyVehiclePrefill(VehicleIntakeService::vehiclePrefillToMap(prefill), QString::fromUtf8("наряду"), false);
    if (!prefill.orderUid.trimmed().isEmpty()) m_order->setText(prefill.orderUid.trimmed());
    if (m_customerName->text().trimmed().isEmpty()) m_customerName->setText(m_client->text());
    if (m_receiverName->text().trimmed().isEmpty()) m_receiverName->setText(masterText());
}

bool VehicleIntakePage::saveDraft()
{
    QVariantMap p = collectPayload();
    QString uid, err;
    if (!VehicleIntakeService::saveIntake(p, &uid, &err)) {
        QMessageBox::warning(this, QString::fromUtf8("Помилка"), err);
        return false;
    }
    m_intakeUid->setText(uid);
    refresh();
    emit requestRefreshAll();
    return true;
}

void VehicleIntakePage::addPhotoForSelected()
{
    QString intakeUid = m_intakeUid ? m_intakeUid->text().trimmed() : QString();
    if (intakeUid.isEmpty()) intakeUid = selectedIntakeUid();
    if (intakeUid.isEmpty()) {
        QMessageBox::information(this, QString::fromUtf8("Приймання авто"), QString::fromUtf8("Спочатку збережіть акт."));
        return;
    }
    const QString path = QFileDialog::getOpenFileName(this, QString::fromUtf8("Додати фото приймання"), QString(), QString::fromUtf8("Зображення (*.png *.jpg *.jpeg *.webp *.bmp)"));
    if (path.isEmpty()) return;
    QString err;
    if (!VehicleIntakeService::addPhoto(intakeUid, m_order->text().trimmed(), path, QString::fromUtf8("Фото приймання"), nullptr, &err)) {
        QMessageBox::warning(this, QString::fromUtf8("Помилка"), err);
        return;
    }
    emit requestRefreshAll();
}

void VehicleIntakePage::pickSignatureForSelected()
{
    QString intakeUid = m_intakeUid ? m_intakeUid->text().trimmed() : QString();
    if (intakeUid.isEmpty()) intakeUid = selectedIntakeUid();
    if (intakeUid.isEmpty()) {
        QMessageBox::information(this, QString::fromUtf8("Приймання авто"), QString::fromUtf8("Спочатку збережіть акт."));
        return;
    }
    const QString path = QFileDialog::getOpenFileName(this, QString::fromUtf8("Додати підпис клієнта"), QString(), QString::fromUtf8("Зображення (*.png *.jpg *.jpeg *.webp *.bmp)"));
    if (path.isEmpty()) return;
    QString err;
    if (!VehicleIntakeService::updateSignaturePath(intakeUid, path, &err)) {
        QMessageBox::warning(this, QString::fromUtf8("Помилка"), err);
        return;
    }
    m_signature->setText(path);
    refresh();
    emit requestRefreshAll();
}

void VehicleIntakePage::generatePdfForSelected()
{
    if (m_pdfGenerationInProgress) return;
    QVariantMap p = collectPayload();
    QString uid, err;
    if (!VehicleIntakeService::saveIntake(p, &uid, &err)) {
        QMessageBox::warning(this, QString::fromUtf8("Помилка"), err);
        return;
    }
    m_intakeUid->setText(uid);

    QString defaultName = QString::fromUtf8("Акт приймання авто");
    if (!m_actNumber->text().trimmed().isEmpty()) defaultName += QStringLiteral(" ") + m_actNumber->text().trimmed();
    defaultName += QStringLiteral(".pdf");
    const QString path = QFileDialog::getSaveFileName(this, QString::fromUtf8("Зберегти акт"), defaultName, QStringLiteral("PDF (*.pdf)"));
    if (path.isEmpty()) return;
    m_pdfGenerationInProgress = true;
    QPointer<VehicleIntakePage> self(this);
    auto* thread = QThread::create([self, uid, path]() {
        QString error;
        const bool ok = VehicleIntakeService::generatePdf(uid, path, &error);
        QMetaObject::invokeMethod(qApp, [self, path, ok, error]() {
            if (!self) return;
            self->m_pdfGenerationInProgress = false;
            if (!ok) {
                QMessageBox::warning(self, QString::fromUtf8("Помилка"), error);
                return;
            }
            self->refresh();
            QFileInfo info(path);
            if (!info.exists() || info.size() <= 0) {
                QMessageBox::warning(self, QString::fromUtf8("Помилка"), QString::fromUtf8("PDF не був коректно створений."));
                return;
            }
            if (!QDesktopServices::openUrl(QUrl::fromLocalFile(info.absoluteFilePath()))) {
                QMessageBox::information(self, QString::fromUtf8("PDF створено"), QDir::toNativeSeparators(info.absoluteFilePath()));
            }
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void VehicleIntakePage::printBlankAct()
{
    if (m_pdfGenerationInProgress) return;

    QString pathError;
    const QString path = VehicleIntakeService::blankActPdfPath(&pathError);
    if (path.isEmpty()) {
        QMessageBox::warning(this, QString::fromUtf8("Помилка"), pathError);
        return;
    }

    m_pdfGenerationInProgress = true;
    QPointer<VehicleIntakePage> self(this);
    auto* thread = QThread::create([self, path]() {
        QString error;
        const bool ok = VehicleIntakeService::createBlankIntakeActPdf(path, &error);
        QMetaObject::invokeMethod(qApp, [self, path, ok, error]() {
            if (!self) return;
            self->m_pdfGenerationInProgress = false;
            if (!ok) {
                QMessageBox::warning(self, QString::fromUtf8("Помилка"), error);
                return;
            }
            QFileInfo info(path);
            if (!info.exists() || info.size() <= 0) {
                QMessageBox::warning(self, QString::fromUtf8("Помилка"), QString::fromUtf8("Пустий PDF не був коректно створений."));
                return;
            }
            QDesktopServices::openUrl(QUrl::fromLocalFile(info.absoluteFilePath()));
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}
