#pragma once
class InventoryEntryDialog;
class QVBoxLayout;
class QWidget;
class QComboBox;
struct WorkshopSpareOrder;
namespace SpareReservePresentation {
// Constructs the reusable visual shell only; no persistence or business commands.
QVBoxLayout* build(InventoryEntryDialog& dialog, const WorkshopSpareOrder& selected);
bool hasIssueTarget(const QComboBox& target);
QWidget* metricStrip(InventoryEntryDialog& dialog, const WorkshopSpareOrder& selected, bool stockBalance);
void addInvoice(InventoryEntryDialog& dialog, QVBoxLayout* root, const WorkshopSpareOrder& selected);
}
