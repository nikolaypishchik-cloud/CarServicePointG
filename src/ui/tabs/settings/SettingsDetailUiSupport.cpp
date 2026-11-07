#include "ui/tabs/settings/SettingsDetailUiSupport.h"

#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QTableWidget>
#include <QHeaderView>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QtGlobal>

namespace SettingsDetailUiSupport {

QString standardButtonStyle(bool compactBackButton)
{
    Q_UNUSED(compactBackButton);
    const QString borderRadius = QStringLiteral("16px");
    const QString padding = QStringLiteral("0 24px");
    const QString fontSize = QStringLiteral("11pt");
    return QString::fromUtf8(
        "QPushButton{"
        "min-height:38px;"
        "background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        " stop:0 rgba(214,224,236,0.82),"
        " stop:0.14 rgba(154,168,184,0.92),"
        " stop:0.42 rgba(95,109,127,0.98),"
        " stop:0.48 rgba(40,52,68,1.0),"
        " stop:0.49 rgba(8,17,30,1.0),"
        " stop:1 rgba(4,12,24,1.0));"
        "color:#f8fbff;"
        "border:1px solid rgba(255,255,255,0.90);"
        "border-top:1px solid rgba(255,255,255,0.98);"
        "border-radius:%1;"
        "padding:%2;"
        "font-size:%3;"
        "font-weight:800;"
        "}"
        "QPushButton:hover{"
        "background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        " stop:0 rgba(228,236,246,0.90),"
        " stop:0.14 rgba(170,184,198,0.96),"
        " stop:0.42 rgba(108,123,141,1.0),"
        " stop:0.48 rgba(48,61,78,1.0),"
        " stop:0.49 rgba(10,21,36,1.0),"
        " stop:1 rgba(6,14,28,1.0));"
        "border:1px solid rgba(255,255,255,0.98);"
        "border-top:1px solid rgba(255,255,255,1.0);"
        "}"
        "QPushButton:pressed{"
        "padding-top:1px;"
        "background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        " stop:0 rgba(40,52,68,1.0),"
        " stop:0.40 rgba(18,29,43,1.0),"
        " stop:1 rgba(6,14,28,1.0));"
        "border:1px solid rgba(255,255,255,0.74);"
        "}"
        "QPushButton:disabled{"
        "background:rgba(112,122,136,0.46);"
        "color:#dbe4ef;"
        "border:1px solid rgba(255,255,255,0.28);"
        "}")
        .arg(borderRadius, padding, fontSize);
}

QString standardInputStyle()
{
    return QStringLiteral(
        "QLineEdit,QPlainTextEdit,QTextEdit,QComboBox,QSpinBox,QDoubleSpinBox,QDateEdit,QTimeEdit,QDateTimeEdit,QAbstractSpinBox,QKeySequenceEdit{"
        "background:#ffffff;"
        "color:#101418;"
        "border:1px solid rgba(22,28,35,0.28);"
        "border-radius:10px;"
        "padding:6px 10px;"
        "selection-background-color:#b8d6ff;"
        "selection-color:#101418;"
        "placeholder-text-color:#5d6672;"
        "min-height:24px;"
        "}"
        "QLineEdit:read-only,QPlainTextEdit:read-only,QTextEdit:read-only{"
        "background:#f7f9fc;"
        "color:#101418;"
        "border:1px solid rgba(22,28,35,0.26);"
        "}"
        "QLineEdit:disabled,QPlainTextEdit:disabled,QTextEdit:disabled,QComboBox:disabled,QSpinBox:disabled,QDoubleSpinBox:disabled,QDateEdit:disabled,QTimeEdit:disabled,QDateTimeEdit:disabled,QAbstractSpinBox:disabled,QKeySequenceEdit:disabled{"
        "background:#edf2f7;"
        "color:#2c333d;"
        "border:1px solid rgba(22,28,35,0.34);"
        "}"
        "QComboBox{padding-right:30px;}"
        "QComboBox::drop-down{width:26px; border-left:1px solid #c7ccd4; background:#eef2f7; border-top-right-radius:10px; border-bottom-right-radius:10px;}"
        "QComboBox::down-arrow{width:9px; height:9px;}"
        "QComboBox QAbstractItemView{background:#ffffff; color:#101418; border:1px solid rgba(22,28,35,0.28); selection-background-color:#dfe8f5; selection-color:#101418;}"
        "QAbstractSpinBox{padding-right:34px;}"
        "QAbstractSpinBox::up-button,QAbstractSpinBox::down-button{width:22px; subcontrol-origin:border; background:#eef2f7; border-left:1px solid #c7ccd4;}"
        "QAbstractSpinBox::up-button{border-top-right-radius:10px;}"
        "QAbstractSpinBox::down-button{border-bottom-right-radius:10px;}"
        "QAbstractSpinBox::up-arrow,QAbstractSpinBox::down-arrow{width:9px; height:9px;}"
    );
}

QString standardCheckBoxStyle()
{
    return QStringLiteral(
        "QCheckBox{color:#f7fbff; spacing:8px; background:transparent;}"
        "QCheckBox:disabled{color:#f7fbff;}"
        "QCheckBox::indicator{width:22px;height:22px;}"
        "QCheckBox::indicator:checked{image:url(:/assets/checkbox_checked_black.png);border:2px solid #111111;border-radius:4px;background:#ffffff;}"
        "QCheckBox::indicator:checked:disabled{image:url(:/assets/checkbox_checked_black.png);border:2px solid rgba(20,26,34,0.55);border-radius:4px;background:#eef2f7;}"
        "QCheckBox::indicator:unchecked{border:2px solid rgba(230,238,248,0.92);border-radius:4px;background:#ffffff;}"
        "QCheckBox::indicator:unchecked:hover{border:2px solid #ffffff;background:#f7fafc;}"
        "QCheckBox::indicator:disabled{border:2px solid rgba(255,255,255,0.55);border-radius:4px;background:#eef2f7;}"
    );
}


QPushButton* makeBackButton(QWidget* parent)
{
    auto* b = new QPushButton(QString::fromUtf8("← Назад"), parent);
    b->setFixedSize(120, 40);
    b->setCursor(Qt::PointingHandCursor);
    b->setStyleSheet(standardButtonStyle(true));
    return b;
}

QWidget* wrapDetailWithBack(QWidget* inner)
{
    if (!inner) return inner;
    auto* wrapper = new QWidget(inner->parentWidget());
    wrapper->setObjectName(QStringLiteral("settings_detail_with_back"));
    auto* outer = new QVBoxLayout(wrapper);
    outer->setContentsMargins(12, 12, 12, 12);
    outer->setSpacing(10);

    const QString inlineTitle = inner->property("settings_detail_inline_title").toString();
    auto* top = new QHBoxLayout();
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(inlineTitle.isEmpty() ? 0 : 30);
    auto* btnBack = makeBackButton(wrapper);
    auto* promotedTopAction = inner->findChild<QPushButton*>(QStringLiteral("settings_detail_top_action_button"));
    if (promotedTopAction) {
        promotedTopAction->setParent(wrapper);
        promotedTopAction->setFixedSize(120, 40);
        promotedTopAction->setCursor(Qt::PointingHandCursor);
        promotedTopAction->setStyleSheet(standardButtonStyle(false));
    }
    top->addWidget(btnBack, 0, Qt::AlignLeft | Qt::AlignVCenter);
    if (!inlineTitle.isEmpty()) {
        auto* title = new QLabel(inlineTitle, wrapper);
        title->setObjectName(QStringLiteral("settings_detail_inline_title"));
        title->setStyleSheet(QStringLiteral("color:#f3f7fb;background:transparent;font-size:16pt;font-weight:800;"));
        top->addWidget(title, 0, Qt::AlignLeft | Qt::AlignVCenter);
    }
    top->addStretch(1);
    if (promotedTopAction) {
        top->addWidget(promotedTopAction, 0, Qt::AlignRight | Qt::AlignVCenter);
    }
    outer->addLayout(top);

    inner->setParent(wrapper);
    outer->addWidget(inner, 1);
    normalizeDetailContrast(wrapper);
    normalizeDetailButtons(wrapper);
    return wrapper;
}

void normalizeDetailContrast(QWidget* root)
{
    if (!root) return;

    auto applyInputPalette = [](QWidget* widget) {
        if (!widget) return;
        QPalette pal = widget->palette();
        const QColor text(QStringLiteral("#101418"));
        const QColor base(QStringLiteral("#ffffff"));
        pal.setColor(QPalette::Base, base);
        pal.setColor(QPalette::Text, text);
        pal.setColor(QPalette::WindowText, text);
        pal.setColor(QPalette::ButtonText, text);
        pal.setColor(QPalette::Disabled, QPalette::Base, base);
        pal.setColor(QPalette::Disabled, QPalette::Text, text);
        pal.setColor(QPalette::Disabled, QPalette::WindowText, text);
        pal.setColor(QPalette::Disabled, QPalette::ButtonText, text);
        widget->setPalette(pal);
    };

    const QString inputStyle = standardInputStyle();

    const auto lineEdits = root->findChildren<QLineEdit*>();
    for (auto* edit : lineEdits) {
        if (!edit) continue;
        edit->setStyleSheet(inputStyle);
        applyInputPalette(edit);
    }

    const auto spinBoxes = root->findChildren<QAbstractSpinBox*>();
    for (auto* spin : spinBoxes) {
        if (!spin) continue;
        spin->setStyleSheet(inputStyle);
        applyInputPalette(spin);
        if (auto* edit = spin->findChild<QLineEdit*>()) {
            edit->setStyleSheet(QStringLiteral("background:#ffffff;color:#101418;border:none;selection-background-color:#b8d6ff;selection-color:#101418;"));
            applyInputPalette(edit);
        }
    }

    const auto combos = root->findChildren<QComboBox*>();
    for (auto* combo : combos) {
        if (!combo) continue;
        combo->setStyleSheet(inputStyle);
        applyInputPalette(combo);
    }

    const auto plainTexts = root->findChildren<QPlainTextEdit*>();
    for (auto* edit : plainTexts) {
        if (!edit) continue;
        edit->setStyleSheet(inputStyle);
        applyInputPalette(edit);
    }

    const auto textEdits = root->findChildren<QTextEdit*>();
    for (auto* edit : textEdits) {
        if (!edit) continue;
        edit->setStyleSheet(inputStyle);
        applyInputPalette(edit);
    }

    const QString checkboxStyle = standardCheckBoxStyle();
    const auto checks = root->findChildren<QCheckBox*>();
    for (auto* cb : checks) {
        if (!cb) continue;
        cb->setStyleSheet(checkboxStyle);
        QPalette pal = cb->palette();
        const QColor light(QStringLiteral("#f7fbff"));
        pal.setColor(QPalette::WindowText, light);
        pal.setColor(QPalette::Text, light);
        pal.setColor(QPalette::ButtonText, light);
        pal.setColor(QPalette::Disabled, QPalette::WindowText, light);
        pal.setColor(QPalette::Disabled, QPalette::Text, light);
        pal.setColor(QPalette::Disabled, QPalette::ButtonText, light);
        cb->setPalette(pal);
    }

    const auto groups = root->findChildren<QGroupBox*>();
    for (auto* group : groups) {
        if (!group) continue;
        QPalette pal = group->palette();
        pal.setColor(QPalette::WindowText, QColor(QStringLiteral("#f7fbff")));
        group->setPalette(pal);
    }

    const auto labels = root->findChildren<QLabel*>();
    for (auto* label : labels) {
        if (!label) continue;
        if (label->property("settingsMutedText").toBool()) continue;
        QPalette pal = label->palette();
        pal.setColor(QPalette::WindowText, QColor(QStringLiteral("#f7fbff")));
        label->setPalette(pal);
    }
}

void normalizeDetailButtons(QWidget* root)
{
    if (!root) return;
    normalizeDetailContrast(root);
    const auto buttons = root->findChildren<QPushButton*>();
    for (auto* btn : buttons) {
        if (!btn) continue;
        const QString text = btn->text().trimmed();
        const QString obj = btn->objectName().trimmed();
        const bool isBackButton = (text == QString::fromUtf8("← Назад") || text == QString::fromUtf8("Назад"));
        const bool isDialogActionButton = (text == QString::fromUtf8("Скасувати") || text == QString::fromUtf8("Зберегти"));
        if (isBackButton) {
            btn->setText(QString::fromUtf8("← Назад"));
            btn->setFixedSize(120, 40);
            btn->setStyleSheet(standardButtonStyle(true));
        } else if (isDialogActionButton) {
            btn->setFixedSize(120, 40);
            btn->setStyleSheet(standardButtonStyle(false));
        } else if (!obj.startsWith(QStringLiteral("qt_"))) {
            btn->setCursor(Qt::PointingHandCursor);
            if (btn->minimumWidth() == btn->maximumWidth() && btn->minimumWidth() > 0 && btn->minimumWidth() < 155) {
                btn->setFixedWidth(155);
            } else {
                btn->setMinimumWidth(qMax(btn->minimumWidth(), 155));
            }
            if (btn->property("access_role_compact_action").toBool()) {
                btn->setFixedHeight(44);
            } else {
                btn->setMinimumHeight(52);
            }
            btn->setStyleSheet(standardButtonStyle(false));
        }
    }
    // Apply compact sizes after the common theme, which otherwise reinstates tall inputs.
    for (auto* field : root->findChildren<QWidget*>()) {
        if (!field->property("settingsCompactInput").toBool()) continue;
        const int height = qMax(18, qRound(field->logicalDpiY() * 5.0 / 25.4));
        field->setStyleSheet(QStringLiteral("background:#ffffff;color:#101418;border:1px solid #9aa6b2;"
            "border-radius:3px;padding:0 5px;min-height:0px;font-size:9pt;"));
        field->setFixedHeight(height);
    }
    // Embedded controls acquire their final themed size only here.
    auto tables = root->findChildren<QTableWidget*>();
    if (auto* table = qobject_cast<QTableWidget*>(root)) tables.prepend(table);
    for (auto* table : tables) {
        if (table->objectName() != QStringLiteral("accessAccountsTable")) continue;
        QTimer::singleShot(0, table, [table] {
            // Preserve Stretch for the employee column instead of forcing content widths.
            for (int col = 0; col < table->columnCount(); ++col)
                if (table->horizontalHeader()->sectionResizeMode(col) == QHeaderView::ResizeToContents)
                    table->resizeColumnToContents(col);
            table->resizeRowsToContents();
            for (int row = 0; row < table->rowCount(); ++row) {
                int height = table->rowHeight(row);
                for (int col : {1, 4}) if (auto* cell = table->cellWidget(row, col)) {
                    height = qMax(height, cell->sizeHint().height() + 2);
                    table->setColumnWidth(col, qMax(table->columnWidth(col), cell->sizeHint().width() + 2));
                }
                table->setRowHeight(row, height);
            }
        });
    }
}

} // namespace SettingsDetailUiSupport
