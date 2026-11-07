#pragma once
#include <QApplication>
#include <QDialog>
#include <QFileDialog>
#include <QScreen>
#include <QTimer>
#include <QScopeGuard>

namespace InvoiceDialogSupport {
inline QString selectFile(QWidget* parent, const QString& title, const QString& filter) {
    const bool previous = QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, false);
    const auto restore = qScopeGuard([previous] {
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, previous);
    });
    return QFileDialog::getOpenFileName(parent, title, QString(), filter);
}

inline void fitOnOpen(QDialog* dialog) {
    QTimer::singleShot(0, dialog, [dialog] {
        const auto* screen = dialog->screen();
        if (!screen) return;
        const int margin = qRound(screen->logicalDotsPerInch() * 10.0 / 25.4);
        const QRect safe = screen->availableGeometry().adjusted(margin, margin, -margin, -margin);
        const QSize frame = dialog->frameGeometry().size() - dialog->size();
        dialog->setMinimumSize(0, 0);
        dialog->resize(qMin(1240, safe.width() - frame.width()), safe.height() - frame.height());
        dialog->move(safe.center().x() - dialog->frameGeometry().width() / 2, safe.top());
    });
}
}
