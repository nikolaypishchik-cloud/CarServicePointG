#include "ui/tabs/settings/SettingsUiFontSectionSupport.h"
#include <functional>

#include "ui/tabs/settings/SettingsPageDataSupport.h"
#include "core/services/settings/UiSettingsService.h"
#include "ui/theme/ThemeManager.h"

#include <QColor>
#include <QColorDialog>
#include <QFont>
#include <QFontComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <memory>

namespace {

bool isDarkUi()
{
    return ThemeManager::currentTheme() == QStringLiteral("graphite");
}

QString cPrimary()
{
    return isDarkUi() ? QStringLiteral("#f7fbff") : QStringLiteral("#111111");
}

QString cSecondary()
{
    return isDarkUi() ? QStringLiteral("#c7d0db") : QStringLiteral("#4b5563");
}

QString standardActionButtonStyle()
{
    return QString::fromUtf8(
        "QPushButton{background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(214,224,236,0.82), stop:0.14 rgba(154,168,184,0.92), stop:0.42 rgba(95,109,127,0.98), stop:0.48 rgba(40,52,68,1.0), stop:0.49 rgba(8,17,30,1.0), stop:1 rgba(4,12,24,1.0));"
        "border:1px solid rgba(255,255,255,0.90);border-top:1px solid rgba(255,255,255,0.98);border-radius:18px;"
        "padding:0 24px;color:#f8fbff;font-size:11pt;font-weight:800;}"
        "QPushButton:hover{background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(228,236,246,0.90), stop:0.14 rgba(170,184,198,0.96), stop:0.42 rgba(108,123,141,1.0), stop:0.48 rgba(48,61,78,1.0), stop:0.49 rgba(10,21,36,1.0), stop:1 rgba(6,14,28,1.0));border:1px solid rgba(255,255,255,0.98);border-top:1px solid rgba(255,255,255,1.0);}"
        "QPushButton:pressed{padding-top:1px;background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(40,52,68,1.0), stop:0.40 rgba(18,29,43,1.0), stop:1 rgba(6,14,28,1.0));border:1px solid rgba(255,255,255,0.74);}"
        "QPushButton:disabled{background:rgba(112,122,136,0.46);color:#dbe4ef;border:1px solid rgba(255,255,255,0.28);}");
}

QPushButton* makeActionButton(const QString& text, QWidget* parent)
{
    auto* b = new QPushButton(text, parent);
    const bool isBackButton = (text == QString::fromUtf8("← Назад") || text == QString::fromUtf8("Назад"));
    if (isBackButton || text == QString::fromUtf8("Скасувати") || text == QString::fromUtf8("Зберегти")) {
        b->setFixedSize(120, 40);
    } else {
        b->setFixedSize(216, 52);
    }
    b->setCursor(Qt::PointingHandCursor);
    b->setStyleSheet(standardActionButtonStyle());
    return b;
}

} // namespace

