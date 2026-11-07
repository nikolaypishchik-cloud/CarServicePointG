#pragma once

#include <QApplication>
#include <QDialog>

namespace PersonnelDialogStyle {
// Settings pages have their own inherited stylesheet; modal editors need explicit surfaces.
inline void apply(QDialog& dialog)
{
    const bool dark = qApp->property("carservice.graphiteThemeActive").toBool();
    dialog.setStyleSheet(QStringLiteral(
        "QDialog { background:%1; color:%2; }"
        "QLabel,QCheckBox { color:%2; background:transparent; }"
        "QComboBox,QLineEdit,QPlainTextEdit,QAbstractSpinBox { background:#ffffff; color:#101418;"
        " border:1px solid #8a99a8; border-radius:5px; padding:6px; min-height:24px;"
        " selection-background-color:#cfe2ff; selection-color:#101418; }"
        "QComboBox QAbstractItemView { background:#ffffff; color:#101418;"
        " selection-background-color:#cfe2ff; selection-color:#101418; }"
        "QPushButton { background:%3; color:%2; border:1px solid #8a99a8;"
        " border-radius:6px; padding:8px 16px; min-height:24px; }"
        "QPushButton:disabled { color:#7c8792; }"
        "QTableView { background:#ffffff; color:#101418; selection-background-color:#cfe2ff; selection-color:#101418; }"
        "QHeaderView::section { background:#e5edf5; color:#101418; }")
        .arg(dark ? "#263542" : "#f4f7fa", dark ? "#ffffff" : "#101418", dark ? "#354b5b" : "#e5edf5"));
}
}
