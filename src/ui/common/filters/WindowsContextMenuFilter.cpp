#include "ui/common/ContextMenuAppearance.h"
#include <algorithm>
#include "ui/common/filters/WindowsContextMenuFilter.h"
#include "ui/common/filters/ManualInputCorrectionFilter.h"
#include "core/util/SoundSupport.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QAction>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QStyle>
#include <QPointer>
#include <QTableWidget>
#include <QTextCursor>
#include <QTextEdit>
#include <QUrl>

#if CS_HAS_MULTIMEDIA
#include <QSoundEffect>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#include <mmsystem.h>
#endif

namespace {

qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

bool contextMenuSoundsEnabled()
{
    return SoundSupport::soundsEnabled(true);
}

int contextMenuSoundsVolumePct()
{
    return SoundSupport::soundsVolumePct(90);
}

QByteArray loadTableDeleteSoundBytes(QString* loadedFrom = nullptr)
{
    return SoundSupport::loadAssetBytes(QStringLiteral("table_context_delete.wav"), loadedFrom, QStringLiteral("Table delete sound"));
}

QByteArray loadTableCopySoundBytes(QString* loadedFrom = nullptr)
{
    return SoundSupport::loadAssetBytes(QStringLiteral("table_context_copy.wav"), loadedFrom, QStringLiteral("Table copy sound"));
}

#if CS_HAS_MULTIMEDIA
QSoundEffect* tableDeleteEffectInstance()
{
    static QSoundEffect* effect = []() {
        auto* e = new QSoundEffect();
        SoundSupport::setupEffect(e, QStringLiteral("table_context_delete.wav"), 1, 0.90f);
        return e;
    }();
    return effect;
}
#endif

#if CS_HAS_MULTIMEDIA
QSoundEffect* tableCopyEffectInstance()
{
    static QSoundEffect* effect = []() {
        auto* e = new QSoundEffect();
        SoundSupport::setupEffect(e, QStringLiteral("table_context_copy.wav"), 1, 0.90f);
        return e;
    }();
    return effect;
}
#endif

#ifdef Q_OS_WIN
QString tableDeleteWavFilePath()
{
    static QString cachedPath;
    if (!cachedPath.isEmpty()) return cachedPath;

    QString loadedFrom;
    const QByteArray bytes = loadTableDeleteSoundBytes(&loadedFrom);
    cachedPath = SoundSupport::ensureTempWavFile(bytes, QStringLiteral("table_context_delete"), loadedFrom, QStringLiteral("Table delete sound loaded from:"));
    return cachedPath;
}

bool playNativeWindowsTableDeleteSound()
{
    return SoundSupport::playNativeFile(tableDeleteWavFilePath(), true, false);
}
#endif

#ifdef Q_OS_WIN
QString tableCopyWavFilePath()
{
    static QString cachedPath;
    if (!cachedPath.isEmpty()) return cachedPath;

    QString loadedFrom;
    const QByteArray bytes = loadTableCopySoundBytes(&loadedFrom);
    cachedPath = SoundSupport::ensureTempWavFile(bytes, QStringLiteral("table_context_copy"), loadedFrom, QStringLiteral("Table copy sound loaded from:"));
    return cachedPath;
}

bool playNativeWindowsTableCopySound()
{
    return SoundSupport::playNativeFile(tableCopyWavFilePath(), true, false);
}
#endif

void playTableDeleteActionSound()
{
    static qint64 s_lastPlayMs = 0;
    const qint64 ts = nowMs();
    if (ts - s_lastPlayMs < 80) {
        return;
    }
    s_lastPlayMs = ts;

    if (!contextMenuSoundsEnabled()) {
        return;
    }

    const int volumePct = contextMenuSoundsVolumePct();
    bool played = false;

#ifdef Q_OS_WIN
    Q_UNUSED(volumePct);
    played = playNativeWindowsTableDeleteSound();
#endif

#if CS_HAS_MULTIMEDIA
    if (!played) {
        auto* effect = tableDeleteEffectInstance();
        effect->setMuted(false);
        effect->setVolume(qBound(0, volumePct, 100) / 100.0f);
        effect->stop();
        effect->play();
        played = true;
    }
#endif

    if (!played) {
        QApplication::beep();
    }
}

void playTableCopyActionSound()
{
    static qint64 s_lastPlayMs = 0;
    const qint64 ts = nowMs();
    if (ts - s_lastPlayMs < 80) {
        return;
    }
    s_lastPlayMs = ts;

    if (!contextMenuSoundsEnabled()) {
        return;
    }

    const int volumePct = contextMenuSoundsVolumePct();
    bool played = false;

#ifdef Q_OS_WIN
    Q_UNUSED(volumePct);
    played = playNativeWindowsTableCopySound();
#endif

#if CS_HAS_MULTIMEDIA
    if (!played) {
        auto* effect = tableCopyEffectInstance();
        effect->setMuted(false);
        effect->setVolume(qBound(0, volumePct, 100) / 100.0f);
        effect->stop();
        effect->play();
        played = true;
    }
#endif

    if (!played) {
        QApplication::beep();
    }
}

void prepareWindowsMenu(QMenu& menu)
{
    ContextMenuAppearance::apply(menu);
}
}