namespace SettingsUiFontSectionSupport {

QWidget* build(QWidget* owner,
               const std::function<void()>& onBack,
               const std::function<void()>& onRefreshAll)
{
    auto* w = new QWidget(owner);
    auto* root = new QVBoxLayout(w);
    root->setContentsMargins(16,16,16,16);
    root->setSpacing(12);

    auto* top = new QHBoxLayout();
    top->setSpacing(12);
    auto* btnBack = makeActionButton(QString::fromUtf8("← Назад"), w);
    auto* title = new QLabel(QString::fromUtf8("Шрифт"), w);
    title->setStyleSheet(QString("font-size:16pt; font-weight:800; color:%1;").arg(cPrimary()));
    auto* btnCancel = makeActionButton(QString::fromUtf8("Скасувати"), w);
    auto* btnSave = makeActionButton(QString::fromUtf8("Зберегти"), w);
    top->addWidget(btnBack);
    top->addWidget(title, 1);
    top->addWidget(btnCancel);
    top->addWidget(btnSave);
    root->addLayout(top);

    auto* stage = new QFrame(w);
    stage->setStyleSheet(QString::fromUtf8(
        "QFrame{background:rgba(255,255,255,26);border:1px solid rgba(255,255,255,0.18);border-radius:18px;}"));
    auto* stageL = new QVBoxLayout(stage);
    stageL->setContentsMargins(14,14,14,14);
    stageL->setSpacing(12);

    auto* chip = new QLabel(QString::fromUtf8("Шрифт інтерфейсу"), stage);
    chip->setStyleSheet(QString::fromUtf8(
        "background:rgba(17,23,30,0.78);border:1px solid rgba(225,235,246,0.24);border-radius:12px;padding:4px 14px;color:#f8fbff;font-weight:600;"));
    stageL->addWidget(chip, 0, Qt::AlignLeft);

    auto* panel = new QFrame(stage);
    panel->setStyleSheet(QString::fromUtf8(
        "QFrame{background:rgba(255,255,255,0.08);border:1px solid rgba(255,255,255,56);border-radius:22px;}"
        "QLabel{color:%1;}"
        "QLineEdit,QSpinBox,QFontComboBox{background:#ffffff;border:1px solid rgba(22,28,35,0.22);border-radius:16px;padding:8px 12px;color:#111111;font-size:11pt;min-height:32px;}"
        "QSpinBox{padding-right:36px;}"
        "QSpinBox::up-button,QSpinBox::down-button{width:22px; subcontrol-origin:border; background:#eef2f7; border-left:1px solid #c7ccd4;}"
        "QSpinBox::up-button{border-top-right-radius:16px;}"
        "QSpinBox::down-button{border-bottom-right-radius:16px;}"
        "QSpinBox::up-arrow,QSpinBox::down-arrow{width:10px;height:10px;}").arg(cPrimary()));
    auto* panelL = new QVBoxLayout(panel);
    panelL->setContentsMargins(22,20,22,20);
    panelL->setSpacing(14);

    const auto uiFontSettings = UiSettingsService::loadFontSettings();

    auto* form1 = new QHBoxLayout();
    auto* familyLbl = new QLabel(QString::fromUtf8("Шрифт:"), panel);
    familyLbl->setMinimumWidth(90);
    auto* family = new QFontComboBox(panel);
    family->setCurrentFont(QFont(uiFontSettings.fontFamily.isEmpty() ? QStringLiteral("Times New Roman") : uiFontSettings.fontFamily));
    auto* familyPopup = new QListView(family);
    familyPopup->setObjectName(QStringLiteral("settingsFontFamilyPopup"));
    familyPopup->setStyleSheet(QString::fromUtf8(
        "QListView#settingsFontFamilyPopup{background:#ffffff;color:#111111;border:1px solid rgba(22,28,35,0.30);outline:none;}"
        "QListView#settingsFontFamilyPopup::item{background:#ffffff;color:#111111;padding:6px 10px;min-height:24px;}"
        "QListView#settingsFontFamilyPopup::item:selected{background:#dfe8f5;color:#111111;}"
        "QListView#settingsFontFamilyPopup::item:hover{background:#eef3fb;color:#111111;}"));
    family->setView(familyPopup);
    family->view()->setStyleSheet(QString::fromUtf8(
        "background:#ffffff;color:#111111;border:1px solid rgba(22,28,35,0.30);selection-background-color:#dfe8f5;selection-color:#111111;"));
    family->setStyleSheet(QString::fromUtf8(
        "QFontComboBox{background:#ffffff;border:1px solid rgba(22,28,35,0.22);border-radius:16px;padding:8px 12px;color:#111111;font-size:11pt;min-height:32px;}"
        "QFontComboBox QAbstractItemView{background:#ffffff;color:#111111;border:1px solid rgba(22,28,35,0.30);selection-background-color:#dfe8f5;selection-color:#111111;}"));
    form1->addWidget(familyLbl);
    form1->addWidget(family, 1);
    panelL->addLayout(form1);

    auto* form2 = new QHBoxLayout();
    auto* sizeLbl = new QLabel(QString::fromUtf8("Розмір:"), panel);
    sizeLbl->setMinimumWidth(90);
    auto* size = new QSpinBox(panel);
    size->setRange(8, 28);
    size->setValue(uiFontSettings.fontSize);
    size->setFixedWidth(86);

    auto* colorLbl = new QLabel(QString::fromUtf8("Колір:"), panel);
    colorLbl->setMinimumWidth(70);
    auto* pickColor = makeActionButton(QString::fromUtf8("Вибрати…"), panel);
    pickColor->setFixedSize(190, 52);
    auto* sample = new QLineEdit(panel);
    sample->setReadOnly(true);
    sample->setText(QString::fromUtf8("Приклад тексту / Sample"));
    form2->addWidget(sizeLbl);
    form2->addWidget(size);
    form2->addSpacing(16);
    form2->addWidget(colorLbl);
    form2->addWidget(pickColor);
    form2->addSpacing(8);
    form2->addWidget(sample, 1);
    panelL->addLayout(form2);

    auto* form3 = new QHBoxLayout();
    auto* tableLbl = new QLabel(QString::fromUtf8("Таблиця: текст"), panel);
    tableLbl->setMinimumWidth(140);
    auto* tableSize = new QSpinBox(panel);
    tableSize->setRange(8, 24);
    tableSize->setValue(uiFontSettings.tableFontSize);
    tableSize->setFixedWidth(86);

    auto* hdrLbl = new QLabel(QString::fromUtf8("Заголовки"), panel);
    hdrLbl->setMinimumWidth(110);
    auto* hdrSize = new QSpinBox(panel);
    hdrSize->setRange(8, 28);
    hdrSize->setValue(uiFontSettings.headingFontSize);
    hdrSize->setFixedWidth(86);

    form3->addWidget(tableLbl);
    form3->addWidget(tableSize);
    form3->addSpacing(20);
    form3->addWidget(hdrLbl);
    form3->addWidget(hdrSize);
    form3->addStretch(1);
    panelL->addLayout(form3);

    auto* hint = new QLabel(QString::fromUtf8("Після збереження стиль застосунку оновиться одразу."), panel);
    hint->setStyleSheet(QString("color:%1; font-size:10.5pt;").arg(cSecondary()));
    panelL->addWidget(hint);

    stageL->addWidget(panel);
    root->addWidget(stage);
    root->addStretch(1);

    auto chosen = std::make_shared<QColor>(QColor(uiFontSettings.fontColor.isEmpty() ? QStringLiteral("#1f2434") : uiFontSettings.fontColor));
    if (!chosen->isValid()) *chosen = QColor("#1f2434");

    auto applyPreview = [sample, family, size]() {
        QFont f = family->currentFont();
        f.setPointSize(size->value());
        sample->setFont(f);
    };
    auto updateColorButton = [pickColor, sample](const QColor& c) {
        pickColor->setStyleSheet(standardActionButtonStyle());
        sample->setStyleSheet(QString("color:%1;").arg(c.name()));
    };
    applyPreview();
    updateColorButton(*chosen);

    auto saveState = [family, size, tableSize, hdrSize, chosen]() {
        UiSettingsService::FontSettingsPayload payload;
        payload.fontFamily = family->currentFont().family();
        payload.fontSize = size->value();
        payload.tableFontSize = tableSize->value();
        payload.headingFontSize = hdrSize->value();
        payload.fontColor = chosen->name();
        UiSettingsService::saveFontSettings(payload);
    };
    auto loadState = [family, size, tableSize, hdrSize, applyPreview, updateColorButton, chosen]() {
        const auto restoredFontSettings = UiSettingsService::loadFontSettings();
        family->setCurrentFont(QFont(restoredFontSettings.fontFamily.isEmpty() ? QStringLiteral("Times New Roman") : restoredFontSettings.fontFamily));
        size->setValue(restoredFontSettings.fontSize);
        tableSize->setValue(restoredFontSettings.tableFontSize);
        hdrSize->setValue(restoredFontSettings.headingFontSize);
        *chosen = QColor(restoredFontSettings.fontColor.isEmpty() ? QStringLiteral("#1f2434") : restoredFontSettings.fontColor);
        if (!chosen->isValid()) *chosen = QColor("#1f2434");
        applyPreview();
        updateColorButton(*chosen);
    };

    QObject::connect(btnBack, &QPushButton::clicked, w, [onBack]() {
        if (onBack) onBack();
    });
    QObject::connect(btnCancel, &QPushButton::clicked, w, loadState);
    QObject::connect(btnSave, &QPushButton::clicked, w, [saveState, family, size, tableSize, hdrSize, chosen, onRefreshAll]() {
        const auto previousFontSettings = UiSettingsService::loadFontSettings();
        const QString previousFamily = previousFontSettings.fontFamily.isEmpty() ? QStringLiteral("Times New Roman") : previousFontSettings.fontFamily;
        const int previousSize = previousFontSettings.fontSize;
        const int previousTableSize = previousFontSettings.tableFontSize;
        const int previousHeadingSize = previousFontSettings.headingFontSize;
        const QString previousColor = previousFontSettings.fontColor.isEmpty() ? QStringLiteral("#1f2434") : previousFontSettings.fontColor;
        saveState();
        if (previousFamily != family->currentFont().family()
            || previousSize != size->value()
            || previousTableSize != tableSize->value()
            || previousHeadingSize != hdrSize->value()
            || previousColor.compare(chosen->name(), Qt::CaseInsensitive) != 0) {
            SettingsPageDataSupport::publishSettingsBackupEvent(QStringLiteral("ui_font_settings_save"),
                                                               QStringLiteral("ui_settings"),
                                                               QStringLiteral("settings.ui.font"),
                                                               QString::fromUtf8("Збережено налаштування шрифту інтерфейсу"),
                                                               {QStringLiteral("settings")});
        }
        if (onRefreshAll) onRefreshAll();
    });
    QObject::connect(family, &QFontComboBox::currentFontChanged, w, [applyPreview](const QFont&) { applyPreview(); });
    QObject::connect(size, qOverload<int>(&QSpinBox::valueChanged), w, [applyPreview](int) { applyPreview(); });
    QObject::connect(pickColor, &QPushButton::clicked, w, [owner, chosen, updateColorButton]() {
        const QColor c = QColorDialog::getColor(*chosen, owner, QString::fromUtf8("Виберіть колір шрифту"));
        if (!c.isValid()) return;
        *chosen = c;
        updateColorButton(*chosen);
    });

    return w;
}

} // namespace SettingsUiFontSectionSupport