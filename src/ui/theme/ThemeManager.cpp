#include "ui/theme/ThemeManager.h"
#include "core/services/settings/UiSettingsService.h"
#include "core/services/settings/GeneralSettingsService.h"
#include "core/services/settings/ThemePreferenceService.h"

#include "core/backup/BackupCommitPublisher.h"

#include <QApplication>
#include <QFont>
#include <QWidget>
#include <QInputDialog>
#include <QComboBox>
#include <QLabel>
#include <QLayout>
#include <QScreen>
#include <QEvent>
#include <QTimer>
#include <QDialog>
#include <QMainWindow>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {
class CompactInputDialogLayout final : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject* object, QEvent* event) override {
#ifdef Q_OS_WIN
        if (auto* popup=qobject_cast<QWidget*>(object);
            popup && popup->isWindow() && !qobject_cast<QMainWindow*>(popup)
            && (event->type()==QEvent::Show || event->type()==QEvent::WindowTitleChange
                || event->type()==QEvent::WindowActivate || event->type()==QEvent::WinIdChange
                || event->type()==QEvent::WindowStateChange)) {
            // Qt adds the application display name to native Windows captions.
            QTimer::singleShot(0,popup,[popup] {
                if (popup->isVisible())
                    SetWindowTextW(reinterpret_cast<HWND>(popup->winId()),
                                   reinterpret_cast<LPCWSTR>(popup->windowTitle().utf16()));
            });
        }
#endif
        auto* dialog=qobject_cast<QInputDialog*>(object);
        if(dialog && event->type()==QEvent::Show) {
            for(auto* label:dialog->findChildren<QLabel*>()) {
                label->setWordWrap(true);
                label->setMinimumWidth(0);
                label->setStyleSheet(qApp->property("carservice.graphiteThemeActive").toBool()
                    ? "color:white;background:transparent;" : "color:black;background:transparent;");
            }
            for(auto* combo:dialog->findChildren<QComboBox*>()) {
                combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
                combo->setMinimumContentsLength(1);
                combo->setMinimumWidth(0);
                combo->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
                combo->setToolTip(combo->currentText());
            }
            if(dialog->layout())dialog->layout()->setSizeConstraint(QLayout::SetNoConstraint);
            const int width=qMin(qRound(90.0*dialog->logicalDpiX()/25.4),
                                 dialog->screen()->availableGeometry().width()-24);
            dialog->setFixedWidth(width);
        }
        return QObject::eventFilter(object,event);
    }
};
}


static QString contrastCheckboxQss()
{
    return QString::fromUtf8(R"QSS(
        QCheckBox {
            spacing: 8px;
            background: transparent;
        }
        QCheckBox::indicator {
            width: 20px;
            height: 20px;
        }
        QCheckBox::indicator:unchecked {
            background: #ffffff;
            border: 2px solid rgba(20,26,34,0.92);
            border-radius: 4px;
        }
        QCheckBox::indicator:unchecked:hover {
            border: 2px solid #111111;
            background: #f7fafc;
        }
        QCheckBox::indicator:checked {
            image: url(:/assets/checkbox_checked_black.png);
            background: #ffffff;
            border: 2px solid #111111;
            border-radius: 4px;
        }
        QCheckBox::indicator:disabled {
            background: #eef2f7;
            border: 2px solid rgba(20,26,34,0.55);
            border-radius: 4px;
        }
    )QSS");
}


