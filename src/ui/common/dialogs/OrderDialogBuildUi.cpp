#include <limits>
#include <QTabBar>
// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

#include "ui/common/dialogs/OrderDialogModulePrelude.h"

namespace {
QGridLayout* newOrderSection(QBoxLayout* column, const QString& title)
{
    auto* panel = new QFrame;
    panel->setObjectName("newOrderSection");
    panel->setStyleSheet("QFrame#newOrderSection {background:#1b2934;border:1px solid #507383;border-radius:10px;}");
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(1,1,1,12);
    auto* heading = new QLabel(QStringLiteral("<span style='color:#70d9ed;font-size:22px'><i>/</i></span>&nbsp; ") + title, panel);
    heading->setStyleSheet("background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #263945,stop:1 #0c1821);"
                           "color:white;font:600 18px 'Segoe UI';padding:7px 12px;border-bottom:1px solid #4f7281;"
                           "border-top-left-radius:9px;border-top-right-radius:9px;");
    layout->addWidget(heading);
    auto* rows = new QGridLayout;
    rows->setContentsMargins(14,6,14,0);
    rows->setHorizontalSpacing(10);
    rows->setVerticalSpacing(7);
    rows->setColumnStretch(1,1);
    layout->addLayout(rows,1);
    column->addWidget(panel);
    return rows;
}
}

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
    QGridLayout* grid = nullptr;
    QGridLayout* details = nullptr;
    QGridLayout* client = nullptr;
    QGridLayout* vehicle = nullptr;
    if (m_isNewOrder) {
        auto* columns = new QHBoxLayout(formPane);
        columns->setContentsMargins(18,18,18,16);
        columns->setSpacing(16);
        auto* left = new QVBoxLayout;
        left->setSpacing(10);
        auto* right = new QVBoxLayout;
        columns->addLayout(left, 39);
        columns->addLayout(right, 61);
        details = newOrderSection(left, tr("Дані нового наряду"));
        client = newOrderSection(left, tr("Клієнт і сервіс"));
        vehicle = newOrderSection(right, tr("Авто"));
        left->setStretch(0, 3);
        left->setStretch(1, 4);
        grid = details;
    } else {
        grid = new QGridLayout(formPane);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(6);
        grid->setVerticalSpacing(4);
        grid->setColumnMinimumWidth(0, 260);
        grid->setColumnMinimumWidth(1, 590);
        grid->setColumnMinimumWidth(2, 220);
        grid->setColumnStretch(0, 0);
        grid->setColumnStretch(1, 7);
        grid->setColumnStretch(2, 3);
    }
    int row = 0;
    auto placeRow = [&](QLabel* label, QWidget* editor) {
        if (m_isNewOrder) {
            label->setMinimumWidth(0);
            label->setContentsMargins(0,0,0,0);
            label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            label->setStyleSheet("color:#f1f4f7;background:transparent;border:0;font:600 14px 'Segoe UI';");
            grid->setRowStretch(row,1);
            grid->addWidget(label,row,0,Qt::AlignVCenter);
            grid->addWidget(editor,row,1,Qt::AlignVCenter);
        } else {
            grid->addWidget(label,row,0,Qt::AlignRight | Qt::AlignVCenter);
            grid->addWidget(editor,row,1,1,2);
        }
        ++row;
    };
    auto addFullRow = [&](const QString& label, QWidget* editor) {
        auto* lbl = new QLabel(label, formPane);
        lbl->setStyleSheet(labelStyle());
        lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        lbl->setMinimumWidth(AdaptiveUi::px(lbl, 180));
        lbl->setContentsMargins(0, 0, 10, 0);
        editor->setStyleSheet(editor->styleSheet() + editorPlaceholderStyle());
        placeRow(lbl, editor);
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
    placeRow(openedLabel, openedWrap);

    addFullRow(QString::fromUtf8("Статус:"), m_status);
    if (m_isNewOrder) { grid = vehicle; row = 0; }
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
    placeRow(engineLabel, engineWrap);

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
    placeRow(fuelLabel, fuelWrap);

    if (m_isNewOrder) { grid = client; row = 0; }
    addFullRow(QString::fromUtf8("Клієнт:"), m_client);
    addFullRow(QString::fromUtf8("Телефон:"), m_phone);
    addFullRow(QString::fromUtf8("Майстер:"), m_master);
    if (m_isNewOrder) {
        m_newOrderPost = new QComboBox(formPane);
        m_newOrderPost->setObjectName("newOrderPost");
        m_newOrderPost->addItem(tr("Оберіть пост"), 0);
        for (int post = 1; post <= 3; ++post)
            m_newOrderPost->addItem(tr("Пост %1").arg(post), post);
        addFullRow(tr("Пост:"), m_newOrderPost);
    }
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

    if (m_isNewOrder) {
        engineLabel->setText(tr("Двигун\n(маркування / об’єм):"));
        m_engineCode->setToolTip(m_engineCode->placeholderText());
        m_engineVolume->setToolTip(m_engineVolume->placeholderText());
        m_discountMode->setFixedWidth(82);
        fuelLay->setSpacing(5);
        delete fuelLay->takeAt(fuelLay->count()-1);
        m_openedDate->setMinimumWidth(90);
        m_openedTime->setMinimumWidth(70);
        delete openedLay->takeAt(2);
        openedLay->setStretch(0,3);
        openedLay->setStretch(1,2);
        openedLay->setAlignment(m_openedDate,Qt::Alignment());
        openedLay->setAlignment(m_openedTime,Qt::Alignment());
        m_engineVolume->setMinimumWidth(85);
        engineLay->setStretch(0,2);
        engineLay->setStretch(1,1);
        const QString field = QStringLiteral("QLineEdit,QComboBox,QAbstractSpinBox {"
            "background:#fbfcfd;color:#17232c;border:1px solid #bccad1;border-radius:6px;"
            "padding:4px 8px;font:600 14px 'Segoe UI';selection-background-color:#226578;}"
            "QLineEdit:focus,QComboBox:focus,QAbstractSpinBox:focus {border:1px solid #50bace;}"
            "QAbstractSpinBox QLineEdit,QComboBox QLineEdit {border:0;padding:0;background:transparent;}");
        for (auto* control : formPane->findChildren<QWidget*>()) {
            if (qobject_cast<QComboBox*>(control) || qobject_cast<QAbstractSpinBox*>(control)
                || (qobject_cast<QLineEdit*>(control) && !qobject_cast<QAbstractSpinBox*>(control->parentWidget()))) {
                control->setMinimumHeight(32);
                control->setStyleSheet(field);
                if(auto* line=qobject_cast<QLineEdit*>(control)) {
                    auto palette=line->palette();
                    palette.setColor(QPalette::PlaceholderText,QColor("#475461"));
                    line->setPalette(palette);
                }
            }
        }
        for(auto* cb : {m_fuelDiesel,m_fuelPetrol,m_fuelHybrid,m_fuelElectric})
            cb->setStyleSheet("QCheckBox {color:white;background:transparent;font:600 13px 'Segoe UI';spacing:4px;} QCheckBox::indicator {width:15px;height:15px;background:#fafafa;border:1px solid #768994;border-radius:3px;} QCheckBox::indicator:checked {background:#249db4;border:3px solid #e4f8ff;}");
        shellLay->addWidget(formPane,1);
    } else {
        shellLay->addWidget(formPane,1,Qt::AlignTop);
    }

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

    if (m_isNewOrder) m_tabs->tabBar()->hide();
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
    if (m_isNewOrder) {
        buttonsRow->setContentsMargins(18,12,18,16);
        m_btnOk->setText(tr("Створити наряд"));
        const QString button = QStringLiteral("QPushButton {color:white;font:600 16px 'Segoe UI';"
            "padding:7px 22px;border-radius:8px;border:1px solid %1;background:"
            "qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 %2,stop:1 #101c24);}"
            "QPushButton:hover {background:#284654;}");
        m_btnOk->setStyleSheet(button.arg("#62c2d3","#325764"));
        m_btnCancel->setStyleSheet(button.arg("#71808a","#36414b"));
        buttonsRow->addWidget(m_btnCancel);
        buttonsRow->addWidget(m_btnOk);
    } else {
        buttonsRow->addWidget(m_btnOk);
        buttonsRow->addWidget(m_btnCancel);
    }
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
    if (m_newOrderPost) armUserEditTracking(m_newOrderPost);
    armUserEditTracking(m_discount);
    armUserEditTracking(m_discountMode);
}


