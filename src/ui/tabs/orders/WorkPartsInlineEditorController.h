#pragma once

// UI-only interface. Implementation and private editor classes compile once.
#include "core/diagnostics/StructuredDiagnosticsLogger.h"

#include <functional>
#include <atomic>
#include <QTimer>
#include <QPointer>
#include <QAbstractItemModel>
#include <QGuiApplication>
#include <QClipboard>
#include <QPersistentModelIndex>
#include <QVariant>

#include <QComboBox>
#include <QTableWidget>
#include <QFont>
#include <QStyledItemDelegate>

QTableWidgetItem* makeTextItem(const QString& text, bool hint = false);
QTableWidgetItem* makeReadOnlyTextItem(const QString& text,
                                              Qt::Alignment alignment = Qt::AlignLeft | Qt::AlignVCenter);
QTableWidgetItem* makeViewportFillCell();
bool rowHasHintMarker(QTableWidget* table, int row);
bool rowIsPureViewportFill(QTableWidget* table, int row);
void applyLeftAlignmentToServiceRow(QTableWidget* table, int row);
QString importedPartTraceId(QTableWidget* table, int row);
bool flushInlineEditorValueToModel(QTableWidget* table);
QPersistentModelIndex inlineEditingPersistentIndex(QTableWidget* table);
void clearInlineEditingIndex(QTableWidget* table);
void resetInlineEditorStateForTable(QTableWidget* table, const char* reason = nullptr);
void scheduleDelayedActivateEditor(QTableWidget* table, int row, int col, const QPoint& clickPosInViewport = QPoint());
void scheduleEnsureNextInputRow(QTableWidget* table, int currentRow);
QVector<int> logicalNavigationColumns(QTableWidget* table);
void removeViewportFillRows(QTableWidget* table);
void setVerticalHeaderLabel(QTableWidget* table, int row, const QString& label);
QString glossyActionButtonStyle(bool primary = false, bool compact = false);
QString whiteMasterComboStyle();
QFont interfaceHeaderFont(int pointDelta = 0);
void fillTableViewportWithRows(QTableWidget* table, bool keepHintRowAtBottom);
void styleDarkFlatTable(QTableWidget* table);
QString currentPrimaryMasterName(QComboBox* combo);
QString moneyText(double v);
bool isOilServiceTitle(const QString& serviceTitle);
bool isOilMaintenancePart(const QString& partName);
void refreshCompactMainPartsCellFonts(QTableWidget* table);
int calcMainServicesRowHeight(QTableWidget* table, int row);
int calcMainPartsRowHeight(QTableWidget* table, int row);
void updateSingleMainPartsRowHeight(QTableWidget* table, int row);
void updateAllMainPartsRowHeights(QTableWidget* table);
QList<QStringList> runBatchEditor(QWidget* parent, const QString& title, const QStringList& headers, const QList<QStringList>& existing, const QString& stateKey = QString());

QComboBox* createWorkPartsArrowCombo(QWidget* parent);
QAbstractItemDelegate* createWorkPartsItemDelegate(QTableWidget* table);
QObject* createWorkPartsKeyboardNavigator(QTableWidget* table);