static QString qssGraphite()
{
    QString qss = QString::fromUtf8(R"QSS(
        QWidget {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 #1c232c,
                                        stop:0.07 #414c5a,
                                        stop:0.16 #667282,
                                        stop:0.24 #526072,
                                        stop:0.60 #2f3b49,
                                        stop:1 #506070);
            color: #f7f8fa;
            font-size: 10.5pt;
        }
        QMainWindow {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 #1c232c,
                                        stop:0.07 #414c5a,
                                        stop:0.16 #667282,
                                        stop:0.24 #526072,
                                        stop:0.60 #2f3b49,
                                        stop:1 #506070);
        }

        #sidebar, QFrame#Sidebar {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 rgba(52,60,70,0.99),
                                        stop:0.08 rgba(124,136,151,0.92),
                                        stop:0.16 rgba(96,108,122,0.96),
                                        stop:0.30 rgba(73,84,97,0.98),
                                        stop:0.62 rgba(56,66,78,0.98),
                                        stop:1 rgba(43,52,62,0.99));
            border-right: 1px solid rgba(255,255,255,0.18);
        }
        #content {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 rgba(25,31,39,0.98),
                                        stop:0.08 rgba(56,67,81,0.98),
                                        stop:0.18 rgba(96,109,125,0.92),
                                        stop:0.28 rgba(69,82,97,0.96),
                                        stop:0.64 rgba(39,48,60,0.98),
                                        stop:1 rgba(55,67,81,0.98));
            border: none;
        }
        #pages {
            background: transparent;
            border: none;
        }
        #top_pill {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                                        stop:0 rgba(22,27,33,0.78),
                                        stop:0.18 rgba(52,61,72,0.68),
                                        stop:0.50 rgba(112,124,138,0.56),
                                        stop:0.82 rgba(54,63,74,0.68),
                                        stop:1 rgba(26,31,37,0.78));
            border: 1px solid rgba(255,255,255,0.26);
            border-radius: 16px;
            padding: 2px;
        }
        #top_title { color: #ffffff; font-weight: 800; }
        QLabel#Title { color: #ffffff; font-size: 18pt; font-weight: 700; }
        QLabel, QCheckBox, QRadioButton, QGroupBox { color: #f7f8fa; background: transparent; }
        %1
        QFormLayout QLabel, QWidget[role="fieldLabel"] { background: transparent; border: none; color: #f7f8fa; }

        QDialog, QMessageBox {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                        stop:0 #40454c,
                                        stop:1 #2c3137);
            color: #f7f8fa;
        }
        QMessageBox QLabel {
            background: transparent;
            color: #f7f8fa;
            font-size: 11.5pt;
            font-weight: 600;
            min-width: 0px;
            max-width: 16777215px;
        }
        QMessageBox QPushButton {
            min-width: 84px;
            min-height: 34px;
        }

        QLineEdit, QTextEdit, QPlainTextEdit, QComboBox, QSpinBox, QDoubleSpinBox,
        QDateEdit, QTimeEdit, QDateTimeEdit, QAbstractSpinBox, QKeySequenceEdit {
            background: #ffffff;
            color: #111111;
            border: 1px solid rgba(22,28,35,0.22);
            border-radius: 10px;
            padding: 6px 10px;
            selection-background-color: #b8d6ff;
            selection-color: #111111;
            placeholder-text-color: #5d6672;
        }
        QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QComboBox:focus, QSpinBox:focus,
        QDoubleSpinBox:focus, QDateEdit:focus, QTimeEdit:focus, QDateTimeEdit:focus,
        QAbstractSpinBox:focus, QKeySequenceEdit:focus {
            border: 1px solid #7ea4f4;
            background: #ffffff;
            color: #111111;
        }
        QLineEdit:read-only, QTextEdit:read-only, QPlainTextEdit:read-only {
            background: #f7f9fc;
            color: #111111;
            border: 1px solid rgba(22,28,35,0.26);
        }
        QLineEdit:disabled, QTextEdit:disabled, QPlainTextEdit:disabled, QComboBox:disabled,
        QSpinBox:disabled, QDoubleSpinBox:disabled, QDateEdit:disabled, QTimeEdit:disabled,
        QDateTimeEdit:disabled, QAbstractSpinBox:disabled, QKeySequenceEdit:disabled {
            background: #edf2f7;
            color: #2c333d;
            border: 1px solid rgba(22,28,35,0.34);
        }
        QComboBox QAbstractItemView {
            background: #ffffff;
            color: #111111;
            border: 1px solid rgba(28,31,36,0.20);
            selection-background-color: #dfe6ef;
            selection-color: #111111;
            outline: none;
        }
        QComboBox QAbstractItemView::item {
            background: #ffffff;
            color: #111111;
            padding: 6px 10px;
            min-height: 24px;
        }
        QComboBox QAbstractItemView::item:selected {
            background: #dfe6ef;
            color: #111111;
        }

        QToolButton, QPushButton {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 rgba(255,255,255,0.18),
                                        stop:0.10 rgba(255,255,255,0.08),
                                        stop:0.11 rgba(63,72,82,0.90),
                                        stop:0.55 rgba(42,48,56,0.94),
                                        stop:1 rgba(26,31,37,0.96));
            color: #f8f9fb;
            border: 1px solid rgba(255,255,255,0.16);
            border-radius: 11px;
            padding: 7px 14px;
            font-weight: 650;
        }
        #sidebar QPushButton, #ordersNavigation QPushButton {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                        stop:0 rgba(56,62,72,0.99),
                                        stop:0.16 rgba(43,48,58,0.99),
                                        stop:0.42 rgba(29,33,40,0.99),
                                        stop:0.74 rgba(18,21,27,0.99),
                                        stop:1 rgba(10,12,16,0.99));
            color: #f7fbff;
            border: 1px solid rgba(255,255,255,0.84);
            border-radius: 18px;
            padding: 8px 12px;
            font-weight: 700;
        }
        #sidebar QPushButton#sb_btn_settings {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                        stop:0 rgba(32,69,150,0.99),
                                        stop:0.22 rgba(23,52,118,0.99),
                                        stop:0.58 rgba(15,34,86,0.99),
                                        stop:1 rgba(8,20,60,0.99));
            color: #f9fcff;
            border: 1px solid rgba(210,228,255,0.96);
        }
        QToolButton:hover, QPushButton:hover,
        #sidebar QPushButton:hover {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                        stop:0 rgba(72,80,92,0.99),
                                        stop:0.18 rgba(55,62,74,0.99),
                                        stop:0.42 rgba(38,43,52,0.99),
                                        stop:0.72 rgba(24,28,35,0.99),
                                        stop:1 rgba(14,17,22,0.99));
            border: 1px solid rgba(220,236,255,0.95);
        }
        QToolButton:pressed, QPushButton:pressed,
        #sidebar QPushButton:pressed {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                        stop:0 rgba(24,28,34,0.99),
                                        stop:1 rgba(8,10,14,0.99));
        }
        QPushButton:checked, #sidebar QPushButton:checked {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                        stop:0 rgba(67,74,86,0.99),
                                        stop:0.22 rgba(50,56,67,0.99),
                                        stop:0.60 rgba(33,38,46,0.99),
                                        stop:1 rgba(16,19,24,0.99));
            color: #f9fcff;
            border: 1px solid rgba(198,226,255,0.98);
        }

        QTabWidget::pane {
            border: 1px solid rgba(255,255,255,0.18);
            border-radius: 14px;
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 rgba(255,255,255,0.10),
                                        stop:0.20 rgba(255,255,255,0.05),
                                        stop:0.55 rgba(255,255,255,0.02),
                                        stop:1 rgba(255,255,255,0.00));
        }
        QTabBar::tab {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 rgba(255,255,255,0.18),
                                        stop:0.10 rgba(255,255,255,0.10),
                                        stop:0.11 rgba(83,94,107,0.82),
                                        stop:0.55 rgba(54,62,72,0.76),
                                        stop:1 rgba(31,36,43,0.88));
            color: #f5f7fa;
            border: 1px solid rgba(255,255,255,0.24);
            border-bottom: none;
            border-top-left-radius: 10px;
            border-top-right-radius: 10px;
            padding: 8px 14px;
            margin-right: 3px;
        }
        QTabBar::tab:selected {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 rgba(255,255,255,0.30),
                                        stop:0.09 rgba(255,255,255,0.16),
                                        stop:0.10 rgba(109,122,138,0.78),
                                        stop:0.55 rgba(71,81,93,0.74),
                                        stop:1 rgba(41,47,55,0.84));
            border: 1px solid rgba(255,255,255,0.34);
        }

        QGroupBox {
            border: 1px solid rgba(255,255,255,0.12);
            border-radius: 10px;
            margin-top: 10px;
            padding-top: 10px;
            font-weight: 600;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
            padding: 0 6px;
            color: #ffffff;
        }

        QScrollArea, QListView, QTreeView {
            background: transparent;
            border: 1px solid rgba(255,255,255,0.10);
            border-radius: 10px;
        }
        QScrollArea#sidebar_top_scroll,
        QWidget#sidebar_top_scroll_viewport,
        QWidget#sidebar_top_content,
        QWidget#sidebar_bottom_menu {
            background: transparent;
            border: none;
            border-radius: 0px;
        }

        QTableView, QTableWidget {
            background: #ffffff;
            alternate-background-color: #ffffff;
            color: #111111;
            gridline-color: #d9dee4;
            border: 1px solid rgba(255,255,255,0.10);
            border-radius: 0px;
            outline: none;
        }
        QTableView::item, QTableWidget::item {
            background: #ffffff;
            color: #111111;
            padding: 4px 6px;
            border-right: 1px solid #e7ebf0;
            border-bottom: 1px solid #edf1f5;
        }
        QTableView::item:hover, QTableWidget::item:hover {
            background: #eef4ff;
            color: #111111;
        }
        QTableView::item:selected, QTableWidget::item:selected {
            background: #d8e6ff;
            color: #111111;
        }
        QHeaderView::section {
            background: #f5f7fa;
            color: #111111;
            padding: 7px 8px;
            border: none;
            border-right: 1px solid #dde3ea;
            border-bottom: 1px solid #d7dde4;
            font-weight: 700;
        }
        QHeaderView::section:hover { background: #edf3fb; }
        QTableView QHeaderView::section:vertical,
        QTableWidget QHeaderView::section:vertical {
            background: #ffffff;
            color: #111111;
            border-right: 1px solid #dde3ea;
            border-bottom: 1px solid #d7dde4;
        }

        QTableCornerButton::section {
            background: #ffffff;
            border: none;
            border-right: 1px solid #dde3ea;
            border-bottom: 1px solid #d7dde4;
        }

        QScrollBar:vertical {
            background: rgba(16,18,22,0.20);
            width: 10px;
            margin: 8px 2px 8px 2px;
            border-radius: 5px;
        }
        QScrollBar::handle:vertical {
            background: rgba(240,244,248,0.30);
            min-height: 28px;
            border-radius: 5px;
        }
        QScrollBar:horizontal {
            background: rgba(16,18,22,0.20);
            height: 10px;
            margin: 2px 8px 2px 8px;
            border-radius: 5px;
        }
        QScrollBar::handle:horizontal {
            background: rgba(240,244,248,0.30);
            min-width: 28px;
            border-radius: 5px;
        }
        QScrollBar::add-line, QScrollBar::sub-line,
        QScrollBar::add-page, QScrollBar::sub-page { background: transparent; border: none; }
    )QSS");

    qss = qss.arg(contrastCheckboxQss());
    return qss;
}

