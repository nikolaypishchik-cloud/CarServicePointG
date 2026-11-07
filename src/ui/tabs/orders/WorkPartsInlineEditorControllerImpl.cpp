#include "ui/tabs/orders/WorkPartsDialogModulePrelude.h"

// Private definitions have one translation unit and one diagnostic counter.
#include "ui/tabs/orders/internal/WorkPartsEditorRowHelpers.ipp"
#include "ui/tabs/orders/internal/WorkPartsEditorNavigation.ipp"
#include "ui/tabs/orders/internal/WorkPartsEditorCommitGuard.ipp"
#include "ui/tabs/orders/internal/WorkPartsEditorDelegateState.ipp"

QComboBox* createWorkPartsArrowCombo(QWidget* parent) { return new ArrowComboBox(parent); }
QAbstractItemDelegate* createWorkPartsItemDelegate(QTableWidget* table) { return new FlatTableItemDelegate(table); }
QObject* createWorkPartsKeyboardNavigator(QTableWidget* table) { return new TableKeyboardNavigator(table); }