WindowsContextMenuFilter::WindowsContextMenuFilter(QObject* parent)
    : QObject(parent)
{
}

bool WindowsContextMenuFilter::eventFilter(QObject* watched, QEvent* event)
{
    if (!watched || !event) return QObject::eventFilter(watched, event);

    if (auto* menu = qobject_cast<QMenu*>(watched)) {
        if (event->type() == QEvent::Polish && !menu->property("unifiedMenuConnected").toBool()) {
            menu->setProperty("unifiedMenuConnected", true);
            ContextMenuAppearance::apply(*menu);
            // Runs before popup geometry is calculated, including reused menus
            // whose actions or shortcuts changed since the previous opening.
            connect(menu, &QMenu::aboutToShow, menu, [menu] { ContextMenuAppearance::apply(*menu); });
        }
    }

    if (handleForLineEdit(watched, event)) return true;
    if (handleForTextEdit(watched, event)) return true;
    if (handleForItemView(watched, event)) return true;

    return QObject::eventFilter(watched, event);
}

bool WindowsContextMenuFilter::handleForLineEdit(QObject* watched, QEvent* event)
{
    auto* edit = qobject_cast<QLineEdit*>(watched);
    if (!edit || event->type() != QEvent::ContextMenu) return false;

    auto* ce = static_cast<QContextMenuEvent*>(event);
    QMenu menu(edit);
    ManualInputCorrectionFilter::appendMenu(menu,edit);

    QAction* actUndo = menu.addAction(QString::fromUtf8("Скасувати"));
    actUndo->setShortcut(QKeySequence::Undo);
    actUndo->setEnabled(edit->isUndoAvailable());

    QAction* actRedo = menu.addAction(QString::fromUtf8("Повторити"));
    actRedo->setShortcut(QKeySequence::Redo);
    actRedo->setEnabled(edit->isRedoAvailable());

    menu.addSeparator();

    const bool editable = !edit->isReadOnly();
    const bool hasSelection = edit->hasSelectedText();
    const bool hasText = !edit->text().isEmpty();
    const bool canPaste = editable && !QApplication::clipboard()->text().isEmpty();

    QAction* actCut = menu.addAction(QString::fromUtf8("Вирізати"));
    actCut->setShortcut(QKeySequence::Cut);
    actCut->setEnabled(editable && hasSelection);

    QAction* actCopy = menu.addAction(QString::fromUtf8("Копіювати"));
    actCopy->setShortcut(QKeySequence::Copy);
    actCopy->setEnabled(hasSelection);

    QAction* actPaste = menu.addAction(QString::fromUtf8("Вставити"));
    actPaste->setShortcut(QKeySequence::Paste);
    actPaste->setEnabled(canPaste);

    QAction* actPastePlain = menu.addAction(QString::fromUtf8("Вставити як звичайний текст"));
    actPastePlain->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+V")));
    actPastePlain->setEnabled(canPaste);

    menu.addSeparator();

    QAction* actSelectAll = menu.addAction(QString::fromUtf8("Виділити все"));
    actSelectAll->setShortcut(QKeySequence::SelectAll);
    actSelectAll->setEnabled(hasText);

    QAction* actDelete = menu.addAction(QString::fromUtf8("Видалити"));
    actDelete->setShortcut(QKeySequence::Delete);
    actDelete->setEnabled(editable && (hasSelection || hasText));

    prepareWindowsMenu(menu);

    QAction* picked = menu.exec(ce->globalPos());
    if (!picked) return true;

    if (picked == actUndo) { edit->setProperty("inputCorrectionSuppressed", true); edit->undo(); return true; }
    if (picked == actRedo) { edit->setProperty("inputCorrectionSuppressed", true); edit->redo(); return true; }
    if (picked == actCut)  { edit->cut();  return true; }
    if (picked == actCopy) { playTableCopyActionSound(); edit->copy(); return true; }
    if (picked == actPaste || picked == actPastePlain) { edit->insert(QApplication::clipboard()->text()); return true; }
    if (picked == actSelectAll) { edit->selectAll(); return true; }
    if (picked == actDelete) {
        const QString text = hasSelection
            ? QString::fromUtf8("Підтвердити видалення виділеного тексту?")
            : QString::fromUtf8("Підтвердити очищення поля?");
        if (QMessageBox::question(edit, QString::fromUtf8("Підтвердження"), text,
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) == QMessageBox::Yes) {
            if (hasSelection) {
                edit->insert(QString());
            } else {
                edit->clear();
            }
        }
        return true;
    }

    return true;
}