static QString qssLight()
{
    QString qss = QString::fromUtf8(R"QSS(
        QWidget {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 #f8fafc,
                                        stop:0.08 #eef2f6,
                                        stop:0.28 #dbe2ea,
                                        stop:0.62 #c8d0d9,
                                        stop:1 #b7c0cb);
            color: #0f1114;
            font-size: 10.5pt;
        }
        QMainWindow {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 #f8fafc,
                                        stop:0.08 #eef2f6,
                                        stop:0.28 #dbe2ea,
                                        stop:0.62 #c8d0d9,
                                        stop:1 #b7c0cb);
        }

        #sidebar, QFrame#Sidebar {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 #fdfefe,
                                        stop:0.06 #f6f8fa,
                                        stop:0.14 #e4e9ef,
                                        stop:0.50 #c8d0d8,
                                        stop:0.82 #b2bcc8,
                                        stop:1 #9ea9b7);
            border-right: 1px solid rgba(46,56,68,0.22);
        }
        #content {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 rgba(249,250,252,0.98),
                                        stop:0.14 rgba(238,242,246,0.98),
                                        stop:0.42 rgba(220,226,233,0.98),
                                        stop:0.78 rgba(202,210,219,0.98),
                                        stop:1 rgba(188,196,206,0.98));
            border: none;
        }
        #pages {
            background: transparent;
            border: none;
        }
        #top_pill {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                                        stop:0 rgba(245,247,249,0.96),
                                        stop:0.12 rgba(231,236,241,0.96),
                                        stop:0.50 rgba(210,217,225,0.96),
                                        stop:0.88 rgba(231,236,241,0.96),
                                        stop:1 rgba(245,247,249,0.96));
            border: 1px solid rgba(52,61,72,0.20);
            border-radius: 16px;
            padding: 2px;
        }
        #top_title { color: #090b0d; font-weight: 800; }
        QLabel#Title { color: #090b0d; font-size: 18pt; font-weight: 800; }
        QLabel, QCheckBox, QRadioButton, QGroupBox { color: #0f1114; background: transparent; }
        %1
        QFormLayout QLabel, QWidget[role="fieldLabel"] { background: transparent; border: none; color: #0f1114; }

        QDialog, QMessageBox {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                        stop:0 #f9fbfc,
                                        stop:0.40 #d8e0e8,
                                        stop:1 #bcc5d0);
            color: #0f1114;
        }
        QMessageBox QLabel {
            background: transparent;
            color: #0f1114;
            font-size: 11.5pt;
            font-weight: 600;
            min-width: 0px;
            max-width: 16777215px;
        }
        QMessageBox QPushButton {
            min-width: 84px;
            min-height: 34px;
        }

        QLineEdit, QTextEdit, QPlainTextEdit, QComboBox, QSpinBox, QDoubleSpinBox,
        QDateEdit, QTimeEdit, QDateTimeEdit, QAbstractSpinBox, QKeySequenceEdit {
            background: #ffffff;
            color: #0f1114;
            border: 1px solid rgba(52,61,72,0.24);
            border-radius: 9px;
            padding: 6px 10px;
            selection-background-color: #b8d6ff;
            selection-color: #0f1114;
            placeholder-text-color: #606a76;
        }
        QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QComboBox:focus, QSpinBox:focus,
        QDoubleSpinBox:focus, QDateEdit:focus, QTimeEdit:focus, QDateTimeEdit:focus,
        QAbstractSpinBox:focus, QKeySequenceEdit:focus {
            border: 1px solid #6c88a9;
            background: #ffffff;
            color: #0f1114;
        }
        QLineEdit:read-only, QTextEdit:read-only, QPlainTextEdit:read-only {
            background: #f7f9fc;
            color: #0f1114;
            border: 1px solid rgba(52,61,72,0.28);
        }
        QLineEdit:disabled, QTextEdit:disabled, QPlainTextEdit:disabled, QComboBox:disabled,
        QSpinBox:disabled, QDoubleSpinBox:disabled, QDateEdit:disabled, QTimeEdit:disabled,
        QDateTimeEdit:disabled, QAbstractSpinBox:disabled, QKeySequenceEdit:disabled {
            background: #edf2f7;
            color: #2c333d;
            border: 1px solid rgba(52,61,72,0.34);
        }
        QComboBox QAbstractItemView {
            background: #fdfefe;
            color: #0f1114;
            border: 1px solid rgba(52,61,72,0.24);
            selection-background-color: #dde5ee;
            selection-color: #0f1114;
            outline: none;
        }
        QComboBox QAbstractItemView::item {
            background: #fdfefe;
            color: #0f1114;
            padding: 6px 10px;
            min-height: 24px;
        }
        QComboBox QAbstractItemView::item:selected {
            background: #dde5ee;
            color: #0f1114;
        }

        QToolButton, QPushButton {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 rgba(255,255,255,0.98),
                                        stop:0.12 rgba(246,248,250,0.96),
                                        stop:0.13 rgba(220,226,233,0.96),
                                        stop:0.56 rgba(176,186,198,0.97),
                                        stop:1 rgba(136,147,161,0.98));
            color: #090b0d;
            border: 1px solid rgba(46,56,68,0.30);
            border-radius: 11px;
            padding: 7px 14px;
            font-weight: 700;
        }
        #sidebar QPushButton, #ordersNavigation QPushButton {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                        stop:0 rgba(58,65,76,0.99),
                                        stop:0.18 rgba(45,51,61,0.99),
                                        stop:0.42 rgba(31,36,44,0.99),
                                        stop:0.72 rgba(20,24,30,0.99),
                                        stop:1 rgba(11,14,19,0.99));
            color: #f7fbff;
            border: 1px solid rgba(255,255,255,0.82);
            border-radius: 18px;
            padding: 8px 12px;
            font-weight: 700;
        }
        #sidebar QPushButton#sb_btn_settings {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                        stop:0 rgba(33,72,156,0.99),
                                        stop:0.22 rgba(24,55,124,0.99),
                                        stop:0.58 rgba(16,37,92,0.99),
                                        stop:1 rgba(9,21,64,0.99));
            color: #f9fcff;
            border: 1px solid rgba(202,224,255,0.96);
        }
        QToolButton:hover, QPushButton:hover,
        #sidebar QPushButton:hover {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                        stop:0 rgba(76,84,96,0.99),
                                        stop:0.18 rgba(58,66,78,0.99),
                                        stop:0.42 rgba(40,46,55,0.99),
                                        stop:0.72 rgba(25,30,37,0.99),
                                        stop:1 rgba(15,18,24,0.99));
            border: 1px solid rgba(220,236,255,0.95);
        }
        QToolButton:pressed, QPushButton:pressed,
        #sidebar QPushButton:pressed {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                        stop:0 rgba(24,28,34,0.99),
                                        stop:1 rgba(8,10,14,0.99));
        }
        QPushButton:checked, #sidebar QPushButton:checked {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                        stop:0 rgba(68,76,88,0.99),
                                        stop:0.22 rgba(51,58,69,0.99),
                                        stop:0.60 rgba(34,39,48,0.99),
                                        stop:1 rgba(17,20,25,0.99));
            color: #f9fcff;
            border: 1px solid rgba(198,226,255,0.98);
        }

        QTabWidget::pane {
            border: 1px solid rgba(52,61,72,0.16);
            border-radius: 14px;
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 rgba(250,251,252,0.92),
                                        stop:1 rgba(224,230,237,0.92));
        }
        QTabBar::tab {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 rgba(255,255,255,0.98),
                                        stop:0.10 rgba(248,250,252,0.97),
                                        stop:0.11 rgba(223,229,236,0.97),
                                        stop:0.56 rgba(184,194,205,0.97),
                                        stop:1 rgba(147,158,171,0.98));
            color: #0f1114;
            border: 1px solid rgba(52,61,72,0.24);
            border-bottom: none;
            border-top-left-radius: 10px;
            border-top-right-radius: 10px;
            padding: 8px 14px;
            margin-right: 3px;
            font-weight: 700;
        }
        QTabBar::tab:selected {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 rgba(255,255,255,1.0),
                                        stop:0.10 rgba(250,252,253,0.99),
                                        stop:0.11 rgba(236,241,245,0.99),
                                        stop:0.56 rgba(201,210,220,0.99),
                                        stop:1 rgba(166,177,190,1.0));
            color: #090b0d;
            border: 1px solid rgba(52,61,72,0.30);
        }

        QGroupBox {
            border: 1px solid rgba(52,61,72,0.16);
            border-radius: 10px;
            margin-top: 10px;
            padding-top: 10px;
            font-weight: 700;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
            padding: 0 6px;
            color: #090b0d;
        }

        QScrollArea, QListView, QTreeView {
            background: transparent;
            border: 1px solid rgba(52,61,72,0.14);
            border-radius: 10px;
            color: #0f1114;
        }
        QScrollArea#sidebar_top_scroll,
        QWidget#sidebar_top_scroll_viewport,
        QWidget#sidebar_top_content,
        QWidget#sidebar_bottom_menu {
            background: transparent;
            border: none;
            border-radius: 0px;
        }

        QTableView, QTableWidget {
            background: #ffffff;
            alternate-background-color: #f7f9fb;
            color: #0f1114;
            gridline-color: #cfd6de;
            border: 1px solid rgba(52,61,72,0.18);
            border-radius: 0px;
            outline: none;
        }
        QTableView::item, QTableWidget::item {
            background: #ffffff;
            color: #0f1114;
            padding: 4px 6px;
            border-right: 1px solid #e2e7ec;
            border-bottom: 1px solid #e8edf2;
        }
        QTableView::item:hover, QTableWidget::item:hover {
            background: #edf3f9;
            color: #0f1114;
        }
        QTableView::item:selected, QTableWidget::item:selected {
            background: #d9e6f5;
            color: #0f1114;
        }
        QHeaderView::section {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 #fcfdfe,
                                        stop:0.14 #f1f4f7,
                                        stop:0.15 #dde3e9,
                                        stop:0.62 #c6ced7,
                                        stop:1 #adb7c3);
            color: #090b0d;
            padding: 7px 8px;
            border: none;
            border-right: 1px solid #d2d9e1;
            border-bottom: 1px solid #c5ced8;
            font-weight: 800;
        }
        QHeaderView::section:hover { background: #edf2f7; }
        QTableView QHeaderView::section:vertical,
        QTableWidget QHeaderView::section:vertical {
            background: #ffffff;
            color: #0f1114;
            border-right: 1px solid #d2d9e1;
            border-bottom: 1px solid #c5ced8;
        }
        QTableCornerButton::section {
            background: #ffffff;
            border: none;
            border-right: 1px solid #d2d9e1;
            border-bottom: 1px solid #c5ced8;
        }

        QScrollBar:vertical {
            background: rgba(70,80,92,0.14);
            width: 10px;
            margin: 8px 2px 8px 2px;
            border-radius: 5px;
        }
        QScrollBar::handle:vertical {
            background: rgba(123,136,150,0.58);
            min-height: 28px;
            border-radius: 5px;
        }
        QScrollBar:horizontal {
            background: rgba(70,80,92,0.14);
            height: 10px;
            margin: 2px 8px 2px 8px;
            border-radius: 5px;
        }
        QScrollBar::handle:horizontal {
            background: rgba(123,136,150,0.58);
            min-width: 28px;
            border-radius: 5px;
        }
        QScrollBar::add-line, QScrollBar::sub-line,
        QScrollBar::add-page, QScrollBar::sub-page { background: transparent; border: none; }
    )QSS");

    const bool whiteTables = GeneralSettingsService::loadWhiteTablesEnabled();
    if (whiteTables) {
        qss += QString::fromUtf8(R"QSS(

QTableView, QTableWidget {
    background: #ffffff;
    alternate-background-color: #f3f6f9;
    color: #0f1114;
    gridline-color: #d0d7df;
    border: 1px solid #cfd6de;
    border-radius: 10px;
}
QHeaderView::section {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                stop:0 #fcfdfe,
                                stop:0.16 #f1f4f7,
                                stop:0.17 #dde3e9,
                                stop:0.62 #c6ced7,
                                stop:1 #adb7c3);
    color: #090b0d;
    padding: 6px 8px;
    border: 0px;
    border-bottom: 1px solid #cfd6de;
    font-weight: 800;
}
QTableView::item:selected, QTableWidget::item:selected {
    background: rgba(113,146,190,0.26);
    color: #0f1114;
}
)QSS");
    }
    qss = qss.arg(contrastCheckboxQss());
    return qss;
}


