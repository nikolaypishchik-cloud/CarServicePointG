#include <limits>
// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

#include "ui/common/dialogs/OrderDialogModulePrelude.h"

void OrderDialog::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(2);
    root->setSizeConstraint(QLayout::SetNoConstraint);

    auto* formTab = new QWidget(this);
    formTab->setObjectName(QStringLiteral("orderFormTab"));
    formTab->setAttribute(Qt::WA_StyledBackground, true);
    auto* formRoot = new QVBoxLayout(formTab);
    formRoot->setContentsMargins(0,0,0,0);
    formRoot->setSpacing(0);

    auto* formShell = new QWidget(this);
    formShell->setObjectName(QStringLiteral("orderFormShell"));
    formShell->setAttribute(Qt::WA_StyledBackground, true);
    auto* shellLay = new QHBoxLayout(formShell);
    shellLay->setContentsMargins(0, 0, 0, 0);
    shellLay->setSpacing(0);

    auto* formPane = new QWidget(formShell);
    formPane->setObjectName(QStringLiteral("orderFormSurface"));
    formPane->setAttribute(Qt::WA_StyledBackground, true);
    formPane->setMaximumWidth(QWIDGETSIZE_MAX);
    formPane->setMinimumWidth(AdaptiveUi::px(formPane, 680));
    auto* grid = new QGridLayout(formPane);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(4);
    grid->setColumnMinimumWidth(0, 260);
    grid->setColumnMinimumWidth(1, 590);
    grid->setColumnMinimumWidth(2, 220);
    grid->setColumnStretch(0, 0);
    grid->setColumnStretch(1, 7);
    grid->setColumnStretch(2, 3);

    int row = 0;
    auto addFullRow = [&](const QString& label, QWidget* editor) {
        auto* lbl = new QLabel(label, formPane);
        lbl->setStyleSheet(labelStyle());
        lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        lbl->setMinimumWidth(AdaptiveUi::px(lbl, 180));
        lbl->setContentsMargins(0, 0, 10, 0);
        editor->setStyleSheet(editor->styleSheet() + editorPlaceholderStyle());
        grid->addWidget(lbl, row, 0, Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(editor, row, 1, 1, 2);
        ++row;
    };

    m_num = new QSpinBox(this);
    m_num->setObjectName(QStringLiteral("orderVisibleNumber"));
    m_num->setRange(0, std::numeric_limits<int>::max());
    m_num->setSpecialValueText(QString::fromUtf8("—"));
    m_num->setToolTip(QString::fromUtf8("Наступний номер наряду підставляється автоматично, але його можна змінити вручну."));
    m_num->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_num->setKeyboardTracking(false);
    m_num->setAccelerated(false);
    m_num->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_num->setMinimumHeight(40);
    m_num->setStyleSheet(editorPlaceholderStyle() + flatOrderFieldStyle() + QString::fromUtf8("QSpinBox { padding-right: 14px; }"));
    if (auto* le = m_num->findChild<QLineEdit*>()) {
        le->setPlaceholderText(QString::fromUtf8("Автоматично, можна змінити"));
        le->setClearButtonEnabled(false);
        le->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }

    m_openedDate = new QDateEdit(QDate::currentDate(), this);
    m_openedDate->setCalendarPopup(true);
    m_openedDate->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_openedDate->setDisplayFormat(QString::fromUtf8("dd.MM.yy"));
    m_openedDate->setKeyboardTracking(false);
    m_openedDate->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_openedDate->setMinimumHeight(40);
    m_openedDate->setMinimumWidth(AdaptiveUi::px(m_openedDate, 180));
    m_openedDate->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_openedDate->setStyleSheet(editorPlaceholderStyle() + flatOrderFieldStyle() + QString::fromUtf8("QDateEdit { padding-left: 12px; padding-right: 26px; }"));
    if (auto* le = m_openedDate->findChild<QLineEdit*>()) {
        le->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        le->setCursorPosition(0);
    }

    m_openedTime = new QTimeEdit(QTime::currentTime(), this);
    m_openedTime->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_openedTime->setDisplayFormat(QString::fromUtf8("HH:mm"));
    m_openedTime->setKeyboardTracking(false);
    m_openedTime->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_openedTime->setMinimumHeight(40);
    m_openedTime->setMinimumWidth(AdaptiveUi::px(m_openedTime, 120));
    m_openedTime->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_openedTime->setStyleSheet(editorPlaceholderStyle() + flatOrderFieldStyle() + QString::fromUtf8("QTimeEdit { padding-left: 12px; padding-right: 16px; }"));
    if (auto* le = m_openedTime->findChild<QLineEdit*>()) {
        le->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        le->setCursorPosition(0);
    }

    m_status = new QComboBox(this);
    m_status->addItem(QString::fromUtf8("Відкритий"), QStringLiteral("open"));
    m_status->addItem(QString::fromUtf8("В роботі"), QStringLiteral("in_progress"));
    m_status->addItem(QString::fromUtf8("Закритий"), QStringLiteral("closed"));
    m_status->setCurrentIndex(0);
    m_status->setStyleSheet(editorPlaceholderStyle() + flatOrderFieldStyle());

    m_plate = new QLineEdit(this);
    m_plate->setProperty("inputCorrectionRole", "plate");
    m_plate->setPlaceholderText(QString::fromUtf8("Напр: AA 1234 BC"));

    m_vin = new QLineEdit(this);
    m_vin->setPlaceholderText(QString::fromUtf8("VIN (обов'язково)"));

    m_car = new QLineEdit(this);
    m_car->setProperty("inputCorrectionRole", "vehicle");
    m_car->setPlaceholderText(QString::fromUtf8("Напр: Volvo XC60"));

    m_color = new QLineEdit(this);
    m_color->setPlaceholderText(QString::fromUtf8("Напр: Чорний"));

    m_year = new QSpinBox(this);
    m_year->setRange(1950, 2100);
    m_year->setSpecialValueText(QString());
    m_year->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_year->setStyleSheet(editorPlaceholderStyle() + flatOrderFieldStyle());
    m_year->setValue(m_year->minimum());
    m_year->clear();

    m_odometer = new QSpinBox(this);
    m_odometer->setRange(0, 2000000);
    m_odometer->setSpecialValueText(QString());
    m_odometer->setSingleStep(1000);
    m_odometer->setGroupSeparatorShown(true);
    m_odometer->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_odometer->setStyleSheet(editorPlaceholderStyle() + flatOrderFieldStyle());
    m_odometer->setValue(m_odometer->minimum());
    m_odometer->clear();

    m_engineCode = new QLineEdit(this);
    m_engineCode->setProperty("inputCorrectionRole", "protected");
    m_engineCode->setPlaceholderText(QString::fromUtf8("Маркування двигуна (напр: D4, B5204T)"));

    m_engineVolume = new QLineEdit(this);
    m_engineVolume->setProperty("inputCorrectionRole", "protected");
    m_engineVolume->setPlaceholderText(QString::fromUtf8("Об'єм (напр: 2.0)"));

    m_client = new QLineEdit(this);
    m_client->setPlaceholderText(QString::fromUtf8("ПІБ клієнта"));

    m_phone = new QLineEdit(this);
    m_phone->setPlaceholderText(QString::fromUtf8("Напр: +380501234567"));

    m_master = new QComboBox(this);
    m_master->setPlaceholderText(QString::fromUtf8("Оберіть майстра"));
    m_master->setStyleSheet(editorPlaceholderStyle() + flatOrderFieldStyle());

    m_discount = new QDoubleSpinBox(this);
    m_discount->setRange(0, 1000000);
    m_discount->setDecimals(2);
    m_discount->setSuffix(QString::fromUtf8(" грн"));
    m_discount->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_discount->setStyleSheet(editorPlaceholderStyle() + flatOrderFieldStyle()
                              + QString::fromUtf8("QDoubleSpinBox { padding-right: 48px; }"
                                                  "QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width:0px; border:none; }"));
    m_discount->setMinimumHeight(40);
    m_discount->installEventFilter(this);
    m_discount->clear();

    auto capitalizeFirst = [](QString s) -> QString {
        s = s.trimmed();
        if (s.isEmpty()) return s;
        s[0] = s[0].toUpper();
        return s;
    };
    connect(m_plate, &QLineEdit::editingFinished, this, [this]() {
        if (!prefillFromCompletedVehicleByPlateIfAvailable()) {
            lookupVehicleByPlateIfConfigured();
        }
    });
    connect(m_vin, &QLineEdit::editingFinished, this, [this]() {
        m_vin->setText(m_vin->text().trimmed().toUpper());
    });
    connect(m_car, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_lastCarEditedText = text;
    });
    connect(m_color, &QLineEdit::editingFinished, this, [this, capitalizeFirst]() {
        m_color->setText(capitalizeFirst(m_color->text()));
    });
    connect(m_client, &QLineEdit::editingFinished, this, [this, capitalizeFirst]() {
        m_client->setText(capitalizeSentence(m_client->text()));
    });

    addFullRow(QString::fromUtf8("№ наряду:"), m_num);

    auto* openedLabel = new QLabel(QString::fromUtf8("Дата / час відкриття:"), formPane);
    openedLabel->setStyleSheet(labelStyle());
    openedLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    openedLabel->setMinimumWidth(AdaptiveUi::px(openedLabel, 180));
    openedLabel->setContentsMargins(0, 0, 10, 0);
    auto* openedWrap = new QWidget(formPane);
    openedWrap->setObjectName(QStringLiteral("orderOpenedWrap"));
    openedWrap->setAttribute(Qt::WA_StyledBackground, true);
    openedWrap->setStyleSheet(QStringLiteral("background:transparent; border:none;"));
    auto* openedLay = new QHBoxLayout(openedWrap);
    openedLay->setContentsMargins(0, 0, 0, 0);
    openedLay->setSpacing(4);
    openedWrap->setMinimumHeight(AdaptiveUi::px(openedWrap, 40));
    openedLay->addWidget(m_openedDate, 0, Qt::AlignLeft | Qt::AlignVCenter);
    openedLay->addWidget(m_openedTime, 0, Qt::AlignLeft | Qt::AlignVCenter);
    openedLay->addStretch(1);
    grid->addWidget(openedLabel, row, 0, Qt::AlignRight | Qt::AlignVCenter);
    grid->addWidget(openedWrap, row, 1, 1, 2);
    ++row;

    addFullRow(QString::fromUtf8("Статус:"), m_status);
    addFullRow(QString::fromUtf8("Номер авто:"), m_plate);
    addFullRow(QString::fromUtf8("VIN:"), m_vin);
    addFullRow(QString::fromUtf8("Авто (марка/модель):"), m_car);
    addFullRow(QString::fromUtf8("Колір:"), m_color);
    addFullRow(QString::fromUtf8("Рік випуску:"), m_year);
    addFullRow(QString::fromUtf8("Одометр (км):"), m_odometer);

    auto* engineLabel = new QLabel(QString::fromUtf8("Двигун (маркування / об'єм):"), formPane);
    engineLabel->setStyleSheet(labelStyle());
    engineLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    engineLabel->setMinimumWidth(AdaptiveUi::px(engineLabel, 180));
    engineLabel->setContentsMargins(0, 0, 10, 0);
    m_engineCode->setStyleSheet(editorPlaceholderStyle());
    m_engineVolume->setStyleSheet(editorPlaceholderStyle() + QString::fromUtf8("QLineEdit { padding-left: 10px; padding-right: 10px; }"));
    m_engineCode->setMinimumHeight(40);
    m_engineVolume->setMinimumHeight(40);
    auto* engineWrap = new QWidget(formPane);
    engineWrap->setObjectName(QStringLiteral("orderEngineWrap"));
    engineWrap->setAttribute(Qt::WA_StyledBackground, true);
    engineWrap->setStyleSheet(QStringLiteral("background:transparent; border:none;"));
    auto* engineLay = new QHBoxLayout(engineWrap);
    engineLay->setContentsMargins(0, 0, 0, 0);
    engineLay->setSpacing(4);
    m_engineVolume->setMinimumWidth(AdaptiveUi::px(m_engineVolume, 180));
    engineLay->addWidget(m_engineCode, 1);
    engineLay->addWidget(m_engineVolume, 0);
    grid->addWidget(engineLabel, row, 0, Qt::AlignRight | Qt::AlignVCenter);
    grid->addWidget(engineWrap, row, 1, 1, 2);
    ++row;

    auto* fuelLabel = new QLabel(QString::fromUtf8("Пальне:"), formPane);
    fuelLabel->setStyleSheet(labelStyle());
    fuelLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    fuelLabel->setMinimumWidth(AdaptiveUi::px(fuelLabel, 180));
    fuelLabel->setContentsMargins(0, 0, 10, 0);
    auto* fuelWrap = new QWidget(formPane);
    fuelWrap->setObjectName(QStringLiteral("orderFuelWrap"));
    fuelWrap->setAttribute(Qt::WA_StyledBackground, true);
    fuelWrap->setStyleSheet(QStringLiteral("background:transparent; border:none;"));
    auto* fuelLay = new QHBoxLayout(fuelWrap);
    fuelLay->setContentsMargins(0, 0, 0, 0);
    fuelLay->setSpacing(8);
    m_fuelDiesel = new QCheckBox(QString::fromUtf8("Дизель"), fuelWrap);
    m_fuelPetrol = new QCheckBox(QString::fromUtf8("Бензин (газ)"), fuelWrap);
    m_fuelHybrid = new QCheckBox(QString::fromUtf8("Гібрид"), fuelWrap);
    m_fuelElectric = new QCheckBox(QString::fromUtf8("Електро"), fuelWrap);
    for (auto* cb : {m_fuelDiesel, m_fuelPetrol, m_fuelHybrid, m_fuelElectric}) {
        cb->setStyleSheet(QString::fromUtf8("QCheckBox { background:transparent; color:%1; font-size:14px; font-weight:600; }").arg(ThemeManager::currentTheme() == QStringLiteral("light") ? QStringLiteral("#111111") : QStringLiteral("#f3f6fa")));
        fuelLay->addWidget(cb);
        connect(cb, &QCheckBox::clicked, this, [this, cb](bool checked) {
            if (!checked) return;
            for (auto* other : {m_fuelDiesel, m_fuelPetrol, m_fuelHybrid, m_fuelElectric}) {
                if (other && other != cb) other->setChecked(false);
            }
        });
    }
    fuelLay->addStretch(1);
    grid->addWidget(fuelLabel, row, 0, Qt::AlignRight | Qt::AlignVCenter);
    grid->addWidget(fuelWrap, row, 1, 1, 2);
    ++row;

    addFullRow(QString::fromUtf8("Клієнт:"), m_client);
    addFullRow(QString::fromUtf8("Телефон:"), m_phone);
    addFullRow(QString::fromUtf8("Майстер:"), m_master);
    m_discountMode = new QComboBox(this);
    m_discountMode->setObjectName(QStringLiteral("orderDiscountMode"));
    m_discountMode->addItem(QString::fromUtf8("грн"), QStringLiteral("uah"));
    m_discountMode->addItem(QStringLiteral("%"), QStringLiteral("percent"));
    m_discountMode->addItem(QString::fromUtf8("Історична (тип не задано)"), QStringLiteral("legacy"));
    auto* discountRow = new QWidget(this);
    auto* discountLayout = new QHBoxLayout(discountRow);
    discountLayout->setContentsMargins(0,0,0,0);
    discountLayout->addWidget(m_discount, 1);
    discountLayout->addWidget(m_discountMode);
    connect(m_discountMode, &QComboBox::currentIndexChanged, this, [this] {
        const bool percent = m_discountMode->currentData().toString() == QStringLiteral("percent");
        m_discount->setSuffix(percent ? QStringLiteral(" %") : QString::fromUtf8(" грн"));
        refreshTotals();
    });
    addFullRow(QString::fromUtf8("Знижка:"), discountRow);
    grid->setRowStretch(row, 0);

    shellLay->addWidget(formPane, 1, Qt::AlignTop);

    formRoot->addWidget(formShell);

    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    m_tabs->setUsesScrollButtons(false);
    m_tabs->setElideMode(Qt::ElideNone);
    if (auto* stacked = m_tabs->findChild<QWidget*>(QStringLiteral("qt_tabwidget_stackedwidget"))) {
        stacked->setAttribute(Qt::WA_StyledBackground, true);
        stacked->setStyleSheet(QStringLiteral("background:transparent; border:none;"));
    }
    m_tabs->addTab(formTab, m_isNewOrder ? QString::fromUtf8("Дані нового наряду") : QString::fromUtf8("Дані наряду"));

    if (!m_isNewOrder) {
    auto* workTab = new QWidget(this);
    auto* workLay = new QVBoxLayout(workTab);
    workLay->setContentsMargins(0,0,0,0);
    workLay->setSpacing(0);
    m_tabs->addTab(workTab, QString::fromUtf8("Роботи та запчастини"));
    }
    root->addWidget(m_tabs, 1);

    auto* buttonsRow = new QHBoxLayout();
    buttonsRow->setContentsMargins(0, 4, 0, 8);
    buttonsRow->setSpacing(8);
    buttonsRow->addStretch(1);
    m_btnOk = new QPushButton(QStringLiteral("OK"), this);
    m_btnCancel = new QPushButton(QString::fromUtf8("Скасувати"), this);
    m_btnOk->setMinimumWidth(AdaptiveUi::px(m_btnOk, 118));
    m_btnCancel->setMinimumWidth(AdaptiveUi::px(m_btnCancel, 118));
    m_btnOk->setMinimumHeight(AdaptiveUi::px(m_btnOk, 34));
    m_btnCancel->setMinimumHeight(AdaptiveUi::px(m_btnCancel, 34));
    m_btnOk->setStyleSheet(volumetricButtonStyle());
    m_btnCancel->setStyleSheet(volumetricButtonStyle());
    buttonsRow->addWidget(m_btnOk);
    buttonsRow->addWidget(m_btnCancel);
    root->addLayout(buttonsRow);

    connect(m_btnOk, &QPushButton::clicked, this, &OrderDialog::accept);
    connect(m_btnCancel, &QPushButton::clicked, this, &OrderDialog::reject);

    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (m_btnOk) m_btnOk->setVisible(index == 0);
        if (m_btnCancel) m_btnCancel->setVisible(index == 0);
        if (!m_tabs || index != 1) return;
        if (m_acceptInProgress || m_draftCreationInProgress) return;
        QElapsedTimer timer;
        timer.start();
        QString initError;
        if (ensureDraftOrder() && ensureWorkPartsPage(&initError) && m_workPartsPage) {
            m_workPartsPage->raise();
            refreshTotals();
            if (RuntimeLoggingControl::deepDebugEnabled()) qInfo() << "OrderDialog lazy workparts init elapsed_ms" << timer.elapsed() << "order" << m_orderNumber;
        } else if (m_tabs) {
            if (!initError.trimmed().isEmpty()) {
                QMessageBox::critical(this,
                                      QString::fromUtf8("Помилка"),
                                      QString::fromUtf8("Не вдалося відкрити роботи та запчастини.\n%1").arg(initError));
            }
            m_tabs->setCurrentIndex(0);
        }
    });

    refreshTotals();
    if (m_btnOk) m_btnOk->setVisible(m_tabs->currentIndex() == 0);
    if (m_btnCancel) m_btnCancel->setVisible(m_tabs->currentIndex() == 0);

    armUserEditTracking(m_num);
    armUserEditTracking(m_openedDate);
    armUserEditTracking(m_openedTime);
    armUserEditTracking(m_status);
    armUserEditTracking(m_plate);
    armUserEditTracking(m_vin);
    armUserEditTracking(m_car);
    armUserEditTracking(m_color);
    armUserEditTracking(m_year);
    armUserEditTracking(m_odometer);
    armUserEditTracking(m_engineCode);
    armUserEditTracking(m_engineVolume);
    armUserEditTracking(m_fuelDiesel);
    armUserEditTracking(m_fuelPetrol);
    armUserEditTracking(m_fuelHybrid);
    armUserEditTracking(m_fuelElectric);
    armUserEditTracking(m_client);
    armUserEditTracking(m_phone);
    armUserEditTracking(m_master);
    armUserEditTracking(m_discount);
    armUserEditTracking(m_discountMode);
}