bool WindowsContextMenuFilter::handleForTextEdit(QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::ContextMenu) return false;

    auto* plain = qobject_cast<QPlainTextEdit*>(watched);
    auto* rich = qobject_cast<QTextEdit*>(watched);
    if (!plain && !rich) return false;

    const bool editable = plain ? !plain->isReadOnly() : !rich->isReadOnly();
    const QTextCursor cursor = plain ? plain->textCursor() : rich->textCursor();
    const bool hasSelection = cursor.hasSelection();
    const bool hasText = plain ? !plain->toPlainText().isEmpty() : !rich->toPlainText().isEmpty();
    const bool canPaste = editable && !QApplication::clipboard()->text().isEmpty();

    auto* ce = static_cast<QContextMenuEvent*>(event);
    QWidget* host = plain ? static_cast<QWidget*>(plain) : static_cast<QWidget*>(rich);
    QMenu menu(host);
    ManualInputCorrectionFilter::appendMenu(menu,host);

    QAction* actUndo = menu.addAction(QString::fromUtf8("Скасувати"));
    actUndo->setShortcut(QKeySequence::Undo);
    actUndo->setEnabled(editable && (plain ? plain->document()->isUndoAvailable() : rich->document()->isUndoAvailable()));

    QAction* actRedo = menu.addAction(QString::fromUtf8("Повторити"));
    actRedo->setShortcut(QKeySequence::Redo);
    actRedo->setEnabled(editable && (plain ? plain->document()->isRedoAvailable() : rich->document()->isRedoAvailable()));

    menu.addSeparator();

    QAction* actCut = menu.addAction(QString::fromUtf8("Вирізати"));
    actCut->setShortcut(QKeySequence::Cut);
    actCut->setEnabled(editable && hasSelection);

    QAction* actCopy = menu.addAction(QString::fromUtf8("Копіювати"));
    actCopy->setShortcut(QKeySequence::Copy);
    actCopy->setEnabled(hasSelection);

    QAction* actPaste = menu.addAction(QString::fromUtf8("Вставити"));
    actPaste->setShortcut(QKeySequence::Paste);
    actPaste->setEnabled(canPaste);

    QAction* actPastePlain = menu.addAction(QString::fromUtf8("Вставити як звичайний текст"));
    actPastePlain->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+V")));
    actPastePlain->setEnabled(canPaste);

    menu.addSeparator();

    QAction* actSelectAll = menu.addAction(QString::fromUtf8("Виділити все"));
    actSelectAll->setShortcut(QKeySequence::SelectAll);
    actSelectAll->setEnabled(hasText);

    QAction* actDelete = menu.addAction(QString::fromUtf8("Видалити"));
    actDelete->setShortcut(QKeySequence::Delete);
    actDelete->setEnabled(editable && (hasSelection || hasText));

    prepareWindowsMenu(menu);

    QAction* picked = menu.exec(ce->globalPos());
    if (!picked) return true;

    if (picked == actUndo) { host->setProperty("inputCorrectionSuppressed", true); if (plain) plain->undo(); else rich->undo(); return true; }
    if (picked == actRedo) { host->setProperty("inputCorrectionSuppressed", true); if (plain) plain->redo(); else rich->redo(); return true; }
    if (picked == actCut) { if (plain) plain->cut(); else rich->cut(); return true; }
    if (picked == actCopy) { playTableCopyActionSound(); if (plain) plain->copy(); else rich->copy(); return true; }
    if (picked == actPaste) { host->setProperty("inputCorrectionDirty",true);host->setProperty("inputCorrectionSuppressed",false);if (plain) plain->paste(); else rich->paste(); return true; }
    if (picked == actPastePlain) {
        host->setProperty("inputCorrectionDirty",true);host->setProperty("inputCorrectionSuppressed",false);
        if (plain) plain->insertPlainText(QApplication::clipboard()->text());
        else rich->insertPlainText(QApplication::clipboard()->text());
        return true;
    }
    if (picked == actSelectAll) { if (plain) plain->selectAll(); else rich->selectAll(); return true; }
    if (picked == actDelete) {
        const QString text = hasSelection
            ? QString::fromUtf8("Підтвердити видалення виділеного тексту?")
            : QString::fromUtf8("Підтвердити очищення текстового поля?");
        if (QMessageBox::question(host, QString::fromUtf8("Підтвердження"), text,
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) == QMessageBox::Yes) {
            if (plain) {
                if (hasSelection) {
                    QTextCursor c = plain->textCursor();
                    c.removeSelectedText();
                    plain->setTextCursor(c);
                } else {
                    plain->clear();
                }
            } else {
                if (hasSelection) {
                    QTextCursor c = rich->textCursor();
                    c.removeSelectedText();
                    rich->setTextCursor(c);
                } else {
                    rich->clear();
                }
            }
        }
        return true;
    }

    return true;
}