static QString uiFontOverrides()
{
    const auto fontSettings = UiSettingsService::loadFontSettings();
    const QString family = fontSettings.fontFamily.trimmed().isEmpty() ? QStringLiteral("Times New Roman") : fontSettings.fontFamily.trimmed();
    const int baseSize = fontSettings.fontSize;
    const int tableSize = fontSettings.tableFontSize;
    const int headingSize = fontSettings.headingFontSize;
    const QString color = fontSettings.fontColor.trimmed();

    const QString escFamily = family.contains(' ') ? QStringLiteral("\"") + family + QStringLiteral("\"") : family;
    QString qss;
    qss += QString::fromUtf8(R"QSS(
        QWidget, QLabel, QPushButton, QToolButton, QCheckBox, QRadioButton, QGroupBox, QTabBar::tab,
        QLineEdit, QTextEdit, QPlainTextEdit, QComboBox, QSpinBox, QDoubleSpinBox,
        QDateEdit, QTimeEdit, QDateTimeEdit, QAbstractSpinBox, QKeySequenceEdit {
            font-family: %1;
            font-size: %2pt;
        }
        QTableWidget, QTableView, QListWidget, QTreeWidget {
            font-family: %1;
            font-size: %3pt;
        }
        QHeaderView::section, QLabel#Title, #top_title, QGroupBox::title {
            font-family: %1;
            font-size: %4pt;
        }
    )QSS").arg(escFamily).arg(baseSize).arg(tableSize).arg(headingSize);

    if (!color.isEmpty()) {
        qss += QString::fromUtf8(R"QSS(
            QLabel, QCheckBox, QRadioButton, QGroupBox, QTabBar::tab,
            QTableWidget, QTableView, QListWidget, QTreeWidget {
                color: %1;
            }
        )QSS").arg(color);
    }

    if (!family.isEmpty()) {
        QFont appFont = qApp->font();
        appFont.setFamily(family);
        if (baseSize > 0) appFont.setPointSize(baseSize);
        qApp->setFont(appFont);
    }

    return qss;
}
QString ThemeManager::currentTheme()
{
    QString t = ThemePreferenceService::loadTheme();
    if (t != "light" && t != "graphite") t = "graphite";
    return t;
}

void ThemeManager::applySavedTheme()
{
    setTheme(currentTheme());
}

void ThemeManager::setTheme(const QString& name)
{
    static auto* compactDialogs=new CompactInputDialogLayout(qApp);
    qApp->installEventFilter(compactDialogs);
    const QString previousTheme = currentTheme();
    const QString t = name.trimmed().toLower();
    const bool graphite = (t != "light");
    qApp->setProperty("carservice.graphiteThemeActive", graphite);

    const QString effectiveTheme = graphite ? QStringLiteral("graphite") : QStringLiteral("light");
    if (!graphite) {
        qApp->setStyleSheet(qssLight() + uiFontOverrides());
        ThemePreferenceService::saveTheme(effectiveTheme);
    } else {
        qApp->setStyleSheet(qssGraphite() + uiFontOverrides());
        ThemePreferenceService::saveTheme(effectiveTheme);
    }

    if (previousTheme != effectiveTheme) {
        BackupCommitPublisher::publish(
            BackupCommitPublisher::makeEvent(QStringLiteral("ThemeManager"),
                                             QStringLiteral("settings"),
                                             QStringLiteral("ui_theme"),
                                             QStringLiteral("ui.theme"),
                                             QString(),
                                             0,
                                             QStringLiteral("theme_change"),
                                             QString::fromUtf8("Змінено тему інтерфейсу: %1").arg(effectiveTheme == QStringLiteral("light")
                                                                                                        ? QString::fromUtf8("світла")
                                                                                                        : QString::fromUtf8("графіт")),
                                             {QStringLiteral("settings")}),
            nullptr);
    }

    for (QWidget* w : qApp->allWidgets()) {
        if (w) w->update();
    }
}