bool WindowsContextMenuFilter::handleForItemView(QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::ContextMenu) return false;

    auto* ce = static_cast<QContextMenuEvent*>(event);
    auto* watchedWidget = qobject_cast<QWidget*>(watched);
    QAbstractItemView* view = qobject_cast<QAbstractItemView*>(watched);
    QPoint viewportPos;

    auto hasCustomMenuOnView = [](QAbstractItemView* candidate) -> bool {
        if (!candidate) return false;
        if (candidate->contextMenuPolicy() == Qt::CustomContextMenu) return true;
        QWidget* viewport = candidate->viewport();
        return viewport && viewport->contextMenuPolicy() == Qt::CustomContextMenu;
    };

    if (view) {
        if (hasCustomMenuOnView(view)) return false;
        viewportPos = view->viewport() ? view->viewport()->mapFromGlobal(ce->globalPos()) : view->mapFromGlobal(ce->globalPos());
    } else if (watchedWidget) {
        QWidget* cursor = watchedWidget;
        while (cursor) {
            auto* parentView = qobject_cast<QAbstractItemView*>(cursor);
            if (parentView) {
                if (hasCustomMenuOnView(parentView)) return false;
                view = parentView;
                if (view->viewport()) {
                    viewportPos = view->viewport()->mapFromGlobal(ce->globalPos());
                } else {
                    viewportPos = view->mapFromGlobal(ce->globalPos());
                }
                break;
            }
            cursor = cursor->parentWidget();
        }
        if (!view) return false;
    } else {
        return false;
    }

    const QModelIndex idx = view->indexAt(viewportPos);
    if (idx.isValid() && view->selectionModel()) {
        const bool alreadySelected = view->selectionModel()->isRowSelected(idx.row(), idx.parent());
        const auto flags = alreadySelected
            ? (QItemSelectionModel::Current | QItemSelectionModel::Rows | QItemSelectionModel::NoUpdate)
            : (QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        view->selectionModel()->setCurrentIndex(idx, flags);
    }

    QMenu menu(view);

    QAction* actCopy = menu.addAction(QString::fromUtf8("Копіювати"));
    actCopy->setShortcut(QKeySequence::Copy);
    actCopy->setEnabled(view->currentIndex().isValid());

    QAction* actSelectAll = menu.addAction(QString::fromUtf8("Виділити все"));
    actSelectAll->setShortcut(QKeySequence::SelectAll);
    actSelectAll->setEnabled(view->model() && view->model()->rowCount() > 0);

    QAction* actClearSelection = menu.addAction(QString::fromUtf8("Зняти виділення"));
    actClearSelection->setEnabled(view->selectionModel() && view->selectionModel()->hasSelection());

    QAction* actDelete = menu.addAction(QString::fromUtf8("Видалити"));
    actDelete->setShortcut(QKeySequence::Delete);

    bool canDelete = false;
    int selectedRowCount = 0;
    auto* businessDelete = view->findChild<QAction*>(QStringLiteral("contextRowDeleteAction"), Qt::FindDirectChildrenOnly);
    if (auto* table = qobject_cast<QTableWidget*>(view)) {
        selectedRowCount = table->selectionModel() ? table->selectionModel()->selectedRows().size() : 0;
        canDelete = selectedRowCount > 0 &&
            (!table->property("allowContextRowRemoval").isValid() ||
             table->property("allowContextRowRemoval").toBool());
    }
    if (businessDelete) {
        selectedRowCount = view->selectionModel() ? view->selectionModel()->selectedRows().size() : 0;
        canDelete = selectedRowCount == 1 && businessDelete->isEnabled();
    }
    actDelete->setEnabled(canDelete);
    if (selectedRowCount > 1) {
        actDelete->setText(QString::fromUtf8("Видалити вибрані рядки"));
    }

    prepareWindowsMenu(menu);

    QAction* picked = menu.exec(ce->globalPos());
    if (!picked) return true;

    if (picked == actCopy) {
        QString text;
        const auto indexes = view->selectionModel() ? view->selectionModel()->selectedIndexes() : QModelIndexList();
        if (!indexes.isEmpty()) {
            QModelIndexList sorted = indexes;
            std::sort(sorted.begin(), sorted.end(), [](const QModelIndex& a, const QModelIndex& b) {
                if (a.row() != b.row()) return a.row() < b.row();
                return a.column() < b.column();
            });
            int currentRow = sorted.first().row();
            QStringList rowParts;
            for (const QModelIndex& mi : sorted) {
                if (mi.row() != currentRow) {
                    text += rowParts.join("\t") + "\n";
                    rowParts.clear();
                    currentRow = mi.row();
                }
                rowParts << mi.data().toString();
            }
            if (!rowParts.isEmpty()) text += rowParts.join("\t");
        } else if (view->currentIndex().isValid()) {
            text = view->currentIndex().data().toString();
        }
        playTableCopyActionSound();
        QApplication::clipboard()->setText(text);
        return true;
    }

    if (picked == actSelectAll) {
        view->selectAll();
        return true;
    }

    if (picked == actClearSelection) {
        if (view->selectionModel()) view->selectionModel()->clearSelection();
        return true;
    }

    if (picked == actDelete) {
        if (!canDelete) return true;
        if (businessDelete) { businessDelete->trigger(); return true; }
        playTableDeleteActionSound();
        auto* table = qobject_cast<QTableWidget*>(view);
        if (!table || !table->selectionModel()) return true;
        QModelIndexList rows = table->selectionModel()->selectedRows();
        if (rows.isEmpty() && table->currentRow() >= 0) {
            rows << table->model()->index(table->currentRow(), 0);
        }
        if (rows.isEmpty()) return true;
        std::sort(rows.begin(), rows.end(), [](const QModelIndex& a, const QModelIndex& b) {
            return a.row() > b.row();
        });
        const QString text = rows.size() > 1
            ? QString::fromUtf8("Видалити вибрані рядки (%1)?").arg(rows.size())
            : QString::fromUtf8("Видалити вибраний рядок?");
        if (QMessageBox::question(table,
                                  QString::fromUtf8("Підтвердження"),
                                  text,
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) == QMessageBox::Yes) {
            int lastRow = -1;
            for (const QModelIndex& mi : rows) {
                if (!mi.isValid() || mi.row() == lastRow) continue;
                table->removeRow(mi.row());
                lastRow = mi.row();
            }
        }
        return true;
    }

    return true;
}
